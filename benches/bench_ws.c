/*
 * Benchmark: Raw TCP vs WebSocket Framing
 *
 * Compares:
 *   1. Raw TCP echo (baseline)
 *   2. WebSocket echo (HTTP upgrade + masked BINARY frames)
 *
 * Build:
 *   gcc -O3 -o bench_ws bench_ws.c \
 *       ../src/transport/websocket/ws_frame.c \
 *       ../src/transport/websocket/ws_handshake.c \
 *       -I../include -I../src -I../src/transport/websocket \
 *       -Iinclude $(pkg-config --cflags --libs openssl) \
 *       -lpthread -D_DEFAULT_SOURCE
 */
#include "ws_frame.h"
#include "ws_handshake.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define PORT_RAW    22000
#define PORT_WS     22001
#define MSG_SIZE    1024
#define ITERATIONS  10000

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in a = {.sin_family=AF_INET, .sin_port=htons((uint16_t)port),
                            .sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    bind(fd, (struct sockaddr*)&a, sizeof(a));
    listen(fd, 1);
    return fd;
}

static int connect_client(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {.sin_family=AF_INET, .sin_port=htons((uint16_t)port),
                            .sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    connect(fd, (struct sockaddr*)&a, sizeof(a));
    return fd;
}

/* ═════════════════════════════════════════════════════════
 *  Raw TCP
 * ═════════════════════════════════════════════════════════ */

static void *raw_echo(void *arg) {
    (void)arg;
    int fd = create_server_socket(PORT_RAW);
    int c = accept(fd, NULL, NULL);
    close(fd);
    uint8_t buf[65536];
    for (int i = 0; i < ITERATIONS; i++) {
        ssize_t n = recv(c, buf, MSG_SIZE, MSG_WAITALL);
        if (n > 0) send(c, buf, (size_t)n, 0);
    }
    close(c);
    return NULL;
}

static double bench_raw(void) {
    printf("\n=== RAW TCP ===\n");
    printf("  %d msgs x %d bytes\n", ITERATIONS, MSG_SIZE);

    pthread_t thr;
    pthread_create(&thr, NULL, raw_echo, NULL);
    usleep(30000);

    int fd = connect_client(PORT_RAW);
    uint8_t msg[MSG_SIZE]; memset(msg, 0xAB, MSG_SIZE);
    uint8_t buf[MSG_SIZE];

    double start = now_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        send(fd, msg, MSG_SIZE, 0);
        recv(fd, buf, MSG_SIZE, MSG_WAITALL);
    }
    double elapsed = now_ms() - start;

    close(fd);
    pthread_join(thr, NULL);
    return elapsed;
}

/* ═════════════════════════════════════════════════════════
 *  WebSocket
 * ═════════════════════════════════════════════════════════ */

static void *ws_echo(void *arg) {
    (void)arg;
    int fd = create_server_socket(PORT_WS);
    int c = accept(fd, NULL, NULL);
    close(fd);

    uint8_t buf[65536];
    ssize_t n = recv(c, buf, sizeof(buf)-1, 0);
    buf[n] = '\0';
    char key[25];
    ws_parse_server_handshake(buf, (size_t)n, key);

    uint8_t resp[1024];
    size_t rl = ws_build_server_response(key, resp, sizeof(resp));
    send(c, resp, rl, 0);

    size_t acc = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        ws_frame f;
        size_t consumed = 0;
        while (consumed == 0) {
            ssize_t rn = recv(c, buf + acc, sizeof(buf) - acc, 0);
            if (rn <= 0) goto ws_end;
            acc += (size_t)rn;
            consumed = ws_frame_decode(&f, buf, acc);
        }

        if (f.opcode == WS_OP_BINARY && f.payload_len > 0) {
            ws_frame out = {.opcode=WS_OP_BINARY, .fin=true, .masked=false,
                            .payload=f.payload, .payload_len=f.payload_len};
            uint8_t obuf[4096];
            size_t ol = ws_frame_encode(&out, obuf, sizeof(obuf));
            send(c, obuf, ol, 0);
        }

        memmove(buf, buf + consumed, acc - consumed);
        acc -= consumed;
    }
ws_end:
    close(c);
    return NULL;
}

static double bench_ws(void) {
    printf("\n=== WebSocket ===\n");
    printf("  %d msgs x %d bytes (masked BINARY frames)\n", ITERATIONS, MSG_SIZE);

    pthread_t thr;
    pthread_create(&thr, NULL, ws_echo, NULL);
    usleep(30000);

    int fd = connect_client(PORT_WS);

    /* Handshake */
    uint8_t hs[1024];
    size_t hsl = ws_build_client_handshake("/", "127.0.0.1", hs, sizeof(hs));
    send(fd, hs, hsl, 0);
    uint8_t buf[65536];
    recv(fd, buf, sizeof(buf), 0);

    /* Data loop */
    uint8_t msg[MSG_SIZE]; memset(msg, 0xAB, MSG_SIZE);
    size_t acc = 0;
    double start = now_ms();

    for (int i = 0; i < ITERATIONS; i++) {
        /* Encode masked frame */
        ws_frame f = {.opcode=WS_OP_BINARY, .fin=true, .masked=true,
                      .mask_key={0x12,0x34,0x56,0x78},
                      .payload=msg, .payload_len=MSG_SIZE};
        size_t fl = ws_frame_encode(&f, buf, sizeof(buf));
        send(fd, buf, fl, 0);

        /* Receive echo */
        ws_frame in;
        size_t consumed = 0;
        do {
            ssize_t rn = recv(fd, buf + acc, sizeof(buf) - acc, 0);
            if (rn <= 0) goto ws_done;
            acc += (size_t)rn;
            consumed = ws_frame_decode(&in, buf, acc);
        } while (consumed == 0);

        memmove(buf, buf + consumed, acc - consumed);
        acc -= consumed;
    }
ws_done:
    double elapsed = now_ms() - start;

    close(fd);
    pthread_join(thr, NULL);
    return elapsed;
}

/* ═════════════════════════════════════════════════════════ */

int main(void) {
    printf("=== WebSocket Frame Overhead Benchmark ===\n");

    double raw = bench_raw();
    double ws  = bench_ws();

    double raw_mbps = (ITERATIONS * MSG_SIZE * 2.0 / (raw / 1000.0)) / (1024.0 * 1024.0);
    double ws_mbps  = (ITERATIONS * MSG_SIZE * 2.0 / (ws / 1000.0)) / (1024.0 * 1024.0);
    double overhead = ((ws - raw) / raw) * 100.0;

    printf("\n--- Results ---\n");
    printf("  Raw TCP:    %.1f ms  (%.2f MB/s,  %.3f ms avg RTT)\n",
           raw, raw_mbps, raw / ITERATIONS);
    printf("  WebSocket:  %.1f ms  (%.2f MB/s,  %.3f ms avg RTT)\n",
           ws, ws_mbps, ws / ITERATIONS);
    printf("  Overhead:   %.1f%%\n", overhead);
    printf("  Frame cost: +6 bytes/msg (masked 2B hdr + 4B mask key)\n");
    printf("  Handshake:  1 round-trip (HTTP Upgrade)\n");

    return 0;
}
