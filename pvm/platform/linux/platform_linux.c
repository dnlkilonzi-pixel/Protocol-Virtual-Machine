/**
 * platform/linux/platform_linux.c — Linux PAL Implementation
 *
 * Networking back-end for Linux using UDP sockets (AF_INET SOCK_DGRAM).
 * A raw-socket (AF_PACKET) path is preferred for real deployments; this
 * implementation uses the portable UDP fallback so the demo runs without
 * elevated privileges.
 *
 * Configuration via environment variables (all optional):
 *   PVM_REMOTE_HOST  — Destination IP address  (default: 127.0.0.1)
 *   PVM_REMOTE_PORT  — Destination UDP port     (default: 9001)
 *   PVM_LOCAL_PORT   — Local bind port          (default: 9001)
 *
 * When PVM_REMOTE_PORT == PVM_LOCAL_PORT on 127.0.0.1 the socket talks
 * to itself, enabling single-process loopback testing.
 *
 * NOTE: This file may include OS-specific headers.  ALL such inclusions
 * must remain within the /platform subtree.
 */

/* POSIX feature macros must appear before any system header. */
#define _POSIX_C_SOURCE 200112L

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

#include "platform.h"  /* PAL interface (OS-agnostic header) */

/* -------------------------------------------------------------------------
 * Module-private state
 * ---------------------------------------------------------------------- */

static int                sock_fd     = -1;
static struct sockaddr_in remote_addr;

/* -------------------------------------------------------------------------
 * Helper — read an unsigned 16-bit integer from an environment variable
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

static int linux_init(void)
{
    /* Create a non-blocking UDP socket. */
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd < 0) {
        perror("[PAL:Linux] socket");
        return -1;
    }

    /* Set non-blocking so poll() returns immediately. */
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("[PAL:Linux] fcntl");
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /* Allow address/port reuse to simplify re-initialisation. */
    int reuse = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Bind to the local port. */
    uint16_t local_port = env_port("PVM_LOCAL_PORT", 9001);

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(local_port);

    if (bind(sock_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("[PAL:Linux] bind");
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /* Record the remote destination. */
    const char *host      = getenv("PVM_REMOTE_HOST");
    if (!host || *host == '\0') host = "127.0.0.1";
    uint16_t remote_port  = env_port("PVM_REMOTE_PORT", 9001);

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &remote_addr.sin_addr) <= 0) {
        fprintf(stderr, "[PAL:Linux] Invalid remote host address: %s\n", host);
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }
    remote_addr.sin_port = htons(remote_port);

    printf("[PAL:Linux] UDP socket ready — bound to *:%u, sending to %s:%u\n",
           local_port, host, remote_port);
    return 0;
}

static int linux_send_frame(const uint8_t *data, size_t len)
{
    if (sock_fd < 0) return -1;

    ssize_t sent = sendto(sock_fd, data, len, 0,
                          (const struct sockaddr *)&remote_addr,
                          sizeof(remote_addr));
    if (sent < 0) {
        /* EAGAIN/EWOULDBLOCK is non-fatal on a non-blocking socket. */
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[PAL:Linux] sendto");
        return -1;
    }
    return (int)sent;
}

static int linux_recv_frame(uint8_t *buffer, size_t max_len)
{
    if (sock_fd < 0) return -1;

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    ssize_t n = recvfrom(sock_fd, buffer, max_len, 0,
                         (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[PAL:Linux] recvfrom");
        return -1;
    }
    return (int)n;
}

static int linux_poll(void)
{
    if (sock_fd < 0) return -1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_fd, &rfds);

    struct timeval tv = {0, 0}; /* non-blocking */
    int ret = select(sock_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("[PAL:Linux] select");
        return -1;
    }
    return FD_ISSET(sock_fd, &rfds) ? 1 : 0;
}

static void linux_shutdown(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
    printf("[PAL:Linux] Shutdown complete.\n");
}

/* -------------------------------------------------------------------------
 * Platform ops vtable
 * ---------------------------------------------------------------------- */

static const PlatformOps linux_ops = {
    .init       = linux_init,
    .send_frame = linux_send_frame,
    .recv_frame = linux_recv_frame,
    .poll       = linux_poll,
    .shutdown   = linux_shutdown,
};

/**
 * platform_get_ops — Linux implementation.
 * The Makefile links exactly one platform_*.c file per build, so this
 * symbol is unique in any given binary.
 */
const PlatformOps *platform_get_ops(void)
{
    return &linux_ops;
}
