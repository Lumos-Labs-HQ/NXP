/*
 * NXP Packet Engine - Implementation
 *
 * Binary wire format encoder/decoder for NXP long and short packet headers.
 */
#include "packet_internal.h"
#include "util/varint.h"
#include "platform/platform_endian.h"

#include <string.h>


/* ── Security Limits ── */
#define NXP_MAX_TOKEN_LEN       1024
#define NXP_MAX_PAYLOAD_LEN     65536

/* ── Encoding ── */

size_t nxp_pkt_encode_long_header(
    const nxp_pkt_long_header *hdr,
    uint8_t *buf, size_t buf_len)
{
    if (hdr == nullptr || buf == nullptr) return 0;
    if (hdr->pkt_num_len < 1 || hdr->pkt_num_len > 4) return 0;
    if (hdr->dcid.len > NXP_MAX_CID_LEN) return 0;
    if (hdr->scid.len > NXP_MAX_CID_LEN) return 0;

    /* Map packet type to 2-bit type field */
    uint8_t type_bits;
    switch (hdr->type) {
    case NXP_PKT_INITIAL:   type_bits = 0; break;
    case NXP_PKT_HANDSHAKE: type_bits = 1; break;
    case NXP_PKT_RETRY:     type_bits = 2; break;
    case NXP_PKT_ZERO_RTT:  type_bits = 3; break;
    default: return 0;
    }

    /* Calculate total header size */
    size_t pos = 0;
    size_t needed = (size_t)7 + hdr->dcid.len + hdr->scid.len;

    /* Initial packets include token length + token */
    size_t token_varint_len = 0;
    if (hdr->type == NXP_PKT_INITIAL) {
        token_varint_len = nxp_varint_len(hdr->token_len);
        if (token_varint_len == 0) return 0;
        needed += token_varint_len + hdr->token_len;
    }

    /* Payload length varint + packet number */
    size_t payload_varint_len = nxp_varint_len(hdr->payload_len);
    if (payload_varint_len == 0) return 0;
    needed += payload_varint_len + hdr->pkt_num_len;

    if (buf_len < needed) return 0;

    /* Byte 0: form=1, fixed=1, type, reserved=0, pn_len */
    buf[pos++] = (uint8_t)(NXP_PKT_FORM_BIT | NXP_PKT_FIXED_BIT |
                           (type_bits << NXP_PKT_LONG_TYPE_SHIFT) |
                           (hdr->pkt_num_len - 1));

    /* Version (4 bytes, big-endian) */
    nxp_write_u32_be(&buf[pos], hdr->version);
    pos += 4;

    /* DCID length + DCID */
    buf[pos++] = hdr->dcid.len;
    if (hdr->dcid.len > 0) {
        memcpy(&buf[pos], hdr->dcid.data, hdr->dcid.len);
        pos += hdr->dcid.len;
    }

    /* SCID length + SCID */
    buf[pos++] = hdr->scid.len;
    if (hdr->scid.len > 0) {
        memcpy(&buf[pos], hdr->scid.data, hdr->scid.len);
        pos += hdr->scid.len;
    }

    /* Token (Initial only) */
    if (hdr->type == NXP_PKT_INITIAL) {
        pos += nxp_varint_encode(hdr->token_len, &buf[pos], buf_len - pos);
        if (hdr->token_len > 0 && hdr->token != nullptr) {
            memcpy(&buf[pos], hdr->token, hdr->token_len);
            pos += hdr->token_len;
        }
    }

    /* Payload length */
    pos += nxp_varint_encode(hdr->payload_len, &buf[pos], buf_len - pos);

    /* Packet number (big-endian, 1-4 bytes) */
    for (uint8_t i = 0; i < hdr->pkt_num_len; i++) {
        uint8_t shift = (uint8_t)((hdr->pkt_num_len - 1 - i) * 8);
        buf[pos++] = (uint8_t)(hdr->pkt_num >> shift);
    }

    return pos;
}

