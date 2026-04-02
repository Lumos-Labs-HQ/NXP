/*
 * NXP Frame Engine - Implementation
 *
 * Encodes and decodes all NXP frame types using QUIC-style varint encoding.
 */
#include "frame_internal.h"
#include "util/varint.h"

#include <string.h>

/* ── Helper: encode varint, advance pos, check bounds ── */

static inline size_t encode_varint(uint64_t val, uint8_t *buf, size_t buf_len, size_t pos) {
    if (pos >= buf_len) return 0;
    size_t n = nxp_varint_encode(val, &buf[pos], buf_len - pos);
    return n;
}

static inline size_t decode_varint(const uint8_t *buf, size_t buf_len, size_t pos, uint64_t *out) {
    if (pos >= buf_len) return 0;
    return nxp_varint_decode(&buf[pos], buf_len - pos, out);
}

/* ── Encoding ── */

size_t nxp_frame_encode_padding(uint64_t count, uint8_t *buf, size_t buf_len) {
    if (count == 0 || buf_len < count) return 0;
    memset(buf, 0, count);
    return (size_t)count;
}

size_t nxp_frame_encode_ping(uint8_t *buf, size_t buf_len) {
    if (buf_len < 1) return 0;
    buf[0] = NXP_FRAME_PING;
    return 1;
}

size_t nxp_frame_encode_ack(const nxp_frame_ack *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;

    size_t pos = 0;
    size_t n;

    /* Frame type */
    uint64_t frame_type = f->has_ecn ? NXP_FRAME_ACK_ECN : NXP_FRAME_ACK;
    n = encode_varint(frame_type, buf, buf_len, pos);
    if (n == 0) return 0;
    pos += n;

    /* Largest Acknowledged */
    n = encode_varint(f->largest_acked, buf, buf_len, pos);
    if (n == 0) return 0;
    pos += n;

    /* ACK Delay */
    n = encode_varint(f->ack_delay, buf, buf_len, pos);
    if (n == 0) return 0;
    pos += n;

    /* ACK Range Count */
    n = encode_varint(f->range_count, buf, buf_len, pos);
    if (n == 0) return 0;
    pos += n;

    /* First ACK Range */
    n = encode_varint(f->first_ack_range, buf, buf_len, pos);
    if (n == 0) return 0;
    pos += n;

    /* Additional ACK Ranges */
    for (uint32_t i = 0; i < f->range_count; i++) {
        /* Gap */
        n = encode_varint(f->ranges[i].gap, buf, buf_len, pos);
        if (n == 0) return 0;
        pos += n;

        /* ACK Range */
        n = encode_varint(f->ranges[i].ack_range, buf, buf_len, pos);
        if (n == 0) return 0;
        pos += n;
    }

    /* ECN counters */
    if (f->has_ecn) {
        n = encode_varint(f->ect0_count, buf, buf_len, pos);
        if (n == 0) return 0;
        pos += n;

        n = encode_varint(f->ect1_count, buf, buf_len, pos);
        if (n == 0) return 0;
        pos += n;

        n = encode_varint(f->ecn_ce_count, buf, buf_len, pos);
        if (n == 0) return 0;
        pos += n;
    }

    return pos;
}

size_t nxp_frame_encode_reset_stream(const nxp_frame_reset_stream *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;

    n = encode_varint(NXP_FRAME_RESET_STREAM, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->stream_id, buf, buf_len, pos);           if (n == 0) return 0; pos += n;
    n = encode_varint(f->error_code, buf, buf_len, pos);          if (n == 0) return 0; pos += n;
    n = encode_varint(f->final_size, buf, buf_len, pos);          if (n == 0) return 0; pos += n;

    return pos;
}

size_t nxp_frame_encode_stop_sending(const nxp_frame_stop_sending *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;

    n = encode_varint(NXP_FRAME_STOP_SENDING, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->stream_id, buf, buf_len, pos);           if (n == 0) return 0; pos += n;
    n = encode_varint(f->error_code, buf, buf_len, pos);          if (n == 0) return 0; pos += n;

    return pos;
}

