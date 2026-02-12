/*
 * NXP Connection Migration - Internal Header
 *
 * Phase 6: PATH_CHALLENGE/RESPONSE path validation, connection ID
 * pool management, and address change detection for seamless mobility.
 *
 * Migration flow:
 *   1. Detect peer address change on incoming datagram
 *   2. Send PATH_CHALLENGE on the new path
 *   3. Peer echoes PATH_RESPONSE
 *   4. On validated: update peer_addr, rotate DCID
 *   5. Supply fresh CIDs via NEW_CONNECTION_ID frames
 */
#ifndef NXP_MIGRATION_INTERNAL_H
#define NXP_MIGRATION_INTERNAL_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Constants ────────────────────────────────────────── */

#define NXP_MAX_CID_POOL_SIZE     8   /* Max CIDs we hold from peer */
#define NXP_CID_SUPPLY_LOW_WATER  2   /* Replenish when pool drops below */
#define NXP_DEFAULT_ACTIVE_CID_LIMIT 4
#define NXP_PATH_VALIDATION_TIMEOUT_FACTOR 3  /* 3x PTO */
#define NXP_STATELESS_RESET_TOKEN_LEN 16

/* ── CID Pool Entry ───────────────────────────────────── */

typedef struct nxp_cid_entry {
    nxp_conn_id cid;
    uint64_t    seq_num;
    uint8_t     stateless_reset_token[NXP_STATELESS_RESET_TOKEN_LEN];
    bool        in_use;     /* Currently being used as our DCID */
    bool        retired;    /* Marked for retirement */
} nxp_cid_entry;

/* ── Path Validation State ────────────────────────────── */

typedef struct nxp_path_state {
    nxp_addr    addr;
    uint8_t     challenge_data[8];      /* Outgoing challenge bytes */
    uint64_t    challenge_sent_time;    /* When we sent the challenge */
    bool        challenge_pending;      /* We sent a challenge, awaiting response */
    bool        validated;              /* Path has been validated */
} nxp_path_state;

/* ── Migration State ──────────────────────────────────── */

typedef struct nxp_migration_state {
    /* CID pool (peer-supplied CIDs for our DCID choices) */
    nxp_cid_entry  peer_cid_pool[NXP_MAX_CID_POOL_SIZE];
    uint32_t       peer_cid_count;

    /* Our own CID sequence (for generating NEW_CONNECTION_ID frames) */
    uint64_t       local_cid_next_seq;

    /* Path validation */
    nxp_path_state current_path;
    nxp_path_state new_path;
    bool           migration_in_progress;

    /* Pending frames to send */
    bool           send_path_challenge;
    bool           send_path_response;
    uint8_t        pending_response_data[8];

    /* Pending NEW_CONNECTION_ID frames to send */
    nxp_cid_entry  pending_new_cids[NXP_DEFAULT_ACTIVE_CID_LIMIT];
    uint32_t       pending_new_cid_count;

    /* Pending RETIRE_CONNECTION_ID */
    bool           send_retire_cid;
    uint64_t       retire_seq;

    /* Peer's active CID limit */
    uint32_t       active_cid_limit;

    /* Whether migration is enabled */
    bool           enabled;
} nxp_migration_state;

/* ── Migration API ────────────────────────────────────── */

/* Initialize migration state */
void nxp_migration_init(nxp_migration_state *ms);

/*
 * Called when a datagram arrives from a different source address.
 * Initiates path validation by generating a PATH_CHALLENGE.
 */
[[nodiscard]] nxp_result nxp_migration_on_peer_addr_change(
    nxp_migration_state *ms,
    const nxp_addr *new_addr,
    uint64_t now_us
);

/*
 * Process an incoming PATH_CHALLENGE frame.
 * Queues a PATH_RESPONSE with the same data.
 */
void nxp_migration_on_path_challenge(
    nxp_migration_state *ms,
    const uint8_t data[8]
);

/*
 * Process an incoming PATH_RESPONSE frame.
 * If it matches our pending challenge, validates the new path.
 * Returns true if the path was validated (caller should update peer_addr).
 */
[[nodiscard]] bool nxp_migration_on_path_response(
    nxp_migration_state *ms,
    const uint8_t data[8]
);

/*
 * Process an incoming NEW_CONNECTION_ID frame.
 * Adds the CID to our peer CID pool.
 */
[[nodiscard]] nxp_result nxp_migration_on_new_connection_id(
    nxp_migration_state *ms,
    const nxp_conn_id *cid,
    uint64_t seq_num,
    uint64_t retire_prior_to,
    const uint8_t stateless_reset_token[16]
);

/*
 * Process an incoming RETIRE_CONNECTION_ID frame.
 * The sequence number refers to one of our locally-generated CIDs.
 */
[[nodiscard]] nxp_result nxp_migration_on_retire_connection_id(
    nxp_migration_state *ms,
    uint64_t seq_num
);

/*
 * Get the next available peer CID for use as DCID (for migration).
 * Returns nullptr if no unused CIDs are available.
 */
[[nodiscard]] const nxp_cid_entry *nxp_migration_get_unused_cid(
    const nxp_migration_state *ms
);

/*
 * Mark a CID as in use (called after selecting it for migration).
 */
void nxp_migration_use_cid(nxp_migration_state *ms, uint64_t seq_num);

/*
 * Generate NEW_CONNECTION_ID frames for the peer.
 * Generates CIDs up to active_cid_limit. Returns count generated.
 */
uint32_t nxp_migration_generate_cids(
    nxp_migration_state *ms,
    const nxp_conn_id *base_scid
);

/*
 * Check if migration has pending frames to send.
 */
[[nodiscard]] bool nxp_migration_has_pending(const nxp_migration_state *ms);

/*
 * Check path validation timeout.
 * Returns true if the validation timed out (caller should revert to old path).
 */
[[nodiscard]] bool nxp_migration_check_timeout(
    nxp_migration_state *ms,
    uint64_t now_us,
    uint64_t pto_us
);

#endif /* NXP_MIGRATION_INTERNAL_H */
