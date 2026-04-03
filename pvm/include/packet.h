/**
 * packet.h — Packet / Frame Structures
 *
 * Defines the generic PVM packet container and the VESPER-LITE wire
 * format.  These types are shared by the core, modules, and dispatcher.
 */
#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * General PVM limits
 * ---------------------------------------------------------------------- */

/** Maximum size of any PVM frame in bytes (payload only). */
#define PVM_MTU  65535u

/* -------------------------------------------------------------------------
 * Generic PVM packet container
 *
 * Used internally to pass frames through the dispatcher without copying
 * the data.
 * ---------------------------------------------------------------------- */
typedef struct {
    const uint8_t *data;     /**< Pointer into a frame buffer (not owned). */
    size_t         len;      /**< Number of bytes pointed to by data.       */
    uint8_t        proto_id; /**< Protocol identifier (see ProtocolId).     */
} PvmPacket;

/* -------------------------------------------------------------------------
 * Protocol identifiers
 *
 * The PVM prepends one of these bytes to every outgoing wire frame so
 * the dispatcher can demultiplex incoming frames without ambiguity.
 * ---------------------------------------------------------------------- */
typedef enum {
    PROTO_UNKNOWN     = 0x00u, /**< Unidentified / error.                   */
    PROTO_UDP         = 0x01u, /**< Baseline UDP transport module.          */
    PROTO_VESPER_LITE = 0x02u, /**< VESPER-LITE custom protocol module.     */
} ProtocolId;

/* -------------------------------------------------------------------------
 * VESPER-LITE wire format
 *
 * Every VESPER-LITE frame (the inner payload after the PVM proto_id byte)
 * begins with a 4-byte fixed header:
 *
 *   Offset  Size  Field    Description
 *   ------  ----  -------  ------------------------------------------
 *      0      1   version  Protocol version (VESPER_VERSION = 0x01)
 *      1      1   type     Message type (VesperMsgType enum)
 *      2      2   length   Payload length in bytes, big-endian uint16_t
 *
 * Followed immediately by <length> bytes of payload.
 * ---------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  version; /**< Always VESPER_VERSION for current revision.    */
    uint8_t  type;    /**< One of the VesperMsgType values.                */
    uint16_t length;  /**< Payload byte count, big-endian.                 */
} VesperHeader;
#pragma pack(pop)

/** Current VESPER-LITE protocol version byte. */
#define VESPER_VERSION  0x01u

/** Byte size of the VESPER-LITE header. */
#define VESPER_HEADER_SIZE  ((size_t)sizeof(VesperHeader))

/** Maximum VESPER-LITE payload that fits in one PVM frame. */
#define VESPER_MAX_PAYLOAD  (PVM_MTU - VESPER_HEADER_SIZE - 1u) /* -1 for proto_id */

/** VESPER-LITE message type codes (stored in VesperHeader.type). */
typedef enum {
    VESPER_TYPE_DATA = 0x01u, /**< Data payload message.                   */
    VESPER_TYPE_CTRL = 0x02u, /**< Control / signalling message.           */
    VESPER_TYPE_ACK  = 0x03u, /**< Acknowledgement message.                */
    VESPER_TYPE_ERR  = 0xFFu, /**< Error / rejection message.              */
} VesperMsgType;

/* -------------------------------------------------------------------------
 * Byte-order helpers (big-endian ↔ host)
 * ---------------------------------------------------------------------- */

/** Encode a host uint16_t into big-endian byte order. */
static inline uint16_t pvm_htons(uint16_t v)
{
    return (uint16_t)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}

/** Decode a big-endian uint16_t into host byte order. */
static inline uint16_t pvm_ntohs(uint16_t v)
{
    return pvm_htons(v); /* symmetric */
}

#endif /* PACKET_H */
