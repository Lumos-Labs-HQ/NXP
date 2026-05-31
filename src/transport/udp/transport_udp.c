/*
 * NXP UDP Transport Backend
 *
 * Wraps the existing nxp_socket (UDP) into the nxp_transport vtable.
 * This is the native NXP transport: encrypted packets over UDP.
 */
#include "platform/transport.h"
#include "platform/platform_socket.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Transport state ───────────────────────────────────── */

typedef struct {
    nxp_socket *sock;
    bool        owns_socket;
    nxp_addr    peer_addr;
    nxp_addr    bind_addr;
} udp_transport;

/* ── Listener state ────────────────────────────────────── */

typedef struct {
    nxp_socket *sock;
    char       *bind_host;
    uint16_t    bind_port;
} udp_listener;

/* ── Transport ops ─────────────────────────────────────── */

static ssize_t udp_send(nxp_transport *t, const uint8_t *data, size_t len,
                        const nxp_addr *to) {
    udp_transport *ut = (udp_transport *)t->state;
    if (to == nullptr) to = &ut->peer_addr;
    return nxp_socket_sendto(ut->sock, data, len, to);
}

static ssize_t udp_recv(nxp_transport *t, uint8_t *buf, size_t cap,
                        nxp_addr *from) {
    udp_transport *ut = (udp_transport *)t->state;
    return nxp_socket_recvfrom(ut->sock, buf, cap, from);
}

static void udp_close(nxp_transport *t) {
    if (t == nullptr) return;
    udp_transport *ut = (udp_transport *)t->state;
    if (ut != nullptr) {
        if (ut->owns_socket) nxp_socket_close(ut->sock);
        free(ut);
    }
    free(t);
}

static intptr_t udp_native_handle(nxp_transport *t) {
    udp_transport *ut = (udp_transport *)t->state;
    return nxp_socket_get_native_handle(ut->sock);
}

static nxp_result udp_local_addr(nxp_transport *t, nxp_addr *out) {
    return NXP_ERROR(NXP_ERR_INTERNAL); /* Not meaningful for ephemeral client */
}

static nxp_transport_type udp_type(nxp_transport *t) {
    (void)t;
    return NXP_TRANSPORT_UDP;
}

static const nxp_transport_ops g_udp_ops = {
    .send          = udp_send,
    .recv          = udp_recv,
    .close         = udp_close,
    .native_handle = udp_native_handle,
    .local_addr    = udp_local_addr,
    .type          = udp_type,
};

/* ── Listener ops ──────────────────────────────────────── */

static nxp_transport *udp_accept(nxp_transport_listener *ln) {
    return nullptr; /* UDP is connectionless — accept is handled by nxp_listener_recv */
}

static void udp_listener_close(nxp_transport_listener *ln) {
    if (ln == nullptr) return;
    udp_listener *ul = (udp_listener *)ln->state;
    if (ul != nullptr) {
        nxp_socket_close(ul->sock);
        free(ul->bind_host);
        free(ul);
    }
    free(ln);
}

static intptr_t udp_listener_native_handle(nxp_transport_listener *ln) {
    udp_listener *ul = (udp_listener *)ln->state;
    return nxp_socket_get_native_handle(ul->sock);
}

static nxp_result udp_listener_local_addr(nxp_transport_listener *ln, nxp_addr *out) {
    udp_listener *ul = (udp_listener *)ln->state;
    return nxp_socket_get_local_addr(ul->sock, out);
}

static const nxp_transport_listener_ops g_udp_listener_ops = {
    .accept       = udp_accept,
    .close        = udp_listener_close,
    .native_handle = udp_listener_native_handle,
    .local_addr   = udp_listener_local_addr,
};

/* ── Connection factory ────────────────────────────────── */

