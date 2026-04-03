/**
 * platform/windows/platform_windows.c — Windows PAL Implementation
 *
 * Networking back-end for Windows using Winsock 2 (WSAStartup).
 * Uses a UDP socket (AF_INET SOCK_DGRAM) with non-blocking I/O via
 * ioctlsocket(FIONBIO).
 *
 * Configuration via environment variables (all optional):
 *   PVM_REMOTE_HOST  — Destination IP address  (default: 127.0.0.1)
 *   PVM_REMOTE_PORT  — Destination UDP port     (default: 9001)
 *   PVM_LOCAL_PORT   — Local bind port          (default: 9001)
 *
 * NOTE: This file may include Windows-specific headers.  ALL such
 * inclusions must remain within the /platform subtree.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#include "platform.h"

/* -------------------------------------------------------------------------
 * Module-private state
 * ---------------------------------------------------------------------- */

static SOCKET            sock        = INVALID_SOCKET;
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

static int win_init(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[PAL:Windows] WSAStartup failed: %d\n",
                WSAGetLastError());
        return -1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "[PAL:Windows] socket() failed: %d\n",
                WSAGetLastError());
        WSACleanup();
        return -1;
    }

    /* Non-blocking mode. */
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);

    /* Bind to local port. */
    uint16_t local_port = env_port("PVM_LOCAL_PORT", 9001);

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = htons(local_port);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) {
        fprintf(stderr, "[PAL:Windows] bind() failed: %d\n",
                WSAGetLastError());
        closesocket(sock);
        sock = INVALID_SOCKET;
        WSACleanup();
        return -1;
    }

    /* Record remote destination. */
    const char *host     = getenv("PVM_REMOTE_HOST");
    if (!host || *host == '\0') host = "127.0.0.1";
    uint16_t remote_port = env_port("PVM_REMOTE_PORT", 9001);

    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &remote_addr.sin_addr) <= 0) {
        fprintf(stderr, "[PAL:Windows] Invalid remote host address: %s\n", host);
        closesocket(sock);
        sock = INVALID_SOCKET;
        WSACleanup();
        return -1;
    }
    remote_addr.sin_port = htons(remote_port);

    printf("[PAL:Windows] Winsock UDP ready — bound to *:%u, sending to %s:%u\n",
           local_port, host, remote_port);
    return 0;
}

static int win_send_frame(const uint8_t *data, size_t len)
{
    if (sock == INVALID_SOCKET) return -1;

    int sent = sendto(sock, (const char *)data, (int)len, 0,
                      (const struct sockaddr *)&remote_addr,
                      sizeof(remote_addr));
    if (sent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
            fprintf(stderr, "[PAL:Windows] sendto failed: %d\n", err);
        return -1;
    }
    return sent;
}

static int win_recv_frame(uint8_t *buffer, size_t max_len)
{
    if (sock == INVALID_SOCKET) return -1;

    struct sockaddr_in from;
    int from_len = sizeof(from);

    int n = recvfrom(sock, (char *)buffer, (int)max_len, 0,
                     (struct sockaddr *)&from, &from_len);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
            fprintf(stderr, "[PAL:Windows] recvfrom failed: %d\n", err);
        return -1;
    }
    return n;
}

static int win_poll(void)
{
    if (sock == INVALID_SOCKET) return -1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    struct timeval tv = {0, 0};
    int ret = select(0 /* ignored on Windows */, &rfds, NULL, NULL, &tv);
    if (ret == SOCKET_ERROR) return -1;
    return (ret > 0) ? 1 : 0;
}

static void win_shutdown(void)
{
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    WSACleanup();
    printf("[PAL:Windows] Shutdown complete.\n");
}

/* -------------------------------------------------------------------------
 * Platform ops vtable
 * ---------------------------------------------------------------------- */

static const PlatformOps win_ops = {
    .init       = win_init,
    .send_frame = win_send_frame,
    .recv_frame = win_recv_frame,
    .poll       = win_poll,
    .shutdown   = win_shutdown,
};

const PlatformOps *platform_get_ops(void)
{
    return &win_ops;
}
