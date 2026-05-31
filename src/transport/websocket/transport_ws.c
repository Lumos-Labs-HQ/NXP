/*
 * NXP WebSocket Transport Backend
 *
 * TCP connection + HTTP Upgrade → WebSocket framing → nxp_transport vtable.
 * Full RFC 6455 implementation with no external WS library dependency.
 */
#include "platform/transport.h"
#include "ws_frame.h"
#include "ws_handshake.h"
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
    bool        connected;      /* Handshake completed */
    uint8_t     recv_buf[WS_MAX_PAYLOAD + WS_MAX_FRAME_HEADER];
    size_t      recv_pos;
    uint8_t     send_buf[WS_MAX_PAYLOAD + WS_MAX_FRAME_HEADER];
    nxp_addr    peer_addr;
    bool        owns_fd;
    char       *host;
    uint16_t    port;
    char       *path;
} ws_transport;

/* ── Helper: set non-blocking ──────────────────────────── */

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

/* ── Helper: resolve host → sockaddr ──────────────────── */

static bool resolve_addr(const char *host, uint16_t port, struct sockaddr_in *addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &addr->sin_addr) != 1) {
        return false;
    }
    return true;
}

/* ── Transport ops ─────────────────────────────────────── */

static ssize_t ws_send(nxp_transport *t, const uint8_t *data, size_t len,
                       const nxp_addr *to) {
    (void)to;
    ws_transport *ws = (ws_transport *)t->state;
    if (!ws->connected) return -1;

    ws_frame f = {
        .opcode      = WS_OP_BINARY,
        .fin         = true,
        .masked      = true,  /* Client → Server MUST be masked */
        .payload     = (uint8_t *)data,
        .payload_len = len,
    };
    /* Generate random mask */
    ws->send_buf[0] = 0;
    f.mask_key[0] = (uint8_t)(rand() & 0xFF);
    f.mask_key[1] = (uint8_t)(rand() & 0xFF);
    f.mask_key[2] = (uint8_t)(rand() & 0xFF);
    f.mask_key[3] = (uint8_t)(rand() & 0xFF);

    size_t frame_len = ws_frame_encode(&f, ws->send_buf, sizeof(ws->send_buf));
    if (frame_len == 0) return -1;

    ssize_t sent = send(ws->fd, ws->send_buf, frame_len, MSG_NOSIGNAL);
    return sent > 0 ? (ssize_t)len : sent;
}

static ssize_t ws_recv(nxp_transport *t, uint8_t *buf, size_t cap,
                       nxp_addr *from) {
    (void)from;
    ws_transport *ws = (ws_transport *)t->state;
    if (!ws->connected) return -1;

    /* Read raw TCP data into accumulation buffer */
    size_t space = sizeof(ws->recv_buf) - ws->recv_pos;
    ssize_t n = recv(ws->fd, ws->recv_buf + ws->recv_pos, space, 0);
    if (n <= 0) return n;

    ws->recv_pos += (size_t)n;

    /* Try to decode a complete WS frame */
    ws_frame f;
    size_t consumed = ws_frame_decode(&f, ws->recv_buf, ws->recv_pos);
    if (consumed == 0) return 0; /* Incomplete frame, wait for more data */

    /* Extract payload */
    size_t payload_len = f.payload_len;
    if (payload_len > cap) payload_len = cap;
    memcpy(buf, f.payload, payload_len);

    /* Remove consumed frame from buffer */
    memmove(ws->recv_buf, ws->recv_buf + consumed, ws->recv_pos - consumed);
    ws->recv_pos -= consumed;

    /* Handle control frames */
    if (f.opcode == WS_OP_PING) {
        /* Send PONG */
        ws_frame pong = {
            .opcode      = WS_OP_PONG,
            .fin         = true,
            .masked      = false,
            .payload     = f.payload,
            .payload_len = f.payload_len,
        };
        uint8_t ctrl_buf[128];
        size_t c_len = ws_frame_encode(&pong, ctrl_buf, sizeof(ctrl_buf));
        if (c_len > 0) (void)send(ws->fd, ctrl_buf, c_len, MSG_NOSIGNAL);
        return 0; /* PING is not data for the app */
    }
    if (f.opcode == WS_OP_CLOSE) {
        ws->connected = false;
        return -1;
    }
    if (f.opcode == WS_OP_PONG) {
        return 0; /* PONG is internal */
    }

    return (ssize_t)payload_len;
}

