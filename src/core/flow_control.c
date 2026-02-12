/*
 * NXP Flow Control
 *
 * Phase 4: Connection-level and per-stream flow control.
 * Tracks send/recv data limits and manages MAX_DATA updates.
 */
#include "connection_internal.h"

/* ── Init ─────────────────────────────────────────────── */

void nxp_flow_init(nxp_flow_ctrl *fc, uint64_t local_max, uint64_t peer_max) {
    fc->peer_max_data  = peer_max;
    fc->data_sent      = 0;
    fc->local_max_data = local_max;
    fc->data_recv      = 0;
    fc->max_data_next  = local_max;
    fc->send_max_data  = false;
}

/* ── Sending Side ─────────────────────────────────────── */

bool nxp_flow_can_send(const nxp_flow_ctrl *fc, uint64_t len) {
    return fc->data_sent + len <= fc->peer_max_data;
}

void nxp_flow_on_send(nxp_flow_ctrl *fc, uint64_t len) {
    fc->data_sent += len;
}

/* ── Receiving Side ───────────────────────────────────── */

void nxp_flow_on_recv(nxp_flow_ctrl *fc, uint64_t len) {
    fc->data_recv += len;
}

void nxp_flow_on_consume(nxp_flow_ctrl *fc, uint64_t len) {
    (void)len;
    /*
     * When the application reads data, we might want to open the receive
     * window by advertising a new MAX_DATA. We send an update when the
     * consumed data exceeds half the window.
     */
    uint64_t consumed = fc->data_recv;  /* All received data is "consumed" */
    uint64_t window   = fc->local_max_data;

    /* Open window: advance to data_recv + original_window */
    if (consumed > window / 2) {
        uint64_t new_max = fc->data_recv + window;
        if (new_max > fc->max_data_next) {
            fc->max_data_next = new_max;
            fc->send_max_data = true;
        }
    }
}

void nxp_flow_set_peer_max(nxp_flow_ctrl *fc, uint64_t max_data) {
    if (max_data > fc->peer_max_data) {
        fc->peer_max_data = max_data;
    }
}

/* ── MAX_DATA Update ──────────────────────────────────── */

bool nxp_flow_should_update(const nxp_flow_ctrl *fc) {
    return fc->send_max_data;
}

uint64_t nxp_flow_get_update(nxp_flow_ctrl *fc) {
    fc->send_max_data  = false;
    fc->local_max_data = fc->max_data_next;
    return fc->local_max_data;
}
