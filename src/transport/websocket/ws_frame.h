/*
 * WebSocket Frame Encode/Decode — RFC 6455 §5.2
 *
 * Frame format (2-14 bytes header + payload):
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 * |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 * |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 * | |1|2|3|       |K|             |                               |
 * +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 * |     Extended payload length continued, if payload len == 127  |
 * + - - - - - - - - - - - - - - - +-------------------------------+
 * |                               |Masking-key, if MASK set to 1  |
 * +-------------------------------+-------------------------------+
 * | Masking-key (continued)       |          Payload Data         |
 * +-------------------------------- - - - - - - - - - - - - - - - +
 * :                     Payload Data continued ...                :
 * + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 * |                     Payload Data continued ...                |
 * +---------------------------------------------------------------+
 */
#ifndef NXP_WS_FRAME_H
#define NXP_WS_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Opcodes ────────────────────────────────────────────── */

#define WS_OP_CONTINUATION  0x0
#define WS_OP_TEXT          0x1
#define WS_OP_BINARY        0x2
#define WS_OP_CLOSE         0x8
#define WS_OP_PING          0x9
#define WS_OP_PONG          0xA

/* ── Frame flags ────────────────────────────────────────── */

#define WS_FLAG_FIN  0x80
#define WS_FLAG_MASK 0x80

/* ── Maximum frame sizes ────────────────────────────────── */

#define WS_MAX_FRAME_HEADER 14
#define WS_MAX_PAYLOAD      65536

/* ── Frame structure ────────────────────────────────────── */

typedef struct {
    uint8_t  opcode;
    bool     fin;
    bool     masked;
    uint8_t  mask_key[4];
    size_t   payload_len;
    uint8_t *payload;
    size_t   frame_total;  /* Total bytes needed for the full frame */
} ws_frame;

/* ── API ────────────────────────────────────────────────── */

/*
 * Encode a WS frame into a buffer.
 * Returns number of bytes written (header + payload), or 0 if buf too small.
 * Client→Server frames MUST be masked. Server→Client frames MUST NOT be masked.
 */
size_t ws_frame_encode(const ws_frame *f, uint8_t *buf, size_t buf_cap);

/*
 * Decode a WS frame from a buffer.
 * Returns number of bytes consumed, or 0 if incomplete/invalid.
 * On success, f->payload points into `buf` (no copy).
 * f->masked must be checked to determine if client→server (masked).
 */
size_t ws_frame_decode(ws_frame *f, const uint8_t *buf, size_t buf_len);

#endif /* NXP_WS_FRAME_H */
