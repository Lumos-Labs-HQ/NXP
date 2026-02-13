/*
 * NXP Public API - Implementation
 *
 * Phase 11: Bridges the public API to the internal sans-I/O engine
 * by wiring sockets + event loop to nxp_conn/nxp_listener.
 *
 * Single-threaded model: user calls nxp_run() (blocking) or
 * nxp_poll() (non-blocking) to drive the event loop.
 */
#include "api_internal.h"

/* ── Global State Singleton ──────────────────────────── */

nxp_global g_nxp = {0};

/* ── Forward declarations ────────────────────────────── */

static void on_conn_socket_event(nxp_event_loop *loop, nxp_socket *sock,
                                  uint32_t events, void *user_data);
static void on_conn_timer(nxp_event_loop *loop, void *user_data);
static void on_listener_socket_event(nxp_event_loop *loop, nxp_socket *sock,
                                      uint32_t events, void *user_data);
static void on_listener_timer(nxp_event_loop *loop, void *user_data);
static void listener_on_new_conn_cb(nxp_conn *conn, void *user_data);

/* ── API Conn linked-list helpers ────────────────────── */

static void api_conn_link(nxp_api_conn *ac) {
    ac->next = g_nxp.conns;
    ac->prev = nullptr;
    if (g_nxp.conns != nullptr) g_nxp.conns->prev = ac;
    g_nxp.conns = ac;
}

static void api_conn_unlink(nxp_api_conn *ac) {
    if (ac->prev != nullptr) ac->prev->next = ac->next;
    else g_nxp.conns = ac->next;
    if (ac->next != nullptr) ac->next->prev = ac->prev;
    ac->next = ac->prev = nullptr;
}

/* ── Listener linked-list helpers ────────────────────── */

static void listener_link(nxp_listener *ln) {
    ln->next = g_nxp.listeners;
    ln->prev = nullptr;
    if (g_nxp.listeners != nullptr) g_nxp.listeners->prev = ln;
    g_nxp.listeners = ln;
}

static void listener_unlink(nxp_listener *ln) {
    if (ln->prev != nullptr) ln->prev->next = ln->next;
    else g_nxp.listeners = ln->next;
    if (ln->next != nullptr) ln->next->prev = ln->prev;
    ln->next = ln->prev = nullptr;
}

/* ── Internal helpers ────────────────────────────────── */

nxp_api_conn *nxp_api_find_conn(nxp_conn *conn) {
    for (nxp_api_conn *ac = g_nxp.conns; ac != nullptr; ac = ac->next) {
        if (ac->conn == conn) return ac;
    }
    return nullptr;
}

void nxp_api_flush_conn(nxp_api_conn *ac) {
    uint8_t out[NXP_PACKET_BUF_SIZE];
    uint64_t now = nxp_time_now_us();

    for (;;) {
        ssize_t n = nxp_conn_send(ac->conn, out, sizeof(out), now);
        if (n <= 0) break;
        (void)nxp_socket_sendto(ac->sock, out, (size_t)n, &ac->conn->peer_addr);
    }
}

void nxp_api_rearm_timer(nxp_api_conn *ac) {
    if (ac->timer != nullptr) {
        nxp_event_loop_cancel_timer(g_nxp.loop, ac->timer);
        ac->timer = nullptr;
    }

    uint64_t now = nxp_time_now_us();
    uint64_t deadline = nxp_conn_timeout(ac->conn, now);
    if (deadline != UINT64_MAX) {
        ac->timer = nxp_event_loop_add_timer(g_nxp.loop, deadline,
                                              on_conn_timer, ac);
    }
}

void nxp_api_flush_listener(nxp_listener *ln) {
    uint8_t out[NXP_PACKET_BUF_SIZE];
    nxp_addr peer_addr;
    uint64_t now = nxp_time_now_us();

    for (;;) {
        ssize_t n = nxp_listener_send(ln->ls, out, sizeof(out),
                                       &peer_addr, now);
        if (n <= 0) break;
        (void)nxp_socket_sendto(ln->sock, out, (size_t)n, &peer_addr);
    }
}

