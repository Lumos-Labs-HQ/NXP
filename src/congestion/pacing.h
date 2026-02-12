/*
 * NXP Packet Pacer - Header
 *
 * Phase 7: Token-bucket pacing engine to smooth out packet sends.
 * BBR sets the pacing rate; the pacer enforces it.
 */
#ifndef NXP_PACING_H
#define NXP_PACING_H

#include <stdint.h>
#include <stdbool.h>

#define NXP_PACER_MAX_BURST   (10 * 1200)  /* Max burst: 10 packets */

typedef struct nxp_pacer {
    uint64_t rate_bps;           /* Pacing rate in bytes/second */
    uint64_t tokens;             /* Available tokens (bytes * 1000) */
    uint64_t last_update;        /* Last time tokens were refilled */
    uint64_t max_burst;          /* Max burst size in bytes */
    bool     enabled;
    bool     time_initialized;   /* Has last_update been set? */
} nxp_pacer;

/* Initialize the pacer */
void nxp_pacer_init(nxp_pacer *p);

/* Set the pacing rate. 0 disables pacing. */
void nxp_pacer_set_rate(nxp_pacer *p, uint64_t rate_bps);

/* Refill tokens based on elapsed time. Call before can_send/on_send. */
void nxp_pacer_update(nxp_pacer *p, uint64_t now_us);

/* Check if we can send `bytes` worth of data */
bool nxp_pacer_can_send(const nxp_pacer *p, uint32_t bytes);

/* Record that we sent `bytes` of data */
void nxp_pacer_on_send(nxp_pacer *p, uint32_t bytes);

/* Get the next time a send will be allowed (UINT64_MAX if unknown) */
uint64_t nxp_pacer_next_send_time(const nxp_pacer *p, uint32_t bytes);

#endif /* NXP_PACING_H */
