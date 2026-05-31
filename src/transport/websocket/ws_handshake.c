/*
 * WebSocket HTTP Upgrade Handshake Implementation
 *
 * Dependencies: OpenSSL for SHA-1 + base64 (already linked by nxp_crypto)
 */
#include "ws_handshake.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Helpers ────────────────────────────────────────────── */

static void generate_key(char key[25]) {
    uint8_t random[16];
    /* Use simple time-based seed for portability */
    for (int i = 0; i < 16; i++) {
        random[i] = (uint8_t)(rand() & 0xFF);
    }

    /* Base64 encode 16 bytes → 24 chars + null */
    EVP_EncodeBlock((unsigned char *)key, random, 16);
    key[24] = '\0';
}

static void compute_accept(const char *key, char accept[29]) {
    /* accept = base64( SHA1( key + GUID ) ) */
    char combined[24 + 36 + 1];
    snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID);

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char *)combined, strlen(combined), hash);

    EVP_EncodeBlock((unsigned char *)accept, hash, SHA_DIGEST_LENGTH);
    accept[28] = '\0';
}

/* ── Client: Build Upgrade Request ─────────────────────── */

size_t ws_build_client_handshake(const char *path, const char *host,
                                  uint8_t *out, size_t out_cap) {
    if (path == nullptr) path = "/";
    if (host == nullptr) host = "localhost";

    char key[25];
    generate_key(key);

    int n = snprintf((char *)out, out_cap,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, key);

    if (n < 0 || (size_t)n >= out_cap) return 0;
    return (size_t)n;
}

/* ── Server: Parse Upgrade Request ─────────────────────── */

nxp_result ws_parse_server_handshake(const uint8_t *data, size_t len,
                                      char key_out[25]) {
    /* Quick check: must start with GET and contain Upgrade: websocket */
    const char *str = (const char *)data;
    if (len < 4 || strncmp(str, "GET ", 4) != 0) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    const char *upgrade = strstr(str, "Upgrade: websocket");
    if (upgrade == nullptr) {
        /* Also try with CRLF before */
        upgrade = strstr(str, "\r\nUpgrade: websocket");
        if (upgrade == nullptr) {
            return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
        }
    }

    const char *key_hdr = strstr(str, "Sec-WebSocket-Key: ");
    if (key_hdr == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    key_hdr += 19; /* Skip "Sec-WebSocket-Key: " */
    const char *key_end = strstr(key_hdr, "\r\n");
    if (key_end == nullptr) key_end = strchr(key_hdr, '\n');
    if (key_end == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    size_t klen = (size_t)(key_end - key_hdr);
    if (klen == 0 || klen > 24) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    memcpy(key_out, key_hdr, klen);
    key_out[klen] = '\0';

    return NXP_SUCCESS;
}

/* ── Server: Build 101 Response ────────────────────────── */

size_t ws_build_server_response(const char *key, uint8_t *out, size_t out_cap) {
    char accept[29];
    compute_accept(key, accept);

    int n = snprintf((char *)out, out_cap,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept);

    if (n < 0 || (size_t)n >= out_cap) return 0;
    return (size_t)n;
}

/* ── Client: Validate Server Response ──────────────────── */

bool ws_validate_client_response(const uint8_t *data, size_t len,
                                  const char *sent_key) {
    const char *str = (const char *)data;
    if (len < 12 || strncmp(str, "HTTP/1.1 101", 12) != 0) {
        return false;
    }

    char expected_accept[29];
    compute_accept(sent_key, expected_accept);

    /* Find Sec-WebSocket-Accept header */
    const char *accept_hdr = strstr(str, "Sec-WebSocket-Accept: ");
    if (accept_hdr == nullptr) return false;

    accept_hdr += 22;
    const char *end = strstr(accept_hdr, "\r\n");
    if (end == nullptr) end = strchr(accept_hdr, '\n');
    if (end == nullptr) return false;

    size_t alen = (size_t)(end - accept_hdr);
    if (alen != 28) return false;

    return memcmp(accept_hdr, expected_accept, 28) == 0;
}
