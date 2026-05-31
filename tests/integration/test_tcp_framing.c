/*
 * TCP Transport E2E Test — length-prefixed framing over real TCP sockets
 *
 * Server: accept TCP, read length-prefixed frames, echo back
 * Client: connect TCP, send length-prefixed frames, verify echo
 * No NXP layer — pure transport-level test
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
static int checks = 0;

#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); fails++; } \
    else printf("  PASS: %s\n", msg); \
} while(0)

/* Length-prefixed send */
static ssize_t frame_send(int fd, const uint8_t *data, size_t len) {
    uint8_t hdr[2] = {(uint8_t)(len >> 8), (uint8_t)(len & 0xFF)};
    send(fd, hdr, 2, MSG_NOSIGNAL);
    return send(fd, data, len, MSG_NOSIGNAL);
}

/* Length-prefixed recv (blocking, simplified for test) */
static ssize_t frame_recv(int fd, uint8_t *buf, size_t cap) {
    uint8_t hdr[2];
    ssize_t n = recv(fd, hdr, 2, MSG_WAITALL);
    if (n != 2) return -1;

    uint16_t len = ((uint16_t)hdr[0] << 8) | hdr[1];
    if (len == 0 || len > cap) return -1;

    n = recv(fd, buf, len, MSG_WAITALL);
    return n;
}

/* Server: echo loop */
static int run_server(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {.sin_family=AF_INET, .sin_port=htons(port),
                               .sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 1);

    int client = accept(fd, nullptr, nullptr);
    close(fd);
    if (client < 0) return 1;

    uint8_t buf[4096];
    for (int i = 0; i < 6; i++) {
        ssize_t n = frame_recv(client, buf, sizeof(buf));
        if (n <= 0) break;
        frame_send(client, buf, (size_t)n);
    }
    close(client);
    return 0;
}

int main(void) {
    printf("=== TCP Framing E2E Test ===\n");

    /* Spawn server */
    uint16_t port = 19500;
    pid_t pid = fork();
    if (pid == 0) {
        _exit(run_server(port));
    }
    usleep(50000);

    /* Client connect */
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {.sin_family=AF_INET, .sin_port=htons(port),
                               .sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    CHECK(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0, "TCP connect");

    /* Small payload */
    const char *s1 = "HELLO";
    frame_send(fd, (uint8_t*)s1, 5);
    uint8_t buf[4096];
    ssize_t n = frame_recv(fd, buf, sizeof(buf));
    CHECK(n == 5 && memcmp(buf, "HELLO", 5) == 0, "Echo: 5 bytes");

    /* Medium payload */
    uint8_t med[1000];
    memset(med, 0xAB, 1000);
    frame_send(fd, med, 1000);
    n = frame_recv(fd, buf, sizeof(buf));
    CHECK(n == 1000 && memcmp(buf, med, 1000) == 0, "Echo: 1000 bytes");

    /* Large payload (within buffer limit) */
    uint8_t large[4000];
    memset(large, 0xCD, 4000);
    frame_send(fd, large, 4000);
    n = frame_recv(fd, buf, sizeof(buf));
    CHECK(n == 4000 && memcmp(buf, large, 4000) == 0, "Echo: 4000 bytes");

    /* Multi-frame in one TCP segment */
    frame_send(fd, (uint8_t*)"AAA", 3);
    frame_send(fd, (uint8_t*)"BBBB", 4);
    frame_send(fd, (uint8_t*)"CC", 2);
    n = frame_recv(fd, buf, sizeof(buf));
    CHECK(n == 3 && memcmp(buf, "AAA", 3) == 0, "Multi-frame [0]: 3 bytes");
    n = frame_recv(fd, buf, sizeof(buf));
    CHECK(n == 4 && memcmp(buf, "BBBB", 4) == 0, "Multi-frame [1]: 4 bytes");
    n = frame_recv(fd, buf, sizeof(buf));
    CHECK(n == 2 && memcmp(buf, "CC", 2) == 0, "Multi-frame [2]: 2 bytes");

    close(fd);
    waitpid(pid, nullptr, 0);

    printf("\n%s (%d checks, %d failed)\n",
           fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails > 0 ? 1 : 0;
}
