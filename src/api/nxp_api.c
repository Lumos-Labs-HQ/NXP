/*
 * NXP Public API - Implementation
 *
 * Phase 11+15: Bridges the public API to the internal sans-I/O engine
 * through pluggable transport backends (UDP/WebSocket/TCP/WebRTC).
 *
 * Single-threaded model: user calls nxp_run() (blocking) or
 * nxp_poll() (non-blocking) to drive the event loop.
 */
#include "api_internal.h"
#include "platform/platform_socket.h"
#include <stdio.h>

/* ── Global State Singleton ──────────────────────────── */

nxp_global g_nxp = {0};

/* ── Forward declarations ────────────────────────────── */

static void on_conn_event(nxp_event_loop *loop, nxp_socket *sock,
                           uint32_t events, void *user_data);
static void on_conn_timer(nxp_event_loop *loop, void *user_data);
static void on_listener_event(nxp_event_loop *loop, nxp_socket *sock,
                               uint32_t events, void *user_data);
static void on_listener_timer(nxp_event_loop *loop, void *user_data);
static void dispatch_listener_conn_events(nxp_listener *ln);
static void dispatch_stream_data(nxp_api_conn *ac);
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
        (void)ac->transport->ops->send(ac->transport, out, (size_t)n,
                                        &ac->conn->peer_addr);
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
        (void)ln->transport->ops->send(ln->transport, out, (size_t)n, &peer_addr);
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

/* ── Transport event callbacks ───────────────────────── */

static void on_conn_event(nxp_event_loop *loop, nxp_socket *sock,
                           uint32_t events, void *user_data) {
    (void)loop; (void)sock;
    nxp_api_conn *ac = (nxp_api_conn *)user_data;

    if (events & NXP_EVENT_READ) {
        uint8_t buf[NXP_PACKET_BUF_SIZE];
        nxp_addr from;

        for (;;) {
            ssize_t n = ac->transport->ops->recv(ac->transport, buf, sizeof(buf), &from);
            if (n <= 0) break;

            uint64_t now = nxp_time_now_us();
            (void)nxp_conn_recv(ac->conn, buf, (size_t)n, now);
            nxp_conn_state cur = ac->conn->state;

            /* Detect transitions for user callbacks */
            if (ac->prev_state != NXP_CONN_ESTABLISHED && cur == NXP_CONN_ESTABLISHED) {
                if (ac->on_connected != nullptr) {
                    ac->on_connected(ac->conn, ac->cb_user_data);
                }
            }
            if ((cur == NXP_CONN_CLOSED || cur == NXP_CONN_DRAINING) &&
                ac->prev_state != NXP_CONN_CLOSED && ac->prev_state != NXP_CONN_DRAINING) {
                if (ac->on_closed != nullptr) {
                    ac->on_closed(ac->conn, ac->cb_user_data);
                }
            }

            ac->prev_state = cur;
        }
    }

    nxp_api_flush_conn(ac);
    dispatch_stream_data(ac);
    nxp_api_rearm_timer(ac);
}

static void on_conn_timer(nxp_event_loop *loop, void *user_data) {
    (void)loop;
    nxp_api_conn *ac = (nxp_api_conn *)user_data;
    ac->timer = nullptr;

    uint64_t now = nxp_time_now_us();
    nxp_conn_on_timeout(ac->conn, now);
    nxp_api_flush_conn(ac);
    dispatch_stream_data(ac);
    nxp_api_rearm_timer(ac);
}

static void dispatch_listener_conn_events(nxp_listener *ln) {
    for (nxp_api_conn *ac = g_nxp.conns; ac != nullptr; ac = ac->next) {
        if (ac->parent_listener != ln) continue;

        nxp_conn_state cur = ac->conn->state;
        nxp_conn_state prev = ac->prev_state;

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

        nxp_api_flush_conn(ac);
        dispatch_stream_data(ac);
        ac->prev_state = cur;
    }
}

