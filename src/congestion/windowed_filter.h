/*
 * NXP Windowed Min/Max Filter
 *
 * Phase 7: Tracks min or max value within a sliding time window.
 * Used by BBR for:
 *   - min_rtt: minimum RTT over last ~10 seconds
 *   - max_bw:  maximum bandwidth over last ~10 round-trips
 *
 * Based on the Kathleen Nichols windowed min/max algorithm.
 * Header-only: small enough that inline is appropriate.
 */
#ifndef NXP_WINDOWED_FILTER_H
#define NXP_WINDOWED_FILTER_H

#include <stdint.h>
#include <stdbool.h>

/* A sample in the filter: value + timestamp */
typedef struct nxp_wf_sample {
    uint64_t value;
    uint64_t time;     /* Timestamp or round number when sampled */
} nxp_wf_sample;

/* Windowed filter with 3 samples (best, 2nd-best, 3rd-best) */
typedef struct nxp_windowed_filter {
    nxp_wf_sample s[3];  /* [0]=best, [1]=2nd, [2]=3rd */
    uint64_t window;      /* Window duration (time or rounds) */
    bool     is_max;      /* true = max filter, false = min filter */
    bool     initialized;
} nxp_windowed_filter;

static inline void nxp_wf_init(nxp_windowed_filter *f, uint64_t window,
                                 bool is_max) {
    f->s[0] = (nxp_wf_sample){0, 0};
    f->s[1] = (nxp_wf_sample){0, 0};
    f->s[2] = (nxp_wf_sample){0, 0};
    f->window = window;
    f->is_max = is_max;
    f->initialized = false;
}

static inline void nxp_wf_reset(nxp_windowed_filter *f, uint64_t value,
                                  uint64_t now) {
    nxp_wf_sample s = {value, now};
    f->s[0] = s;
    f->s[1] = s;
    f->s[2] = s;
    f->initialized = true;
}

static inline uint64_t nxp_wf_get(const nxp_windowed_filter *f) {
    return f->s[0].value;
}

static inline bool nxp_wf_is_better(const nxp_windowed_filter *f,
                                      uint64_t a, uint64_t b) {
    return f->is_max ? (a >= b) : (a <= b);
}

/*
 * Update the filter with a new sample.
 * Uses the Kathleen Nichols "windowed min/max" approach:
 * We maintain best, 2nd-best, 3rd-best in the window.
 */
static inline void nxp_wf_update(nxp_windowed_filter *f, uint64_t value,
                                   uint64_t now) {
    nxp_wf_sample new_s = {value, now};

    if (!f->initialized) {
        nxp_wf_reset(f, value, now);
        return;
    }

    /* If the new value is better than the current best, reset */
    if (nxp_wf_is_better(f, value, f->s[0].value)) {
        nxp_wf_reset(f, value, now);
        return;
    }

    /* If the best has expired, rotate */
    if (now - f->s[0].time > f->window) {
        f->s[0] = f->s[1];
        f->s[1] = f->s[2];
        f->s[2] = new_s;

        if (now - f->s[0].time > f->window) {
            f->s[0] = f->s[1];
            f->s[1] = f->s[2];
        }
        if (now - f->s[0].time > f->window) {
            f->s[0] = new_s;
            f->s[1] = new_s;
        }
        return;
    }

    /* Check if new value should replace 2nd or 3rd best */
    if (nxp_wf_is_better(f, value, f->s[1].value) ||
        now - f->s[1].time > f->window / 4) {
        f->s[1] = new_s;
        f->s[2] = new_s;
        return;
    }

    if (nxp_wf_is_better(f, value, f->s[2].value) ||
        now - f->s[2].time > f->window / 2) {
        f->s[2] = new_s;
    }
}

#endif /* NXP_WINDOWED_FILTER_H */
