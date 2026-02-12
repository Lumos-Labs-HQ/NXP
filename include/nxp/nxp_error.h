/*
 * NEXUS Protocol (NXP) - Error Codes and Handling
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef NXP_ERROR_H
#define NXP_ERROR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum nxp_error_code {
    NXP_OK                     =  0,
    NXP_ERR_INVALID_ARGUMENT   = -1,
    NXP_ERR_OUT_OF_MEMORY      = -2,
    NXP_ERR_BUFFER_TOO_SMALL   = -3,
    NXP_ERR_WOULD_BLOCK        = -4,
    NXP_ERR_CONNECTION_CLOSED  = -5,
    NXP_ERR_STREAM_CLOSED      = -6,
    NXP_ERR_FLOW_CONTROL       = -7,
    NXP_ERR_CRYPTO_FAIL        = -8,
    NXP_ERR_HANDSHAKE_FAIL     = -9,
    NXP_ERR_INVALID_PACKET     = -10,
    NXP_ERR_INVALID_FRAME      = -11,
    NXP_ERR_IDLE_TIMEOUT       = -12,
    NXP_ERR_VERSION_MISMATCH   = -13,
    NXP_ERR_INTERNAL           = -14,
    NXP_ERR_PLATFORM           = -15,
    NXP_ERR_OVERFLOW           = -16,
    NXP_ERR_REPLAY_DETECTED    = -17,
    NXP_ERR_TOKEN_INVALID      = -18,
    NXP_ERR_STREAM_LIMIT       = -19,
    NXP_ERR_CONGESTION         = -20,
} nxp_error_code;

typedef struct nxp_result {
    nxp_error_code code;
#ifdef NXP_DEBUG
    const char    *file;
    int            line;
#endif
} nxp_result;

#define NXP_SUCCESS ((nxp_result){ .code = NXP_OK })

#ifdef NXP_DEBUG
    #define NXP_ERROR(c) ((nxp_result){ .code = (c), .file = __FILE__, .line = __LINE__ })
#else
    #define NXP_ERROR(c) ((nxp_result){ .code = (c) })
#endif

[[nodiscard]] static inline bool nxp_result_is_ok(nxp_result r) {
    return r.code == NXP_OK;
}

[[nodiscard]] static inline bool nxp_result_is_err(nxp_result r) {
    return r.code != NXP_OK;
}

/* Human-readable error string */
const char *nxp_error_str(nxp_error_code code);

#endif /* NXP_ERROR_H */