static void on_listener_event(nxp_event_loop *loop, nxp_socket *sock,
                               uint32_t events, void *user_data) {
    (void)loop; (void)sock;
    nxp_listener *ln = (nxp_listener *)user_data;

    if (events & NXP_EVENT_READ) {
        uint8_t buf[NXP_PACKET_BUF_SIZE];
        nxp_addr from;

        for (;;) {
            ssize_t n = ln->transport->ops->recv(ln->transport, buf, sizeof(buf), &from);
            if (n <= 0) break;
            uint64_t now = nxp_time_now_us();
            (void)nxp_listener_recv(ln->ls, buf, (size_t)n, &from, now);
        }
    }

    nxp_api_flush_listener(ln);
    dispatch_listener_conn_events(ln);
    nxp_api_rearm_listener_timer(ln);
}

static void on_listener_timer(nxp_event_loop *loop, void *user_data) {
    (void)loop;
    nxp_listener *ln = (nxp_listener *)user_data;
    ln->timer = nullptr;

    uint64_t now = nxp_time_now_us();
    nxp_listener_on_timeout(ln->ls, now);
    nxp_api_flush_listener(ln);
    dispatch_listener_conn_events(ln);
    nxp_api_rearm_listener_timer(ln);
}

/* ── Internal new-connection callback (from listener) ── */