static nxp_result udp_connect_impl(const char *url, nxp_transport **out) {
    const char *host_start = url;
    if (strncmp(url, "nxp://", 6) == 0) host_start = url + 6;

    /* Parse host:port from URL */
    char host[256] = "127.0.0.1";
    uint16_t port = 8443;

    const char *colon = strrchr(host_start, ':');
    if (colon != nullptr) {
        size_t host_len = (size_t)(colon - host_start);
        if (host_len > 0 && host_len < sizeof(host)) {
            memcpy(host, host_start, host_len);
            host[host_len] = '\0';
        }
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 8443;
    } else if (host_start[0] != '\0') {
        size_t hlen = strlen(host_start);
        /* Strip trailing slash */
        while (hlen > 0 && host_start[hlen - 1] == '/') hlen--;
        if (hlen > 0 && hlen < sizeof(host)) {
            memcpy(host, host_start, hlen);
            host[hlen] = '\0';
        }
    }

    /* Create UDP socket (ephemeral port) */
    nxp_addr bind_addr;
    nxp_result r = nxp_addr_from_string("0.0.0.0", 0, &bind_addr);
    if (nxp_result_is_err(r)) return r;

    nxp_socket *sock = nullptr;
    r = nxp_socket_create_udp(&bind_addr, &sock);
    if (nxp_result_is_err(r)) return r;

    r = nxp_socket_set_nonblocking(sock);
    if (nxp_result_is_err(r)) { nxp_socket_close(sock); return r; }

    /* Resolve peer address */
    nxp_addr peer_addr;
    r = nxp_addr_from_string(host, port, &peer_addr);
    if (nxp_result_is_err(r)) { nxp_socket_close(sock); return r; }

    udp_transport *ut = (udp_transport *)calloc(1, sizeof(*ut));
    if (ut == nullptr) { nxp_socket_close(sock); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    ut->sock        = sock;
    ut->owns_socket = true;
    ut->peer_addr   = peer_addr;

    nxp_transport *t = (nxp_transport *)calloc(1, sizeof(*t));
    if (t == nullptr) { nxp_socket_close(sock); free(ut); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    t->ops   = &g_udp_ops;
    t->state = ut;

    *out = t;
    return NXP_SUCCESS;
}

/* ── Listener factory ──────────────────────────────────── */

static nxp_result udp_listen_impl(const char *url,
                                   void (*on_new_conn)(nxp_transport *, void *),
                                   void *user_data,
                                   nxp_transport_listener **out) {
    const char *host_start = url;
    if (strncmp(url, "nxp://", 6) == 0) host_start = url + 6;

    char host[256] = "0.0.0.0";
    uint16_t port = 8443;

    const char *colon = strrchr(host_start, ':');
    if (colon != nullptr) {
        size_t host_len = (size_t)(colon - host_start);
        if (host_len > 0 && host_len < sizeof(host)) {
            memcpy(host, host_start, host_len);
            host[host_len] = '\0';
        }
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 8443;
    }

    nxp_addr bind_addr;
    nxp_result r = nxp_addr_from_string(host, port, &bind_addr);
    if (nxp_result_is_err(r)) return r;

    nxp_socket *sock = nullptr;
    r = nxp_socket_create_udp(&bind_addr, &sock);
    if (nxp_result_is_err(r)) return r;

    r = nxp_socket_set_nonblocking(sock);
    if (nxp_result_is_err(r)) { nxp_socket_close(sock); return r; }

    udp_listener *ul = (udp_listener *)calloc(1, sizeof(*ul));
    if (ul == nullptr) { nxp_socket_close(sock); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    ul->sock      = sock;
    ul->bind_port = port;
    ul->bind_host = strdup(host);

    nxp_transport_listener *ln = (nxp_transport_listener *)calloc(1, sizeof(*ln));
    if (ln == nullptr) { nxp_socket_close(sock); free(ul->bind_host); free(ul); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    ln->ops   = &g_udp_listener_ops;
    ln->state = ul;

    (void)on_new_conn;
    (void)user_data;
    *out = ln;
    return NXP_SUCCESS;
}

/* ── Create a transport wrapping an existing listener socket ── */

nxp_transport *nxp_udp_transport_from_listener(nxp_transport_listener *ln) {
    if (ln == nullptr) return nullptr;
    udp_listener *ul = (udp_listener *)ln->state;

    udp_transport *ut = (udp_transport *)calloc(1, sizeof(*ut));
    if (ut == nullptr) return nullptr;
    ut->sock        = ul->sock;
    ut->owns_socket = false;  /* listener owns the socket */

    nxp_transport *t = (nxp_transport *)calloc(1, sizeof(*t));
    if (t == nullptr) { free(ut); return nullptr; }
    t->ops   = &g_udp_ops;
    t->state = ut;
    return t;
}

/* ── Get the nxp_socket* from a transport ── */

nxp_socket *nxp_transport_get_socket(nxp_transport *t) {
    if (t == nullptr) return nullptr;
    /* Only works for UDP transport — check by ops pointer */
    if (t->ops != &g_udp_ops) return nullptr;
    udp_transport *ut = (udp_transport *)t->state;
    return ut->sock;
}

/* ── Registration ──────────────────────────────────────── */

void nxp_transport_udp_register(void) {
    nxp_transport_register(
        NXP_TRANSPORT_UDP,
        nullptr,   /* can_connect — always available */
        udp_connect_impl,
        nullptr,   /* can_listen — always available */
        udp_listen_impl
    );
}
