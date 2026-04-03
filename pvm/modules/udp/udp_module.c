/**
 * modules/udp/udp_module.c — UDP Protocol Module
 *
 * Baseline transport module that passes user data through the PAL with
 * a one-byte PVM proto_id header prepended for dispatcher routing.
 *
 * Wire format on the UDP socket:
 *   [0x01 (PROTO_UDP)] [raw user payload]
 *
 * This module is compiled as a position-independent shared library:
 *   Linux/macOS  → udp.so / udp.dylib
 *   Windows      → udp.dll
 *
 * Entry point (MODULE_ENTRY_SYMBOL): get_module()
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "protocol.h"
#include "platform.h"
#include "packet.h"

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

static const PlatformOps *g_pal  = NULL;
static char               g_remote_addr[64];
static uint16_t           g_remote_port = 0;
static int                g_connected   = 0;

/* Transmit buffer: 1 byte proto_id + up to PVM_MTU bytes of user data. */
static uint8_t tx_buf[PVM_MTU + 1];

/* -------------------------------------------------------------------------
 * ProtocolModule function implementations
 * ---------------------------------------------------------------------- */

static int udp_init(const PlatformOps *ops)
{
    if (!ops) return -1;
    g_pal        = ops;
    g_connected  = 0;
    g_remote_port = 0;
    memset(g_remote_addr, 0, sizeof(g_remote_addr));
    printf("[UDP] Module initialised.\n");
    return 0;
}

static int udp_connect(const char *addr, uint16_t port)
{
    if (!addr || port == 0) return -1;
    strncpy(g_remote_addr, addr, sizeof(g_remote_addr) - 1);
    g_remote_addr[sizeof(g_remote_addr) - 1] = '\0';
    g_remote_port = port;
    g_connected   = 1;
    printf("[UDP] Connected to %s:%u.\n", addr, port);
    return 0;
}

static int udp_send(const uint8_t *data, size_t len)
{
    if (!g_pal || !g_connected) {
        fprintf(stderr, "[UDP] send: not connected.\n");
        return -1;
    }
    if (!data || len == 0) return 0;
    if (len > PVM_MTU) {
        fprintf(stderr, "[UDP] send: payload too large (%zu > %u).\n", len, PVM_MTU);
        return -1;
    }

    /* Build PVM wire frame: [proto_id | raw payload]. */
    tx_buf[0] = PROTO_UDP;
    memcpy(tx_buf + 1, data, len);

    int sent = g_pal->send_frame(tx_buf, len + 1);
    if (sent < 0) {
        fprintf(stderr, "[UDP] send_frame failed.\n");
        return -1;
    }
    /* Return number of user-payload bytes sent (excluding PVM header). */
    return (sent > 0) ? (sent - 1) : 0;
}

static int udp_receive(uint8_t *buffer, size_t max_len)
{
    if (!g_pal) return -1;

    /* The PVM core handles frame reception through the dispatcher;
     * this function provides a direct (non-dispatcher) receive path
     * for callers who want to bypass the dispatcher loop.             */
    uint8_t raw[PVM_MTU + 1];
    int n = g_pal->recv_frame(raw, sizeof(raw));
    if (n < 2) return -1; /* Need at least proto_id + 1 byte of data. */

    if (raw[0] != PROTO_UDP) {
        /* Frame belongs to a different protocol. */
        return -1;
    }

    size_t payload_len = (size_t)(n - 1);
    size_t copy_len    = (payload_len < max_len) ? payload_len : max_len;
    memcpy(buffer, raw + 1, copy_len);
    return (int)copy_len;
}

static void udp_close(void)
{
    g_connected   = 0;
    g_remote_port = 0;
    memset(g_remote_addr, 0, sizeof(g_remote_addr));
    printf("[UDP] Connection closed.\n");
}

static void udp_destroy(void)
{
    g_pal = NULL;
    printf("[UDP] Module destroyed.\n");
}

/* -------------------------------------------------------------------------
 * Module descriptor
 * ---------------------------------------------------------------------- */

static ProtocolModule udp_module = {
    .name    = "udp",
    .version = "1.0",
    .init    = udp_init,
    .connect = udp_connect,
    .send    = udp_send,
    .receive = udp_receive,
    .close   = udp_close,
    .destroy = udp_destroy,
};

/**
 * get_module — Module entry point.
 *
 * The PVM core resolves this symbol by name (MODULE_ENTRY_SYMBOL) after
 * dlopen()ing the shared library.
 */
ProtocolModule *get_module(void)
{
    return &udp_module;
}
