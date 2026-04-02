/**
 * platform/macos/platform_macos.c — macOS PAL Implementation
 *
 * Networking back-end for macOS using BSD sockets (AF_INET SOCK_DGRAM).
 * Non-blocking I/O uses fcntl(O_NONBLOCK); availability polling uses
 * select(2) — both are POSIX-portable BSD interfaces available on macOS.
 *
 * Configuration via environment variables (all optional):
 *   PVM_REMOTE_HOST  — Destination IP address  (default: 127.0.0.1)
 *   PVM_REMOTE_PORT  — Destination UDP port     (default: 9001)
 *   PVM_LOCAL_PORT   — Local bind port          (default: 9001)
 *
 * NOTE: This file may include OS-specific headers.  ALL such inclusions
 * must remain within the /platform subtree.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

/* -------------------------------------------------------------------------
 * Module-private state
 * ---------------------------------------------------------------------- */

static int                sock_fd     = -1;
static struct sockaddr_in remote_addr;

/* -------------------------------------------------------------------------
 * Helper
 * ---------------------------------------------------------------------- */
static uint16_t env_port(const char *var, uint16_t default_val)
{
    const char *s = getenv(var);
    if (!s || *s == '\0') return default_val;
    int v = atoi(s);
    return (v > 0 && v < 65536) ? (uint16_t)v : default_val;
}

/* -------------------------------------------------------------------------
 * PAL implementation functions
 * ---------------------------------------------------------------------- */

static int macos_init(void)
{
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd < 0) {
        perror("[PAL:macOS] socket");
        return -1;
    }

    /* Non-blocking. */
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("[PAL:macOS] fcntl");
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /* Address reuse. */
    int reuse = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    /* Bind to local port. */
    uint16_t local_port = env_port("PVM_LOCAL_PORT", 9001);

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(local_port);
    local.sin_len         = sizeof(local);  /* BSD field */

    if (bind(sock_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("[PAL:macOS] bind");
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /* Record remote destination. */
    const char *host     = getenv("PVM_REMOTE_HOST");
    if (!host || *host == '\0') host = "127.0.0.1";
    uint16_t remote_port = env_port("PVM_REMOTE_PORT", 9001);

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &remote_addr.sin_addr) <= 0) {
        fprintf(stderr, "[PAL:macOS] Invalid remote host address: %s\n", host);
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }
    remote_addr.sin_port = htons(remote_port);
    remote_addr.sin_len  = sizeof(remote_addr);

    printf("[PAL:macOS] BSD UDP socket ready — bound to *:%u, sending to %s:%u\n",
           local_port, host, remote_port);
    return 0;
}

static int macos_send_frame(const uint8_t *data, size_t len)
{
    if (sock_fd < 0) return -1;

    ssize_t sent = sendto(sock_fd, data, len, 0,
                          (const struct sockaddr *)&remote_addr,
                          sizeof(remote_addr));
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[PAL:macOS] sendto");
        return -1;
    }
    return (int)sent;
}

static int macos_recv_frame(uint8_t *buffer, size_t max_len)
{
    if (sock_fd < 0) return -1;

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    ssize_t n = recvfrom(sock_fd, buffer, max_len, 0,
                         (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[PAL:macOS] recvfrom");
        return -1;
    }
    return (int)n;
}

static int macos_poll(void)
{
    if (sock_fd < 0) return -1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_fd, &rfds);

    struct timeval tv = {0, 0};
    int ret = select(sock_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("[PAL:macOS] select");
        return -1;
    }
    return FD_ISSET(sock_fd, &rfds) ? 1 : 0;
}

static void macos_shutdown(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
    printf("[PAL:macOS] Shutdown complete.\n");
}

/* -------------------------------------------------------------------------
 * Platform ops vtable
 * ---------------------------------------------------------------------- */

static const PlatformOps macos_ops = {
    .init       = macos_init,
    .send_frame = macos_send_frame,
    .recv_frame = macos_recv_frame,
    .poll       = macos_poll,
    .shutdown   = macos_shutdown,
};

const PlatformOps *platform_get_ops(void)
{
    return &macos_ops;
}