void nxp_api_rearm_listener_timer(nxp_listener *ln) {
    if (ln->timer != nullptr) {
        nxp_event_loop_cancel_timer(g_nxp.loop, ln->timer);
        ln->timer = nullptr;
    }

    uint64_t now = nxp_time_now_us();
    uint64_t deadline = nxp_listener_timeout(ln->ls, now);
    if (deadline != UINT64_MAX) {
        ln->timer = nxp_event_loop_add_timer(g_nxp.loop, deadline,
                                              on_listener_timer, ln);
    }
}

/* ── Socket event callbacks ──────────────────────────── */

static void on_conn_socket_event(nxp_event_loop *loop, nxp_socket *sock,
                                  uint32_t events, void *user_data) {
    (void)loop; (void)sock;
    nxp_api_conn *ac = (nxp_api_conn *)user_data;

    if (events & NXP_EVENT_READ) {
        uint8_t buf[NXP_PACKET_BUF_SIZE];
        nxp_addr from;

        for (;;) {
            ssize_t n = nxp_socket_recvfrom(ac->sock, buf, sizeof(buf), &from);
            if (n <= 0) break;

            nxp_conn_state prev = ac->conn->state;
            uint64_t now = nxp_time_now_us();
            (void)nxp_conn_recv(ac->conn, buf, (size_t)n, now);
            nxp_conn_state cur = ac->conn->state;

            /* Detect transitions for user callbacks */
            if (prev != NXP_CONN_ESTABLISHED && cur == NXP_CONN_ESTABLISHED) {
                if (ac->on_connected != nullptr) {
                    ac->on_connected(ac->conn, ac->cb_user_data);
                }
            }
            if ((cur == NXP_CONN_CLOSED || cur == NXP_CONN_DRAINING) &&
                prev != NXP_CONN_CLOSED && prev != NXP_CONN_DRAINING) {
                if (ac->on_closed != nullptr) {
                    ac->on_closed(ac->conn, ac->cb_user_data);
                }
            }
        }
    }

    nxp_api_flush_conn(ac);
    nxp_api_rearm_timer(ac);
}

static void on_conn_timer(nxp_event_loop *loop, void *user_data) {
    (void)loop;
    nxp_api_conn *ac = (nxp_api_conn *)user_data;
    ac->timer = nullptr; /* Timer has fired, handle is no longer valid */

    uint64_t now = nxp_time_now_us();
    nxp_conn_on_timeout(ac->conn, now);
    nxp_api_flush_conn(ac);
    nxp_api_rearm_timer(ac);
}

static void on_listener_socket_event(nxp_event_loop *loop, nxp_socket *sock,
                                      uint32_t events, void *user_data) {
    (void)loop; (void)sock;
    nxp_listener *ln = (nxp_listener *)user_data;

    if (events & NXP_EVENT_READ) {
        uint8_t buf[NXP_PACKET_BUF_SIZE];
        nxp_addr from;

        for (;;) {
            ssize_t n = nxp_socket_recvfrom(ln->sock, buf, sizeof(buf), &from);
            if (n <= 0) break;
            uint64_t now = nxp_time_now_us();
            (void)nxp_listener_recv(ln->ls, buf, (size_t)n, &from, now);
        }
    }

    nxp_api_flush_listener(ln);
    nxp_api_rearm_listener_timer(ln);
}

static void on_listener_timer(nxp_event_loop *loop, void *user_data) {
    (void)loop;
    nxp_listener *ln = (nxp_listener *)user_data;
    ln->timer = nullptr;

    uint64_t now = nxp_time_now_us();
    nxp_listener_on_timeout(ln->ls, now);
    nxp_api_flush_listener(ln);
    nxp_api_rearm_listener_timer(ln);
}

/* ── Internal new-connection callback (from listener) ── */

static void listener_on_new_conn_cb(nxp_conn *conn, void *user_data) {
    nxp_listener *ln = (nxp_listener *)user_data;

    /* Start handshake on the server side.
     * The listener has already fed the Initial packet to the connection
     * via nxp_conn_recv, but the handshake context needs to be set up. */
    nxp_conn_id peer_cid = conn->dcid;
    (void)nxp_conn_start_handshake(conn, &peer_cid);

    /* Create API conn wrapper for this server-accepted connection.
     * Server connections share the LISTENER's socket (all connections
     * on the same bound port). */
    nxp_api_conn *ac = (nxp_api_conn *)calloc(1, sizeof(nxp_api_conn));
    if (ac == nullptr) return;

    ac->conn = conn;
    ac->sock = ln->sock;
    ac->owns_socket = false;
    ac->parent_listener = ln;

    api_conn_link(ac);

    /* Notify the user's listener callback */
    if (ln->on_new_conn != nullptr) {
        ln->on_new_conn(ln, conn, ln->user_data);
    }
}

