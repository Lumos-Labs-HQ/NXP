/*
 * NXP Per-Stream Rate Limiter - Header
 *
 * Phase 8: Token-bucket rate limiter per stream.
 * Independent of the connection-level BBR pacing.
 * This limits how fast the application can push data into a stream,
 * providing per-stream bandwidth isolation.
 */
#ifndef NXP_STREAM_RATE_H
#define NXP_STREAM_RATE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct nxp_stream_rate {
    uint64_t rate_bps;           /* Max bytes/second (0 = unlimited) */
    uint64_t tokens;             /* Available tokens (bytes * 1000) */
    uint64_t last_update;        /* Last refill time */
    uint64_t max_burst;          /* Max burst (bytes) */
    bool     enabled;
    bool     time_initialized;
} nxp_stream_rate;

/* Initialize a stream rate limiter */
static inline void nxp_stream_rate_init(nxp_stream_rate *r) {
    r->rate_bps = 0;
    r->tokens = 0;
    r->last_update = 0;
    r->max_burst = 0;
    r->enabled = false;
    r->time_initialized = false;
}

/* Set the rate limit for a stream. 0 = unlimited. */
static inline void nxp_stream_rate_set(nxp_stream_rate *r, uint64_t rate_bps) {
    r->rate_bps = rate_bps;
    r->enabled = (rate_bps > 0);
    r->max_burst = rate_bps > 0 ? rate_bps / 10 : 0; /* 100ms worth */
    if (r->max_burst < 4800) r->max_burst = 4800;     /* min 4 packets */
    r->tokens = r->max_burst * 1000ULL;
}

/* Refill tokens based on elapsed time */
static inline void nxp_stream_rate_update(nxp_stream_rate *r, uint64_t now_us) {
    if (!r->enabled || r->rate_bps == 0) return;

    if (!r->time_initialized) {
        r->last_update = now_us;
        r->time_initialized = true;
        return;
    }

    if (now_us <= r->last_update) return;

    uint64_t elapsed = now_us - r->last_update;
    r->last_update = now_us;

    uint64_t new_tokens = (r->rate_bps * elapsed * 1000ULL) / 1000000ULL;
    r->tokens += new_tokens;

    uint64_t max_tokens = r->max_burst * 1000ULL;
    if (r->tokens > max_tokens) r->tokens = max_tokens;
}

/* Check if we can send `bytes` of data on this stream */
static inline bool nxp_stream_rate_can_send(const nxp_stream_rate *r,
                                              uint32_t bytes) {
    if (!r->enabled) return true;
    return r->tokens >= (uint64_t)bytes * 1000ULL;
}

/* Record that we sent `bytes` on this stream */
static inline void nxp_stream_rate_on_send(nxp_stream_rate *r, uint32_t bytes) {
    if (!r->enabled) return;
    uint64_t cost = (uint64_t)bytes * 1000ULL;
    if (r->tokens >= cost) {
        r->tokens -= cost;
    } else {
        r->tokens = 0;
    }
}

/* Get the available send budget in bytes */
static inline uint64_t nxp_stream_rate_budget(const nxp_stream_rate *r) {
    if (!r->enabled) return UINT64_MAX;
    return r->tokens / 1000ULL;
}

#endif /* NXP_STREAM_RATE_H */