static void ws_close(nxp_transport *t) {
    if (t == nullptr) return;
    ws_transport *ws = (ws_transport *)t->state;
    if (ws != nullptr) {
        if (ws->connected) {
            /* Send close frame */
            ws_frame f = {
                .opcode      = WS_OP_CLOSE,
                .fin         = true,
                .masked      = true,
                .payload     = nullptr,
                .payload_len = 0,
            };
            uint8_t ctrl_buf[32];
            size_t len = ws_frame_encode(&f, ctrl_buf, sizeof(ctrl_buf));
            if (len > 0) (void)send(ws->fd, ctrl_buf, len, MSG_NOSIGNAL);
        }
        if (ws->owns_fd) close(ws->fd);
        free(ws->host);
        free(ws->path);
        free(ws);
    }
    free(t);
}

static intptr_t ws_native_handle(nxp_transport *t) {
    ws_transport *ws = (ws_transport *)t->state;
    return (intptr_t)ws->fd;
}

static nxp_result ws_local_addr(nxp_transport *t, nxp_addr *out) {
    (void)t; (void)out;
    return NXP_ERROR(NXP_ERR_INTERNAL);
}

static nxp_transport_type ws_type(nxp_transport *t) {
    (void)t;
    return NXP_TRANSPORT_WS;
}

static const nxp_transport_ops g_ws_ops = {
    .send          = ws_send,
    .recv          = ws_recv,
    .close         = ws_close,
    .native_handle = ws_native_handle,
    .local_addr    = ws_local_addr,
    .type          = ws_type,
};

/* ── Connection factory ────────────────────────────────── */

static nxp_result ws_connect_impl(const char *url, nxp_transport **out) {
    const char *host_start = url;
    if (strncmp(url, "ws://", 5) == 0)  host_start = url + 5;
    if (strncmp(url, "wss://", 6) == 0) host_start = url + 6;

    char host[256] = "127.0.0.1";
    uint16_t port = 80;
    char path[256] = "/";

    /* Parse host:port/path */
    const char *slash = strchr(host_start, '/');
    const char *colon = strchr(host_start, ':');

    if (colon != nullptr && (slash == nullptr || colon < slash)) {
        size_t hlen = (size_t)(colon - host_start);
        if (hlen > 0 && hlen < sizeof(host)) {
            memcpy(host, host_start, hlen);
            host[hlen] = '\0';
        }
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 80;
    } else if (slash != nullptr) {
        size_t hlen = (size_t)(slash - host_start);
        if (hlen > 0 && hlen < sizeof(host)) {
            memcpy(host, host_start, hlen);
            host[hlen] = '\0';
        }
    } else {
        size_t hlen = strlen(host_start);
        if (hlen > 0 && hlen < sizeof(host)) {
            memcpy(host, host_start, hlen);
            host[hlen] = '\0';
        }
    }

    if (slash != nullptr) {
        size_t plen = strlen(slash);
        if (plen < sizeof(path)) {
            memcpy(path, slash, plen);
            path[plen] = '\0';
        }
    }

    /* Create TCP socket */
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

    /* Build and send HTTP Upgrade request */
    uint8_t hs_buf[WS_MAX_HANDSHAKE_BUF];
    size_t hs_len = ws_build_client_handshake(path, host, hs_buf, sizeof(hs_buf));
    if (hs_len == 0) { close(fd); return NXP_ERROR(NXP_ERR_INTERNAL); }

    ssize_t sent = send(fd, hs_buf, hs_len, 0);
    if (sent != (ssize_t)hs_len) { close(fd); return NXP_ERROR(NXP_ERR_PLATFORM); }

    ws_transport *ws = (ws_transport *)calloc(1, sizeof(*ws));
    if (ws == nullptr) { close(fd); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    ws->fd        = fd;
    ws->owns_fd   = true;
    ws->host      = strdup(host);
    ws->port      = port;
    ws->path      = strdup(path);
    ws->connected = false; /* Not connected until 101 response */

    nxp_transport *t = (nxp_transport *)calloc(1, sizeof(*t));
    if (t == nullptr) { close(fd); free(ws->host); free(ws->path); free(ws); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }
    t->ops   = &g_ws_ops;
    t->state = ws;

    *out = t;
    return NXP_SUCCESS;
}

/*
 * Complete the handshake: read the server's 101 response.
 * Called by the application after epoll signals readability on the fd.
 * Returns 0 on success, -1 on failure.
 */
int ws_complete_handshake(nxp_transport *t) {
    ws_transport *ws = (ws_transport *)t->state;
    if (ws->connected) return 0;

    /* Read the HTTP response */
    uint8_t buf[2048];
    ssize_t n = recv(ws->fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return -1;
    buf[n] = '\0';

    if (strstr((char *)buf, "101") == nullptr) return -1;

    ws->connected = true;
    return 0;
}

/* ── Registration ──────────────────────────────────────── */

void nxp_transport_ws_register(void) {
    nxp_transport_register(
        NXP_TRANSPORT_WS,
        nullptr,
        ws_connect_impl,
        nullptr,  /* No listener yet */
        nullptr
    );
}
