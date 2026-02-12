/*
 * NXP Handshake State Machine - Internal Header
 *
 * Phase 5: 1-RTT handshake using X25519 key exchange.
 * Messages are carried in CRYPTO frames within Initial/Handshake packets.
 *
 * Handshake flow:
 *   Client -> Initial [ClientHello]      (Initial keys from DCID)
 *   Server -> Handshake [ServerHello]    (Handshake keys from X25519)
 *   Server -> 1-RTT [HANDSHAKE_DONE]    (Application keys)
 *   Client ack -> ESTABLISHED
 */
#ifndef NXP_HANDSHAKE_INTERNAL_H
#define NXP_HANDSHAKE_INTERNAL_H

#include "crypto_internal.h"
#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Handshake Message Types ──────────────────────────── */

#define NXP_HS_CLIENT_HELLO  0x01
#define NXP_HS_SERVER_HELLO  0x02

/* ── Handshake State ──────────────────────────────────── */

typedef enum nxp_hs_state {
    NXP_HS_IDLE = 0,
    NXP_HS_WAIT_SERVER_HELLO,     /* Client: sent CH, waiting for SH */
    NXP_HS_WAIT_CLIENT_HELLO,     /* Server: waiting for CH */
    NXP_HS_WAIT_HANDSHAKE_DONE,   /* Client: got SH, waiting for HS_DONE */
    NXP_HS_ZERO_RTT_SENT,         /* Client: sent 0-RTT, waiting for SH */
    NXP_HS_COMPLETE,
} nxp_hs_state;

/* ── Transport Parameters ─────────────────────────────── */

/* Parameter IDs for transport params TLV encoding */
#define NXP_TP_INITIAL_MAX_DATA       0x01
#define NXP_TP_INITIAL_MAX_STREAM_DATA 0x02
#define NXP_TP_MAX_STREAMS_BIDI       0x03
#define NXP_TP_MAX_STREAMS_UNI        0x04
#define NXP_TP_IDLE_TIMEOUT           0x05
#define NXP_TP_MAX_EARLY_DATA         0x06
#define NXP_TP_ACTIVE_CID_LIMIT       0x0A

typedef struct nxp_transport_params {
    uint64_t initial_max_data;
    uint64_t initial_max_stream_data;
    uint32_t max_streams_bidi;
    uint32_t max_streams_uni;
    uint64_t idle_timeout_us;
    uint64_t max_early_data;       /* 0-RTT early data limit (Phase 6) */
    uint32_t active_cid_limit;     /* Max CIDs peer can supply (Phase 6) */
} nxp_transport_params;

/* ── Handshake Context ────────────────────────────────── */

#define NXP_HS_BUF_SIZE 512  /* Max handshake message size */

typedef struct nxp_handshake {
    nxp_hs_state state;
    bool         is_server;

    /* Local X25519 keypair */
    uint8_t local_privkey[NXP_X25519_KEY_LEN];
    uint8_t local_pubkey[NXP_X25519_KEY_LEN];

    /* Peer's X25519 public key */
    uint8_t peer_pubkey[NXP_X25519_KEY_LEN];
    bool    has_peer_pubkey;

    /* Shared secret */
    uint8_t shared_secret[NXP_X25519_KEY_LEN];

    /* Selected AEAD algorithm */
    nxp_aead_algo selected_algo;

    /* Transcript: concatenation of ClientHello + ServerHello messages */
    uint8_t transcript[NXP_HS_BUF_SIZE * 2];
    size_t  transcript_len;

    /* Crypto keys per level */
    nxp_crypto_state initial_keys;
    nxp_crypto_state handshake_keys;
    nxp_crypto_state app_keys;

    /* Packet numbers per handshake level (simple counters) */
    uint64_t initial_pn;
    uint64_t handshake_pn;

    /* Outgoing handshake data (CRYPTO frame content to send) */
    uint8_t send_buf[NXP_HS_BUF_SIZE];
    size_t  send_len;
    size_t  send_offset;
    nxp_crypto_level send_level; /* Which level to send on */

    /* Send HANDSHAKE_DONE frame (server only, 1-RTT level) */
    bool send_handshake_done;

    /* Received transport parameters from peer */
    nxp_transport_params peer_params;
    bool has_peer_params;

    /* Local transport parameters */
    nxp_transport_params local_params;

    /* Timestamp for handshake duration */
    uint64_t start_time;

    /* ── Phase 6: 0-RTT / Session Resumption ────────── */

    /* Resumption secret (derived after handshake, used for session tickets) */
    uint8_t resumption_secret[NXP_HASH_LEN];
    bool    has_resumption_secret;

    /* Master secret (kept temporarily for resumption secret derivation) */
    uint8_t master_secret[NXP_HASH_LEN];
    bool    has_master_secret;

    /* 0-RTT state */
    nxp_crypto_state zero_rtt_keys;
    bool             zero_rtt_attempted;   /* Client attempted 0-RTT */
    bool             zero_rtt_accepted;    /* Server accepted 0-RTT */
    bool             zero_rtt_rejected;    /* Server rejected 0-RTT */
    uint64_t         max_early_data;       /* Max 0-RTT data bytes */

    /* Session ticket (client receives from server via NEW_TOKEN) */
    uint8_t session_ticket[256];
    size_t  session_ticket_len;
    bool    has_session_ticket;

    /* Server-side: pending NEW_TOKEN frame to send after handshake */
    bool    send_new_token;
    uint8_t new_token_buf[256];
    size_t  new_token_len;
} nxp_handshake;

