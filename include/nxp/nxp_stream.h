/*
 * NEXUS Protocol (NXP) - Stream API
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef NXP_STREAM_H
#define NXP_STREAM_H

#include "nxp_types.h"
#include "nxp_error.h"

/* Portable ssize_t */
#ifdef _WIN32
    #include <basetsd.h>
    typedef SSIZE_T ssize_t;
#else
    #include <sys/types.h>
#endif

/* Open a new stream on a connection */
[[nodiscard]] nxp_result nxp_stream_open(
    nxp_conn        *conn,
    nxp_stream_type  type,
    uint8_t          priority,
    nxp_stream_cb    on_data,
    nxp_stream_cb    on_writable,
    nxp_stream_cb    on_close,
    void            *user_data,
    nxp_stream     **out_stream
);

/* Send data on a stream */
[[nodiscard]] ssize_t nxp_stream_send(
    nxp_stream     *stream,
    const uint8_t  *data,
    size_t          len,
    bool            fin
);

/* Receive data from a stream */
[[nodiscard]] ssize_t nxp_stream_recv(
    nxp_stream *stream,
    uint8_t    *buf,
    size_t      buf_cap,
    bool       *fin
);

/* Shut down stream in one or both directions */
void nxp_stream_shutdown(nxp_stream *stream, nxp_shutdown_dir dir);

/* Close and destroy a stream */
void nxp_stream_close(nxp_stream *stream);

/* Query stream state */
[[nodiscard]] nxp_stream_state nxp_stream_get_state(const nxp_stream *stream);

/* Bytes available to write (backpressure) */
[[nodiscard]] size_t nxp_stream_writable(const nxp_stream *stream);

/* Bytes available to read */
[[nodiscard]] size_t nxp_stream_readable(const nxp_stream *stream);

/* Get stream ID */
[[nodiscard]] uint64_t nxp_stream_get_id(const nxp_stream *stream);

/* Get/set user data */
void  nxp_stream_set_user_data(nxp_stream *stream, void *data);
void *nxp_stream_get_user_data(const nxp_stream *stream);

#endif /* NXP_STREAM_H */
