/**
 * negotiation.h — Cross-Device Protocol Negotiation
 *
 * Enables two or more PVM instances to discover each other's capabilities
 * and automatically agree on a mutually-supported protocol.
 *
 * The negotiation protocol works over the existing PAL:
 *
 *   1. Device A sends a CAPABILITY_OFFER listing its loaded modules.
 *   2. Device B responds with a CAPABILITY_ACCEPT choosing the best
 *      mutually supported protocol, or CAPABILITY_REJECT if none match.
 *   3. Both devices call pvm_switch() to the agreed protocol.
 *
 * This creates a self-negotiating protocol ecosystem.
 *
 * Wire format (over the PVM dispatcher, proto_id = 0xFE):
 *
 *   [0xFE]  [msg_type (1B)]  [count (1B)]  [name_list (variable)]
 *
 * Each name in name_list is a fixed 32-byte NUL-padded string.
 */
#ifndef NEGOTIATION_H
#define NEGOTIATION_H

#include <stdint.h>
#include <stddef.h>

/** Protocol ID reserved for negotiation control messages. */
#define PROTO_NEGOTIATION  0xFEu

/** Maximum protocols in a capability advertisement. */
#define NEGO_MAX_PROTOCOLS  16

/** Fixed width of each protocol name in wire format. */
#define NEGO_NAME_WIDTH     32

/* -------------------------------------------------------------------------
 * Negotiation message types
 * ---------------------------------------------------------------------- */
typedef enum {
    NEGO_OFFER   = 0x01, /**< "Here are my available protocols."           */
    NEGO_ACCEPT  = 0x02, /**< "I accept this protocol."                    */
    NEGO_REJECT  = 0x03, /**< "No mutually supported protocol found."      */
} NegoMsgType;

/* -------------------------------------------------------------------------
 * Capability descriptor — list of protocols a device supports
 * ---------------------------------------------------------------------- */
typedef struct {
    char    names[NEGO_MAX_PROTOCOLS][NEGO_NAME_WIDTH];
    int     count;
} NegoCapabilities;

/* -------------------------------------------------------------------------
 * Negotiation result
 * ---------------------------------------------------------------------- */
typedef struct {
    int  success;                  /**< 1 = protocol agreed, 0 = failed.    */
    char agreed_protocol[NEGO_NAME_WIDTH]; /**< Agreed protocol name.       */
    int  local_initiated;          /**< 1 = we sent the offer.              */
} NegoResult;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_nego_init — Initialise the negotiation subsystem.
 * @return  0 on success, -1 on failure.
 */
int pvm_nego_init(void);

/**
 * pvm_nego_shutdown — Release negotiation state.
 */
void pvm_nego_shutdown(void);

/**
 * pvm_nego_get_local_caps — Build a NegoCapabilities from loaded modules.
 *
 * Queries the PVM module registry and fills caps with the names of all
 * currently loaded protocol modules.
 */
void pvm_nego_get_local_caps(NegoCapabilities *caps);

/**
 * pvm_nego_send_offer — Send a capability offer to the remote device.
 *
 * Serialises the local capabilities and transmits them over the PAL
 * using proto_id = PROTO_NEGOTIATION.
 *
 * @return  0 on success, -1 on error.
 */
int pvm_nego_send_offer(void);

/**
 * pvm_nego_receive_offer — Receive and process an incoming offer.
 *
 * Parses the remote capabilities, finds the best mutual protocol,
 * and sends back an ACCEPT or REJECT.
 *
 * @param result  Filled with the negotiation outcome.
 * @return        0 on success, -1 on error.
 */
int pvm_nego_receive_offer(NegoResult *result);

/**
 * pvm_nego_find_common — Find the best mutually supported protocol.
 *
 * Given local and remote capability lists, returns the first matching
 * protocol name (in local preference order).
 *
 * @param local   Local capabilities.
 * @param remote  Remote capabilities.
 * @param out     Output: matched protocol name (NUL-terminated).
 * @return        0 if a match was found, -1 if no common protocol exists.
 */
int pvm_nego_find_common(const NegoCapabilities *local,
                         const NegoCapabilities *remote,
                         char *out);

/**
 * pvm_nego_serialize_caps — Serialize capabilities into a wire buffer.
 *
 * @param caps     Capabilities to serialize.
 * @param msg_type Message type (NEGO_OFFER, NEGO_ACCEPT, NEGO_REJECT).
 * @param out      Output buffer.
 * @param max_len  Buffer capacity.
 * @return         Number of bytes written, or -1 on error.
 */
int pvm_nego_serialize_caps(const NegoCapabilities *caps,
                            uint8_t msg_type,
                            uint8_t *out, size_t max_len);

/**
 * pvm_nego_deserialize_caps — Parse a wire buffer into capabilities.
 *
 * @param data      Wire data (after proto_id byte is stripped).
 * @param len       Data length.
 * @param msg_type  Output: the message type byte.
 * @param caps      Output: parsed capabilities.
 * @return          0 on success, -1 on parse error.
 */
int pvm_nego_deserialize_caps(const uint8_t *data, size_t len,
                              uint8_t *msg_type,
                              NegoCapabilities *caps);

/**
 * pvm_nego_print_caps — Print capabilities to stdout.
 */
void pvm_nego_print_caps(const char *label, const NegoCapabilities *caps);

#endif /* NEGOTIATION_H */
