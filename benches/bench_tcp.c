/*
 * Benchmark: Raw vs Length-Prefixed Framing over socketpair
 *
 * Measures the overhead of NXP's 2-byte length prefix framing.
 * socketpair avoids TCP handshake/port issues — pure protocol overhead.
 *
 * Run: gcc -O3 -o bench_tcp bench_tcp.c -D_DEFAULT_SOURCE && ./bench_tcp
 */
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ITERATIONS  50000
#define MSG_SIZE    1024

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static double bench_raw(void) {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

    uint8_t msg[MSG_SIZE], buf[MSG_SIZE];
    memset(msg, 0xCD, MSG_SIZE);

    double start = now_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        write(sv[0], msg, MSG_SIZE);
        size_t total = 0;
        while (total < MSG_SIZE) total += (size_t)read(sv[1], buf + total, MSG_SIZE - total);
        write(sv[1], buf, MSG_SIZE);
        total = 0;
        while (total < MSG_SIZE) total += (size_t)read(sv[0], buf + total, MSG_SIZE - total);
    }
    double elapsed = now_ms() - start;

    close(sv[0]); close(sv[1]);
    return elapsed;
}

static double bench_framed(void) {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

    uint8_t msg[MSG_SIZE], buf[MSG_SIZE + 2];
    uint8_t hdr[2] = {(uint8_t)(MSG_SIZE >> 8), (uint8_t)(MSG_SIZE & 0xFF)};
    memset(msg, 0xCD, MSG_SIZE);

    double start = now_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        /* Send framed */
        write(sv[0], hdr, 2);
        write(sv[0], msg, MSG_SIZE);

        /* Receive framed */
        size_t total = 0;
        while (total < 2) total += (size_t)read(sv[1], buf + total, 2 - total);
        uint16_t len = ((uint16_t)buf[0] << 8) | buf[1];
        total = 0;
        while (total < len) total += (size_t)read(sv[1], buf + 2 + total, len - total);

        /* Echo back framed */
        write(sv[1], buf, 2);        /* length prefix */
        write(sv[1], buf + 2, len);   /* payload */

        /* Receive echo */
        total = 0;
        while (total < 2) total += (size_t)read(sv[0], buf + total, 2 - total);
        len = ((uint16_t)buf[0] << 8) | buf[1];
        total = 0;
        while (total < len) total += (size_t)read(sv[0], buf + total, len - total);
    }
    double elapsed = now_ms() - start;

    close(sv[0]); close(sv[1]);
    return elapsed;
}

int main(void) {
    printf("=== TCP Framing Overhead (socketpair) ===\n");
    printf("  %d msgs x %d bytes\n\n", ITERATIONS, MSG_SIZE);

    double raw = bench_raw();
    printf("  Raw echo:    %.1f ms\n", raw);

    double framed = bench_framed();
    printf("  Framed echo: %.1f ms\n", framed);

    double raw_mbps  = (ITERATIONS * MSG_SIZE * 2.0 / (raw / 1000.0)) / (1024.0 * 1024.0);
    double frm_mbps  = (ITERATIONS * MSG_SIZE * 2.0 / (framed / 1000.0)) / (1024.0 * 1024.0);
    double overhead  = ((framed - raw) / raw) * 100.0;

    printf("\n--- Results ---\n");
    printf("  Raw:     %.2f MB/s  (%.3f ms/msg)\n", raw_mbps, raw / ITERATIONS);
    printf("  Framed:  %.2f MB/s  (%.3f ms/msg)\n", frm_mbps, framed / ITERATIONS);
    printf("  Overhead: %.1f%%\n", overhead);
    printf("  Cost:     2 bytes per message (length prefix)\n");

    return 0;
}
