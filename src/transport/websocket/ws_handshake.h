/*
 * WebSocket HTTP Upgrade Handshake — RFC 6455 §4
 *
 * Client sends:
 *   GET /path HTTP/1.1
 *   Host: host:port
 *   Upgrade: websocket
 *   Connection: Upgrade
 *   Sec-WebSocket-Key: <base64-encoded 16 random bytes>
 *   Sec-WebSocket-Version: 13
 *
 * Server responds:
 *   HTTP/1.1 101 Switching Protocols
 *   Upgrade: websocket
 *   Connection: Upgrade
 *   Sec-WebSocket-Accept: <base64 SHA-1 of Key + GUID>
 */
#ifndef NXP_WS_HANDSHAKE_H
#define NXP_WS_HANDSHAKE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "nxp/nxp_error.h"

/* ── Constants ──────────────────────────────────────────── */

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MAX_HANDSHAKE_BUF 1024

/* ── Client: Build Upgrade Request ─────────────────────── */

/*
 * Build an HTTP Upgrade request.
 * path:  URL path (e.g. "/chat")
 * host:  Host header value (e.g. "localhost:8080")
 * out:   output buffer
 * Returns number of bytes written, or 0 if buffer too small.
 */
size_t ws_build_client_handshake(const char *path, const char *host,
                                  uint8_t *out, size_t out_cap);

/* ── Server: Parse Upgrade Request ─────────────────────── */

/*
 * Parse a client's Upgrade request and extract the Sec-WebSocket-Key.
 * Returns NXP_SUCCESS if valid, error code otherwise.
 * key_out: 24-byte buffer for the base64 key value.
 */
nxp_result ws_parse_server_handshake(const uint8_t *data, size_t len,
                                      char key_out[25]);

/* ── Server: Build 101 Response ────────────────────────── */

/*
 * Build a 101 Switching Protocols response.
 * key: Sec-WebSocket-Key from client request (24 chars + null).
 * out: output buffer.
 * Returns number of bytes written, or 0 if buffer too small.
 */
size_t ws_build_server_response(const char *key, uint8_t *out, size_t out_cap);

/* ── Client: Validate Server Response ──────────────────── */

/*
 * Validate the server's 101 response against the key we sent.
 * key: the Sec-WebSocket-Key we sent (24 chars + null).
 * Returns true if valid 101 with correct Accept hash.
 */
bool ws_validate_client_response(const uint8_t *data, size_t len,
                                  const char *sent_key);

#endif /* NXP_WS_HANDSHAKE_H */
