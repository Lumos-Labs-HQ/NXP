/*
 * WebSocket Unit Tests
 *
 * Tests: frame encode/decode, HTTP handshake build/parse.
 * No network required — pure unit tests of WS protocol layer.
 */
#include "ws_frame.h"
#include "ws_handshake.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
static int total = 0;

#define CHECK(cond, msg) do { \
    total++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); fails++; } \
    else printf("  PASS: %s\n", msg); \
} while(0)

/* ── Frame encode/decode tests ────────────────────────── */

static void test_frame_small_unmasked(void) {
    uint8_t buf[256];
    ws_frame f = {
        .opcode = WS_OP_BINARY,
        .fin = true,
        .masked = false,
        .payload = (uint8_t *)"hello",
        .payload_len = 5,
    };
    size_t n = ws_frame_encode(&f, buf, sizeof(buf));
    CHECK(n == 7, "frame_small: encode 5 bytes → 7 frame bytes");

    ws_frame decoded;
    size_t consumed = ws_frame_decode(&decoded, buf, n);
    CHECK(consumed == 7, "frame_small: decode consumes 7 bytes");
    CHECK(decoded.fin, "frame_small: FIN set");
    CHECK(decoded.opcode == WS_OP_BINARY, "frame_small: opcode BINARY");
    CHECK(!decoded.masked, "frame_small: not masked");
    CHECK(decoded.payload_len == 5, "frame_small: payload_len=5");
    CHECK(memcmp(decoded.payload, "hello", 5) == 0, "frame_small: payload matches");
}

static void test_frame_masked(void) {
    uint8_t buf[256];
    ws_frame f = {
        .opcode = WS_OP_TEXT,
        .fin = true,
        .masked = true,
        .mask_key = {0x12, 0x34, 0x56, 0x78},
        .payload = (uint8_t *)"ABCD",
        .payload_len = 4,
    };
    size_t n = ws_frame_encode(&f, buf, sizeof(buf));
    CHECK(n == 10, "frame_masked: encode 4 bytes → 10 frame bytes (2 hdr + 4 mask + 4 payload)");

    ws_frame decoded;
    size_t consumed = ws_frame_decode(&decoded, buf, n);
    CHECK(consumed == 10, "frame_masked: decode consumes 10 bytes");
    CHECK(decoded.masked, "frame_masked: masked");
    CHECK(decoded.payload_len == 4, "frame_masked: payload_len=4");
    CHECK(memcmp(decoded.payload, "ABCD", 4) == 0, "frame_masked: payload recovered after unmask");
}

static void test_frame_large(void) {
    uint8_t buf[16 * 1024];
    uint8_t *payload = (uint8_t *)malloc(2000);
    memset(payload, 0xAB, 2000);

    ws_frame f = {
        .opcode = WS_OP_BINARY,
        .fin = true,
        .masked = false,
        .payload = payload,
        .payload_len = 2000,
    };
    size_t n = ws_frame_encode(&f, buf, sizeof(buf));
    CHECK(n == 2004, "frame_large: encode 2000 bytes → 2004 frame bytes (2 hdr + 2 ext + 2000)");

    ws_frame decoded;
    size_t consumed = ws_frame_decode(&decoded, buf, n);
    CHECK(consumed == 2004, "frame_large: decode consumes 2004 bytes");
    CHECK(decoded.payload_len == 2000, "frame_large: payload_len=2000");

    free(payload);
}

static void test_frame_opcodes(void) {
    uint8_t opcodes[] = {WS_OP_TEXT, WS_OP_BINARY, WS_OP_PING, WS_OP_PONG, WS_OP_CLOSE};
    const char *names[] = {"TEXT", "BINARY", "PING", "PONG", "CLOSE"};

    for (int i = 0; i < 5; i++) {
        uint8_t buf[256];
        ws_frame f = {
            .opcode = opcodes[i],
            .fin = true,
            .masked = false,
            .payload = (uint8_t *)"x",
            .payload_len = 1,
        };
        size_t n = ws_frame_encode(&f, buf, sizeof(buf));
        CHECK(n == 3, names[i]);

        ws_frame decoded;
        ws_frame_decode(&decoded, buf, n);
        CHECK(decoded.opcode == opcodes[i], names[i]);
    }
}

/* ── Handshake tests ───────────────────────────────────── */

static void test_handshake_client(void) {
    uint8_t buf[1024];
    size_t n = ws_build_client_handshake("/chat", "localhost:8080", buf, sizeof(buf));
    CHECK(n > 0, "hs_client: generates request");

    char *r = (char *)buf;
    CHECK(strncmp(r, "GET /chat HTTP/1.1", 18) == 0, "hs_client: GET line");
    CHECK(strstr(r, "Upgrade: websocket") != nullptr, "hs_client: Upgrade header");
    CHECK(strstr(r, "Sec-WebSocket-Key: ") != nullptr, "hs_client: Key header");
    CHECK(strstr(r, "Sec-WebSocket-Version: 13") != nullptr, "hs_client: Version header");
}

static void test_handshake_server_parse(void) {
    /* Build a valid request */
    uint8_t req_buf[1024];
    size_t n = ws_build_client_handshake("/test", "localhost:9000", req_buf, sizeof(req_buf));

    char key[25];
    nxp_result r = ws_parse_server_handshake(req_buf, n, key);
    CHECK(r.code == 0, "hs_parse: valid request parses");
    CHECK(strlen(key) == 24, "hs_parse: key is 24 chars");
}

static void test_handshake_server_response(void) {
    /* Fixed key from RFC 6455 example */
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    uint8_t resp[1024];
    size_t n = ws_build_server_response(key, resp, sizeof(resp));
    CHECK(n > 0, "hs_response: builds 101 response");
    CHECK(strncmp((char *)resp, "HTTP/1.1 101", 12) == 0, "hs_response: 101 status");
    CHECK(strstr((char *)resp, "Sec-WebSocket-Accept: ") != nullptr, "hs_response: Accept header");
    /* Known accept for that key */
    CHECK(strstr((char *)resp, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != nullptr,
          "hs_response: correct Accept hash");
}

static void test_handshake_roundtrip(void) {
    /* Client → Server → Client */
    uint8_t req[1024];
    size_t req_len = ws_build_client_handshake("/test", "localhost", req, sizeof(req));

    /* Extract key */
    char key[25];
    nxp_result r = ws_parse_server_handshake(req, req_len, key);
    CHECK(r.code == 0, "hs_roundtrip: parse key");

    /* Build response */
    uint8_t resp[1024];
    size_t resp_len = ws_build_server_response(key, resp, sizeof(resp));
    CHECK(resp_len > 0, "hs_roundtrip: build response");

    /* Client validates */
    bool valid = ws_validate_client_response(resp, resp_len, key);
    CHECK(valid, "hs_roundtrip: client validates response");
}

int main(void) {
    printf("=== NXP WebSocket Tests ===\n");

    printf("\n--- Frame Encoding ---\n");
    test_frame_small_unmasked();
    test_frame_masked();
    test_frame_large();
    test_frame_opcodes();

    printf("\n--- HTTP Handshake ---\n");
    test_handshake_client();
    test_handshake_server_parse();
    test_handshake_server_response();
    test_handshake_roundtrip();

    printf("\n=== Summary: %d/%d passed ===\n", total - fails, total);
    return fails ? 1 : 0;
}
