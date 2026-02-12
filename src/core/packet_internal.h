/*
 * NXP Packet Engine - Internal Header
 *
 * Binary wire format for NXP packets.
 * Long header (handshake) and short header (1-RTT data).
 */
#ifndef NXP_PACKET_INTERNAL_H
#define NXP_PACKET_INTERNAL_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Long Header (handshake packets):
 *
 * Byte 0: [1][F][Type:2][Reserved:2][PktNumLen:2]
 *   Bit 7 (header form):  1 = long header
 *   Bit 6 (fixed):        1 (must be set)
 *   Bits 5-4 (type):      00=Initial, 01=Handshake, 10=Retry, 11=0-RTT
 *   Bits 3-2 (reserved):  00 (protected by header protection)
 *   Bits 1-0 (pn_len):    Packet number length - 1 (0..3 => 1..4 bytes)
 *
 * Bytes 1-4: Version (0x4E585001)
 * Byte 5: DCID Length (0-20)
 * Bytes 6+: DCID
 * Next byte: SCID Length (0-20)
 * Next bytes: SCID
 * For Initial: Token Length (varint) + Token
 * Payload Length (varint)
 * Packet Number (1-4 bytes)
 * Payload (encrypted frames)
 * AEAD Tag (16 bytes, part of ciphertext)
 *
 *
 * Short Header (1-RTT data):
 *
 * Byte 0: [0][F][Spin][KeyPhase][Reserved:2][PktNumLen:2]
 *   Bit 7 (header form):  0 = short header
 *   Bit 6 (fixed):        1
 *   Bit 5 (spin):         Latency spin bit
 *   Bit 4 (key_phase):    Key phase for key rotation
 *   Bits 3-2 (reserved):  00 (header-protected)
 *   Bits 1-0 (pn_len):    Packet number length - 1
 *
 * Bytes 1+: DCID (length known from connection)
 * Packet Number (1-4 bytes)
 * Payload (encrypted frames)
 * AEAD Tag (16 bytes)
 */

/* First byte bit masks */
#define NXP_PKT_FORM_BIT       0x80   /* 1 = long, 0 = short */
#define NXP_PKT_FIXED_BIT      0x40   /* Must be 1 */

/* Long header masks */
#define NXP_PKT_LONG_TYPE_MASK 0x30   /* Bits 5-4 */
#define NXP_PKT_LONG_TYPE_SHIFT 4
#define NXP_PKT_LONG_RESERVED  0x0C   /* Bits 3-2 */
#define NXP_PKT_LONG_PN_LEN   0x03   /* Bits 1-0 */

/* Short header masks */
#define NXP_PKT_SHORT_SPIN    0x20   /* Bit 5 */
#define NXP_PKT_SHORT_KEY_PHASE 0x10  /* Bit 4 */
#define NXP_PKT_SHORT_RESERVED 0x0C  /* Bits 3-2 */
#define NXP_PKT_SHORT_PN_LEN  0x03   /* Bits 1-0 */

/* Maximum header sizes */
#define NXP_PKT_LONG_HEADER_MAX  (1 + 4 + 1 + NXP_MAX_CID_LEN + 1 + NXP_MAX_CID_LEN + 8 + 4)
#define NXP_PKT_SHORT_HEADER_MAX (1 + NXP_MAX_CID_LEN + 4)

/* Minimum packet sizes */
#define NXP_PKT_MIN_INITIAL_SIZE 1200  /* Like QUIC, initial must be >= 1200 */

/* Parsed long header */
typedef struct nxp_pkt_long_header {
    nxp_packet_type type;          /* INITIAL, HANDSHAKE, RETRY, ZERO_RTT */
    uint32_t        version;
    nxp_conn_id     dcid;
    nxp_conn_id     scid;

    /* Initial-only */
    const uint8_t  *token;         /* Points into the source buffer */
    uint64_t        token_len;

    uint64_t        payload_len;   /* Length of payload + pkt number */
    uint64_t        pkt_num;       /* Truncated packet number */
    uint8_t         pkt_num_len;   /* 1-4 bytes */

    /* Offsets into original buffer */
    size_t          header_len;    /* Total header size (up to end of pkt number) */
    size_t          pkt_num_offset;/* Offset of packet number in buffer */
} nxp_pkt_long_header;

/* Parsed short header */
typedef struct nxp_pkt_short_header {
    nxp_conn_id     dcid;
    bool            spin_bit;
    bool            key_phase;
    uint64_t        pkt_num;
    uint8_t         pkt_num_len;

    size_t          header_len;
    size_t          pkt_num_offset;
} nxp_pkt_short_header;

/* Unified parsed packet header */
typedef struct nxp_pkt_header {
    bool is_long;
    union {
        nxp_pkt_long_header  lng;
        nxp_pkt_short_header shrt;
    };
} nxp_pkt_header;

/* ── Encoding ── */

/*
 * Encode a long header into buf.
 * Returns bytes written, or 0 on error.
 * Does NOT encode the payload or AEAD tag.
 */
[[nodiscard]] size_t nxp_pkt_encode_long_header(
    const nxp_pkt_long_header *hdr,
    uint8_t *buf, size_t buf_len
);

/*
 * Encode a short header into buf.
 * Returns bytes written, or 0 on error.
 */
[[nodiscard]] size_t nxp_pkt_encode_short_header(
    const nxp_pkt_short_header *hdr,
    uint8_t dcid_len,
    uint8_t *buf, size_t buf_len
);

/* ── Decoding ── */

/*
 * Peek at the first byte to determine if this is a long or short header.
 */
[[nodiscard]] static inline bool nxp_pkt_is_long_header(uint8_t first_byte) {
    return (first_byte & NXP_PKT_FORM_BIT) != 0;
}

/*
 * Parse a long header from raw bytes.
 * On success returns NXP_SUCCESS and fills out hdr.
 * The buffer must contain at least the complete header.
 */
[[nodiscard]] nxp_result nxp_pkt_decode_long_header(
    const uint8_t *buf, size_t buf_len,
    nxp_pkt_long_header *hdr
);

/*
 * Parse a short header from raw bytes.
 * dcid_len must be known from the connection context.
 */
[[nodiscard]] nxp_result nxp_pkt_decode_short_header(
    const uint8_t *buf, size_t buf_len,
    uint8_t dcid_len,
    nxp_pkt_short_header *hdr
);

/*
 * Reconstruct full 64-bit packet number from truncated value.
 * Uses the expected PN and the truncated PN length.
 */
[[nodiscard]] uint64_t nxp_pkt_decode_pkt_num(
    uint64_t largest_acked,
    uint64_t truncated_pn,
    uint8_t  pn_len
);

/*
 * Compute the minimum packet number length needed for the given pn
 * relative to the largest acknowledged.
 */
[[nodiscard]] uint8_t nxp_pkt_num_len(uint64_t pkt_num, uint64_t largest_acked);

#endif /* NXP_PACKET_INTERNAL_H */
