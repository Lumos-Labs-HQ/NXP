/*
 * NXP Event Loop - Async I/O + Timer Interface
 *
 * Backend-agnostic event loop with pluggable backends:
 *   Linux:   epoll + eventfd (default)
 *   Windows: IOCP (stub, future)
 *   Linux:   io_uring (optional, future)
 *   Windows: RIO (optional, future)
 *
 * Timer management and run-loop logic are shared across all backends
 * (implemented in event_loop.c). Each backend only implements socket
 * I/O multiplexing and cross-thread wakeup.
 */
#ifndef NXP_EVENT_LOOP_H
#define NXP_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include "nxp/nxp_error.h"

/* Forward declarations */
typedef struct nxp_socket nxp_socket;
typedef struct nxp_event_loop nxp_event_loop;
typedef struct nxp_timer nxp_timer;
struct nxp_priority_queue;

/* ── Event flags ───────────────────────────────────────── */

#define NXP_EVENT_READ   0x01u
#define NXP_EVENT_WRITE  0x02u
#define NXP_EVENT_ERROR  0x04u

/* ── Callback types ────────────────────────────────────── */

/* Socket event callback */
typedef void (*nxp_event_cb)(nxp_event_loop *loop, nxp_socket *sock,
                             uint32_t events, void *user_data);

/* Timer callback */
typedef void (*nxp_timer_cb)(nxp_event_loop *loop, void *user_data);

/* ── Backend vtable ────────────────────────────────────── */

typedef struct nxp_event_loop_ops {
    void       (*destroy)(nxp_event_loop *loop);
    nxp_result (*add_socket)(nxp_event_loop *loop, nxp_socket *sock,
                             uint32_t events, nxp_event_cb cb, void *user_data);
    nxp_result (*mod_socket)(nxp_event_loop *loop, nxp_socket *sock,
                             uint32_t events);
    void       (*del_socket)(nxp_event_loop *loop, nxp_socket *sock);
    nxp_result (*poll)(nxp_event_loop *loop, int64_t timeout_ms);
    void       (*wakeup)(nxp_event_loop *loop);
} nxp_event_loop_ops;

/* ── Event loop base struct (backends embed this as first field) ── */

struct nxp_event_loop {
    const nxp_event_loop_ops  *ops;
    struct nxp_priority_queue *timers;
    bool                       running;
};

/* ── Lifecycle ─────────────────────────────────────────── */

/* Create a platform-appropriate event loop */
[[nodiscard]] nxp_event_loop *nxp_event_loop_create(void);

/* Destroy the event loop and free all resources */
void nxp_event_loop_destroy(nxp_event_loop *loop);

/* Initialize base fields (called by backend constructors) */
void nxp_event_loop_init_base(nxp_event_loop *loop,
                              const nxp_event_loop_ops *ops);

/* Cleanup base fields (called by backend destructors) */
void nxp_event_loop_cleanup_base(nxp_event_loop *loop);

/* ── Socket events ─────────────────────────────────────── */

/* Register a socket for events (NXP_EVENT_READ, NXP_EVENT_WRITE, or both) */
[[nodiscard]] nxp_result nxp_event_loop_add_socket(
    nxp_event_loop *loop, nxp_socket *sock,
    uint32_t events, nxp_event_cb cb, void *user_data);

/* Modify which events a registered socket listens for */
[[nodiscard]] nxp_result nxp_event_loop_mod_socket(
    nxp_event_loop *loop, nxp_socket *sock, uint32_t events);

/* Remove a socket from the event loop */
void nxp_event_loop_del_socket(nxp_event_loop *loop, nxp_socket *sock);

/* ── Timers ────────────────────────────────────────────── */

/* Schedule a one-shot timer. Returns handle or nullptr on failure.
 * The callback fires when nxp_time_now_us() >= deadline_us.
 * The timer handle is valid until the timer fires or is cancelled. */
[[nodiscard]] nxp_timer *nxp_event_loop_add_timer(
    nxp_event_loop *loop, uint64_t deadline_us,
    nxp_timer_cb cb, void *user_data);

/* Cancel a pending timer. The handle must not be used after this call.
 * Memory is freed lazily when the timer's deadline passes. */
void nxp_event_loop_cancel_timer(nxp_event_loop *loop, nxp_timer *timer);

/* ── Run loop ──────────────────────────────────────────── */

/* Poll for events and fire timers (single iteration).
 * timeout_ms: -1 = block until event, 0 = non-blocking, >0 = max wait ms */
[[nodiscard]] nxp_result nxp_event_loop_run_once(
    nxp_event_loop *loop, int64_t timeout_ms);

/* Run the event loop until nxp_event_loop_stop() is called */
[[nodiscard]] nxp_result nxp_event_loop_run(nxp_event_loop *loop);

/* Stop a running event loop (safe to call from callbacks or other threads) */
void nxp_event_loop_stop(nxp_event_loop *loop);

/* Wake up a blocked event loop from another thread */
void nxp_event_loop_wakeup(nxp_event_loop *loop);

#endif /* NXP_EVENT_LOOP_H */
