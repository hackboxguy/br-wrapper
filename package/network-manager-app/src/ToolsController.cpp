#include "ToolsController.h"
#include "NetTool.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>

ToolsController::ToolsController(const Options &options, QObject *parent)
    : QObject(parent), m_options(options)
{
    m_tool = new NetTool(options.tool, options.dryRun, this);
    connect(m_tool, &NetTool::result, this, &ToolsController::onResult);
    connect(m_tool, &NetTool::finished, this, &ToolsController::onFinished);
}

void ToolsController::setActive(bool active)
{
    if (!active) stop();
}

void ToolsController::stop()
{
    m_pending.clear();
    if (m_tool->busy()) m_tool->stop();
}

// Starting a tool stops the one that runs; the new one starts when the old
// one has ended (onFinished), so two never overlap
void ToolsController::start(const QString &what, const QStringList &args, bool elevated)
{
    if (m_tool->busy()) {
        m_pending = what;
        m_pendingArgs = args;
        m_pendingElevated = elevated;
        m_tool->stop();
        return;
    }
    m_running = what;
    emit runningChanged();
    m_tool->run(args, elevated);
}

void ToolsController::ping(const QString &target, const QString &iface, int count)
{
    if (target.isEmpty()) return;
    m_replies.clear();
    m_pingSummary.clear();
    m_pingSummary["target"] = target;
    m_pingSummary["iface"] = iface;
    emit pingChanged();
    QStringList args{"ping", "--target=" + target, "--count=" + QString::number(qBound(1, count, 100))};
    if (!iface.isEmpty()) args << "--iface=" + iface;
    start("ping", args, false);
}

void ToolsController::internetCheck(const QString &iface, const QString &ip, const QString &gateway)
{
    m_checkSteps.clear();
    for (const char *step : {"gateway", "dns", "https"}) {
        QVariantMap m;
        m["step"] = QString::fromLatin1(step);
        m["state"] = "pending";
        m_checkSteps.append(m);
    }
    m_checkIface = iface;
    m_checkIp = ip;
    m_checkGateway = gateway;
    emit checkChanged();
    QStringList args{"internet-check"};
    if (!iface.isEmpty()) args << "--iface=" + iface;
    // root binds every step to the port; a dry run still checks, unbound
    start("check", args, true);
}

void ToolsController::startServer()
{
    m_samples.clear();
    m_iperf.clear();
    m_iperf["mode"] = "server";
    emit iperfChanged();
    start("server", {"iperf-server", "--start"}, false);
}

void ToolsController::startClient(const QString &host, int secs, bool udp, bool reverse)
{
    if (host.isEmpty()) return;
    m_samples.clear();
    m_iperf.clear();
    m_iperf["mode"] = "client";
    m_iperf["host"] = host;
    m_iperf["secs"] = secs;
    m_iperf["udp"] = udp;
    m_iperf["reverse"] = reverse;
    emit iperfChanged();
    QStringList args{"iperf-client", "--host=" + host, "--secs=" + QString::number(secs)};
    if (udp) args << "--udp";
    if (reverse) args << "--reverse";
    start("client", args, false);
}

