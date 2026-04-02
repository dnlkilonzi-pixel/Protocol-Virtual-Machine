/**
 * net/dispatcher.c — Packet Dispatcher Implementation
 *
 * Maintains a table of (proto_id → handler) pairs.  When a raw PVM
 * wire frame arrives the dispatcher strips the 1-byte proto_id header
 * and invokes the registered callback.
 *
 * Thread-safety: the current implementation is single-threaded.
 * For multi-threaded use, add a mutex around the handler table.
 */

#include <stdio.h>
#include <string.h>
#include "dispatcher.h"

/* -------------------------------------------------------------------------
 * Internal handler table
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t           proto_id;
    packet_handler_fn handler;
} HandlerEntry;

static HandlerEntry handler_table[DISPATCHER_MAX_HANDLERS];
static int          handler_count  = 0;
static int          disp_initialized = 0;

/* =========================================================================
 * Public API
 * ====================================================================== */

int dispatcher_init(void)
{
    memset(handler_table, 0, sizeof(handler_table));
    handler_count    = 0;
    disp_initialized = 1;
    return 0;
}

int dispatcher_register(uint8_t proto_id, packet_handler_fn handler)
{
    if (!disp_initialized) return -1;
    if (!handler)          return -1;

    /* Replace existing entry for this proto_id if present. */
    for (int i = 0; i < handler_count; ++i) {
        if (handler_table[i].proto_id == proto_id) {
            handler_table[i].handler = handler;
            return 0;
        }
    }

    /* Add a new entry. */
    if (handler_count >= DISPATCHER_MAX_HANDLERS) {
        fprintf(stderr, "[Dispatcher] Handler table full.\n");
        return -1;
    }

    handler_table[handler_count].proto_id = proto_id;
    handler_table[handler_count].handler  = handler;
    ++handler_count;
    return 0;
}

void dispatcher_dispatch(const uint8_t *data, size_t len)
{
    if (!disp_initialized || !data || len < 2) {
        /* Frame too short to contain a proto_id + at least 1 byte payload. */
        return;
    }

    uint8_t proto_id = data[0];

    /* Look up the handler. */
    for (int i = 0; i < handler_count; ++i) {
        if (handler_table[i].proto_id == proto_id) {
            PvmPacket pkt;
            pkt.proto_id = proto_id;
            pkt.data     = data + 1;   /* strip the proto_id header byte */
            pkt.len      = len  - 1;
            handler_table[i].handler(&pkt);
            return;
        }
    }

    /* No handler registered — drop the frame silently. */
    fprintf(stderr, "[Dispatcher] No handler for proto_id=0x%02X, dropping %zu bytes.\n",
            proto_id, len);
}

void dispatcher_shutdown(void)
{
    memset(handler_table, 0, sizeof(handler_table));
    handler_count    = 0;
    disp_initialized = 0;
}