static void listener_on_new_conn_cb(nxp_conn *conn, void *user_data) {
    nxp_listener *ln = (nxp_listener *)user_data;

    nxp_api_conn *ac = (nxp_api_conn *)calloc(1, sizeof(nxp_api_conn));
    if (ac == nullptr) return;

    ac->conn           = conn;
    ac->transport       = ln->transport;      /* Share listener's transport */
    ac->owns_transport  = false;
    ac->parent_listener = ln;
    ac->prev_state      = conn->state;

    api_conn_link(ac);

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

    /* Initialize transport subsystem (registers all backends) */
    nxp_transport_init();

    g_nxp.loop = nxp_event_loop_create();
    if (g_nxp.loop == nullptr) {
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
        if (ac->owns_transport && ac->transport != nullptr) {
            nxp_socket *sock = nxp_transport_get_socket(ac->transport);
            nxp_event_loop_del_socket(g_nxp.loop, sock);
            ac->transport->ops->close(ac->transport);
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
        if (ln->transport != nullptr) {
            nxp_socket *sock = nxp_transport_get_socket(ln->transport);
            nxp_event_loop_del_socket(g_nxp.loop, sock);
            ln->transport->ops->close(ln->transport);
        }
        nxp_listener_destroy(ln->ls);
        free(ln);
    }

    nxp_event_loop_destroy(g_nxp.loop);
    memset(&g_nxp, 0, sizeof(g_nxp));
}

void nxp_run(void) {
    if (!g_nxp.initialized) return;
    (void)nxp_event_loop_run(g_nxp.loop);
}

void nxp_poll(void) {
    if (!g_nxp.initialized) return;
    (void)nxp_event_loop_run_once(g_nxp.loop, 1);
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

    /* Create transport via URL factory */
    char url[320];
    snprintf(url, sizeof(url), "nxp://%s:%u", host, port);
    nxp_transport *transport = nullptr;
    r = nxp_transport_connect(url, &transport);
    if (nxp_result_is_err(r)) return r;

    /* Generate random SCID and initial DCID */
    nxp_conn_id scid = {0};
    scid.len = 8;
    (void)nxp_random_bytes(scid.data, scid.len);

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
        transport->ops->close(transport);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    /* Start handshake */
    r = nxp_conn_start_handshake(conn, &dcid);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        transport->ops->close(transport);
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
        transport->ops->close(transport);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    ac->conn           = conn;
    ac->transport       = transport;
    ac->owns_transport  = true;
    ac->on_connected    = on_connected;
    ac->on_closed       = on_closed;
    ac->cb_user_data    = user_data;
    ac->prev_state      = conn->state;

    /* Register transport with event loop */
    nxp_socket *sock = nxp_transport_get_socket(transport);
    r = nxp_event_loop_add_socket(g_nxp.loop, sock,
                                   NXP_EVENT_READ, on_conn_event, ac);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        transport->ops->close(transport);
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
 *  PUBLIC API — Transport-Agnostic Connection (URL-based)
 * ═════════════════════════════════════════════════════════ */

nxp_result nxp_connect_url(const char *url,
                            nxp_conn_cb on_connected,
                            nxp_conn_cb on_closed,
                            void *user_data,
                            nxp_conn **out_conn) {
    if (!g_nxp.initialized) return NXP_ERROR(NXP_ERR_INTERNAL);
    if (url == nullptr || out_conn == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Create transport via URL factory */
    nxp_transport *transport = nullptr;
    nxp_result r = nxp_transport_connect(url, &transport);
    if (nxp_result_is_err(r)) return r;

    /* Generate IDs */
    nxp_conn_id scid = {0};
    scid.len = 8;
    (void)nxp_random_bytes(scid.data, scid.len);

    nxp_conn_id dcid = {0};
    dcid.len = 8;
    (void)nxp_random_bytes(dcid.data, dcid.len);

    /* Build connection config (use defaults — caller can override with set_callbacks) */
    nxp_conn_config cc;
    memset(&cc, 0, sizeof(cc));
    cc.scid                     = scid;
    cc.idle_timeout_us          = (uint64_t)NXP_IDLE_TIMEOUT_DEFAULT * 1000;
    cc.max_streams_bidi         = NXP_MAX_STREAMS_DEFAULT;
    cc.max_streams_uni          = NXP_MAX_STREAMS_DEFAULT;
    cc.initial_max_data         = NXP_DEFAULT_MAX_DATA;
    cc.initial_max_stream_data  = NXP_DEFAULT_MAX_STREAM_DATA;

    /* Create the sans-I/O connection */
    nxp_conn *conn = nxp_conn_create(&cc, false);
    if (conn == nullptr) {
        transport->ops->close(transport);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    /* Start handshake */
    r = nxp_conn_start_handshake(conn, &dcid);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        transport->ops->close(transport);
        return r;
    }

    /* Create API wrapper */
    nxp_api_conn *ac = (nxp_api_conn *)calloc(1, sizeof(nxp_api_conn));
    if (ac == nullptr) {
        nxp_conn_destroy(conn);
        transport->ops->close(transport);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    ac->conn           = conn;
    ac->transport       = transport;
    ac->owns_transport  = true;
    ac->on_connected    = on_connected;
    ac->on_closed       = on_closed;
    ac->cb_user_data    = user_data;
    ac->prev_state      = conn->state;

    /* Register transport with event loop */
    nxp_socket *sock = nxp_transport_get_socket(transport);
    r = nxp_event_loop_add_socket(g_nxp.loop, sock,
                                   NXP_EVENT_READ, on_conn_event, ac);
    if (nxp_result_is_err(r)) {
        nxp_conn_destroy(conn);
        transport->ops->close(transport);
        free(ac);
        return r;
    }

    api_conn_link(ac);
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

    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac != nullptr) {
        nxp_api_flush_conn(ac);
    }
}

nxp_conn_stats nxp_conn_get_stats(const nxp_conn *conn) {
    if (conn == nullptr) {
        nxp_conn_stats empty = {0};
        return empty;
    }
    return conn->stats;
}

void nxp_conn_set_callbacks(nxp_conn *conn, nxp_conn_cb on_connected,
                             nxp_conn_cb on_closed, void *user_data) {
    if (conn == nullptr) return;
    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac == nullptr) return;
    ac->on_connected = on_connected;
    ac->on_closed    = on_closed;
    ac->cb_user_data = user_data;
}

/* Called by the core when a new remote-initiated stream arrives */
static void on_new_remote_stream(nxp_conn *conn, uint64_t stream_id, void *ud) {
    nxp_api_conn *ac = (nxp_api_conn *)ud;
    if (ac == nullptr || ac->stream_accept_cb == nullptr) return;

    nxp_stream *s = (nxp_stream *)calloc(1, sizeof(nxp_stream));
    if (s == nullptr) return;

    s->id       = stream_id;
    s->conn     = conn;
    s->api_conn = ac;
    /* Prepend to stream list */
    s->next     = ac->streams;
    ac->streams = s;

    ac->stream_accept_cb(conn, s, ac->stream_accept_ud);
}

/* Fire on_data for all streams that have readable data */
static void dispatch_stream_data(nxp_api_conn *ac) {
    for (nxp_stream *s = ac->streams; s != nullptr; s = s->next) {
        if (s->on_data == nullptr) continue;
        nxp_stream_s *inner = (nxp_stream_s *)nxp_hash_map_get(
            ac->conn->streams, s->id);
        if (inner == nullptr) continue;

        bool has_new_data = inner->recv.recv_offset > s->last_dispatched_recv_offset;
        bool has_new_fin  = inner->recv.fin_received && !s->fin_notified;

        if (has_new_data || has_new_fin) {
            s->last_dispatched_recv_offset = inner->recv.recv_offset;
            if (inner->recv.fin_received) s->fin_notified = true;
            s->on_data(s, s->user_data);
        }
    }
}

void nxp_conn_set_stream_accept_cb(nxp_conn *conn, nxp_stream_accept_cb cb,
                                    void *user_data) {
    if (conn == nullptr) return;
    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac == nullptr) return;
    ac->stream_accept_cb = cb;
    ac->stream_accept_ud = user_data;

    conn->on_new_stream    = on_new_remote_stream;
    conn->on_new_stream_ud = ac;
}

void nxp_conn_set_user_data(nxp_conn *conn, void *data) {
    if (conn == nullptr) return;
    nxp_api_conn *ac = nxp_api_find_conn(conn);
    if (ac == nullptr) return;
    ac->app_user_data = data;
}

void *nxp_conn_get_user_data(const nxp_conn *conn) {
    if (conn == nullptr) return nullptr;
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

    /* Allocate public listener wrapper first (needed as user_data for callback) */
    nxp_listener *ln = (nxp_listener *)calloc(1, sizeof(nxp_listener));
    if (ln == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    char url[320];
    snprintf(url, sizeof(url), "nxp://%s:%u", bind_addr_str, port);

    /* Create transport listener (binds the UDP socket) */
    nxp_result r = nxp_transport_listen(url, nullptr, nullptr, &ln->transport_ln);
    if (nxp_result_is_err(r)) { free(ln); return r; }

    /* Reuse the listener's socket for send/recv — no second bind */
    ln->transport = nxp_udp_transport_from_listener(ln->transport_ln);
    if (ln->transport == nullptr) {
        ln->transport_ln->ops->close(ln->transport_ln);
        free(ln);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    lc.on_new_conn = listener_on_new_conn_cb;
    lc.user_data   = ln;

    /* Create the internal sans-I/O listener */
    nxp_listener_s *ls = nxp_listener_create(&lc);
    if (ls == nullptr) {
        ln->transport->ops->close(ln->transport);
        ln->transport_ln->ops->close(ln->transport_ln);
        free(ln);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    ln->ls          = ls;
    ln->on_new_conn = on_new_conn;
    ln->user_data   = user_data;

    /* Register transport with event loop */
    nxp_socket *sock = nxp_transport_get_socket(ln->transport);
    r = nxp_event_loop_add_socket(g_nxp.loop, sock,
                                   NXP_EVENT_READ, on_listener_event, ln);
    if (nxp_result_is_err(r)) {
        nxp_listener_destroy(ls);
        ln->transport->ops->close(ln->transport);
        ln->transport_ln->ops->close(ln->transport_ln);
        free(ln);
        return r;
    }

    listener_link(ln);
    nxp_api_rearm_listener_timer(ln);

    *out_listener = ln;
    return NXP_SUCCESS;
}

nxp_result nxp_listen_url(const char *url,
                           nxp_listener_cb on_new_conn,
                           void *user_data,
                           nxp_listener **out_listener) {
    if (!g_nxp.initialized) return NXP_ERROR(NXP_ERR_INTERNAL);
    if (url == nullptr || out_listener == nullptr) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Build internal listener config */
    nxp_listener_config lc;
    memset(&lc, 0, sizeof(lc));
    lc.max_connections      = NXP_LISTENER_DEFAULT_MAX_CONNS;
    lc.idle_timeout_us       = (uint64_t)NXP_IDLE_TIMEOUT_DEFAULT * 1000;
    lc.max_streams_bidi      = NXP_MAX_STREAMS_DEFAULT;
    lc.max_streams_uni       = NXP_MAX_STREAMS_DEFAULT;
    lc.initial_max_data      = NXP_DEFAULT_MAX_DATA;
    lc.initial_max_stream_data = NXP_DEFAULT_MAX_STREAM_DATA;

    /* Allocate listener wrapper */
    nxp_listener *ln = (nxp_listener *)calloc(1, sizeof(nxp_listener));
    if (ln == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    /* Create transport listener */
    nxp_result r = nxp_transport_listen(url, nullptr, nullptr, &ln->transport_ln);
    if (nxp_result_is_err(r)) { free(ln); return r; }

    /* Reuse the listener's socket for send/recv — no second bind */
    ln->transport = nxp_udp_transport_from_listener(ln->transport_ln);
    if (ln->transport == nullptr) {
        ln->transport_ln->ops->close(ln->transport_ln);
        free(ln);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    lc.on_new_conn = listener_on_new_conn_cb;
    lc.user_data   = ln;

    nxp_listener_s *ls = nxp_listener_create(&lc);
    if (ls == nullptr) {
        ln->transport->ops->close(ln->transport);
        ln->transport_ln->ops->close(ln->transport_ln);
        free(ln);
        return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
    }

    ln->ls          = ls;
    ln->on_new_conn = on_new_conn;
    ln->user_data   = user_data;

    nxp_socket *sock = nxp_transport_get_socket(ln->transport);
    r = nxp_event_loop_add_socket(g_nxp.loop, sock,
                                   NXP_EVENT_READ, on_listener_event, ln);
    if (nxp_result_is_err(r)) {
        nxp_listener_destroy(ls);
        ln->transport->ops->close(ln->transport);
        ln->transport_ln->ops->close(ln->transport_ln);
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

    /* Remove API wrappers for server-accepted connections */
    nxp_api_conn *ac = g_nxp.conns;
    while (ac != nullptr) {
        nxp_api_conn *next = ac->next;
        if (ac->parent_listener == listener) {
            api_conn_unlink(ac);
            if (ac->timer != nullptr) {
                nxp_event_loop_cancel_timer(g_nxp.loop, ac->timer);
            }
            free(ac);
        }
        ac = next;
    }

    listener_unlink(listener);

    if (listener->timer != nullptr) {
        nxp_event_loop_cancel_timer(g_nxp.loop, listener->timer);
    }
    if (listener->transport != nullptr) {
        nxp_socket *sock = nxp_transport_get_socket(listener->transport);
        nxp_event_loop_del_socket(g_nxp.loop, sock);
        listener->transport->ops->close(listener->transport);
    }
    if (listener->transport_ln != nullptr) {
        listener->transport_ln->ops->close(listener->transport_ln);
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
    (void)priority;

    /* Track stream for data dispatch */
    if (s->api_conn != nullptr) {
        s->next = s->api_conn->streams;
        s->api_conn->streams = s;
    }

    *out_stream = s;

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
        (void)nxp_conn_stream_send(stream->conn, stream->id, nullptr, 0, true);
        if (stream->api_conn != nullptr) {
            nxp_api_flush_conn(stream->api_conn);
        }
    }
}

void nxp_stream_close(nxp_stream *stream) {
    if (stream == nullptr) return;

    (void)nxp_conn_stream_send(stream->conn, stream->id, nullptr, 0, true);
    if (stream->api_conn != nullptr) {
        nxp_api_flush_conn(stream->api_conn);
    }

    free(stream);
}

nxp_stream_state nxp_stream_get_state(const nxp_stream *stream) {
    if (stream == nullptr) return NXP_STREAM_CLOSED;

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

    uint64_t used = s->send.write_offset - s->send.acked_offset;
    if (used >= s->send.cap) return 0;
    return s->send.cap - (size_t)used;
}

size_t nxp_stream_readable(const nxp_stream *stream) {
    if (stream == nullptr) return 0;

    nxp_stream_s *s = (nxp_stream_s *)nxp_hash_map_get(
        stream->conn->streams, stream->id);
    if (s == nullptr) return 0;

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
