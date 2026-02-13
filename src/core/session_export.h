/*
 * NXP Session Export/Import - Header
 *
 * Phase 9: Serialize/deserialize connection state for server-to-server
 * migration. Allows a connection to be moved between server instances
 * without disruption (the client sees a seamless migration).
 *
 * Export captures: CIDs, crypto keys, flow control state, connection-level
 * state (pkt numbers, RTT estimates, CC state). Streams are NOT migrated
 * (the receiving server creates fresh streams on demand).
 *
 * Wire format (all fields little-endian):
 *   magic[4] | version[4] | total_len[4] |
 *   scid_len[1] | scid[scid_len] |
 *   dcid_len[1] | dcid[dcid_len] |
 *   state[1] | is_server[1] |
 *   next_pkt_num[8] |
 *   peer_addr[28] |
 *   crypto_state (send+recv keys, algo, available) |
 *   flow_ctrl (peer_max_data, data_sent, local_max_data, data_recv) |
 *   ack_rtt (smoothed_rtt, rtt_var, min_rtt) |
 *   idle_timeout_us[8]
 */
#ifndef NXP_SESSION_EXPORT_H
#define NXP_SESSION_EXPORT_H

#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"

#include <stddef.h>
#include <stdint.h>

/* Magic: "NXPS" (NXP Session) */
#define NXP_SESSION_MAGIC       0x5350584EU

/* Format version */
#define NXP_SESSION_VERSION     1

/* Max export buffer size (generous upper bound) */
#define NXP_SESSION_MAX_EXPORT  1024

/* Forward declare connection */
typedef struct nxp_conn nxp_conn;

/*
 * Export connection state to a buffer.
 *
 * The caller must provide a buffer of at least NXP_SESSION_MAX_EXPORT bytes.
 * On success, *out_len is set to the actual bytes written.
 *
 * The connection is NOT destroyed by this call - the caller should
 * close it after the remote side confirms receipt.
 */
[[nodiscard]] nxp_result nxp_session_export(
    const nxp_conn *conn,
    uint8_t *buf, size_t buf_cap,
    size_t *out_len
);

/*
 * Import connection state from a buffer.
 *
 * Creates a new nxp_conn with the serialized state.
 * Returns the connection via conn_out.
 *
 * The imported connection starts in ESTABLISHED state with the
 * original CIDs, crypto keys, and flow control limits.
 */
[[nodiscard]] nxp_result nxp_session_import(
    const uint8_t *buf, size_t buf_len,
    nxp_conn **conn_out
);

/*
 * Get the required buffer size for exporting a connection.
 */
[[nodiscard]] size_t nxp_session_export_size(const nxp_conn *conn);

#endif /* NXP_SESSION_EXPORT_H */
