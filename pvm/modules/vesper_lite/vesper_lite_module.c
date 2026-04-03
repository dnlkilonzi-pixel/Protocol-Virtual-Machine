/**
 * modules/vesper_lite/vesper_lite_module.c — VESPER-LITE Protocol Module
 *
 * Custom lightweight protocol with a fixed 4-byte header:
 *
 *   PVM wire frame layout:
 *   ┌──────────┬─────────┬────────┬─────────────────────┐
 *   │ proto_id │ version │  type  │ length (BE) │payload │
 *   │  1 byte  │ 1 byte  │ 1 byte │   2 bytes   │  …    │
 *   └──────────┴─────────┴────────┴─────────────────────┘
 *     0x02       0x01     VesperMsgType            variable
 *
 * proto_id  = PROTO_VESPER_LITE (0x02)
 * version   = VESPER_VERSION    (0x01)
 * type      = VESPER_TYPE_DATA for payload frames
 * length    = big-endian uint16_t count of payload bytes
 *
 * This module is compiled as a position-independent shared library.
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

/*
 * Transmit buffer layout:
 *   [0]       proto_id     (PROTO_VESPER_LITE = 0x02)
 *   [1]       version      (VESPER_VERSION    = 0x01)
 *   [2]       type         (VesperMsgType)
 *   [3-4]     length       (big-endian uint16_t)
 *   [5 … N]   payload
 */
#define VESPER_WIRE_OVERHEAD  (1u + VESPER_HEADER_SIZE)   /* proto_id + hdr */
static uint8_t tx_buf[PVM_MTU + VESPER_WIRE_OVERHEAD];

/* -------------------------------------------------------------------------
 * Helper — encode a big-endian uint16_t into two bytes
 * ---------------------------------------------------------------------- */
static void put_be16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)((v >> 8) & 0xFFu);
    dst[1] = (uint8_t)( v       & 0xFFu);
}

static uint16_t get_be16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

/* -------------------------------------------------------------------------
 * ProtocolModule function implementations
 * ---------------------------------------------------------------------- */

static int vesper_init(const PlatformOps *ops)
{
    if (!ops) return -1;
    g_pal        = ops;
    g_connected  = 0;
    g_remote_port = 0;
    memset(g_remote_addr, 0, sizeof(g_remote_addr));
    printf("[VESPER-LITE] Module initialised (header=%zu bytes).\n",
           VESPER_HEADER_SIZE);
    return 0;
}

static int vesper_connect(const char *addr, uint16_t port)
{
    if (!addr || port == 0) return -1;
    strncpy(g_remote_addr, addr, sizeof(g_remote_addr) - 1);
    g_remote_addr[sizeof(g_remote_addr) - 1] = '\0';
    g_remote_port = port;
    g_connected   = 1;
    printf("[VESPER-LITE] Connected to %s:%u.\n", addr, port);
    return 0;
}

static int vesper_send(const uint8_t *data, size_t len)
{
    if (!g_pal || !g_connected) {
        fprintf(stderr, "[VESPER-LITE] send: not connected.\n");
        return -1;
    }
    if (len > VESPER_MAX_PAYLOAD) {
        fprintf(stderr, "[VESPER-LITE] send: payload too large "
                        "(%zu > %zu bytes).\n", len, VESPER_MAX_PAYLOAD);
        return -1;
    }

    /* Build the PVM + VESPER wire frame in tx_buf:
     *   tx_buf[0]    = PROTO_VESPER_LITE
     *   tx_buf[1]    = VESPER_VERSION
     *   tx_buf[2]    = VESPER_TYPE_DATA
     *   tx_buf[3-4]  = big-endian payload length
     *   tx_buf[5…]   = payload                                         */
    tx_buf[0] = PROTO_VESPER_LITE;
    tx_buf[1] = VESPER_VERSION;
    tx_buf[2] = VESPER_TYPE_DATA;
    put_be16(&tx_buf[3], (uint16_t)len);
    if (data && len > 0)
        memcpy(&tx_buf[5], data, len);

    size_t frame_len = VESPER_WIRE_OVERHEAD + len;
    int sent = g_pal->send_frame(tx_buf, frame_len);
    if (sent < 0) {
        fprintf(stderr, "[VESPER-LITE] send_frame failed.\n");
        return -1;
    }
    /* Return user-payload bytes sent (subtract proto_id + VESPER header). */
    int user_sent = sent - (int)VESPER_WIRE_OVERHEAD;
    return (user_sent > 0) ? user_sent : 0;
}

static int vesper_receive(uint8_t *buffer, size_t max_len)
{
    if (!g_pal) return -1;

    uint8_t raw[PVM_MTU + VESPER_WIRE_OVERHEAD];
    int n = g_pal->recv_frame(raw, sizeof(raw));
    if (n < (int)(VESPER_WIRE_OVERHEAD + 0)) return -1;

    /* Validate PVM proto_id. */
    if (raw[0] != PROTO_VESPER_LITE) return -1;

    /* Validate VESPER header. */
    if (n < (int)(1 + VESPER_HEADER_SIZE)) return -1;
    if (raw[1] != VESPER_VERSION)            return -1;

    uint16_t payload_len = get_be16(&raw[3]);
    if ((size_t)n < VESPER_WIRE_OVERHEAD + payload_len) return -1;

    size_t copy_len = (payload_len < max_len) ? payload_len : max_len;
    memcpy(buffer, &raw[5], copy_len);
    return (int)copy_len;
}

static void vesper_close(void)
{
    g_connected   = 0;
    g_remote_port = 0;
    memset(g_remote_addr, 0, sizeof(g_remote_addr));
    printf("[VESPER-LITE] Connection closed.\n");
}

static void vesper_destroy(void)
{
    g_pal = NULL;
    printf("[VESPER-LITE] Module destroyed.\n");
}

/* -------------------------------------------------------------------------
 * Module descriptor
 * ---------------------------------------------------------------------- */

static ProtocolModule vesper_module = {
    .name    = "vesper_lite",
    .version = "1.0",
    .init    = vesper_init,
    .connect = vesper_connect,
    .send    = vesper_send,
    .receive = vesper_receive,
    .close   = vesper_close,
    .destroy = vesper_destroy,
};

/**
 * get_module — Module entry point.
 */
ProtocolModule *get_module(void)
{
    return &vesper_module;
}