/* ═════════════════════════════════════════════════════════
 *  PUBLIC API — Library Lifecycle
 * ═════════════════════════════════════════════════════════ */

nxp_result nxp_init(const nxp_global_config *config) {
    (void)config;
    if (g_nxp.initialized) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    nxp_result r = nxp_socket_init();
    if (nxp_result_is_err(r)) return r;

    g_nxp.loop = nxp_event_loop_create();
    if (g_nxp.loop == nullptr) {
        nxp_socket_cleanup();
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    g_nxp.initialized = true;
    return NXP_SUCCESS;
}

void nxp_shutdown(void) {
    if (!g_nxp.initialized) return;

    /* Close all connections */
    while (g_nxp.conns != nullptr) {
        nxp_api_conn *ac = g_nxp.conns;
        api_conn_unlink(ac);

        if (ac->timer != nullptr) {
            nxp_event_loop_cancel_timer(g_nxp.loop, ac->timer);
        }
        if (ac->owns_socket && ac->sock != nullptr) {
            nxp_event_loop_del_socket(g_nxp.loop, ac->sock);
            nxp_socket_close(ac->sock);
        }
        nxp_conn_destroy(ac->conn);
        free(ac);
    }

    /* Close all listeners */
    while (g_nxp.listeners != nullptr) {
        nxp_listener *ln = g_nxp.listeners;
        listener_unlink(ln);

        if (ln->timer != nullptr) {
            nxp_event_loop_cancel_timer(g_nxp.loop, ln->timer);
        }
        if (ln->sock != nullptr) {
            nxp_event_loop_del_socket(g_nxp.loop, ln->sock);
            nxp_socket_close(ln->sock);
        }
        nxp_listener_destroy(ln->ls);
        free(ln);
    }

    nxp_event_loop_destroy(g_nxp.loop);
    nxp_socket_cleanup();
    memset(&g_nxp, 0, sizeof(g_nxp));
}

void nxp_run(void) {
    if (!g_nxp.initialized) return;
    (void)nxp_event_loop_run(g_nxp.loop);
}

void nxp_poll(void) {
    if (!g_nxp.initialized) return;
    (void)nxp_event_loop_run_once(g_nxp.loop, 0);
}

/* ═════════════════════════════════════════════════════════
 *  PUBLIC API — Client Connection
 * ═════════════════════════════════════════════════════════ */

nxp_result nxp_connect(const nxp_config *config, const char *host,
                        uint16_t port, nxp_conn_cb on_connected,
                        nxp_conn_cb on_closed, void *user_data,
                        nxp_conn **out_conn) {
    if (!g_nxp.initialized) return NXP_ERROR(NXP_ERR_INTERNAL);
    if (host == nullptr || out_conn == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Resolve host to nxp_addr */
    nxp_addr peer_addr;
    nxp_result r = nxp_addr_from_string(host, port, &peer_addr);
    if (nxp_result_is_err(r)) return r;

    /* Create a client UDP socket (bind to ephemeral port) */
    nxp_addr bind_addr;
    (void)nxp_addr_from_string("0.0.0.0", 0, &bind_addr);

    nxp_socket *sock = nullptr;
    r = nxp_socket_create_udp(&bind_addr, &sock);
    if (nxp_result_is_err(r)) return r;

    r = nxp_socket_set_nonblocking(sock);
    if (nxp_result_is_err(r)) { nxp_socket_close(sock); return r; }

    /* Generate random SCID */
    nxp_conn_id scid = {0};
    scid.len = 8;
    (void)nxp_random_bytes(scid.data, scid.len);

    /* Generate random initial DCID (server will replace after handshake) */
    nxp_conn_id dcid = {0};
    dcid.len = 8;
    (void)nxp_random_bytes(dcid.data, dcid.len);

    /* Build internal conn config */
    nxp_conn_config cc;
    memset(&cc, 0, sizeof(cc));
    cc.scid       = scid;
    cc.peer_addr  = peer_addr;

    if (config != nullptr) {
        cc.idle_timeout_us         = config->idle_timeout_ms * 1000;
        cc.max_streams_bidi        = (uint32_t)config->max_streams_bidi;
        cc.max_streams_uni         = (uint32_t)config->max_streams_uni;
    } else {
        cc.idle_timeout_us         = (uint64_t)NXP_IDLE_TIMEOUT_DEFAULT * 1000;
        cc.max_streams_bidi        = NXP_MAX_STREAMS_DEFAULT;
        cc.max_streams_uni         = NXP_MAX_STREAMS_DEFAULT;
    }
    cc.initial_max_data            = NXP_DEFAULT_MAX_DATA;
    cc.initial_max_stream_data     = NXP_DEFAULT_MAX_STREAM_DATA;

    /* Create the sans-I/O connection */
    nxp_conn *conn = nxp_conn_create(&cc, false);
    if (conn == nullptr) {
        nxp_socket_close(sock);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    /* Start handshake */
    r = nxp_conn_start_handshake(conn, &dcid);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        nxp_socket_close(sock);
        return r;
    }

    /* Heartbeat if configured */
    if (config != nullptr && config->heartbeat_interval_ms > 0) {
        nxp_conn_set_heartbeat(conn, config->heartbeat_interval_ms * 1000);
    }

    /* Create API wrapper */
    nxp_api_conn *ac = (nxp_api_conn *)calloc(1, sizeof(nxp_api_conn));
    if (ac == nullptr) {
        nxp_conn_destroy(conn);
        nxp_socket_close(sock);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    ac->conn         = conn;
    ac->sock         = sock;
    ac->owns_socket  = true;
    ac->on_connected = on_connected;
    ac->on_closed    = on_closed;
    ac->cb_user_data = user_data;

    /* Register socket with event loop */
    r = nxp_event_loop_add_socket(g_nxp.loop, sock, NXP_EVENT_READ,
                                   on_conn_socket_event, ac);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        nxp_socket_close(sock);
        free(ac);
        return r;
    }

    api_conn_link(ac);

    /* Flush initial handshake packets */
    nxp_api_flush_conn(ac);
    nxp_api_rearm_timer(ac);

    *out_conn = conn;
    return NXP_SUCCESS;
}

/* ═════════════════════════════════════════════════════════
 *  PUBLIC API — Connection Management
 * ═════════════════════════════════════════════════════════ */

void nxp_conn_close(nxp_conn *conn, uint64_t error_code) {
    if (conn == nullptr) return;

    (void)nxp_conn_initiate_close(conn, error_code);

    /* Flush the CONNECTION_CLOSE frame */
    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac != nullptr) {
        nxp_api_flush_conn(ac);
    }
}

/* nxp_conn_get_state is already provided by nxp_core (same signature) */

nxp_conn_stats nxp_conn_get_stats(const nxp_conn *conn) {
    if (conn == nullptr) {
        nxp_conn_stats empty = {0};
        return empty;
    }
    return conn->stats;
}

void nxp_conn_set_stream_accept_cb(nxp_conn *conn, nxp_stream_accept_cb cb,
                                    void *user_data) {
    if (conn == nullptr) return;
    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac == nullptr) return;
    ac->stream_accept_cb = cb;
    ac->stream_accept_ud = user_data;
}

