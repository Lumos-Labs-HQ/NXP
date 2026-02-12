/*
 * NXP Windows Socket - Winsock2 UDP Implementation
 */
#ifdef _WIN32

#include "../platform_socket.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

struct nxp_socket {
    SOCKET fd;
};

nxp_result nxp_socket_init(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }
    return NXP_SUCCESS;
}

void nxp_socket_cleanup(void) {
    WSACleanup();
}

static void addr_to_sockaddr(const nxp_addr *addr, struct sockaddr_storage *ss, int *ss_len) {
    memset(ss, 0, sizeof(*ss));
    if (addr->family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(addr->port);
        memcpy(&sin->sin_addr, addr->ip.v4, 4);
        *ss_len = sizeof(struct sockaddr_in);
    } else {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->port);
        memcpy(&sin6->sin6_addr, addr->ip.v6, 16);
        *ss_len = sizeof(struct sockaddr_in6);
    }
}

static void sockaddr_to_addr(const struct sockaddr_storage *ss, nxp_addr *addr) {
    memset(addr, 0, sizeof(*addr));
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)ss;
        addr->family = AF_INET;
        addr->port = ntohs(sin->sin_port);
        memcpy(addr->ip.v4, &sin->sin_addr, 4);
    } else if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)ss;
        addr->family = AF_INET6;
        addr->port = ntohs(sin6->sin6_port);
        memcpy(addr->ip.v6, &sin6->sin6_addr, 16);
    }
}

nxp_result nxp_socket_create_udp(const nxp_addr *bind_addr, nxp_socket **out) {
    int af = (bind_addr->family == AF_INET6) ? AF_INET6 : AF_INET;

    SOCKET fd = socket(af, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    /* Allow address reuse */
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    /* Bind */
    struct sockaddr_storage ss;
    int ss_len;
    addr_to_sockaddr(bind_addr, &ss, &ss_len);

    if (bind(fd, (struct sockaddr *)&ss, ss_len) == SOCKET_ERROR) {
        closesocket(fd);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    nxp_socket *sock = (nxp_socket *)malloc(sizeof(nxp_socket));
    if (sock == nullptr) {
        closesocket(fd);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }
    sock->fd = fd;
    *out = sock;

    return NXP_SUCCESS;
}

void nxp_socket_close(nxp_socket *sock) {
    if (sock == nullptr) return;
    closesocket(sock->fd);
    free(sock);
}

ssize_t nxp_socket_sendto(nxp_socket *sock, const uint8_t *data, size_t len,
                          const nxp_addr *to) {
    struct sockaddr_storage ss;
    int ss_len;
    addr_to_sockaddr(to, &ss, &ss_len);

    int ret = sendto(sock->fd, (const char *)data, (int)len, 0,
                     (struct sockaddr *)&ss, ss_len);
    return (ssize_t)ret;
}

ssize_t nxp_socket_recvfrom(nxp_socket *sock, uint8_t *buf, size_t buf_len,
                            nxp_addr *from) {
    struct sockaddr_storage ss;
    int ss_len = sizeof(ss);

    int ret = recvfrom(sock->fd, (char *)buf, (int)buf_len, 0,
                       (struct sockaddr *)&ss, &ss_len);
    if (ret == SOCKET_ERROR) {
        return -1;
    }

    if (from != nullptr) {
        sockaddr_to_addr(&ss, from);
    }
    return (ssize_t)ret;
}

intptr_t nxp_socket_get_native_handle(nxp_socket *sock) {
    return (intptr_t)sock->fd;
}

nxp_result nxp_socket_get_local_addr(nxp_socket *sock, nxp_addr *out) {
    struct sockaddr_storage ss;
    int ss_len = sizeof(ss);
    if (getsockname(sock->fd, (struct sockaddr *)&ss, &ss_len) == SOCKET_ERROR) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }
    sockaddr_to_addr(&ss, out);
    return NXP_SUCCESS;
}

nxp_result nxp_socket_set_nonblocking(nxp_socket *sock) {
    u_long mode = 1;
    if (ioctlsocket(sock->fd, FIONBIO, &mode) != 0) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }
    return NXP_SUCCESS;
}

/* ── Address Utilities ──────────────────────────────────── */

nxp_result nxp_addr_from_string(const char *str, uint16_t port, nxp_addr *out) {
    memset(out, 0, sizeof(*out));

    struct in_addr v4;
    struct in6_addr v6;

    if (inet_pton(AF_INET, str, &v4) == 1) {
        out->family = AF_INET;
        out->port = port;
        memcpy(out->ip.v4, &v4, 4);
        return NXP_SUCCESS;
    }

    if (inet_pton(AF_INET6, str, &v6) == 1) {
        out->family = AF_INET6;
        out->port = port;
        memcpy(out->ip.v6, &v6, 16);
        return NXP_SUCCESS;
    }

    return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
}

bool nxp_addr_equal(const nxp_addr *a, const nxp_addr *b) {
    if (a->family != b->family) return false;
    if (a->port != b->port) return false;
    if (a->family == AF_INET) {
        return memcmp(a->ip.v4, b->ip.v4, 4) == 0;
    }
    return memcmp(a->ip.v6, b->ip.v6, 16) == 0;
}

void nxp_addr_to_string(const nxp_addr *addr, char *buf, size_t buf_len) {
    if (addr->family == AF_INET) {
        struct in_addr v4;
        memcpy(&v4, addr->ip.v4, 4);
        inet_ntop(AF_INET, &v4, buf, (socklen_t)buf_len);
    } else {
        struct in6_addr v6;
        memcpy(&v6, addr->ip.v6, 16);
        inet_ntop(AF_INET6, &v6, buf, (socklen_t)buf_len);
    }
    /* Append :port */
    size_t slen = strlen(buf);
    if (slen + 7 < buf_len) {
        snprintf(buf + slen, buf_len - slen, ":%u", addr->port);
    }
}

#endif /* _WIN32 */
