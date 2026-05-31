/*
 * NXP Raw TCP Transport Backend
 *
 * NXP packets over TCP with 2-byte big-endian length prefix.
 * TCP is stream-oriented, not datagram-preserving, so each
 * "message" is framed as:
 *   [2 bytes BE length] [NXP packet bytes]
 */
#include "platform/transport.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ── Transport state ───────────────────────────────────── */

typedef struct {
    int         fd;
    nxp_addr    peer_addr;
    bool        owns_fd;

    /* Receive buffering (TCP is a stream) */
    uint8_t     recv_buf[65536];
    size_t      recv_pos;      /* Bytes accumulated */

    /* Send buffering */
    uint8_t     send_buf[65536 + 2]; /* +2 for length prefix */
} tcp_transport;

/* ── Helpers ───────────────────────────────────────────── */

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static bool resolve_addr(const char *host, uint16_t port, struct sockaddr_in *addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port   = htons(port);
    return inet_pton(AF_INET, host, &addr->sin_addr) == 1;
}

/* ── Transport ops ─────────────────────────────────────── */

static ssize_t tcp_send(nxp_transport *t, const uint8_t *data, size_t len,
                        const nxp_addr *to) {
    (void)to;
    tcp_transport *tc = (tcp_transport *)t->state;

    /* Frame: 2-byte BE length prefix + payload */
    if (len > 65535) return -1;

    tc->send_buf[0] = (uint8_t)(len >> 8);
    tc->send_buf[1] = (uint8_t)(len);
    memcpy(tc->send_buf + 2, data, len);

    ssize_t sent = send(tc->fd, tc->send_buf, len + 2, MSG_NOSIGNAL);
    if (sent <= 2) return -1;
    return (ssize_t)((size_t)sent - 2); /* Return bytes of actual payload */
}

static ssize_t tcp_recv(nxp_transport *t, uint8_t *buf, size_t cap,
                        nxp_addr *from) {
    (void)from;
    tcp_transport *tc = (tcp_transport *)t->state;

    /* Accumulate raw TCP data */
    size_t space = sizeof(tc->recv_buf) - tc->recv_pos;
    ssize_t n = recv(tc->fd, tc->recv_buf + tc->recv_pos, space, 0);
    if (n <= 0) return n;
    tc->recv_pos += (size_t)n;

    /* Need at least 2 bytes for length prefix */
    if (tc->recv_pos < 2) return 0;

    uint16_t msg_len = ((uint16_t)tc->recv_buf[0] << 8) | tc->recv_buf[1];
    if (msg_len == 0) return 0;

    /* Need full message */
    if (tc->recv_pos < (size_t)msg_len + 2) return 0;

    /* Extract payload */
    size_t extract = msg_len;
    if (extract > cap) extract = cap;
    memcpy(buf, tc->recv_buf + 2, extract);

    /* Remove consumed bytes from buffer */
    size_t total = msg_len + 2;
    memmove(tc->recv_buf, tc->recv_buf + total, tc->recv_pos - total);
    tc->recv_pos -= total;

    return (ssize_t)extract;
}

static void tcp_close(nxp_transport *t) {
    if (t == nullptr) return;
    tcp_transport *tc = (tcp_transport *)t->state;
    if (tc != nullptr) {
        if (tc->owns_fd) close(tc->fd);
        free(tc);
    }
    free(t);
}

static intptr_t tcp_native_handle(nxp_transport *t) {
    tcp_transport *tc = (tcp_transport *)t->state;
    return (intptr_t)tc->fd;
}

static nxp_result tcp_local_addr(nxp_transport *t, nxp_addr *out) {
    (void)t; (void)out;
    return NXP_ERROR(NXP_ERR_INTERNAL);
}

static nxp_transport_type tcp_type(nxp_transport *t) {
    (void)t;
    return NXP_TRANSPORT_TCP;
}

static const nxp_transport_ops g_tcp_ops = {
    .send          = tcp_send,
    .recv          = tcp_recv,
    .close         = tcp_close,
    .native_handle = tcp_native_handle,
    .local_addr    = tcp_local_addr,
    .type          = tcp_type,
};

/* ── Listener ops ──────────────────────────────────────── */

typedef struct {
    int fd;
    char *host;
    uint16_t port;
    void (*on_new_conn)(nxp_transport *, void *);
    void *user_data;
} tcp_listener;

static nxp_transport *tcp_accept(nxp_transport_listener *ln) {
    tcp_listener *tl = (tcp_listener *)ln->state;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int client = accept(tl->fd, (struct sockaddr *)&addr, &alen);
    if (client < 0) return nullptr;

    set_nonblocking(client);

    tcp_transport *tc = (tcp_transport *)calloc(1, sizeof(*tc));
    if (tc == nullptr) { close(client); return nullptr; }
    tc->fd      = client;
    tc->owns_fd = true;

    nxp_transport *t = (nxp_transport *)calloc(1, sizeof(*t));
    if (t == nullptr) { close(client); free(tc); return nullptr; }
    t->ops   = &g_tcp_ops;
    t->state = tc;

    return t;
}

