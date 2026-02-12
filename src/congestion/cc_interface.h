/*
 * NXP Congestion Control Interface
 *
 * Phase 7: Pluggable congestion control via vtable pattern.
 * The connection engine calls these hooks; concrete algorithms
 * (BBR, Cubic, etc.) implement them.
 */
#ifndef NXP_CC_INTERFACE_H
#define NXP_CC_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declaration */
typedef struct nxp_cc_ops nxp_cc_ops;

/* Information passed to the CC on packet ACK */
typedef struct nxp_cc_ack_info {
    uint64_t pkt_num;
    uint32_t acked_bytes;       /* Bytes acknowledged in this packet */
    uint64_t sent_time;         /* When the ACKed packet was sent */
    uint64_t now_us;            /* Current time */
    uint64_t rtt_us;            /* RTT measured from this ACK (0 if N/A) */
    uint64_t min_rtt_us;        /* Current min RTT estimate */
    uint64_t bytes_in_flight;   /* After deducting this ACK */
    uint64_t delivery_rate_bps; /* Delivery rate sample (bytes/sec) */
    uint64_t round_count;       /* Delivery rate round counter */
    bool     is_app_limited;    /* Was this an app-limited sample? */
    bool     round_start;       /* Did a new RTT round start? */
} nxp_cc_ack_info;

/* Information passed to the CC on packet loss */
typedef struct nxp_cc_loss_info {
    uint32_t lost_bytes;        /* Bytes declared lost */
    uint64_t now_us;
    uint64_t bytes_in_flight;   /* After deducting the loss */
} nxp_cc_loss_info;

/* Congestion control vtable */
struct nxp_cc_ops {
    const char *name;

    /* Create a new CC state instance */
    void *(*create)(void);

    /* Destroy CC state */
    void (*destroy)(void *state);

    /* Called when a packet is sent */
    void (*on_sent)(void *state, uint32_t sent_bytes, uint64_t now_us,
                    uint64_t bytes_in_flight);

    /* Called when a packet is ACKed */
    void (*on_ack)(void *state, const nxp_cc_ack_info *info);

    /* Called when a packet is declared lost */
    void (*on_loss)(void *state, const nxp_cc_loss_info *info);

    /* Get the current congestion window (bytes) */
    uint64_t (*get_cwnd)(const void *state);

    /* Get the current pacing rate (bytes/second), 0 = no pacing */
    uint64_t (*get_pacing_rate)(const void *state);

    /* Check if we're in slow start / startup */
    bool (*in_slow_start)(const void *state);

    /* Get the send quantum (max burst size, bytes) */
    uint64_t (*get_send_quantum)(const void *state);
};

/* The default (BBR) CC operations - defined in bbr.c */
extern const nxp_cc_ops nxp_cc_bbr;

#endif /* NXP_CC_INTERFACE_H */
