/**
 * dispatcher.h — Packet Dispatcher Interface
 *
 * The dispatcher demultiplexes raw PVM wire frames arriving from the PAL
 * and routes them to the handler registered for each protocol.
 *
 * Wire frame layout expected by dispatcher_dispatch():
 *
 *   [0]      1 byte  — PVM protocol identifier (ProtocolId enum)
 *   [1 … N]  N bytes — Protocol-specific payload (module handles framing)
 */
#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <stdint.h>
#include <stddef.h>
#include "packet.h"

/** Maximum number of protocol handlers the dispatcher can hold. */
#define DISPATCHER_MAX_HANDLERS  16

/**
 * packet_handler_fn — Callback invoked when a frame arrives for a
 * specific protocol.
 *
 * @param pkt  Pointer to the demultiplexed packet.  The pkt->data field
 *             points to the protocol-specific payload (the PVM proto_id
 *             byte has already been stripped).  The caller owns the
 *             buffer; do not store the pointer past the callback return.
 */
typedef void (*packet_handler_fn)(const PvmPacket *pkt);

/**
 * dispatcher_init — Initialise the dispatcher.
 * Must be called before any other dispatcher function.
 * @return  0 on success, -1 on failure.
 */
int dispatcher_init(void);

/**
 * dispatcher_register — Register a handler for a given protocol.
 *
 * If a handler is already registered for proto_id it is replaced.
 * @param proto_id  Protocol identifier (ProtocolId enum value).
 * @param handler   Callback to invoke for frames of that protocol.
 * @return          0 on success, -1 if the table is full.
 */
int dispatcher_register(uint8_t proto_id, packet_handler_fn handler);

/**
 * dispatcher_dispatch — Demultiplex and deliver a raw PVM wire frame.
 *
 * Reads the first byte as the protocol identifier, looks up the handler,
 * and calls it with a PvmPacket that points to the remaining bytes.
 * Frames with an unknown proto_id are silently dropped.
 *
 * @param data  Pointer to the complete PVM wire frame.
 * @param len   Total frame length (including the 1-byte proto_id header).
 */
void dispatcher_dispatch(const uint8_t *data, size_t len);

/**
 * dispatcher_shutdown — Reset the dispatcher and release all state.
 */
void dispatcher_shutdown(void);

#endif /* DISPATCHER_H */
