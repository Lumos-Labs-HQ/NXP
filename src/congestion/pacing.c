/*
 * NXP Packet Pacer - Implementation
 *
 * Phase 7: Token-bucket pacing engine.
 * Tokens represent bytes that can be sent. They accumulate at the
 * pacing rate and are consumed when packets are sent.
 *
 * We store tokens * 1000 internally to avoid integer truncation
 * when computing token refill from small time deltas.
 */
#include "pacing.h"

#include <string.h>

/* Scale factor for token precision (milli-bytes) */
#define TOKEN_SCALE 1000ULL

void nxp_pacer_init(nxp_pacer *p) {
    memset(p, 0, sizeof(*p));
    p->max_burst = NXP_PACER_MAX_BURST;
    /* Start with a full burst of tokens */
    p->tokens = p->max_burst * TOKEN_SCALE;
}

void nxp_pacer_set_rate(nxp_pacer *p, uint64_t rate_bps) {
    p->rate_bps = rate_bps;
    p->enabled  = (rate_bps > 0);
}

void nxp_pacer_update(nxp_pacer *p, uint64_t now_us) {
    if (!p->enabled || p->rate_bps == 0) return;

    if (!p->time_initialized) {
        p->last_update = now_us;
        p->time_initialized = true;
        return;
    }

    if (now_us <= p->last_update) return;

    uint64_t elapsed = now_us - p->last_update;
    p->last_update = now_us;

    /* tokens += rate * elapsed / 1e6 (scaled by TOKEN_SCALE) */
    uint64_t new_tokens = (p->rate_bps * elapsed * TOKEN_SCALE) / 1000000ULL;
    p->tokens += new_tokens;

    /* Cap tokens at max burst */
    uint64_t max_tokens = p->max_burst * TOKEN_SCALE;
    if (p->tokens > max_tokens) {
        p->tokens = max_tokens;
    }
}

bool nxp_pacer_can_send(const nxp_pacer *p, uint32_t bytes) {
    if (!p->enabled) return true;
    return p->tokens >= (uint64_t)bytes * TOKEN_SCALE;
}

void nxp_pacer_on_send(nxp_pacer *p, uint32_t bytes) {
    if (!p->enabled) return;

    uint64_t cost = (uint64_t)bytes * TOKEN_SCALE;
    if (p->tokens >= cost) {
        p->tokens -= cost;
    } else {
        p->tokens = 0;
    }
}

uint64_t nxp_pacer_next_send_time(const nxp_pacer *p, uint32_t bytes) {
    if (!p->enabled || p->rate_bps == 0) return 0;

    uint64_t cost = (uint64_t)bytes * TOKEN_SCALE;
    if (p->tokens >= cost) return 0; /* Can send now */

    /* How long until we have enough tokens?
     * deficit = cost - tokens (in milli-bytes)
     * time = deficit * 1e6 / (rate * TOKEN_SCALE) */
    uint64_t deficit = cost - p->tokens;
    return (deficit * 1000000ULL) / (p->rate_bps * TOKEN_SCALE);
}
