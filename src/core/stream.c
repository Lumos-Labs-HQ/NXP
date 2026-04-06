/*
 * NXP Stream Management
 *
 * Phase 4: Stream lifecycle, send/recv buffers, frame generation,
 * and data reassembly.
 */
#include "connection_internal.h"
#include "util/flight_recorder.h"

#include <stdlib.h>
#include <string.h>


/* Validate stream state transition */
static bool is_valid_stream_state_transition(nxp_stream_state from, nxp_stream_state to) {
    /* Valid transitions:
     * IDLE -> OPEN
     * OPEN -> HALF_CLOSED_LOCAL
     * OPEN -> HALF_CLOSED_REMOTE
     * OPEN -> CLOSED
     * HALF_CLOSED_LOCAL -> CLOSED
     * HALF_CLOSED_REMOTE -> CLOSED
     * Any -> RESET
     */
    if (to == NXP_STREAM_RESET || to == NXP_STREAM_CLOSED) return true;
    
    switch (from) {
    case NXP_STREAM_IDLE:
        return to == NXP_STREAM_OPEN;
    case NXP_STREAM_OPEN:
        return to == NXP_STREAM_HALF_CLOSED_LOCAL || 
               to == NXP_STREAM_HALF_CLOSED_REMOTE;
    case NXP_STREAM_HALF_CLOSED_LOCAL:
    case NXP_STREAM_HALF_CLOSED_REMOTE:
        return to == NXP_STREAM_CLOSED;
    case NXP_STREAM_CLOSED:
    case NXP_STREAM_RESET:
        return false; /* No transitions from terminal states */
    }
    return false;
}

static inline void set_stream_state(nxp_stream_s *s, nxp_stream_state new_state) {
    if (!is_valid_stream_state_transition(s->state, new_state)) {
        return; /* Invalid transition - ignore */
    }
    s->state = new_state;
}

/* ── Helpers ───────────────────────────────────────────── */

static uint64_t u64_min(uint64_t a, uint64_t b) { return a < b ? a : b; }

/* ── Create / Destroy ─────────────────────────────────── */

nxp_stream_s *nxp_stream_create(uint64_t id, nxp_stream_type type,
                                 uint64_t initial_max_send,
                                 uint64_t initial_max_recv) {
    nxp_stream_s *s = (nxp_stream_s *)calloc(1, sizeof(nxp_stream_s));
    if (s == nullptr) return nullptr;

    s->id       = id;
    s->type     = type;
    s->state    = NXP_STREAM_OPEN;
    s->priority = 128;  /* Default mid-priority */

    /* Allocate send buffer */
    s->send.cap  = NXP_STREAM_BUF_SIZE;
    s->send.data = (uint8_t *)calloc(1, s->send.cap);
    if (s->send.data == nullptr) {
        free(s);
        return nullptr;
    }

    /* Allocate recv buffer */
    s->recv.cap  = NXP_STREAM_BUF_SIZE;
    s->recv.data = (uint8_t *)calloc(1, s->recv.cap);
    if (s->recv.data == nullptr) {
        free(s->send.data);
        free(s);
        return nullptr;
    }

    /* Per-stream flow control */
    nxp_flow_init(&s->flow, initial_max_recv, initial_max_send);

    /* Scheduler linkage (not in any list) */
    s->sched_next = nullptr;
    s->sched_prev = nullptr;
    s->scheduled  = false;

    return s;
}

void nxp_stream_destroy(nxp_stream_s *s) {
    if (s == nullptr) return;
    free(s->send.data);
    free(s->recv.data);
    free(s);
}

/* ── Application Write ────────────────────────────────── */

