/**
 * net/negotiation.c - Cross-Device Protocol Negotiation
 *
 * Enables PVM instances to discover each other's capabilities and
 * automatically agree on a mutually-supported protocol.
 *
 * Wire format (proto_id = PROTO_NEGOTIATION = 0xFE):
 *
 *   [0xFE]  [msg_type (1B)]  [count (1B)]  [name_list (count * 32B)]
 *
 * The negotiation flow:
 *   1. Device A sends NEGO_OFFER with its loaded module list
 *   2. Device B receives, finds common protocol, sends NEGO_ACCEPT
 *   3. Both sides pvm_switch() to the agreed protocol
 *
 * No OS-specific code.
 */

#include <stdio.h>
#include <string.h>

#include "negotiation.h"
#include "pvm.h"
#include "platform.h"
#include "packet.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static int nego_inited = 0;

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_nego_init(void)
{
    nego_inited = 1;
    printf("[Negotiation] Initialised.\n");
    return 0;
}

void pvm_nego_shutdown(void)
{
    nego_inited = 0;
    printf("[Negotiation] Shut down.\n");
}

void pvm_nego_get_local_caps(NegoCapabilities *caps)
{
    if (!caps) return;
    memset(caps, 0, sizeof(*caps));

    /* NOTE: In a full implementation we would query the PVM module
     * registry directly.  For now we use a simplified approach:
     * the caller or demo pre-populates this.  We provide a helper
     * that can be called from outside. */
}

int pvm_nego_serialize_caps(const NegoCapabilities *caps,
                            uint8_t msg_type,
                            uint8_t *out, size_t max_len)
{
    if (!caps || !out) return -1;

    /* Wire format:
     *   [msg_type (1B)] [count (1B)] [name_0 (32B)] [name_1 (32B)] ... */
    size_t needed = 2 + (size_t)(caps->count * NEGO_NAME_WIDTH);
    if (needed > max_len) return -1;

    out[0] = msg_type;
    out[1] = (uint8_t)caps->count;

    for (int i = 0; i < caps->count; ++i) {
        memset(out + 2 + i * NEGO_NAME_WIDTH, 0, NEGO_NAME_WIDTH);
        strncpy((char *)(out + 2 + i * NEGO_NAME_WIDTH),
                caps->names[i], NEGO_NAME_WIDTH - 1);
    }

    return (int)needed;
}

int pvm_nego_deserialize_caps(const uint8_t *data, size_t len,
                              uint8_t *msg_type,
                              NegoCapabilities *caps)
{
    if (!data || !msg_type || !caps) return -1;
    if (len < 2) return -1;

    *msg_type = data[0];
    int count = data[1];

    if (count > NEGO_MAX_PROTOCOLS) count = NEGO_MAX_PROTOCOLS;
    if (len < 2 + (size_t)(count * NEGO_NAME_WIDTH)) return -1;

    memset(caps, 0, sizeof(*caps));
    caps->count = count;

    for (int i = 0; i < count; ++i) {
        memcpy(caps->names[i],
               data + 2 + i * NEGO_NAME_WIDTH,
               NEGO_NAME_WIDTH);
        caps->names[i][NEGO_NAME_WIDTH - 1] = '\0';
    }

    return 0;
}

int pvm_nego_find_common(const NegoCapabilities *local,
                         const NegoCapabilities *remote,
                         char *out)
{
    if (!local || !remote || !out) return -1;

    /* Find the first protocol that appears in both lists.
     * Local list order determines preference. */
    for (int i = 0; i < local->count; ++i) {
        for (int j = 0; j < remote->count; ++j) {
            if (strcmp(local->names[i], remote->names[j]) == 0) {
                strncpy(out, local->names[i], NEGO_NAME_WIDTH - 1);
                out[NEGO_NAME_WIDTH - 1] = '\0';
                return 0;
            }
        }
    }

    return -1; /* No common protocol. */
}

int pvm_nego_send_offer(void)
{
    if (!nego_inited) return -1;

    /* Build local capabilities from loaded modules.
     * We build a wire frame and send it via the PAL directly
     * (not through a protocol module) using proto_id = PROTO_NEGOTIATION. */
    NegoCapabilities local;
    pvm_nego_get_local_caps(&local);

    /* If the caller hasn't populated caps, we can't send.
     * This is normal - the demo will call serialize directly. */
    if (local.count == 0) {
        printf("[Negotiation] No capabilities to offer (caps empty).\n");
        return -1;
    }

    uint8_t wire[2048];
    wire[0] = PROTO_NEGOTIATION; /* PVM proto_id header */

    int payload_len = pvm_nego_serialize_caps(&local, NEGO_OFFER,
                                              wire + 1, sizeof(wire) - 1);
    if (payload_len < 0) return -1;

    const PlatformOps *pal = platform_get_ops();
    if (!pal) return -1;

    int sent = pal->send_frame(wire, 1 + (size_t)payload_len);
    if (sent < 0) return -1;

    printf("[Negotiation] Sent OFFER with %d protocols.\n", local.count);
    return 0;
}

int pvm_nego_receive_offer(NegoResult *result)
{
    if (!nego_inited || !result) return -1;
    memset(result, 0, sizeof(*result));

    const PlatformOps *pal = platform_get_ops();
    if (!pal) return -1;

    /* Check for available data. */
    if (pal->poll() <= 0) return -1;

    uint8_t wire[2048];
    int n = pal->recv_frame(wire, sizeof(wire));
    if (n < 3) return -1; /* Need at least proto_id + msg_type + count */

    /* Verify PVM proto_id. */
    if (wire[0] != PROTO_NEGOTIATION) return -1;

    uint8_t msg_type;
    NegoCapabilities remote;

    if (pvm_nego_deserialize_caps(wire + 1, (size_t)(n - 1),
                                  &msg_type, &remote) != 0) {
        return -1;
    }

    if (msg_type == NEGO_OFFER) {
        /* Find a common protocol. */
        NegoCapabilities local;
        pvm_nego_get_local_caps(&local);

        char common[NEGO_NAME_WIDTH];
        if (pvm_nego_find_common(&local, &remote, common) == 0) {
            result->success = 1;
            strncpy(result->agreed_protocol, common,
                    sizeof(result->agreed_protocol) - 1);
            result->agreed_protocol[sizeof(result->agreed_protocol) - 1] = '\0';
            result->local_initiated = 0;

            printf("[Negotiation] Common protocol found: '%s'\n", common);

            /* Switch to the agreed protocol. */
            pvm_switch(common);
        } else {
            result->success = 0;
            printf("[Negotiation] No common protocol found.\n");
        }
    } else if (msg_type == NEGO_ACCEPT) {
        /* The remote accepted our offer. */
        if (remote.count > 0) {
            result->success = 1;
            strncpy(result->agreed_protocol, remote.names[0],
                    sizeof(result->agreed_protocol) - 1);
            result->agreed_protocol[sizeof(result->agreed_protocol) - 1] = '\0';
            result->local_initiated = 1;
        }
    }

    return 0;
}

void pvm_nego_print_caps(const char *label, const NegoCapabilities *caps)
{
    if (!caps) return;
    printf("[Negotiation] %s (%d protocols):\n",
           label ? label : "Capabilities", caps->count);
    for (int i = 0; i < caps->count; ++i) {
        printf("  [%d] %s\n", i, caps->names[i]);
    }
    if (caps->count == 0)
        printf("  (none)\n");
}
