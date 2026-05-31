/*
 * WebSocket Frame Encode/Decode — RFC 6455 §5.2 Implementation
 */
#include "ws_frame.h"
#include <string.h>

size_t ws_frame_encode(const ws_frame *f, uint8_t *buf, size_t buf_cap) {
    size_t pos = 0;

    /* First byte: FIN + opcode */
    buf[pos++] = (f->fin ? 0x80 : 0x00) | (f->opcode & 0x0F);

    /* Second byte: MASK + payload length */
    uint8_t mask_bit = f->masked ? 0x80 : 0x00;
    size_t p_len = f->payload_len;

    if (p_len <= 125) {
        buf[pos++] = mask_bit | (uint8_t)p_len;
    } else if (p_len <= 65535) {
        buf[pos++] = mask_bit | 126;
        buf[pos++] = (uint8_t)(p_len >> 8);
        buf[pos++] = (uint8_t)(p_len);
    } else {
        buf[pos++] = mask_bit | 127;
        for (int i = 7; i >= 0; i--) {
            buf[pos++] = (uint8_t)(p_len >> (i * 8));
        }
    }

    /* Masking key (if masked) */
    if (f->masked) {
        memcpy(buf + pos, f->mask_key, 4);
        pos += 4;
    }

    /* Payload */
    if (f->payload != nullptr && p_len > 0) {
        if (pos + p_len > buf_cap) return 0;
        memcpy(buf + pos, f->payload, p_len);

        /* Apply mask in-place */
        if (f->masked) {
            for (size_t i = 0; i < p_len; i++) {
                buf[pos + i] ^= f->mask_key[i % 4];
            }
        }
        pos += p_len;
    }

    return pos;
}

size_t ws_frame_decode(ws_frame *f, const uint8_t *buf, size_t buf_len) {
    if (buf_len < 2) return 0; /* Need at least 2 bytes */

    size_t pos = 0;

    /* First byte */
    f->fin    = (buf[pos] & 0x80) != 0;
    f->opcode = buf[pos] & 0x0F;
    pos++;

    /* Second byte */
    f->masked = (buf[pos] & 0x80) != 0;
    size_t p_len = buf[pos] & 0x7F;
    pos++;

    /* Extended payload length */
    if (p_len == 126) {
        if (buf_len < pos + 2) return 0;
        p_len = ((size_t)buf[pos] << 8) | buf[pos + 1];
        pos += 2;
    } else if (p_len == 127) {
        if (buf_len < pos + 8) return 0;
        p_len = 0;
        for (int i = 0; i < 8; i++) {
            p_len = (p_len << 8) | buf[pos + i];
        }
        pos += 8;
    }

    /* Masking key */
    if (f->masked) {
        if (buf_len < pos + 4) return 0;
        memcpy(f->mask_key, buf + pos, 4);
        pos += 4;
    }

    /* Payload */
    if (buf_len < pos + p_len) return 0;

    f->payload_len = p_len;
    f->payload     = (uint8_t *)(buf + pos); /* Points into caller's buffer */

    /* Unmask in-place if masked */
    if (f->masked) {
        for (size_t i = 0; i < p_len; i++) {
            f->payload[i] ^= f->mask_key[i % 4];
        }
    }

    f->frame_total = pos + p_len;
    return f->frame_total;
}