size_t nxp_pkt_encode_short_header(
    const nxp_pkt_short_header *hdr,
    uint8_t dcid_len,
    uint8_t *buf, size_t buf_len)
{
    if (hdr == nullptr || buf == nullptr) return 0;
    if (hdr->pkt_num_len < 1 || hdr->pkt_num_len > 4) return 0;
    if (dcid_len > NXP_MAX_CID_LEN) return 0;

    size_t needed = (size_t)1 + dcid_len + hdr->pkt_num_len;
    if (buf_len < needed) return 0;

    size_t pos = 0;

    /* Byte 0: form=0, fixed=1, spin, key_phase, reserved=0, pn_len */
    buf[pos] = NXP_PKT_FIXED_BIT;
    if (hdr->spin_bit)  buf[pos] |= NXP_PKT_SHORT_SPIN;
    if (hdr->key_phase) buf[pos] |= NXP_PKT_SHORT_KEY_PHASE;
    buf[pos] |= (hdr->pkt_num_len - 1);
    pos++;

    /* DCID (length known from connection, not encoded) */
    if (dcid_len > 0) {
        memcpy(&buf[pos], hdr->dcid.data, dcid_len);
        pos += dcid_len;
    }

    /* Packet number (big-endian, 1-4 bytes) */
    for (uint8_t i = 0; i < hdr->pkt_num_len; i++) {
        uint8_t shift = (uint8_t)((hdr->pkt_num_len - 1 - i) * 8);
        buf[pos++] = (uint8_t)(hdr->pkt_num >> shift);
    }

    return pos;
}

/* ── Decoding ── */

nxp_result nxp_pkt_decode_long_header(
    const uint8_t *buf, size_t buf_len,
    nxp_pkt_long_header *hdr)
{
    if (buf == nullptr || hdr == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }
    if (buf_len < 7) { /* Minimum: 1 + 4 + 1 + 0 + 1 */
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    memset(hdr, 0, sizeof(*hdr));
    size_t pos = 0;

    /* Byte 0 */
    uint8_t first = buf[pos++];
    if (!(first & NXP_PKT_FORM_BIT)) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET); /* Not a long header */
    }
    if (!(first & NXP_PKT_FIXED_BIT)) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    uint8_t type_bits = (first & NXP_PKT_LONG_TYPE_MASK) >> NXP_PKT_LONG_TYPE_SHIFT;
    switch (type_bits) {
    case 0: hdr->type = NXP_PKT_INITIAL;   break;
    case 1: hdr->type = NXP_PKT_HANDSHAKE; break;
    case 2: hdr->type = NXP_PKT_RETRY;     break;
    case 3: hdr->type = NXP_PKT_ZERO_RTT;  break;
    }
    hdr->pkt_num_len = (first & NXP_PKT_LONG_PN_LEN) + 1;

    /* Version */
    if (pos + 4 > buf_len) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    hdr->version = nxp_read_u32_be(&buf[pos]);
    pos += 4;

    /* DCID */
    if (pos >= buf_len) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    hdr->dcid.len = buf[pos++];
    if (hdr->dcid.len > NXP_MAX_CID_LEN) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }
    if (pos + hdr->dcid.len > buf_len) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (hdr->dcid.len > 0) {
        memcpy(hdr->dcid.data, &buf[pos], hdr->dcid.len);
        pos += hdr->dcid.len;
    }

    /* SCID */
    if (pos >= buf_len) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    hdr->scid.len = buf[pos++];
    if (hdr->scid.len > NXP_MAX_CID_LEN) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }
    if (pos + hdr->scid.len > buf_len) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    if (hdr->scid.len > 0) {
        memcpy(hdr->scid.data, &buf[pos], hdr->scid.len);
        pos += hdr->scid.len;
    }

    /* Token (Initial packets only) */
    if (hdr->type == NXP_PKT_INITIAL) {
        uint64_t token_len;
        size_t consumed = nxp_varint_decode(&buf[pos], buf_len - pos, &token_len);
        if (consumed == 0) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
        pos += consumed;
        
        if (token_len > NXP_MAX_TOKEN_LEN) return NXP_ERROR(NXP_ERR_INVALID_PACKET);

        hdr->token_len = token_len;
        if (token_len > 0) {
            if (pos > buf_len || token_len > buf_len - pos) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
            hdr->token = &buf[pos];
            pos += (size_t)token_len;
        }
    }

    /* Retry packets have no payload length or packet number */
    if (hdr->type == NXP_PKT_RETRY) {
        hdr->header_len = pos;
        hdr->pkt_num_offset = 0;
        hdr->pkt_num = 0;
        hdr->pkt_num_len = 0;
        hdr->payload_len = buf_len - pos; /* Rest is retry data */
        return NXP_SUCCESS;
    }

    /* Payload length */
    uint64_t payload_len;
    size_t consumed = nxp_varint_decode(&buf[pos], buf_len - pos, &payload_len);
    if (consumed == 0) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    pos += consumed;
    
    if (payload_len > NXP_MAX_PAYLOAD_LEN) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    hdr->payload_len = payload_len;

    /* Packet number */
    hdr->pkt_num_offset = pos;
    if (pos + hdr->pkt_num_len > buf_len) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }
    hdr->pkt_num = 0;
    for (uint8_t i = 0; i < hdr->pkt_num_len; i++) {
        hdr->pkt_num = (hdr->pkt_num << 8) | buf[pos + i];
    }
    pos += hdr->pkt_num_len;

    hdr->header_len = pos;
    return NXP_SUCCESS;
}

