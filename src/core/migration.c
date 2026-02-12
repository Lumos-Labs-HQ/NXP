/*
 * NXP Connection Migration - Implementation
 *
 * Phase 6: Path validation via PATH_CHALLENGE/RESPONSE,
 * connection ID pool management, and address change handling.
 */
#include "migration_internal.h"
#include "util/random.h"

#include <string.h>

/* ── Init ──────────────────────────────────────────────── */

void nxp_migration_init(nxp_migration_state *ms) {
    memset(ms, 0, sizeof(*ms));
    ms->active_cid_limit = NXP_DEFAULT_ACTIVE_CID_LIMIT;
    ms->local_cid_next_seq = 1; /* seq 0 is the initial CID from handshake */
    ms->enabled = true;
}

/* ── Peer Address Change ──────────────────────────────── */

nxp_result nxp_migration_on_peer_addr_change(
    nxp_migration_state *ms,
    const nxp_addr *new_addr,
    uint64_t now_us)
{
    if (!ms->enabled) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    /* If already migrating, ignore (wait for current validation) */
    if (ms->migration_in_progress) return NXP_SUCCESS;

    /* Set up path validation for the new address */
    ms->new_path.addr = *new_addr;
    ms->new_path.validated = false;

    /* Generate random challenge data */
    nxp_result r = nxp_random_bytes(ms->new_path.challenge_data, 8);
    if (r.code != NXP_OK) return r;

    ms->new_path.challenge_sent_time = now_us;
    ms->new_path.challenge_pending = true;
    ms->send_path_challenge = true;
    ms->migration_in_progress = true;

    return NXP_SUCCESS;
}

/* ── PATH_CHALLENGE Handling ──────────────────────────── */

void nxp_migration_on_path_challenge(
    nxp_migration_state *ms,
    const uint8_t data[8])
{
    /* Queue a PATH_RESPONSE with the same data */
    memcpy(ms->pending_response_data, data, 8);
    ms->send_path_response = true;
}

/* ── PATH_RESPONSE Handling ───────────────────────────── */

bool nxp_migration_on_path_response(
    nxp_migration_state *ms,
    const uint8_t data[8])
{
    if (!ms->new_path.challenge_pending) return false;

    /* Check if the response matches our challenge */
    if (memcmp(data, ms->new_path.challenge_data, 8) != 0) {
        return false;
    }

    /* Path validated */
    ms->new_path.validated = true;
    ms->new_path.challenge_pending = false;

    /* Promote new path to current */
    ms->current_path = ms->new_path;
    ms->migration_in_progress = false;

    memset(&ms->new_path, 0, sizeof(ms->new_path));

    return true;
}

/* ── NEW_CONNECTION_ID Handling ────────────────────────── */

nxp_result nxp_migration_on_new_connection_id(
    nxp_migration_state *ms,
    const nxp_conn_id *cid,
    uint64_t seq_num,
    uint64_t retire_prior_to,
    const uint8_t stateless_reset_token[16])
{
    /* Retire CIDs with seq < retire_prior_to */
    for (uint32_t i = 0; i < ms->peer_cid_count; i++) {
        if (ms->peer_cid_pool[i].seq_num < retire_prior_to &&
            !ms->peer_cid_pool[i].retired) {
            ms->peer_cid_pool[i].retired = true;
            /* If this was in use, we need to switch */
            if (ms->peer_cid_pool[i].in_use) {
                ms->peer_cid_pool[i].in_use = false;
            }
        }
    }

    /* Check if we already have this seq_num */
    for (uint32_t i = 0; i < ms->peer_cid_count; i++) {
        if (ms->peer_cid_pool[i].seq_num == seq_num) {
            return NXP_SUCCESS; /* Duplicate, ignore */
        }
    }

    /* Find a free slot: prefer appending, reuse retired only when full */
    int slot = -1;
    if (ms->peer_cid_count < NXP_MAX_CID_POOL_SIZE) {
        slot = (int)ms->peer_cid_count;
        ms->peer_cid_count++;
    } else {
        for (uint32_t i = 0; i < NXP_MAX_CID_POOL_SIZE; i++) {
            if (ms->peer_cid_pool[i].retired && !ms->peer_cid_pool[i].in_use) {
                slot = (int)i;
                break;
            }
        }
    }

    if (slot < 0) {
        /* Pool is full - send RETIRE for the oldest non-in-use CID */
        return NXP_SUCCESS; /* Silently ignore for now */
    }

    nxp_cid_entry *entry = &ms->peer_cid_pool[slot];
    entry->cid = *cid;
    entry->seq_num = seq_num;
    memcpy(entry->stateless_reset_token, stateless_reset_token, 16);
    entry->in_use = false;
    entry->retired = false;

    return NXP_SUCCESS;
}

