/*
 * NXP Error - Human-readable error strings
 */
#include "nxp/nxp_error.h"

const char *nxp_error_str(nxp_error_code code) {
    switch (code) {
    case NXP_OK:                     return "success";
    case NXP_ERR_INVALID_ARGUMENT:   return "invalid argument";
    case NXP_ERR_OUT_OF_MEMORY:      return "out of memory";
    case NXP_ERR_BUFFER_TOO_SMALL:   return "buffer too small";
    case NXP_ERR_WOULD_BLOCK:        return "operation would block";
    case NXP_ERR_CONNECTION_CLOSED:  return "connection closed";
    case NXP_ERR_STREAM_CLOSED:      return "stream closed";
    case NXP_ERR_FLOW_CONTROL:       return "flow control limit";
    case NXP_ERR_CRYPTO_FAIL:        return "cryptographic failure";
    case NXP_ERR_HANDSHAKE_FAIL:     return "handshake failed";
    case NXP_ERR_INVALID_PACKET:     return "invalid packet";
    case NXP_ERR_INVALID_FRAME:      return "invalid frame";
    case NXP_ERR_IDLE_TIMEOUT:       return "idle timeout";
    case NXP_ERR_VERSION_MISMATCH:   return "protocol version mismatch";
    case NXP_ERR_INTERNAL:           return "internal error";
    case NXP_ERR_PLATFORM:           return "platform error";
    case NXP_ERR_OVERFLOW:           return "arithmetic overflow";
    case NXP_ERR_REPLAY_DETECTED:    return "replay attack detected";
    case NXP_ERR_TOKEN_INVALID:      return "invalid token";
    case NXP_ERR_STREAM_LIMIT:       return "stream limit exceeded";
    case NXP_ERR_CONGESTION:         return "congestion control limit";
    }
    return "unknown error";
}
