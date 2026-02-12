/*
 * NXP Event Loop - Common Implementation
 *
 * Timer management and run-loop logic shared across all backends.
 * Each backend (epoll, IOCP, io_uring) implements the ops vtable;
 * this file handles timers and the main run loop generically.
 */
#include "event_loop.h"
#include "platform_time.h"
#include "priority_queue.h"

#include <stdlib.h>

/* ── Timer entry (heap-allocated, stored in priority queue) ─── */

struct nxp_timer {
    uint64_t     deadline_us;
    nxp_timer_cb cb;
    void        *user_data;
    bool         cancelled;
};

/* ── Base initialization / cleanup ─────────────────────── */

void nxp_event_loop_init_base(nxp_event_loop *loop,
                              const nxp_event_loop_ops *ops) {
    loop->ops     = ops;
    loop->timers  = nxp_pq_create(32);
    loop->running = false;
}

void nxp_event_loop_cleanup_base(nxp_event_loop *loop) {
    /* Free all remaining timer entries */
    uint64_t key;
    void *val;
    while (nxp_pq_pop(loop->timers, &key, &val)) {
        free(val);
    }
    nxp_pq_destroy(loop->timers);
}

/* ── Lifecycle ─────────────────────────────────────────── */

void nxp_event_loop_destroy(nxp_event_loop *loop) {
    if (loop == nullptr) return;
    nxp_event_loop_cleanup_base(loop);
    loop->ops->destroy(loop);
}

/* ── Socket delegation ─────────────────────────────────── */

nxp_result nxp_event_loop_add_socket(nxp_event_loop *loop, nxp_socket *sock,
                                     uint32_t events, nxp_event_cb cb,
                                     void *user_data) {
    return loop->ops->add_socket(loop, sock, events, cb, user_data);
}

nxp_result nxp_event_loop_mod_socket(nxp_event_loop *loop, nxp_socket *sock,
                                     uint32_t events) {
    return loop->ops->mod_socket(loop, sock, events);
}

void nxp_event_loop_del_socket(nxp_event_loop *loop, nxp_socket *sock) {
    loop->ops->del_socket(loop, sock);
}

/* ── Timers ────────────────────────────────────────────── */

nxp_timer *nxp_event_loop_add_timer(nxp_event_loop *loop,
                                    uint64_t deadline_us,
                                    nxp_timer_cb cb, void *user_data) {
    nxp_timer *t = (nxp_timer *)malloc(sizeof(nxp_timer));
    if (t == nullptr) return nullptr;

    t->deadline_us = deadline_us;
    t->cb          = cb;
    t->user_data   = user_data;
    t->cancelled   = false;

    if (!nxp_pq_push(loop->timers, deadline_us, t)) {
        free(t);
        return nullptr;
    }

    return t;
}

void nxp_event_loop_cancel_timer(nxp_event_loop *loop, nxp_timer *timer) {
    (void)loop;
    if (timer != nullptr) {
        timer->cancelled = true;
    }
}

static void fire_timers(nxp_event_loop *loop) {
    uint64_t now = nxp_time_now_us();

    while (!nxp_pq_is_empty(loop->timers)) {
        uint64_t deadline;
        nxp_timer *t;

        if (!nxp_pq_peek(loop->timers, &deadline, (void **)&t)) break;
        if (deadline > now) break;

        nxp_pq_pop(loop->timers, &deadline, (void **)&t);

        if (!t->cancelled) {
            t->cb(loop, t->user_data);
        }
        free(t);
    }
}

static int64_t next_timer_timeout_ms(nxp_event_loop *loop) {
    /* Drain cancelled timers at the head of the queue */
    while (!nxp_pq_is_empty(loop->timers)) {
        uint64_t deadline;
        nxp_timer *t;
        if (!nxp_pq_peek(loop->timers, &deadline, (void **)&t)) break;
        if (!t->cancelled) break;
        nxp_pq_pop(loop->timers, &deadline, (void **)&t);
        free(t);
    }

    if (nxp_pq_is_empty(loop->timers)) return -1;

    uint64_t deadline;
    void *val;
    (void)nxp_pq_peek(loop->timers, &deadline, &val);

    uint64_t now = nxp_time_now_us();
    if (deadline <= now) return 0;

    /* Convert us to ms, rounding up to avoid waking too early */
    uint64_t diff_us = deadline - now;
    return (int64_t)((diff_us + (uint64_t)999) / (uint64_t)1000);
}

/* ── Run loop ──────────────────────────────────────────── */

nxp_result nxp_event_loop_run_once(nxp_event_loop *loop, int64_t timeout_ms) {
    /* Compute effective timeout: min of user timeout and next timer */
    int64_t timer_timeout = next_timer_timeout_ms(loop);
    int64_t effective;

    if (timeout_ms < 0 && timer_timeout < 0) {
        effective = -1;
    } else if (timeout_ms < 0) {
        effective = timer_timeout;
    } else if (timer_timeout < 0) {
        effective = timeout_ms;
    } else {
        effective = (timeout_ms < timer_timeout) ? timeout_ms : timer_timeout;
    }

    /* Poll for I/O events */
    nxp_result r = loop->ops->poll(loop, effective);

    /* Fire expired timers regardless of poll result */
    fire_timers(loop);

    return r;
}

nxp_result nxp_event_loop_run(nxp_event_loop *loop) {
    loop->running = true;
    while (loop->running) {
        nxp_result r = nxp_event_loop_run_once(loop, -1);
        if (r.code != NXP_OK) {
            return r;
        }
    }
    return NXP_SUCCESS;
}

void nxp_event_loop_stop(nxp_event_loop *loop) {
    loop->running = false;
    loop->ops->wakeup(loop);
}

void nxp_event_loop_wakeup(nxp_event_loop *loop) {
    loop->ops->wakeup(loop);
}
