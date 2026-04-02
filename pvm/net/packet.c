/**
 * net/packet.c — Packet / Frame Utility Functions
 *
 * Provides helpers for constructing, parsing, and validating PVM wire
 * frames.  All functions are OS-agnostic.
 */

#include <stdio.h>
#include <string.h>
#include "packet.h"

/* -------------------------------------------------------------------------
 * PVM wire-frame helpers
 *
 * PVM wire format:
 *   Byte 0:      proto_id  (ProtocolId enum)
 *   Bytes 1…N:  protocol-specific payload
 * ---------------------------------------------------------------------- */

/**
 * pvm_frame_build — Build a PVM wire frame in-place.
 *
 * Writes proto_id into out[0] and copies payload into out[1…].
 *
 * @param out        Output buffer (must be at least len+1 bytes).
 * @param proto_id   Protocol identifier byte.
 * @param payload    Pointer to the protocol-specific payload.
 * @param len        Length of payload in bytes.
 * @return           Total frame length (len + 1), or 0 on error.
 */
size_t pvm_frame_build(uint8_t *out, uint8_t proto_id,
                       const uint8_t *payload, size_t len)
{
    if (!out || len > PVM_MTU) return 0;
    out[0] = proto_id;
    if (len > 0 && payload)
        memcpy(out + 1, payload, len);
    return len + 1;
}

/**
 * pvm_frame_parse — Parse a PVM wire frame into a PvmPacket.
 *
 * @param frame    Raw frame buffer (first byte is proto_id).
 * @param frame_len Total frame length in bytes.
 * @param pkt      Output packet struct (data points into frame, not a copy).
 * @return         0 on success, -1 if the frame is too short.
 */
int pvm_frame_parse(const uint8_t *frame, size_t frame_len, PvmPacket *pkt)
{
    if (!frame || !pkt || frame_len < 2) return -1;
    pkt->proto_id = frame[0];
    pkt->data     = frame + 1;
    pkt->len      = frame_len - 1;
    return 0;
}

/* -------------------------------------------------------------------------
 * VESPER-LITE frame helpers
 * ---------------------------------------------------------------------- */

/**
 * vesper_frame_build — Construct a VESPER-LITE wire frame.
 *
 * Layout: [VesperHeader (4 B)] [payload]
 *
 * @param out      Output buffer (must be >= VESPER_HEADER_SIZE + payload_len).
 * @param type     Message type (VesperMsgType).
 * @param payload  Payload bytes.
 * @param len      Payload length.
 * @return         Total frame byte count, or 0 on error.
 */
size_t vesper_frame_build(uint8_t *out, uint8_t type,
                          const uint8_t *payload, size_t len)
{
    if (!out || len > VESPER_MAX_PAYLOAD) return 0;

    VesperHeader hdr;
    hdr.version = VESPER_VERSION;
    hdr.type    = type;
    hdr.length  = pvm_htons((uint16_t)len);

    memcpy(out, &hdr, VESPER_HEADER_SIZE);
    if (payload && len > 0)
        memcpy(out + VESPER_HEADER_SIZE, payload, len);

    return VESPER_HEADER_SIZE + len;
}

/**
 * vesper_frame_parse — Validate and parse a VESPER-LITE wire frame.
 *
 * @param frame      Raw VESPER frame (including header).
 * @param frame_len  Total byte count of the frame buffer.
 * @param hdr_out    Pointer to a VesperHeader struct to populate.
 * @param payload    Set to point at the payload region inside frame.
 * @param payload_len Set to the payload byte count.
 * @return           0 on success, -1 on parse error.
 */
int vesper_frame_parse(const uint8_t *frame, size_t frame_len,
                       VesperHeader *hdr_out,
                       const uint8_t **payload, size_t *payload_len)
{
    if (!frame || !hdr_out || !payload || !payload_len) return -1;
    if (frame_len < VESPER_HEADER_SIZE) return -1;

    memcpy(hdr_out, frame, VESPER_HEADER_SIZE);
    hdr_out->length = pvm_ntohs(hdr_out->length);

    if (frame_len < VESPER_HEADER_SIZE + hdr_out->length) return -1;

    *payload     = frame + VESPER_HEADER_SIZE;
    *payload_len = hdr_out->length;
    return 0;
}

/**
 * vesper_type_name — Return a human-readable string for a VesperMsgType.
 */
const char *vesper_type_name(uint8_t type)
{
    switch ((VesperMsgType)type) {
        case VESPER_TYPE_DATA: return "DATA";
        case VESPER_TYPE_CTRL: return "CTRL";
        case VESPER_TYPE_ACK:  return "ACK";
        case VESPER_TYPE_ERR:  return "ERR";
        default:               return "UNKNOWN";
    }
}

/**
 * proto_id_name — Return a human-readable string for a ProtocolId.
 */
const char *proto_id_name(uint8_t id)
{
    switch ((ProtocolId)id) {
        case PROTO_UDP:         return "UDP";
        case PROTO_VESPER_LITE: return "VESPER-LITE";
        default:                return "UNKNOWN";
    }
}
