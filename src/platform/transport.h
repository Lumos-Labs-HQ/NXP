/*
 * NXP Transport Abstraction Layer
 *
 * Pluggable transport backends (UDP, WebSocket, TCP, WebRTC).
 * The sans-I/O core never touches sockets — it consumes/produces
 * bytes through this interface.
 *
 * Each backend provides a nxp_transport (client connection) and
 * optionally a nxp_transport_listener (server accept loop).
 */
#ifndef NXP_TRANSPORT_H
#define NXP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include "platform_socket.h"

#ifdef _WIN32
    #include <basetsd.h>
    typedef SSIZE_T ssize_t;
#else
    #include <sys/types.h>
#endif

/* ── Transport type ────────────────────────────────────── */

typedef enum {
    NXP_TRANSPORT_UDP = 0,       /* NXP native over UDP ("nxp://") */
    NXP_TRANSPORT_WS,            /* WebSocket ("ws://" / "wss://") */
    NXP_TRANSPORT_TCP,           /* Raw TCP with length prefix ("ntc://") */
    NXP_TRANSPORT_RTC,           /* WebRTC DataChannel ("nrtc://") */
    NXP_TRANSPORT_COUNT,
} nxp_transport_type;

/* ── Forward declarations ──────────────────────────────── */

typedef struct nxp_transport          nxp_transport;
typedef struct nxp_transport_listener  nxp_transport_listener;
typedef struct nxp_transport_ops      nxp_transport_ops;

/* ── Transport listener vtable ─────────────────────────── */

typedef struct nxp_transport_listener_ops {
    /* Accept an incoming connection (non-blocking, returns NULL if none) */
    nxp_transport *(*accept)(nxp_transport_listener *ln);

    /* Close the listener */
    void           (*close)(nxp_transport_listener *ln);

    /* Get native handle for event loop registration */
    intptr_t       (*native_handle)(nxp_transport_listener *ln);

    /* Get the local address this listener is bound to */
    nxp_result     (*local_addr)(nxp_transport_listener *ln, nxp_addr *out);
} nxp_transport_listener_ops;

struct nxp_transport_listener {
    const nxp_transport_listener_ops *ops;
    void                             *state;
};

/* ── Transport (client connection) vtable ─────────────── */

typedef struct nxp_transport_ops {
    /* Send data (non-blocking) */
    ssize_t    (*send)(nxp_transport *t, const uint8_t *data, size_t len,
                       const nxp_addr *to);

    /* Receive data (non-blocking, fills *from if not NULL) */
    ssize_t    (*recv)(nxp_transport *t, uint8_t *buf, size_t cap,
                       nxp_addr *from);

    /* Close the transport */
    void       (*close)(nxp_transport *t);

    /* Get native handle for event loop registration (fd / SOCKET / etc) */
    intptr_t   (*native_handle)(nxp_transport *t);

    /* Get local address this transport is bound to */
    nxp_result (*local_addr)(nxp_transport *t, nxp_addr *out);

    /* Get the transport type */
    nxp_transport_type (*type)(nxp_transport *t);
} nxp_transport_ops;

struct nxp_transport {
    const nxp_transport_ops *ops;
    void                    *state;
};

/* ── Transport factory functions ───────────────────────── */

/*
 * Create a client transport connected to the given URL.
 * URL scheme determines the backend:
 *   nxp://host:port  → UDP (NXPNATIVE)
 *   ws://host:port   → WebSocket
 *   wss://host:port  → WebSocket + TLS
 *   ntc://host:port  → Raw TCP
 *   nrtc://host:port → WebRTC
 */
[[nodiscard]] nxp_result nxp_transport_connect(
    const char *url,
    nxp_transport **out
);

/*
 * Create a server listener bound to the given URL.
 * Same scheme detection as nxp_transport_connect.
 */
[[nodiscard]] nxp_result nxp_transport_listen(
    const char *url,
    void (*on_new_conn)(nxp_transport *t, void *user_data),
    void *user_data,
    nxp_transport_listener **out
);

/* ── Transport backends (internal, called by factory) ──── */

/* Register a backend. Called at init time by each backend's constructor. */
void nxp_transport_register(
    nxp_transport_type type,
    bool (*can_connect)(void),
    nxp_result (*connect_impl)(const char *url, nxp_transport **out),
    bool (*can_listen)(void),
    nxp_result (*listen_impl)(const char *url,
                               void (*on_new_conn)(nxp_transport *, void *),
                               void *user_data,
                               nxp_transport_listener **out)
);

/* UDP-specific: create a transport that shares the listener's socket */
nxp_transport *nxp_udp_transport_from_listener(nxp_transport_listener *ln);

/* Get the underlying nxp_socket* from a transport (UDP only, for event loop registration) */
nxp_socket *nxp_transport_get_socket(nxp_transport *t);

/* Internal: called by nxp_init() to set up backends */
void nxp_transport_init(void);

#endif /* NXP_TRANSPORT_H */