/* ── Handshake API ────────────────────────────────────── */

/* Create a handshake context. Generates X25519 keypair. */
[[nodiscard]] nxp_handshake *nxp_handshake_create(bool is_server);

/* Free handshake context (wipes key material). */
void nxp_handshake_destroy(nxp_handshake *hs);

/* Set local transport parameters (must be called before starting). */
void nxp_handshake_set_local_params(nxp_handshake *hs,
                                     const nxp_transport_params *params);

/*
 * Client: initiate the handshake. Generates ClientHello and
 * derives initial keys from the server's DCID.
 */
[[nodiscard]] nxp_result nxp_handshake_start_client(
    nxp_handshake *hs,
    const nxp_conn_id *server_dcid
);

/*
 * Server: prepare to receive a handshake.
 * Derives initial keys from our own SCID (which is client's DCID).
 */
[[nodiscard]] nxp_result nxp_handshake_start_server(
    nxp_handshake *hs,
    const nxp_conn_id *local_scid
);

/*
 * Process incoming CRYPTO frame data at a given crypto level.
 * Drives the handshake state machine forward.
 */
[[nodiscard]] nxp_result nxp_handshake_recv_crypto(
    nxp_handshake *hs,
    nxp_crypto_level level,
    const uint8_t *data, size_t len
);

/* Called when HANDSHAKE_DONE frame is received (client side). */
[[nodiscard]] nxp_result nxp_handshake_on_handshake_done(nxp_handshake *hs);

/* Check if handshake has data to send */
[[nodiscard]] bool nxp_handshake_has_data(const nxp_handshake *hs);

/* Get send level for outgoing handshake data */
[[nodiscard]] nxp_crypto_level nxp_handshake_send_level(const nxp_handshake *hs);

/*
 * Fill a CRYPTO frame with handshake data to send.
 * Returns the number of bytes filled, or 0 if nothing to send.
 */
size_t nxp_handshake_fill_crypto(
    nxp_handshake *hs,
    uint8_t *data, size_t max_len,
    uint64_t *offset
);

/* Check if handshake is complete */
[[nodiscard]] static inline bool nxp_handshake_is_complete(const nxp_handshake *hs) {
    return hs->state == NXP_HS_COMPLETE;
}

/* ── Phase 6: 0-RTT / Session Resumption API ─────────── */

/*
 * Client: initiate a 0-RTT handshake using a saved session ticket.
 * Derives 0-RTT keys from the ticket's resumption secret.
 * The ClientHello includes the ticket token in the Initial packet.
 */
[[nodiscard]] nxp_result nxp_handshake_start_client_0rtt(
    nxp_handshake *hs,
    const nxp_conn_id *server_dcid,
    const uint8_t *ticket, size_t ticket_len,
    const uint8_t server_key[32]
);

/* Check if this handshake has 0-RTT keys available */
[[nodiscard]] static inline bool nxp_handshake_has_zero_rtt(const nxp_handshake *hs) {
    return hs->zero_rtt_keys.available;
}

/* Check if 0-RTT was accepted by the server */
[[nodiscard]] static inline bool nxp_handshake_is_0rtt_accepted(const nxp_handshake *hs) {
    return hs->zero_rtt_accepted;
}

/* Get the resumption secret (available after handshake completes) */
[[nodiscard]] static inline bool nxp_handshake_get_resumption_secret(
    const nxp_handshake *hs, uint8_t out[32]) {
    if (!hs->has_resumption_secret) return false;
    memcpy(out, hs->resumption_secret, 32);
    return true;
}

#endif /* NXP_HANDSHAKE_INTERNAL_H */
