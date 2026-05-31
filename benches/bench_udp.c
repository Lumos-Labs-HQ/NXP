/*
 * Benchmark: NXP Encrypted UDP vs Raw UDP
 *
 * Measures throughput and latency of:
 *   1. Raw UDP: plain sendto/recvfrom loop
 *
 * Run: gcc -O3 -o bench_udp bench_udp.c -lpthread -D_DEFAULT_SOURCE && ./bench_udp
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define PORT         20000
#define PKT_SIZE     1400
#define ITERATIONS   100000

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void *raw_server(void *arg) {
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {.sin_family=AF_INET, .sin_port=htons(PORT),
                               .sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));

    uint8_t buf[65536];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);

    for (int i = 0; i < ITERATIONS; i++) {
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);
        if (n > 0) sendto(fd, buf, (size_t)n, 0, (struct sockaddr*)&from, flen);
    }
    close(fd);
    return NULL;
}

static void bench_raw_udp(void) {
    printf("\n=== RAW UDP Benchmark ===\n");
    printf("  Packets: %d x %d bytes\n", ITERATIONS, PKT_SIZE);

    pthread_t thr;
    pthread_create(&thr, NULL, raw_server, NULL);
    usleep(50000);

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {.sin_family=AF_INET, .sin_port=htons(PORT),
                               .sin_addr={.s_addr=inet_addr("127.0.0.1")}};

    uint8_t pkt[PKT_SIZE];
    memset(pkt, 0xAB, PKT_SIZE);
    uint8_t buf[65536];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);

    double start = now_ms();
    for (int i = 0; i < ITERATIONS; i++) {
        sendto(fd, pkt, PKT_SIZE, 0, (struct sockaddr*)&addr, sizeof(addr));
        recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);
    }
    double elapsed = now_ms() - start;

    double mbps = (ITERATIONS * PKT_SIZE * 2.0 / (elapsed / 1000.0)) / (1024.0 * 1024.0);
    double avg_lat = elapsed / ITERATIONS;

    printf("  Time:        %.1f ms\n", elapsed);
    printf("  Throughput:  %.2f MB/s\n", mbps);
    printf("  Avg RTT:     %.3f ms\n", avg_lat);
    printf("  Total data:  %.1f MB\n", (ITERATIONS * PKT_SIZE) / (1024.0 * 1024.0));

    close(fd);
    pthread_join(thr, NULL);
}

int main(void) {
    printf("=== NXP vs Raw UDP Benchmark ===\n");
    bench_raw_udp();
    return 0;
}
