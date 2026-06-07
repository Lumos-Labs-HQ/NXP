/*
 * NXP Public API - Internal Header
 *
 * Phase 11: Defines wrapper structs, global state, and helpers
 * that bridge the public API to the internal sans-I/O engine.
 */
#ifndef NXP_API_INTERNAL_H
#define NXP_API_INTERNAL_H

#include "nxp/nxp.h"
#include "nxp/nxp_config.h"
#include "nxp/nxp_connection.h"
#include "nxp/nxp_stream.h"
#include "nxp/nxp_listener.h"

#include "core/connection_internal.h"
#include "core/listener_internal.h"
#include "platform/event_loop.h"
#include "platform/transport.h"
#include "platform/platform_time.h"
#include "util/random.h"

#include <stdlib.h>
#include <string.h>

/* ── Config (public opaque type) ─────────────────────── */

struct nxp_config {
    char     *cert_file;
    char     *key_file;
    char    **alpns;
    size_t    alpn_count;
    uint64_t  max_streams_bidi;
    uint64_t  max_streams_uni;
    uint64_t  idle_timeout_ms;
    uint16_t  max_udp_payload;
    uint64_t  heartbeat_interval_ms;
};

/* ── Per-connection API state ────────────────────────── */

typedef struct nxp_api_conn {
    nxp_conn              *conn;         /* Internal sans-I/O connection */
    nxp_transport         *transport;    /* Transport (UDP/WS/TCP/RTC) */
    nxp_timer             *timer;        /* Event loop timer handle */
    bool                   owns_transport;/* false for server-accepted conns */

    /* Public callbacks */
    nxp_conn_cb            on_connected;
    nxp_conn_cb            on_closed;
    void                  *cb_user_data; /* For on_connected / on_closed */
    nxp_conn_state         prev_state;   /* For state transition detection */

    /* Application user data (via set/get_user_data) */
    void                  *app_user_data;

    /* Stream accept callback (server-side) */
    nxp_stream_accept_cb   stream_accept_cb;
    void                  *stream_accept_ud;

    /* Linked list of all nxp_stream wrappers for this connection */
    struct nxp_stream     *streams;

    /* Parent listener (if server-accepted) */
    struct nxp_listener   *parent_listener;

    /* Linked list for global tracking */
    struct nxp_api_conn   *next;
    struct nxp_api_conn   *prev;
} nxp_api_conn;

/* ── Listener wrapper (public opaque type) ───────────── */

struct nxp_listener {
    nxp_listener_s        *ls;           /* Internal sans-I/O listener */
    nxp_transport         *transport;    /* Bound transport */
    nxp_transport_listener*transport_ln; /* Listener handle */
    nxp_timer             *timer;        /* Event loop timer handle */

    /* Public callback */
    nxp_listener_cb        on_new_conn;
    void                  *user_data;

    /* Linked list for global tracking */
    struct nxp_listener   *next;
    struct nxp_listener   *prev;
};

/* ── Stream wrapper (public opaque type) ─────────────── */

struct nxp_stream {
    uint64_t               id;
    nxp_conn              *conn;         /* Parent connection */
    nxp_api_conn          *api_conn;     /* API state for parent conn */

    /* Public callbacks */
    nxp_stream_cb          on_data;
    nxp_stream_cb          on_writable;
    nxp_stream_cb          on_close;
    void                  *user_data;

    /* Dispatch tracking: avoid re-firing when no new data */
    uint64_t               last_dispatched_recv_offset;
    bool                   fin_notified;

    /* Per-connection stream list */
    struct nxp_stream     *next;
};

/* ── Global State ───────────────────────────────────── */

typedef struct nxp_global {
    bool                   initialized;
    nxp_event_loop        *loop;
    nxp_api_conn          *conns;        /* Linked list of all connections */
    struct nxp_listener   *listeners;    /* Linked list of all listeners */
} nxp_global;

/* Global singleton (defined in nxp_api.c) */
extern nxp_global g_nxp;

/* ── Internal helpers (defined in nxp_api.c) ─────────── */

/* Find API conn wrapper by internal conn pointer */
nxp_api_conn *nxp_api_find_conn(nxp_conn *conn);

/* Drive I/O: flush all pending sends for a client connection */
void nxp_api_flush_conn(nxp_api_conn *ac);

/* Re-arm the timer for a connection */
void nxp_api_rearm_timer(nxp_api_conn *ac);

/* Drive I/O: flush all pending sends for a listener */
void nxp_api_flush_listener(struct nxp_listener *ln);

/* Re-arm the timer for a listener */
void nxp_api_rearm_listener_timer(struct nxp_listener *ln);

#endif /* NXP_API_INTERNAL_H */
