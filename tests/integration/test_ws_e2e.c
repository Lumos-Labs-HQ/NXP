/*
 * WebSocket E2E Test — RFC 6455 protocol over socketpair
 *
 * Tests: HTTP Upgrade handshake, masked/unmasked binary frames,
 * PING/PONG, CLOSE. No NXP layer — pure WS wire-level test.
 */
#include "../../src/transport/websocket/ws_frame.h"
#include "../../src/transport/websocket/ws_handshake.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static int fails = 0;
static int checks = 0;

#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); fails++; } \
    else printf("  PASS: %s\n", msg); \
} while(0)

int main(void) {
    printf("=== WebSocket E2E Test ===\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        printf("FAIL: socketpair\n"); return 1;
    }

    /* ── HTTP Upgrade round-trip ─────────────────────── */
    uint8_t buf[4096];
    size_t req_len = ws_build_client_handshake("/chat", "localhost:8080", buf, sizeof(buf));
    CHECK(req_len > 0, "build client handshake");
    send(sv[1], buf, req_len, 0);

    ssize_t n = recv(sv[0], buf, sizeof(buf), 0);
    CHECK(n > 0, "server receives request");

    char key[25];
    nxp_result r = ws_parse_server_handshake(buf, (size_t)n, key);
    CHECK(r.code == 0, "parse handshake");

    uint8_t resp[1024];
    size_t resp_len = ws_build_server_response(key, resp, sizeof(resp));
    CHECK(resp_len > 0, "build 101 response");
    send(sv[0], resp, resp_len, 0);

    n = recv(sv[1], buf, sizeof(buf), 0);
    CHECK(n > 0, "client receives 101");
    CHECK(ws_validate_client_response(buf, (size_t)n, key), "validate 101");

    /* ── Binary frame exchange ───────────────────────── */
    const char *msg = "HELLO_NXP_WS_E2E";
    size_t mlen = strlen(msg);

    /* Client → Server: masked BINARY */
    ws_frame f = {.opcode=WS_OP_BINARY, .fin=true, .masked=true,
                  .mask_key={0x12,0x34,0x56,0x78},
                  .payload=(uint8_t*)msg, .payload_len=mlen};
    size_t len = ws_frame_encode(&f, buf, sizeof(buf));
    CHECK(len > 0, "encode client frame (masked)");
    send(sv[1], buf, len, 0);

    n = recv(sv[0], buf, sizeof(buf), 0);
    CHECK(n > 0, "server receives frame");
    ws_frame in;
    size_t c = ws_frame_decode(&in, buf, (size_t)n);
    CHECK(c > 0, "server decodes frame");
    CHECK(in.masked && in.opcode == WS_OP_BINARY, "server: frame is masked BINARY");
    CHECK(in.payload_len == mlen && memcmp(in.payload, msg, mlen) == 0, "server: payload matches");

    /* Server → Client: unmasked echo */
    ws_frame echo = {.opcode=WS_OP_BINARY, .fin=true, .masked=false,
                     .payload=(uint8_t*)msg, .payload_len=mlen};
    len = ws_frame_encode(&echo, buf, sizeof(buf));
    CHECK(len > 0, "encode server frame (unmasked)");
    send(sv[0], buf, len, 0);

    n = recv(sv[1], buf, sizeof(buf), 0);
    CHECK(n > 0, "client receives echo");
    c = ws_frame_decode(&in, buf, (size_t)n);
    CHECK(c > 0 && !in.masked, "client: frame is unmasked");
    CHECK(in.payload_len == mlen && memcmp(in.payload, msg, mlen) == 0, "client: echo payload matches");

    /* ── PING/PONG ───────────────────────────────────── */
    ws_frame ping = {.opcode=WS_OP_PING, .fin=true, .masked=true,
                     .mask_key={1,2,3,4},
                     .payload=(uint8_t*)"ping", .payload_len=4};
    len = ws_frame_encode(&ping, buf, sizeof(buf));
    send(sv[1], buf, len, 0);
    n = recv(sv[0], buf, sizeof(buf), 0);
    c = ws_frame_decode(&in, buf, (size_t)n);
    CHECK(c > 0 && in.opcode == WS_OP_PING, "PING received");

    ws_frame pong = {.opcode=WS_OP_PONG, .fin=true, .masked=false,
                     .payload=(uint8_t*)"ping", .payload_len=4};
    len = ws_frame_encode(&pong, buf, sizeof(buf));
    send(sv[0], buf, len, 0);
    n = recv(sv[1], buf, sizeof(buf), 0);
    c = ws_frame_decode(&in, buf, (size_t)n);
    CHECK(c > 0 && in.opcode == WS_OP_PONG, "PONG received");

    /* ── CLOSE ───────────────────────────────────────── */
    ws_frame cls = {.opcode=WS_OP_CLOSE, .fin=true, .masked=true,
                    .mask_key={9,9,9,9},
                    .payload=(uint8_t*)"bye", .payload_len=3};
    len = ws_frame_encode(&cls, buf, sizeof(buf));
    send(sv[1], buf, len, 0);
    n = recv(sv[0], buf, sizeof(buf), 0);
    c = ws_frame_decode(&in, buf, (size_t)n);
    CHECK(c > 0 && in.opcode == WS_OP_CLOSE, "CLOSE received");

    close(sv[0]);
    close(sv[1]);

    printf("\n%s (%d checks, %d failed)\n",
           fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails > 0 ? 1 : 0;
}