static void tl_close(nxp_transport_listener *ln) {
    if (ln == nullptr) return;
    tcp_listener *tl = (tcp_listener *)ln->state;
    if (tl != nullptr) {
        close(tl->fd);
        free(tl->host);
        free(tl);
    }
    free(ln);
}

static intptr_t tl_native_handle(nxp_transport_listener *ln) {
    tcp_listener *tl = (tcp_listener *)ln->state;
    return (intptr_t)tl->fd;
}

static nxp_result tl_local_addr(nxp_transport_listener *ln, nxp_addr *out) {
    (void)ln; (void)out;
    return NXP_ERROR(NXP_ERR_INTERNAL);
}

static const nxp_transport_listener_ops g_tcp_listener_ops = {
    .accept       = tcp_accept,
    .close        = tl_close,
    .native_handle = tl_native_handle,
    .local_addr   = tl_local_addr,
};

/* ── Parse host:port from URL ──────────────────────────── */

static void parse_url(const char *url, char *host_out, size_t host_cap,
                      uint16_t *port_out) {
    const char *host_start = url;
    if (strncmp(url, "ntc://", 6) == 0) host_start = url + 6;
    if (strncmp(url, "nxp://", 6) == 0) host_start = url + 6;

    *port_out = 8443;

    const char *colon = strrchr(host_start, ':');
    const char *slash = strchr(host_start, '/');

    if (colon != nullptr && (slash == nullptr || colon < slash)) {
        size_t hlen = (size_t)(colon - host_start);
        if (hlen > 0 && hlen < host_cap) {
            memcpy(host_out, host_start, hlen);
            host_out[hlen] = '\0';
        }
        *port_out = (uint16_t)atoi(colon + 1);
        if (*port_out == 0) *port_out = 8443;
    } else if (host_start[0] != '\0') {
        size_t hlen = (slash != nullptr ? (size_t)(slash - host_start) : strlen(host_start));
        if (hlen > 0 && hlen < host_cap) {
            memcpy(host_out, host_start, hlen);
            host_out[hlen] = '\0';
        }
    } else {
        strncpy(host_out, "127.0.0.1", host_cap);
    }
}

/* ── Connection factory ────────────────────────────────── */

static nxp_result tcp_connect_impl(const char *url, nxp_transport **out) {
    char host[256] = "127.0.0.1";
    uint16_t port = 8443;
    parse_url(url, host, sizeof(host), &port);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return NXP_ERROR(NXP_ERR_PLATFORM);

    struct sockaddr_in addr;
    if (!resolve_addr(host, port, &addr)) {
        close(fd);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    if (!set_nonblocking(fd)) {
        close(fd);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    tcp_transport *tc = (tcp_transport *)calloc(1, sizeof(*tc));
    if (tc == nullptr) { close(fd); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    tc->fd      = fd;
    tc->owns_fd = true;

    nxp_transport *t = (nxp_transport *)calloc(1, sizeof(*t));
    if (t == nullptr) { close(fd); free(tc); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    t->ops   = &g_tcp_ops;
    t->state = tc;

    *out = t;
    return NXP_SUCCESS;
}

/* ── Listener factory ──────────────────────────────────── */

static nxp_result tcp_listen_impl(const char *url,
                                   void (*on_new_conn)(nxp_transport *, void *),
                                   void *user_data,
                                   nxp_transport_listener **out) {
    char host[256] = "0.0.0.0";
    uint16_t port = 8443;
    parse_url(url, host, sizeof(host), &port);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return NXP_ERROR(NXP_ERR_PLATFORM);

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
    };
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    if (listen(fd, 128) < 0) {
        close(fd);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    set_nonblocking(fd);

    tcp_listener *tl = (tcp_listener *)calloc(1, sizeof(*tl));
    if (tl == nullptr) { close(fd); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    tl->fd          = fd;
    tl->port        = port;
    tl->host        = strdup(host);
    tl->on_new_conn = on_new_conn;
    tl->user_data   = user_data;

    nxp_transport_listener *ln = (nxp_transport_listener *)calloc(1, sizeof(*ln));
    if (ln == nullptr) { close(fd); free(tl->host); free(tl); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    ln->ops   = &g_tcp_listener_ops;
    ln->state = tl;

    *out = ln;
    return NXP_SUCCESS;
}

/* ── Registration ──────────────────────────────────────── */

void nxp_transport_tcp_register(void) {
    nxp_transport_register(
        NXP_TRANSPORT_TCP,
        nullptr,
        tcp_connect_impl,
        nullptr,
        tcp_listen_impl
    );
}
