/*
 * NXP Stream Scheduler
 *
 * Phase 4: Round-robin scheduler using a circular doubly-linked list.
 * Streams with data to send are added to the list; nxp_sched_next()
 * returns the next stream in round-robin order.
 */
#include "connection_internal.h"

/* ── Add Stream to Scheduler ──────────────────────────── */

void nxp_sched_add(nxp_conn *conn, nxp_stream_s *s) {
    if (s->scheduled) return;

    if (conn->sched_head == nullptr) {
        /* Empty list: self-loop */
        s->sched_next = s;
        s->sched_prev = s;
        conn->sched_head = s;
    } else {
        /* Insert before head (at tail of circular list) */
        nxp_stream_s *tail = conn->sched_head->sched_prev;
        s->sched_next = conn->sched_head;
        s->sched_prev = tail;
        tail->sched_next = s;
        conn->sched_head->sched_prev = s;
    }

    s->scheduled = true;
}

/* ── Remove Stream from Scheduler ─────────────────────── */

void nxp_sched_remove(nxp_conn *conn, nxp_stream_s *s) {
    if (!s->scheduled) return;

    if (s->sched_next == s) {
        /* Only element */
        conn->sched_head = nullptr;
    } else {
        s->sched_prev->sched_next = s->sched_next;
        s->sched_next->sched_prev = s->sched_prev;
        if (conn->sched_head == s) {
            conn->sched_head = s->sched_next;
        }
    }

    s->sched_next = nullptr;
    s->sched_prev = nullptr;
    s->scheduled  = false;
}

/* ── Get Next Stream (Round-Robin) ────────────────────── */

nxp_stream_s *nxp_sched_next(nxp_conn *conn) {
    if (conn->sched_head == nullptr) return nullptr;

    nxp_stream_s *s = conn->sched_head;

    /* Advance head for next call (round-robin) */
    conn->sched_head = s->sched_next;

    /* If this stream has no more data, remove it */
    if (nxp_stream_unsent(s) == 0 &&
        !(s->send.fin && !s->send.fin_sent)) {
        nxp_sched_remove(conn, s);
    }

    return s;
}
