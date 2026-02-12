/*
 * NEXUS Protocol (NXP) - Connection API
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef NXP_CONNECTION_H
#define NXP_CONNECTION_H

#include "nxp_types.h"
#include "nxp_error.h"

/* Connect to a remote NXP server (async) */
[[nodiscard]] nxp_result nxp_connect(
    const nxp_config *config,
    const char       *host,
    uint16_t          port,
    nxp_conn_cb       on_connected,
    nxp_conn_cb       on_closed,
    void             *user_data,
    nxp_conn        **out_conn
);

/* Close a connection gracefully */
void nxp_conn_close(nxp_conn *conn, uint64_t error_code);

/* Get connection state */
[[nodiscard]] nxp_conn_state nxp_conn_get_state(const nxp_conn *conn);

/* Get connection statistics */
[[nodiscard]] nxp_conn_stats nxp_conn_get_stats(const nxp_conn *conn);

/* Set callback for incoming streams (server-side) */
void nxp_conn_set_stream_accept_cb(nxp_conn *conn, nxp_stream_accept_cb cb, void *user_data);

/* Get/set user data */
void  nxp_conn_set_user_data(nxp_conn *conn, void *data);
void *nxp_conn_get_user_data(const nxp_conn *conn);

#endif /* NXP_CONNECTION_H */