ssize_t nxp_stream_write(nxp_stream_s *s, const uint8_t *data,
                          size_t len, bool fin) {
    if (s->state == NXP_STREAM_HALF_CLOSED_LOCAL ||
        s->state == NXP_STREAM_CLOSED ||
        s->state == NXP_STREAM_RESET) {
        return -1;
    }

    if (s->send.fin) {
        /* Already queued FIN, can't write more */
        return -1;
    }

    /* How much space is available in the send buffer?
     * We use a linear buffer with write_offset as the logical position.
     * The acked_offset marks data that's been fully acknowledged and
     * can be reclaimed. */
    uint64_t buffered = s->send.write_offset - s->send.acked_offset;
    size_t avail = 0;
    if (s->send.cap > buffered) {
        avail = (size_t)(s->send.cap - buffered);
    }

    /* Also constrain by flow control */
    uint64_t fc_avail = 0;
    if (s->flow.peer_max_data > s->send.write_offset) {
        fc_avail = s->flow.peer_max_data - s->send.write_offset;
    }
    if ((uint64_t)avail > fc_avail) {
        avail = (size_t)fc_avail;
    }

    size_t to_write = (len < avail) ? len : avail;

    if (to_write > 0) {
        /* Copy into the circular-ish buffer (using modular offset) */
        size_t buf_pos = (size_t)(s->send.write_offset % s->send.cap);
        size_t first   = s->send.cap - buf_pos;
        if (first > to_write) first = to_write;

        memcpy(s->send.data + buf_pos, data, first);
        if (to_write > first) {
            memcpy(s->send.data, data + first, to_write - first);
        }

        s->send.write_offset += to_write;
    }

    if (fin) {
        s->send.fin = true;
    }

    if (to_write > 0) {
        NXP_FLIGHT_STREAM(s->id, "write", to_write);
    }
    
    return (ssize_t)to_write;
}

/* ── Application Read ─────────────────────────────────── */

ssize_t nxp_stream_read(nxp_stream_s *s, uint8_t *buf,
                         size_t buf_len, bool *fin) {
    *fin = false;

    if (s->recv.read_offset >= s->recv.recv_offset) {
        /* No contiguous data available */
        if (s->recv.fin_received && s->recv.read_offset >= s->recv.fin_offset) {
            *fin = true;
        }
        return 0;
    }

    uint64_t available = s->recv.recv_offset - s->recv.read_offset;
    size_t to_read = (buf_len < (size_t)available) ? buf_len : (size_t)available;

    /* Copy from circular buffer */
    size_t buf_pos = (size_t)(s->recv.read_offset % s->recv.cap);
    size_t first   = s->recv.cap - buf_pos;
    if (first > to_read) first = to_read;

    memcpy(buf, s->recv.data + buf_pos, first);
    if (to_read > first) {
        memcpy(buf + first, s->recv.data, to_read - first);
    }

    s->recv.read_offset += to_read;

    /* Check for FIN */
    if (s->recv.fin_received && s->recv.read_offset >= s->recv.fin_offset) {
        *fin = true;
    }

    /* Update flow control - data consumed by app */
    nxp_flow_on_consume(&s->flow, to_read);

    if (to_read > 0) {
        NXP_FLIGHT_STREAM(s->id, "read", to_read);
    }
    
    return (ssize_t)to_read;
}

/* ── Unsent Data Query ────────────────────────────────── */

uint64_t nxp_stream_unsent(const nxp_stream_s *s) {
    if (s->send.sent_offset >= s->send.write_offset) return 0;
    return s->send.write_offset - s->send.sent_offset;
}

/* ── Fill STREAM Frame for Sending ────────────────────── */

bool nxp_stream_fill_frame(nxp_stream_s *s, nxp_frame_stream *out,
                            size_t max_data_len) {
    uint64_t unsent = nxp_stream_unsent(s);

    /* If no data and no FIN to send, nothing to do */
    if (unsent == 0 && !(s->send.fin && !s->send.fin_sent)) {
        return false;
    }

    out->stream_id  = s->id;
    out->offset     = s->send.sent_offset;
    out->has_offset = (s->send.sent_offset > 0);
    out->has_length = true;

    /* Determine how much data to put in this frame */
    size_t data_len = (size_t)u64_min(unsent, (uint64_t)max_data_len);

    /* We need a stable pointer to the data. Copy from circular buffer
     * into a contiguous region. We use the buffer itself if the data
     * doesn't wrap around. */
    if (data_len > 0) {
        size_t buf_pos = (size_t)(s->send.sent_offset % s->send.cap);
        size_t first   = s->send.cap - buf_pos;

        if (data_len <= first) {
            /* No wrap - point directly into the send buffer */
            out->data = s->send.data + buf_pos;
        } else {
            /* Wraps around - point to start, limit to what fits before wrap.
             * The connection layer handles partial sends. */
            data_len = first;
            out->data = s->send.data + buf_pos;
        }
    } else {
        out->data = nullptr;
    }

    out->length = (uint64_t)data_len;

    /* FIN */
    out->fin = false;
    if (s->send.fin && !s->send.fin_sent &&
        s->send.sent_offset + data_len == s->send.write_offset) {
        out->fin = true;
        s->send.fin_sent = true;
    }

    /* Advance sent_offset */
    s->send.sent_offset += data_len;

    return true;
}

