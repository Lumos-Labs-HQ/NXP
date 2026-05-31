/*
 * NEXUS Protocol (NXP) - Server Listener API
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef NXP_LISTENER_H
#define NXP_LISTENER_H

#include "nxp_types.h"
#include "nxp_error.h"

/* Start listening for incoming NXP connections (UDP — legacy). */
[[nodiscard]] nxp_result nxp_listen(
    const nxp_config *config,
    const char       *bind_addr,
    uint16_t          port,
    nxp_listener_cb   on_new_conn,
    void             *user_data,
    nxp_listener    **out_listener
);

/*
 * Start listening using a transport URL.
 *
 * Supported schemes:
 *   nxp://host:port     NXP native over UDP
 *   ws://host:port      WebSocket server
 *   ntc://host:port     Raw TCP server
 *   nrtc://host:port    WebRTC server (future)
 *
 * Example:
 *   nxp_listen_url("ws://0.0.0.0:8080", on_conn, ctx, &listener);
 */
[[nodiscard]] nxp_result nxp_listen_url(
    const char *url,
    nxp_listener_cb on_new_conn,
    void *user_data,
    nxp_listener **out_listener
);

/* Stop listening */
void nxp_listener_close(nxp_listener *listener);

#endif /* NXP_LISTENER_H */
