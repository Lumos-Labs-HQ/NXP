/*
 * NXP Server Listener - Internal Header
 *
 * Phase 9: Multi-connection server listener with CID routing,
 * stateless retry support, and round-robin connection assignment.
 *
 * Sans-I/O design: the listener never touches sockets. It consumes
 * raw datagrams via nxp_listener_recv() and produces datagrams via
 * nxp_listener_send(). The I/O layer calls these functions from the
 * event loop.
 *
 * CID routing: when node_id_len > 0, generated CIDs embed a node ID
 * prefix so load balancers can route datagrams to the correct server
 * without decryption.
 */
#ifndef NXP_LISTENER_INTERNAL_H
#define NXP_LISTENER_INTERNAL_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include "connection_internal.h"
#include "hash_map.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Limits ────────────────────────────────────────────── */

#define NXP_LISTENER_DEFAULT_MAX_CONNS     4096
#define NXP_LISTENER_CID_LEN              8     /* Default CID length */
#define NXP_LISTENER_MAX_NODE_ID_LEN      4     /* Max node ID prefix bytes */
#define NXP_LISTENER_RETRY_KEY_LEN        16

/* ── Listener Configuration ────────────────────────────── */

typedef struct nxp_listener_config {
    /* CID routing: embed node ID prefix in generated CIDs */
    uint8_t  node_id[NXP_LISTENER_MAX_NODE_ID_LEN];
    uint8_t  node_id_len;         /* 0 = no CID routing */

    /* Connection defaults */
    uint32_t max_connections;     /* Max concurrent connections */
    uint64_t idle_timeout_us;
    uint64_t initial_max_data;
    uint64_t initial_max_stream_data;
    uint32_t max_streams_bidi;
    uint32_t max_streams_uni;

    /* Address validation */
    bool     require_retry;       /* Require Retry for address validation */
    uint8_t  retry_key[NXP_LISTENER_RETRY_KEY_LEN];

    /* New connection callback */
    void (*on_new_conn)(nxp_conn *conn, void *user_data);
    void  *user_data;
} nxp_listener_config;

/* ── Listener ──────────────────────────────────────────── */

typedef struct nxp_listener_s {
    /* CID → nxp_conn* lookup (uses CID hash as key) */
    nxp_hash_map       *conn_map;

    /* Array of all connections (for iteration / send loop) */
    nxp_conn          **conns;
    uint32_t            conn_count;
    uint32_t            conn_cap;

    /* Round-robin index for nxp_listener_send iteration */
    uint32_t            send_index;

    /* Configuration (owned copy) */
    nxp_listener_config config;

    /* CID generation counter (for uniqueness) */
    uint64_t            cid_counter;

    /* Stats */
    uint64_t            total_accepted;
    uint64_t            total_rejected;
    uint64_t            total_retried;
} nxp_listener_s;

/* ── Listener API ──────────────────────────────────────── */

/*
 * Create a server listener.
 * Returns nullptr on allocation failure.
 */
[[nodiscard]] nxp_listener_s *nxp_listener_create(
    const nxp_listener_config *config
);

/* Destroy the listener and all owned connections. */
void nxp_listener_destroy(nxp_listener_s *ls);

/*
 * Process an incoming UDP datagram.
 *
 * Routes the datagram to the correct connection by DCID lookup.
 * If no connection exists and the datagram is an Initial packet,
 * creates a new server-side connection (optionally with Retry).
 *
 * Returns NXP_SUCCESS on successful processing, or an error code
 * if the packet is malformed or the connection limit is reached.
 */
[[nodiscard]] nxp_result nxp_listener_recv(
    nxp_listener_s *ls,
    const uint8_t *data, size_t len,
    const nxp_addr *from_addr,
    uint64_t now_us
);

/*
 * Generate the next outgoing UDP datagram.
 *
 * Iterates connections round-robin, calling nxp_conn_send on each
 * until one produces a datagram. Fills peer_addr_out with the
 * destination address.
 *
 * Returns bytes written (0 = nothing to send).
 */
[[nodiscard]] ssize_t nxp_listener_send(
    nxp_listener_s *ls,
    uint8_t *out, size_t max_len,
    nxp_addr *peer_addr_out,
    uint64_t now_us
);

/*
 * Get the next timer deadline across all connections.
 * Returns UINT64_MAX if no timers pending.
 */
[[nodiscard]] uint64_t nxp_listener_timeout(
    const nxp_listener_s *ls,
    uint64_t now_us
);

/*
 * Fire timer events on all connections.
 * Cleans up closed/dead connections.
 */
void nxp_listener_on_timeout(nxp_listener_s *ls, uint64_t now_us);

/*
 * Look up a connection by CID hash.
 * Returns nullptr if not found.
 */
[[nodiscard]] nxp_conn *nxp_listener_find_conn(
    const nxp_listener_s *ls,
    const nxp_conn_id *dcid
);

/*
 * Remove a connection from the listener.
 * The connection is destroyed.
 */
void nxp_listener_remove_conn(nxp_listener_s *ls, nxp_conn *conn);

/*
 * Get number of active connections.
 */
[[nodiscard]] static inline uint32_t nxp_listener_conn_count(
    const nxp_listener_s *ls
) {
    return ls->conn_count;
}

/*
 * Generate a CID with optional node ID prefix for CID routing.
 * The CID format is: [node_id_prefix][random_bytes]
 */
void nxp_listener_generate_cid(
    const nxp_listener_s *ls,
    nxp_conn_id *cid_out
);

/*
 * Extract node ID from a CID (for load balancer routing).
 * Returns true if node_id_len bytes were extracted.
 */
[[nodiscard]] bool nxp_listener_extract_node_id(
    const nxp_conn_id *cid,
    uint8_t node_id_len,
    uint8_t *node_id_out
);

#endif /* NXP_LISTENER_INTERNAL_H */