void ToolsController::onResult(const QVariantMap &f)
{
    const QString kind = f.value("kind").toString();
    if (kind == "reply" || kind == "lost") {
        QVariantMap r;
        r["seq"] = f.value("seq").toInt();
        r["ms"] = f.value("ms");
        r["lost"] = kind == "lost";
        r["reason"] = f.value("reason");
        m_replies.append(r);
        while (m_replies.size() > 100) m_replies.removeFirst();
        emit pingChanged();
    } else if (kind == "ping") {
        // ping -O does not report the last packets' loss (it ends at the
        // timeout): those it sent and nobody answered are lost too
        const int sent = f.value("sent").toInt();
        QSet<int> seen;
        for (const QVariant &v : m_replies) seen.insert(v.toMap().value("seq").toInt());
        for (int seq = 1; seq <= sent && m_replies.size() < sent; ++seq) {
            if (seen.contains(seq)) continue;
            QVariantMap r;
            r["seq"] = seq;
            r["lost"] = true;
            m_replies.append(r);
        }
        std::sort(m_replies.begin(), m_replies.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("seq").toInt() < b.toMap().value("seq").toInt();
        });
        QVariantMap s = f;
        s["iface"] = m_pingSummary.value("iface");
        s["done"] = true;
        m_pingSummary = s;
        emit pingChanged();
    } else if (kind == "check") {
        const QString step = f.value("step").toString();
        for (int i = 0; i < m_checkSteps.size(); ++i) {
            QVariantMap m = m_checkSteps[i].toMap();
            if (m.value("step") != step) continue;
            m["state"] = f.value("ok") == "1" ? "ok" : "failed";
            m["ms"] = f.value("ms");
            m["reason"] = f.value("reason");
            m["target"] = f.value("target");
            m["server"] = f.value("server");
            m["addr"] = f.value("addr");
            m["code"] = f.value("code");
            m_checkSteps[i] = m;
        }
        emit checkChanged();
    } else if (kind == "internet") {
        const QString iface = f.value("iface").toString();
        QVariantMap r;
        r["ok"] = f.value("ok") == "1";
        r["ip"] = m_checkIp;
        r["gateway"] = m_checkGateway;
        r["at"] = double(QDateTime::currentMSecsSinceEpoch());
        if (!iface.isEmpty()) m_checkResults[iface] = r;
        if (m_checkIface.isEmpty()) m_checkIface = iface;
        emit checkChanged();
    } else if (kind == "iperf") {
        m_samples.append(f.value("mbit").toDouble());
        while (m_samples.size() > 60) m_samples.removeFirst();
        m_iperf["last"] = f.value("mbit").toDouble();
        m_iperf["state"] = "running";
        emit iperfChanged();
    } else if (kind == "iperf-server") {
        m_iperf["listening"] = f.value("running") == "1";
        if (f.contains("addrs")) m_iperf["addrs"] = f.value("addrs").toString().split(',', Qt::SkipEmptyParts);
        m_iperf["port"] = f.value("port");
        if (f.contains("reason")) m_iperf["reason"] = f.value("reason");
        emit iperfChanged();
    } else if (kind == "iperf-peer") {
        m_iperf["peer"] = f.value("from");
        m_samples.clear();
        emit iperfChanged();
    } else if (kind == "iperf-sum") {
        // the receiver's line is the throughput that arrived
        const QString role = f.value("role").toString();
        m_iperf[role + "Mbit"] = f.value("mbit").toDouble();
        if (f.contains("retr")) m_iperf["retr"] = f.value("retr");
        if (f.contains("lost")) {
            m_iperf["lost"] = f.value("lost");
            m_iperf["packets"] = f.value("packets");
            m_iperf["jitter"] = f.value("jitter");
        }
        m_iperf["summary"] = true;
        emit iperfChanged();
    } else if (kind == "iperf-done") {
        m_iperf["ok"] = f.value("ok") == "1";
        if (f.contains("reason")) m_iperf["reason"] = f.value("reason");
        emit iperfChanged();
    }
}

void ToolsController::onFinished(int exitCode)
{
    const QString was = m_running;
    if (was == "server" || was == "client") {
        m_iperf["state"] = exitCode == 130 ? "stopped" : "done";
        m_iperf["exit"] = exitCode;
        emit iperfChanged();
    } else if (was == "ping" && !m_pingSummary.value("done").toBool()) {
        m_pingSummary["stopped"] = true;
        emit pingChanged();
    } else if (was == "check") {
        for (int i = 0; i < m_checkSteps.size(); ++i) {
            QVariantMap m = m_checkSteps[i].toMap();
            if (m.value("state") == "pending") { m["state"] = "failed"; m["reason"] = "stopped"; m_checkSteps[i] = m; }
        }
        emit checkChanged();
    }
    m_running = "idle";
    emit runningChanged();
    if (!m_pending.isEmpty()) {
        const QString what = m_pending;
        m_pending.clear();
        start(what, m_pendingArgs, m_pendingElevated);
    }
}