void nxp_conn_set_user_data(nxp_conn *conn, void *data) {
    if (conn == nullptr) return;
    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac == nullptr) return;
    ac->app_user_data = data;
}

void *nxp_conn_get_user_data(const nxp_conn *conn) {
    if (conn == nullptr) return nullptr;
    /* Need non-const for find, but we don't modify */
    nxp_api_conn *ac = nxp_api_find_conn((nxp_conn *)conn);
    if (ac == nullptr) return nullptr;
    return ac->app_user_data;
}

/* ═════════════════════════════════════════════════════════
 *  PUBLIC API — Server Listener
 * ═════════════════════════════════════════════════════════ */

nxp_result nxp_listen(const nxp_config *config, const char *bind_addr_str,
                       uint16_t port, nxp_listener_cb on_new_conn,
                       void *user_data, nxp_listener **out_listener) {
    if (!g_nxp.initialized) return NXP_ERROR(NXP_ERR_INTERNAL);
    if (bind_addr_str == nullptr || out_listener == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Resolve bind address */
    nxp_addr bind_addr;
    nxp_result r = nxp_addr_from_string(bind_addr_str, port, &bind_addr);
    if (nxp_result_is_err(r)) return r;

    /* Create and bind socket */
    nxp_socket *sock = nullptr;
    r = nxp_socket_create_udp(&bind_addr, &sock);
    if (nxp_result_is_err(r)) return r;

    r = nxp_socket_set_nonblocking(sock);
    if (nxp_result_is_err(r)) { nxp_socket_close(sock); return r; }

    /* Build internal listener config */
    nxp_listener_config lc;
    memset(&lc, 0, sizeof(lc));
    lc.max_connections = NXP_LISTENER_DEFAULT_MAX_CONNS;

    if (config != nullptr) {
        lc.idle_timeout_us         = config->idle_timeout_ms * 1000;
        lc.max_streams_bidi        = (uint32_t)config->max_streams_bidi;
        lc.max_streams_uni         = (uint32_t)config->max_streams_uni;
    } else {
        lc.idle_timeout_us         = (uint64_t)NXP_IDLE_TIMEOUT_DEFAULT * 1000;
        lc.max_streams_bidi        = NXP_MAX_STREAMS_DEFAULT;
        lc.max_streams_uni         = NXP_MAX_STREAMS_DEFAULT;
    }
    lc.initial_max_data            = NXP_DEFAULT_MAX_DATA;
    lc.initial_max_stream_data     = NXP_DEFAULT_MAX_STREAM_DATA;

    /* Allocate the public listener wrapper first (so we can pass it as
     * user_data to the internal on_new_conn callback) */
    nxp_listener *ln = (nxp_listener *)calloc(1, sizeof(nxp_listener));
    if (ln == nullptr) { nxp_socket_close(sock); return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY); }

    lc.on_new_conn = listener_on_new_conn_cb;
    lc.user_data   = ln;

    /* Create the internal sans-I/O listener */
    nxp_listener_s *ls = nxp_listener_create(&lc);
    if (ls == nullptr) {
        nxp_socket_close(sock);
        free(ln);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    ln->ls          = ls;
    ln->sock        = sock;
    ln->on_new_conn = on_new_conn;
    ln->user_data   = user_data;

    /* Register socket with event loop */
    r = nxp_event_loop_add_socket(g_nxp.loop, sock, NXP_EVENT_READ,
                                   on_listener_socket_event, ln);
    if (nxp_result_is_err(r)) {
        nxp_listener_destroy(ls);
        nxp_socket_close(sock);
        free(ln);
        return r;
    }

    listener_link(ln);
    nxp_api_rearm_listener_timer(ln);

    *out_listener = ln;
    return NXP_SUCCESS;
}

void nxp_listener_close(nxp_listener *listener) {
    if (listener == nullptr) return;

    /* Remove server-accepted connections that belong to this listener */
    nxp_api_conn *ac = g_nxp.conns;
    while (ac != nullptr) {
        nxp_api_conn *next = ac->next;
        if (ac->parent_listener == listener) {
            api_conn_unlink(ac);
            if (ac->timer != nullptr) {
                nxp_event_loop_cancel_timer(g_nxp.loop, ac->timer);
            }
            /* Don't close socket — listener owns it */
            nxp_conn_destroy(ac->conn);
            free(ac);
        }
        ac = next;
    }

    listener_unlink(listener);

    if (listener->timer != nullptr) {
        nxp_event_loop_cancel_timer(g_nxp.loop, listener->timer);
    }
    if (listener->sock != nullptr) {
        nxp_event_loop_del_socket(g_nxp.loop, listener->sock);
        nxp_socket_close(listener->sock);
    }

    nxp_listener_destroy(listener->ls);
    free(listener);
}

/* ═════════════════════════════════════════════════════════
 *  PUBLIC API — Streams
 * ═════════════════════════════════════════════════════════ */

nxp_result nxp_stream_open(nxp_conn *conn, nxp_stream_type type,
                            uint8_t priority, nxp_stream_cb on_data,
                            nxp_stream_cb on_writable, nxp_stream_cb on_close,
                            void *user_data, nxp_stream **out_stream) {
    if (conn == nullptr || out_stream == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    uint64_t stream_id;
    nxp_result r = nxp_conn_open_stream(conn, &stream_id, type, false);
    if (nxp_result_is_err(r)) return r;

    nxp_stream *s = (nxp_stream *)calloc(1, sizeof(nxp_stream));
    if (s == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    s->id           = stream_id;
    s->conn         = conn;
    s->api_conn     = nxp_api_find_conn(conn);
    s->on_data      = on_data;
    s->on_writable  = on_writable;
    s->on_close     = on_close;
    s->user_data    = user_data;
    (void)priority;  /* scheduler handles internally */

    *out_stream = s;

    /* Flush in case stream open triggers frames */
    if (s->api_conn != nullptr) {
        nxp_api_flush_conn(s->api_conn);
    }

    return NXP_SUCCESS;
}

ssize_t nxp_stream_send(nxp_stream *stream, const uint8_t *data,
                          size_t len, bool fin) {
    if (stream == nullptr) return -1;

    ssize_t written = nxp_conn_stream_send(stream->conn, stream->id,
                                            data, len, fin);

    /* Flush to get data on the wire */
    if (written > 0 && stream->api_conn != nullptr) {
        nxp_api_flush_conn(stream->api_conn);
    }

    return written;
}

ssize_t nxp_stream_recv(nxp_stream *stream, uint8_t *buf,
                          size_t buf_cap, bool *fin) {
    if (stream == nullptr) return -1;
    return nxp_conn_stream_recv(stream->conn, stream->id, buf, buf_cap, fin);
}

void nxp_stream_shutdown(nxp_stream *stream, nxp_shutdown_dir dir) {
    if (stream == nullptr) return;

    if (dir == NXP_SHUTDOWN_WRITE || dir == NXP_SHUTDOWN_BOTH) {
        /* Send FIN by writing 0 bytes with fin=true */
        (void)nxp_conn_stream_send(stream->conn, stream->id, nullptr, 0, true);
        if (stream->api_conn != nullptr) {
            nxp_api_flush_conn(stream->api_conn);
        }
    }
    /* NXP_SHUTDOWN_READ: stop delivering data (no wire action needed) */
}

void nxp_stream_close(nxp_stream *stream) {
    if (stream == nullptr) return;

    /* Send FIN if not already sent */
    (void)nxp_conn_stream_send(stream->conn, stream->id, nullptr, 0, true);
    if (stream->api_conn != nullptr) {
        nxp_api_flush_conn(stream->api_conn);
    }

    free(stream);
}

nxp_stream_state nxp_stream_get_state(const nxp_stream *stream) {
    if (stream == nullptr) return NXP_STREAM_CLOSED;

    /* Look up internal stream */
    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(
        stream->conn->streams, stream->id);
    if (s == nullptr) return NXP_STREAM_CLOSED;
    return s->state;
}

size_t nxp_stream_writable(const nxp_stream *stream) {
    if (stream == nullptr) return 0;

    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(
        stream->conn->streams, stream->id);
    if (s == nullptr) return 0;

    /* Available space in send buffer */
    uint64_t used = s->send.write_offset - s->send.acked_offset;
    if (used >= s->send.cap) return 0;
    return s->send.cap - (size_t)used;
}

size_t nxp_stream_readable(const nxp_stream *stream) {
    if (stream == nullptr) return 0;

    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(
        stream->conn->streams, stream->id);
    if (s == nullptr) return 0;

    /* Available data in recv buffer */
    if (s->recv.recv_offset <= s->recv.read_offset) return 0;
    return (size_t)(s->recv.recv_offset - s->recv.read_offset);
}

uint64_t nxp_stream_get_id(const nxp_stream *stream) {
    if (stream == nullptr) return UINT64_MAX;
    return stream->id;
}

void nxp_stream_set_user_data(nxp_stream *stream, void *data) {
    if (stream != nullptr) stream->user_data = data;
}

void *nxp_stream_get_user_data(const nxp_stream *stream) {
    if (stream == nullptr) return nullptr;
    return stream->user_data;
}