nxp_result nxp_pkt_decode_short_header(
    const uint8_t *buf, size_t buf_len,
    uint8_t dcid_len,
    nxp_pkt_short_header *hdr)
{
    if (buf == nullptr || hdr == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }
    if (dcid_len > NXP_MAX_CID_LEN) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    memset(hdr, 0, sizeof(*hdr));
    size_t pos = 0;

    /* Need at least: 1 (first byte) + dcid_len + 1 (min pkt num) */
    if (buf_len < 1 + (size_t)dcid_len + 1) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    /* Byte 0 */
    uint8_t first = buf[pos++];
    if (first & NXP_PKT_FORM_BIT) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET); /* Not a short header */
    }
    if (!(first & NXP_PKT_FIXED_BIT)) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }

    hdr->spin_bit  = (first & NXP_PKT_SHORT_SPIN) != 0;
    hdr->key_phase = (first & NXP_PKT_SHORT_KEY_PHASE) != 0;
    hdr->pkt_num_len = (first & NXP_PKT_SHORT_PN_LEN) + 1;

    /* DCID */
    hdr->dcid.len = dcid_len;
    if (dcid_len > 0) {
        if (pos + dcid_len > buf_len) return NXP_ERROR(NXP_ERR_INVALID_PACKET);
        memcpy(hdr->dcid.data, &buf[pos], dcid_len);
        pos += dcid_len;
    }

    /* Packet number */
    hdr->pkt_num_offset = pos;
    if (pos + hdr->pkt_num_len > buf_len) {
        return NXP_ERROR(NXP_ERR_INVALID_PACKET);
    }
    hdr->pkt_num = 0;
    for (uint8_t i = 0; i < hdr->pkt_num_len; i++) {
        hdr->pkt_num = (hdr->pkt_num << 8) | buf[pos + i];
    }
    pos += hdr->pkt_num_len;

    hdr->header_len = pos;
    return NXP_SUCCESS;
}

/* ── Packet Number Reconstruction ── */

uint64_t nxp_pkt_decode_pkt_num(
    uint64_t largest_acked,
    uint64_t truncated_pn,
    uint8_t  pn_len)
{
    /*
     * RFC 9000, Appendix A: Sample Packet Number Decoding Algorithm
     *
     * The expected packet number is largest_acked + 1.
     * We find the value closest to expected that matches the
     * truncated_pn in the lower pn_len bytes.
     */
    uint64_t expected = largest_acked + 1;
    uint64_t pn_nbits = (uint64_t)pn_len * 8;
    uint64_t pn_win   = (uint64_t)1 << pn_nbits;
    uint64_t pn_hwin  = pn_win / 2;
    uint64_t pn_mask  = pn_win - 1;

    /* Replace the lower bits of expected with truncated_pn */
    uint64_t candidate = (expected & ~pn_mask) | truncated_pn;

    if (candidate + pn_hwin <= expected && candidate + pn_win < ((uint64_t)1 << 62)) {
        return candidate + pn_win;
    }
    if (candidate > expected + pn_hwin && candidate >= pn_win) {
        return candidate - pn_win;
    }
    return candidate;
}

uint8_t nxp_pkt_num_len(uint64_t pkt_num, uint64_t largest_acked) {
    /*
     * Determine the minimum number of bytes needed to encode pkt_num
     * such that the receiver (knowing largest_acked) can reconstruct it.
     * We need at least twice the distance from largest_acked.
     */
    uint64_t num_unacked = pkt_num - largest_acked;
    if (num_unacked < 0x80)       return 1;
    if (num_unacked < 0x8000)     return 2;
    if (num_unacked < 0x800000)   return 3;
    return 4;
}