/* ── Process Incoming STREAM Frame ────────────────────── */

nxp_result nxp_stream_on_recv(nxp_stream_s *s, const nxp_frame_stream *f) {
    /* Validate state */
    if (s->state == NXP_STREAM_HALF_CLOSED_REMOTE ||
        s->state == NXP_STREAM_CLOSED ||
        s->state == NXP_STREAM_RESET) {
        return NXP_ERROR(NXP_ERR_STREAM_CLOSED);
    }

    uint64_t offset = f->has_offset ? f->offset : 0;
    uint64_t end    = offset + f->length;

    /* Flow control check */
    if (end > s->flow.local_max_data) {
        return NXP_ERROR(NXP_ERR_FLOW_CONTROL);
    }

    /* Write data into recv buffer */
    if (f->length > 0 && f->data != nullptr) {
        /* Only accept data at or beyond what we've already delivered */
        if (end > s->recv.read_offset) {
            uint64_t copy_start = offset;
            const uint8_t *src = f->data;
            size_t copy_len = (size_t)f->length;

            /* Skip data the app already consumed */
            if (copy_start < s->recv.read_offset) {
                size_t skip = (size_t)(s->recv.read_offset - copy_start);
                src       += skip;
                copy_len  -= skip;
                copy_start = s->recv.read_offset;
            }

            /* Copy into circular buffer */
            size_t buf_pos = (size_t)(copy_start % s->recv.cap);
            size_t first   = s->recv.cap - buf_pos;
            if (first > copy_len) first = copy_len;

            memcpy(s->recv.data + buf_pos, src, first);
            if (copy_len > first) {
                memcpy(s->recv.data, src + first, copy_len - first);
            }

            /* Update recv_offset (contiguous frontier) */
            if (end > s->recv.recv_offset) {
                s->recv.recv_offset = end;
            }
        }

        nxp_flow_on_recv(&s->flow, f->length);
    }

    /* FIN */
    if (f->fin) {
        s->recv.fin_received = true;
        s->recv.fin_offset   = end;

        /* Update stream state */
        if (s->state == NXP_STREAM_OPEN) {
            set_stream_state(s, NXP_STREAM_HALF_CLOSED_REMOTE);
        } else if (s->state == NXP_STREAM_HALF_CLOSED_LOCAL) {
            set_stream_state(s, NXP_STREAM_CLOSED);
        }
    }

    return NXP_SUCCESS;
}

/* ── ACK/Loss Callbacks for Sent Stream Data ──────────── */

void nxp_stream_on_ack(nxp_stream_s *s, uint64_t offset,
                        uint32_t len, bool fin) {
    /* Advance acked_offset if this is contiguous */
    uint64_t end = offset + len;
    if (end > s->send.acked_offset) {
        s->send.acked_offset = end;
    }

    if (fin) {
        s->send.fin_acked = true;
        /* Check if stream is fully closed */
        if (s->state == NXP_STREAM_HALF_CLOSED_REMOTE) {
            set_stream_state(s, NXP_STREAM_CLOSED);
        } else if (s->state == NXP_STREAM_OPEN) {
            set_stream_state(s, NXP_STREAM_HALF_CLOSED_LOCAL);
        }
    }
}

void nxp_stream_on_loss(nxp_stream_s *s, uint64_t offset,
                         uint32_t len, bool fin) {
    /* Rewind sent_offset so the data gets retransmitted */
    if (offset < s->send.sent_offset) {
        s->send.sent_offset = offset;
    }

    if (fin) {
        s->send.fin_sent = false;
    }

    (void)len;
}
