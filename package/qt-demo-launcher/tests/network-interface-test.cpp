// Host test for NetworkInterface (the launcher's command port), with a fake
// launcher behind it. Built and run by tests/test-network-interface.sh.
//
// Fake commands:  echo X   -> "X"
//                 slow MS  -> blocks MS ms (stop-app's waitForFinished), "OK"
//                 twice X  -> "X", then a second reply (startApp's double "OK")
//                 mark X   -> "OK" (only recorded)
// Every command run is recorded, in order, to check what ran and how often.
#include "NetworkInterface.h"

#include <QCoreApplication>
#include <QTimer>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static int g_port;
static std::mutex g_mutex;
static std::vector<std::string> g_ran;   // commands run, in order
static int g_fail = 0, g_pass = 0;

static void check(bool ok, const std::string &what, const std::string &got = "")
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        std::printf("FAILED: %s%s%s\n", what.c_str(), got.empty() ? "" : " (got: ",
                    got.empty() ? "" : (got + ")").c_str());
    }
}

static void msleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static int connectTo()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(g_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// launcher-client's way: connect, send, read one line, timeout_s receive timeout.
// "RESET" / "TIMEOUT" / "CLOSED" / "NOCONNECT" when there is no reply.
static std::string request(const std::string &cmd, int timeout_s = 6)
{
    int fd = connectTo();
    if (fd < 0) return "NOCONNECT";
    timeval tv{timeout_s, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string line = cmd + "\n";
    send(fd, line.data(), line.size(), MSG_NOSIGNAL);
    std::string out;
    char c;
    for (;;) {
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 1) {
            if (c == '\n') break;
            out += c;
            continue;
        }
        if (n == 0) { out = out.empty() ? "CLOSED" : out; break; }
        out = errno == ECONNRESET ? "RESET" : "TIMEOUT";
        break;
    }
    close(fd);
    return out;
}

static std::vector<std::string> ran()
{
    std::lock_guard<std::mutex> l(g_mutex);
    return g_ran;
}
static int count(const std::string &cmd)
{
    int n = 0;
    for (auto &c : ran()) n += c == cmd;
    return n;
}
static void clearRan()
{
    std::lock_guard<std::mutex> l(g_mutex);
    g_ran.clear();
}

static void tests()
{
    // 1. A command blocks the launcher (stop-app); 20 clients arrive meanwhile:
    //    all answered, each command run once, in arrival order
    {
        clearRan();
        std::thread slow([] { check(request("slow 1500") == "OK", "slow command answered"); });
        msleep(100);
        std::vector<std::thread> ts;
        std::vector<std::string> got(20);
        for (int i = 0; i < 20; i++) {
            ts.emplace_back([i, &got] { got[i] = request("echo " + std::to_string(i)); });
            msleep(10);
        }
        for (auto &t : ts) t.join();
        slow.join();
        int ok = 0;
        std::string bad;
        for (int i = 0; i < 20; i++) {
            if (got[i] == std::to_string(i)) ok++;
            else bad += std::to_string(i) + "=" + got[i] + " ";
        }
        check(ok == 20, "20 clients during a blocking command: each gets its own reply", bad);
        bool once = true;
        for (int i = 0; i < 20; i++) once &= count("echo " + std::to_string(i)) == 1;
        check(once, "each command run exactly once");
        auto r = ran();
        bool ordered = r.size() == 21 && r[0] == "slow 1500";
        for (int i = 0; ordered && i < 20; i++) ordered = r[i + 1] == "echo " + std::to_string(i);
        std::string order;
        for (auto &c : r) order += c.substr(c.find(' ') + 1) + " ";
        check(ordered, "served in arrival order", order);
    }
    // 2. A handler that answers twice: the second reply reaches nobody
    {
        std::string a, b;
        std::thread t1([&] { a = request("twice first"); });
        msleep(20);
        std::thread t2([&] { b = request("echo second"); });
        t1.join();
        t2.join();
        check(a == "first", "double reply: the first reply to its client", a);
        check(b == "second", "double reply: the next client gets its own reply", b);
    }
    // 3. A queued client that gives up (launcher-client timeout) before its
    //    turn: its command is dropped; the client after it is served
    {
        clearRan();
        std::thread slow([] { request("slow 1500"); });
        msleep(100);
        std::thread gaveUp([] {
            int fd = connectTo();
            send(fd, "mark stale\n", 11, MSG_NOSIGNAL);
            msleep(300);
            close(fd);   // gave up while queued
        });
        msleep(50);
        std::string c;
        std::thread after([&] { c = request("echo after"); });
        gaveUp.join();
        slow.join();
        after.join();
        check(count("mark stale") == 0, "a queued client that gave up: its command not run");
        check(c == "after", "the client after it is served", c);
    }
    // 4. Fire and forget while idle ("echo stop-app | nc ..."): still run
    {
        clearRan();
        int fd = connectTo();
        send(fd, "mark nc\n", 8, MSG_NOSIGNAL);
        close(fd);
        msleep(300);
        check(count("mark nc") == 1, "fire-and-forget while idle: run");
    }
    // 5. A connection that never sends: closed after the idle timeout, the
    //    client behind it then served
    {
        int idle = connectTo();
        msleep(50);
        auto t0 = std::chrono::steady_clock::now();
        std::string r = request("echo behind-idle", 10);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(r == "behind-idle", "client behind an idle connection: served", r);
        check(s < NetworkInterface::kIdleTimeoutMs / 1000.0 + 1.5, "... within the idle timeout",
              std::to_string(s) + " s");
        char c;
        timeval itv{2, 0};
        setsockopt(idle, SOL_SOCKET, SO_RCVTIMEO, &itv, sizeof(itv));
        check(recv(idle, &c, 1, 0) == 0, "the idle connection is closed");
        close(idle);
    }
    // 6. A command sent in pieces; and plain requests keep working
    {
        int fd = connectTo();
        send(fd, "ec", 2, MSG_NOSIGNAL);
        msleep(150);
        send(fd, "ho split\n", 9, MSG_NOSIGNAL);
        char buf[64] = {};
        timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        close(fd);
        check(n > 0 && std::string(buf) == "split\n", "a command in pieces", buf);
        int ok = 0;
        for (int i = 0; i < 50; i++) ok += request("echo x") == "x";
        check(ok == 50, "50 requests in a row", std::to_string(ok));
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    alarm(90);   // a hang is a failure, not a stuck test
    g_port = argc > 1 ? atoi(argv[1]) : 18081;

    NetworkInterface ni(g_port);
    if (!ni.startServer()) {
        std::printf("FAILED: server did not start on %d\n", g_port);
        return 1;
    }
    QObject::connect(&ni, &NetworkInterface::commandReceived, [&ni](const QString &q) {
        std::string cmd = q.toStdString();
        {
            std::lock_guard<std::mutex> l(g_mutex);
            g_ran.push_back(cmd);
        }
        std::string arg = cmd.find(' ') == std::string::npos ? "" : cmd.substr(cmd.find(' ') + 1);
        if (cmd.rfind("slow ", 0) == 0) {
            msleep(std::stoi(arg));   // blocks the event loop, as stop-app does
            ni.sendResponse("OK");
        } else if (cmd.rfind("twice ", 0) == 0) {
            ni.sendResponse(QString::fromStdString(arg));
            ni.sendResponse("SECOND-REPLY");
        } else if (cmd.rfind("echo ", 0) == 0) {
            ni.sendResponse(QString::fromStdString(arg));
        } else {
            ni.sendResponse("OK");
        }
    });

    std::thread t([&] {
        tests();
        QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
    });
    int rc = app.exec();
    t.join();
    std::printf("network-interface-test: %d passed, %d failed\n", g_pass, g_fail);
    return rc || g_fail ? 1 : 0;
}