/* ── RETIRE_CONNECTION_ID Handling ────────────────────── */

nxp_result nxp_migration_on_retire_connection_id(
    nxp_migration_state *ms,
    uint64_t seq_num)
{
    (void)ms;
    (void)seq_num;
    /*
     * The peer is retiring one of our locally-generated CIDs.
     * We should stop using it as our SCID for this connection.
     * For now, just acknowledge - the connection engine tracks SCID.
     */
    return NXP_SUCCESS;
}

/* ── CID Pool Queries ─────────────────────────────────── */

const nxp_cid_entry *nxp_migration_get_unused_cid(
    const nxp_migration_state *ms)
{
    for (uint32_t i = 0; i < ms->peer_cid_count; i++) {
        if (!ms->peer_cid_pool[i].in_use && !ms->peer_cid_pool[i].retired) {
            return &ms->peer_cid_pool[i];
        }
    }
    return nullptr;
}

void nxp_migration_use_cid(nxp_migration_state *ms, uint64_t seq_num) {
    /* Unmark the currently in-use CID */
    for (uint32_t i = 0; i < ms->peer_cid_count; i++) {
        if (ms->peer_cid_pool[i].in_use) {
            ms->peer_cid_pool[i].in_use = false;
            /* Queue retirement of the old CID */
            ms->send_retire_cid = true;
            ms->retire_seq = ms->peer_cid_pool[i].seq_num;
        }
    }

    /* Mark the new CID as in use */
    for (uint32_t i = 0; i < ms->peer_cid_count; i++) {
        if (ms->peer_cid_pool[i].seq_num == seq_num) {
            ms->peer_cid_pool[i].in_use = true;
            break;
        }
    }
}

/* ── Generate NEW_CONNECTION_ID Frames ────────────────── */

uint32_t nxp_migration_generate_cids(
    nxp_migration_state *ms,
    const nxp_conn_id *base_scid)
{
    uint32_t generated = 0;

    /* Generate CIDs up to active_cid_limit */
    while (ms->pending_new_cid_count < ms->active_cid_limit &&
           ms->pending_new_cid_count < NXP_DEFAULT_ACTIVE_CID_LIMIT) {
        nxp_cid_entry *entry = &ms->pending_new_cids[ms->pending_new_cid_count];

        /* Generate a CID based on base SCID with random suffix */
        entry->cid.len = base_scid->len;
        (void)nxp_random_bytes(entry->cid.data, entry->cid.len);

        entry->seq_num = ms->local_cid_next_seq++;

        /* Generate stateless reset token */
        (void)nxp_random_bytes(entry->stateless_reset_token,
                                NXP_STATELESS_RESET_TOKEN_LEN);

        entry->in_use = false;
        entry->retired = false;

        ms->pending_new_cid_count++;
        generated++;
    }

    return generated;
}

/* ── Pending Frame Check ──────────────────────────────── */

bool nxp_migration_has_pending(const nxp_migration_state *ms) {
    return ms->send_path_challenge ||
           ms->send_path_response ||
           ms->send_retire_cid ||
           ms->pending_new_cid_count > 0;
}

/* ── Timeout Check ────────────────────────────────────── */

bool nxp_migration_check_timeout(
    nxp_migration_state *ms,
    uint64_t now_us,
    uint64_t pto_us)
{
    if (!ms->new_path.challenge_pending) return false;

    uint64_t timeout = ms->new_path.challenge_sent_time +
                       pto_us * NXP_PATH_VALIDATION_TIMEOUT_FACTOR;

    if (now_us >= timeout) {
        /* Validation timed out - revert */
        ms->new_path.challenge_pending = false;
        ms->migration_in_progress = false;
        memset(&ms->new_path, 0, sizeof(ms->new_path));
        return true;
    }

    return false;
}