size_t nxp_frame_encode_crypto(const nxp_frame_crypto *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;

    n = encode_varint(NXP_FRAME_CRYPTO, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->offset, buf, buf_len, pos);        if (n == 0) return 0; pos += n;
    n = encode_varint(f->length, buf, buf_len, pos);        if (n == 0) return 0; pos += n;

    /* Data */
    if (pos + f->length > buf_len) return 0;
    if (f->data != nullptr && f->length > 0) {
        memcpy(&buf[pos], f->data, f->length);
    }
    pos += f->length;

    return pos;
}

size_t nxp_frame_encode_new_token(const nxp_frame_new_token *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;

    n = encode_varint(NXP_FRAME_NEW_TOKEN, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->token_len, buf, buf_len, pos);        if (n == 0) return 0; pos += n;

    if (pos + f->token_len > buf_len) return 0;
    if (f->token != nullptr && f->token_len > 0) {
        memcpy(&buf[pos], f->token, f->token_len);
    }
    pos += f->token_len;

    return pos;
}

size_t nxp_frame_encode_stream(const nxp_frame_stream *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;

    /* Build type byte */
    uint8_t type_byte = NXP_FRAME_STREAM_BASE;
    if (f->has_offset) type_byte |= NXP_FRAME_STREAM_OFF;
    if (f->has_length) type_byte |= NXP_FRAME_STREAM_LEN;
    if (f->fin)        type_byte |= NXP_FRAME_STREAM_FIN;

    /* Type (always 1 byte since it's 0x08-0x0F) */
    if (buf_len < 1) return 0;
    buf[pos++] = type_byte;

    /* Stream ID */
    n = encode_varint(f->stream_id, buf, buf_len, pos); if (n == 0) return 0; pos += n;

    /* Offset (if present) */
    if (f->has_offset) {
        n = encode_varint(f->offset, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    }

    /* Length (if present) */
    if (f->has_length) {
        n = encode_varint(f->length, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    }

    /* Stream data */
    uint64_t data_len = f->has_length ? f->length : 0;
    if (data_len > 0) {
        if (pos + data_len > buf_len) return 0;
        if (f->data != nullptr) {
            memcpy(&buf[pos], f->data, data_len);
        }
        pos += data_len;
    }

    return pos;
}

size_t nxp_frame_encode_max_data(uint64_t max_data, uint8_t *buf, size_t buf_len) {
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_MAX_DATA, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(max_data, buf, buf_len, pos);           if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_max_stream_data(const nxp_frame_max_stream_data *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_MAX_STREAM_DATA, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->stream_id, buf, buf_len, pos);              if (n == 0) return 0; pos += n;
    n = encode_varint(f->max_stream_data, buf, buf_len, pos);        if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_max_streams(const nxp_frame_max_streams *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    uint64_t frame_type = f->is_bidi ? NXP_FRAME_MAX_STREAMS_BIDI : NXP_FRAME_MAX_STREAMS_UNI;
    n = encode_varint(frame_type, buf, buf_len, pos);     if (n == 0) return 0; pos += n;
    n = encode_varint(f->max_streams, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_data_blocked(uint64_t max_data, uint8_t *buf, size_t buf_len) {
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_DATA_BLOCKED, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(max_data, buf, buf_len, pos);               if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_stream_data_blocked(const nxp_frame_stream_data_blocked *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_STREAM_DATA_BLOCKED, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->stream_id, buf, buf_len, pos);                  if (n == 0) return 0; pos += n;
    n = encode_varint(f->max_stream_data, buf, buf_len, pos);            if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_streams_blocked(const nxp_frame_streams_blocked *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    uint64_t frame_type = f->is_bidi ? NXP_FRAME_STREAMS_BLOCKED_BIDI : NXP_FRAME_STREAMS_BLOCKED_UNI;
    n = encode_varint(frame_type, buf, buf_len, pos);     if (n == 0) return 0; pos += n;
    n = encode_varint(f->max_streams, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_new_connection_id(const nxp_frame_new_connection_id *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    if (f->cid.len > NXP_MAX_CID_LEN) return 0;
    size_t pos = 0, n;

    n = encode_varint(NXP_FRAME_NEW_CONNECTION_ID, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->seq_num, buf, buf_len, pos);                  if (n == 0) return 0; pos += n;
    n = encode_varint(f->retire_prior_to, buf, buf_len, pos);          if (n == 0) return 0; pos += n;

    /* CID length (1 byte) + CID + stateless reset token (16 bytes) */
    if (pos + 1 + f->cid.len + 16 > buf_len) return 0;
    buf[pos++] = f->cid.len;
    if (f->cid.len > 0) {
        memcpy(&buf[pos], f->cid.data, f->cid.len);
        pos += f->cid.len;
    }
    memcpy(&buf[pos], f->stateless_reset_token, 16);
    pos += 16;

    return pos;
}

size_t nxp_frame_encode_retire_connection_id(uint64_t seq_num, uint8_t *buf, size_t buf_len) {
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_RETIRE_CONNECTION_ID, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(seq_num, buf, buf_len, pos);                        if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_path_challenge(const uint8_t data[8], uint8_t *buf, size_t buf_len) {
    if (data == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_PATH_CHALLENGE, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    if (pos + 8 > buf_len) return 0;
    memcpy(&buf[pos], data, 8);
    pos += 8;
    return pos;
}

size_t nxp_frame_encode_path_response(const uint8_t data[8], uint8_t *buf, size_t buf_len) {
    if (data == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_PATH_RESPONSE, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    if (pos + 8 > buf_len) return 0;
    memcpy(&buf[pos], data, 8);
    pos += 8;
    return pos;
}

size_t nxp_frame_encode_connection_close(const nxp_frame_connection_close *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;

    uint64_t frame_type = f->is_app ? NXP_FRAME_CONNECTION_CLOSE_APP : NXP_FRAME_CONNECTION_CLOSE;
    n = encode_varint(frame_type, buf, buf_len, pos);    if (n == 0) return 0; pos += n;
    n = encode_varint(f->error_code, buf, buf_len, pos); if (n == 0) return 0; pos += n;

    /* Transport-level close includes the offending frame type */
    if (!f->is_app) {
        n = encode_varint(f->frame_type, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    }

    n = encode_varint(f->reason_len, buf, buf_len, pos); if (n == 0) return 0; pos += n;

    if (f->reason_len > 0) {
        if (pos + f->reason_len > buf_len) return 0;
        if (f->reason != nullptr) {
            memcpy(&buf[pos], f->reason, f->reason_len);
        }
        pos += f->reason_len;
    }

    return pos;
}

size_t nxp_frame_encode_handshake_done(uint8_t *buf, size_t buf_len) {
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_HANDSHAKE_DONE, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_heartbeat(uint64_t timestamp_us, uint8_t *buf, size_t buf_len) {
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_HEARTBEAT, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(timestamp_us, buf, buf_len, pos);        if (n == 0) return 0; pos += n;
    return pos;
}

size_t nxp_frame_encode_stream_priority(const nxp_frame_stream_priority *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_STREAM_PRIORITY, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->stream_id, buf, buf_len, pos);              if (n == 0) return 0; pos += n;
    if (pos >= buf_len) return 0;
    buf[pos++] = f->priority;
    return pos;
}

size_t nxp_frame_encode_datagram(const nxp_frame_datagram *f, uint8_t *buf, size_t buf_len) {
    if (f == nullptr || buf == nullptr) return 0;
    size_t pos = 0, n;
    n = encode_varint(NXP_FRAME_DATAGRAM, buf, buf_len, pos); if (n == 0) return 0; pos += n;
    n = encode_varint(f->length, buf, buf_len, pos);           if (n == 0) return 0; pos += n;

    if (f->length > 0) {
        if (pos + f->length > buf_len) return 0;
        if (f->data != nullptr) {
            memcpy(&buf[pos], f->data, f->length);
        }
        pos += f->length;
    }

    return pos;
}

/* ── Decoding ── */

size_t nxp_frame_decode(
    const uint8_t *buf, size_t buf_len,
    nxp_frame *out)
{
    if (buf == nullptr || out == nullptr || buf_len == 0) return 0;

    memset(out, 0, sizeof(*out));
    size_t pos = 0;

    /* Peek at type byte - PADDING is special (type byte IS the frame) */
    if (buf[0] == NXP_FRAME_PADDING) {
        /* Count consecutive zero bytes */
        uint64_t count = 0;
        while (pos < buf_len && buf[pos] == 0) {
            count++;
            pos++;
        }
        out->type = NXP_FRAME_PADDING;
        out->padding.count = count;
        return pos;
    }

    /* Check for STREAM frames (0x08-0x0F) before varint decode */
    if (nxp_frame_is_stream(buf[0])) {
        uint8_t type_byte = buf[pos++];
        out->type = NXP_FRAME_STREAM;

        bool has_off = (type_byte & NXP_FRAME_STREAM_OFF) != 0;
        bool has_len = (type_byte & NXP_FRAME_STREAM_LEN) != 0;
        bool fin     = (type_byte & NXP_FRAME_STREAM_FIN) != 0;

        out->stream.has_offset = has_off;
        out->stream.has_length = has_len;
        out->stream.fin = fin;

        size_t n;

        /* Stream ID */
        n = decode_varint(buf, buf_len, pos, &out->stream.stream_id);
        if (n == 0) return 0;
        pos += n;

        /* Offset */
        if (has_off) {
            n = decode_varint(buf, buf_len, pos, &out->stream.offset);
            if (n == 0) return 0;
            pos += n;
        }

        /* Length */
        if (has_len) {
            n = decode_varint(buf, buf_len, pos, &out->stream.length);
            if (n == 0) return 0;
            pos += n;
        } else {
            /* Without LEN, data extends to end of packet */
            out->stream.length = buf_len - pos;
        }

        /* Data pointer */
        if (out->stream.length > 0) {
            if (pos + out->stream.length > buf_len) return 0;
            out->stream.data = &buf[pos];
            pos += out->stream.length;
        }

        return pos;
    }

    /* Decode frame type as varint */
    uint64_t frame_type;
    size_t n = decode_varint(buf, buf_len, pos, &frame_type);
    if (n == 0) return 0;
    pos += n;

    switch (frame_type) {
    case NXP_FRAME_PING:
        out->type = NXP_FRAME_PING;
        return pos;

    case NXP_FRAME_ACK:
    case NXP_FRAME_ACK_ECN: {
        out->type = (nxp_frame_type)frame_type;
        out->ack.has_ecn = (frame_type == NXP_FRAME_ACK_ECN);

        n = decode_varint(buf, buf_len, pos, &out->ack.largest_acked); if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->ack.ack_delay);     if (n == 0) return 0; pos += n;

        uint64_t range_count;
        n = decode_varint(buf, buf_len, pos, &range_count); if (n == 0) return 0; pos += n;
        if (range_count > NXP_ACK_MAX_RANGES) return 0;
        out->ack.range_count = (uint32_t)range_count;

        n = decode_varint(buf, buf_len, pos, &out->ack.first_ack_range); if (n == 0) return 0; pos += n;

        for (uint32_t i = 0; i < out->ack.range_count; i++) {
            n = decode_varint(buf, buf_len, pos, &out->ack.ranges[i].gap);       if (n == 0) return 0; pos += n;
            n = decode_varint(buf, buf_len, pos, &out->ack.ranges[i].ack_range); if (n == 0) return 0; pos += n;
        }

        if (out->ack.has_ecn) {
            n = decode_varint(buf, buf_len, pos, &out->ack.ect0_count);  if (n == 0) return 0; pos += n;
            n = decode_varint(buf, buf_len, pos, &out->ack.ect1_count);  if (n == 0) return 0; pos += n;
            n = decode_varint(buf, buf_len, pos, &out->ack.ecn_ce_count); if (n == 0) return 0; pos += n;
        }

        return pos;
    }

    case NXP_FRAME_RESET_STREAM:
        out->type = NXP_FRAME_RESET_STREAM;
        n = decode_varint(buf, buf_len, pos, &out->reset_stream.stream_id);  if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->reset_stream.error_code); if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->reset_stream.final_size); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_STOP_SENDING:
        out->type = NXP_FRAME_STOP_SENDING;
        n = decode_varint(buf, buf_len, pos, &out->stop_sending.stream_id);  if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->stop_sending.error_code); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_CRYPTO:
        out->type = NXP_FRAME_CRYPTO;
        n = decode_varint(buf, buf_len, pos, &out->crypto.offset); if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->crypto.length); if (n == 0) return 0; pos += n;
        if (out->crypto.length > 0) {
            if (pos + out->crypto.length > buf_len) return 0;
            out->crypto.data = &buf[pos];
            pos += out->crypto.length;
        }
        return pos;

    case NXP_FRAME_NEW_TOKEN:
        out->type = NXP_FRAME_NEW_TOKEN;
        n = decode_varint(buf, buf_len, pos, &out->new_token.token_len); if (n == 0) return 0; pos += n;
        if (out->new_token.token_len > 0) {
            if (pos + out->new_token.token_len > buf_len) return 0;
            out->new_token.token = &buf[pos];
            pos += out->new_token.token_len;
        }
        return pos;

    case NXP_FRAME_MAX_DATA:
        out->type = NXP_FRAME_MAX_DATA;
        n = decode_varint(buf, buf_len, pos, &out->max_data.max_data); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_MAX_STREAM_DATA:
        out->type = NXP_FRAME_MAX_STREAM_DATA;
        n = decode_varint(buf, buf_len, pos, &out->max_stream_data.stream_id);       if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->max_stream_data.max_stream_data); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_MAX_STREAMS_BIDI:
        out->type = NXP_FRAME_MAX_STREAMS_BIDI;
        out->max_streams.is_bidi = true;
        n = decode_varint(buf, buf_len, pos, &out->max_streams.max_streams); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_MAX_STREAMS_UNI:
        out->type = NXP_FRAME_MAX_STREAMS_UNI;
        out->max_streams.is_bidi = false;
        n = decode_varint(buf, buf_len, pos, &out->max_streams.max_streams); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_DATA_BLOCKED:
        out->type = NXP_FRAME_DATA_BLOCKED;
        n = decode_varint(buf, buf_len, pos, &out->data_blocked.max_data); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_STREAM_DATA_BLOCKED:
        out->type = NXP_FRAME_STREAM_DATA_BLOCKED;
        n = decode_varint(buf, buf_len, pos, &out->stream_data_blocked.stream_id);       if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->stream_data_blocked.max_stream_data); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_STREAMS_BLOCKED_BIDI:
        out->type = NXP_FRAME_STREAMS_BLOCKED_BIDI;
        out->streams_blocked.is_bidi = true;
        n = decode_varint(buf, buf_len, pos, &out->streams_blocked.max_streams); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_STREAMS_BLOCKED_UNI:
        out->type = NXP_FRAME_STREAMS_BLOCKED_UNI;
        out->streams_blocked.is_bidi = false;
        n = decode_varint(buf, buf_len, pos, &out->streams_blocked.max_streams); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_NEW_CONNECTION_ID: {
        out->type = NXP_FRAME_NEW_CONNECTION_ID;
        n = decode_varint(buf, buf_len, pos, &out->new_connection_id.seq_num);          if (n == 0) return 0; pos += n;
        n = decode_varint(buf, buf_len, pos, &out->new_connection_id.retire_prior_to);  if (n == 0) return 0; pos += n;

        /* CID length (1 byte) */
        if (pos >= buf_len) return 0;
        uint8_t cid_len = buf[pos++];
        if (cid_len > NXP_MAX_CID_LEN) return 0;
        out->new_connection_id.cid.len = cid_len;

        if (pos + cid_len + 16 > buf_len) return 0;
        if (cid_len > 0) {
            memcpy(out->new_connection_id.cid.data, &buf[pos], cid_len);
            pos += cid_len;
        }
        memcpy(out->new_connection_id.stateless_reset_token, &buf[pos], 16);
        pos += 16;

        return pos;
    }

    case NXP_FRAME_RETIRE_CONNECTION_ID:
        out->type = NXP_FRAME_RETIRE_CONNECTION_ID;
        n = decode_varint(buf, buf_len, pos, &out->retire_connection_id.seq_num); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_PATH_CHALLENGE:
        out->type = NXP_FRAME_PATH_CHALLENGE;
        if (pos + 8 > buf_len) return 0;
        memcpy(out->path_challenge.data, &buf[pos], 8);
        pos += 8;
        return pos;

    case NXP_FRAME_PATH_RESPONSE:
        out->type = NXP_FRAME_PATH_RESPONSE;
        if (pos + 8 > buf_len) return 0;
        memcpy(out->path_response.data, &buf[pos], 8);
        pos += 8;
        return pos;

    case NXP_FRAME_CONNECTION_CLOSE:
    case NXP_FRAME_CONNECTION_CLOSE_APP:
        out->type = (nxp_frame_type)frame_type;
        out->connection_close.is_app = (frame_type == NXP_FRAME_CONNECTION_CLOSE_APP);

        n = decode_varint(buf, buf_len, pos, &out->connection_close.error_code); if (n == 0) return 0; pos += n;

        if (!out->connection_close.is_app) {
            n = decode_varint(buf, buf_len, pos, &out->connection_close.frame_type); if (n == 0) return 0; pos += n;
        }

        n = decode_varint(buf, buf_len, pos, &out->connection_close.reason_len); if (n == 0) return 0; pos += n;

        if (out->connection_close.reason_len > 0) {
            if (pos + out->connection_close.reason_len > buf_len) return 0;
            out->connection_close.reason = &buf[pos];
            pos += out->connection_close.reason_len;
        }
        return pos;

    case NXP_FRAME_HANDSHAKE_DONE:
        out->type = NXP_FRAME_HANDSHAKE_DONE;
        return pos;

    /* NXP extension frames */
    case NXP_FRAME_HEARTBEAT:
        out->type = NXP_FRAME_HEARTBEAT;
        n = decode_varint(buf, buf_len, pos, &out->heartbeat.timestamp_us); if (n == 0) return 0; pos += n;
        return pos;

    case NXP_FRAME_STREAM_PRIORITY:
        out->type = NXP_FRAME_STREAM_PRIORITY;
        n = decode_varint(buf, buf_len, pos, &out->stream_priority.stream_id); if (n == 0) return 0; pos += n;
        if (pos >= buf_len) return 0;
        out->stream_priority.priority = buf[pos++];
        return pos;

    case NXP_FRAME_DATAGRAM:
        out->type = NXP_FRAME_DATAGRAM;
        n = decode_varint(buf, buf_len, pos, &out->datagram.length); if (n == 0) return 0; pos += n;
        if (out->datagram.length > 0) {
            if (pos + out->datagram.length > buf_len) return 0;
            out->datagram.data = &buf[pos];
            pos += out->datagram.length;
        }
        return pos;

    default:
        /* Unknown frame type - cannot skip without knowing length */
        return 0;
    }
}
