// qvue-warpctl: send one command to qvue-warpd and print the reply.
//   qvue-warpctl [--socket PATH] [--timeout SEC] COMMAND [ARGS...]
// Exit status: 0 reply "OK ...", 1 reply "ERROR ...", 2 no daemon / no reply.
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *path = getenv("QVUE_WARP_SOCKET");
    if (!path || !*path) path = "/run/qvue-warp/ctl.sock";
    int timeout_ms = 10000, i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--socket") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_ms = (int)(atof(argv[++i]) * 1000);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) i = argc + 1;
        else break;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: %s [--socket PATH] [--timeout SEC] COMMAND [ARGS...]\n"
                        "commands: status | on | off | toggle | suspend | resume | tune on|off |\n"
                        "  tune source grid|fb0 | select panel N|next|prev | select handle NAME |\n"
                        "  select next|prev | nudge DX DY | step N | save | revert | reset | reload |\n"
                        "  dump /OUT.ppm [/FB0.ppm] | help\n", argv[0]);
        return 2;
    }
    char req[1024];
    size_t n = 0;
    for (int k = i; k < argc; k++) {
        int w = snprintf(req + n, sizeof req - n, "%s%s", k > i ? " " : "", argv[k]);
        if (w < 0 || (size_t)w >= sizeof req - n - 1) { fprintf(stderr, "request too long\n"); return 2; }
        n += (size_t)w;
    }
    req[n++] = '\n';

    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof a.sun_path) { fprintf(stderr, "socket path too long\n"); return 2; }
    strcpy(a.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) {
        fprintf(stderr, "qvue-warpctl: %s: %s (is qvue-warpd running?)\n", path, strerror(errno));
        return 2;
    }
    if (send(fd, req, n, MSG_NOSIGNAL) != (ssize_t)n) { perror("qvue-warpctl: send"); return 2; }
    char buf[2048];
    size_t len = 0;
    while (len < sizeof buf - 1) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int rc = poll(&p, 1, timeout_ms);
        if (rc <= 0) { fprintf(stderr, "qvue-warpctl: no reply\n"); return 2; }
        ssize_t r = recv(fd, buf + len, sizeof buf - 1 - len, 0);
        if (r <= 0) break;
        len += (size_t)r;
        if (memchr(buf, '\n', len)) break;
    }
    close(fd);
    buf[len] = 0;
    char *nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    if (!len) { fprintf(stderr, "qvue-warpctl: no reply\n"); return 2; }
    puts(buf);
    return strncmp(buf, "OK", 2) == 0 ? 0 : 1;
}
