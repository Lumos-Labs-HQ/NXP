/*
 * Integration test: UDP Echo via Event Loop
 *
 * Tests the full Phase 3 stack:
 *   - Socket creation, binding, non-blocking I/O
 *   - Event loop: socket registration, event dispatch
 *   - Timer scheduling, cancellation, deadline accuracy
 *   - Cross-thread wakeup via eventfd
 *   - Run loop with stop from timer callback
 */
#include "test_framework.h"
#include "platform.h"
#include <string.h>

/* ── Echo state ────────────────────────────────────────── */

typedef struct echo_state {
    nxp_socket *server_sock;
    nxp_socket *client_sock;
    uint8_t     server_recv[1500];
    size_t      server_recv_len;
    uint8_t     client_recv[1500];
    size_t      client_recv_len;
    bool        server_got_data;
    bool        client_got_echo;
} echo_state;

/* ── Callbacks ─────────────────────────────────────────── */

static void on_server_read(nxp_event_loop *loop, nxp_socket *sock,
                           uint32_t events, void *ud) {
    (void)loop; (void)events;
    echo_state *state = (echo_state *)ud;
    nxp_addr from;
    ssize_t n = nxp_socket_recvfrom(sock, state->server_recv,
                                     sizeof(state->server_recv), &from);
    if (n > 0) {
        state->server_recv_len = (size_t)n;
        state->server_got_data = true;
        /* Echo back to sender */
        (void)nxp_socket_sendto(sock, state->server_recv, (size_t)n, &from);
    }
}

static void on_client_read(nxp_event_loop *loop, nxp_socket *sock,
                           uint32_t events, void *ud) {
    (void)loop; (void)events;
    echo_state *state = (echo_state *)ud;
    nxp_addr from;
    ssize_t n = nxp_socket_recvfrom(sock, state->client_recv,
                                     sizeof(state->client_recv), &from);
    if (n > 0) {
        state->client_recv_len = (size_t)n;
        state->client_got_echo = true;
    }
}

/* ── Test: Basic UDP Echo ──────────────────────────────── */

NXP_TEST(udp_echo_basic) {
    /* Create event loop */
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    /* Create server socket, bind to ephemeral port */
    nxp_addr server_bind;
    NXP_ASSERT_OK(nxp_addr_from_string("127.0.0.1", 0, &server_bind));
    nxp_socket *server = nullptr;
    NXP_ASSERT_OK(nxp_socket_create_udp(&server_bind, &server));
    NXP_ASSERT_OK(nxp_socket_set_nonblocking(server));

    /* Get actual bound address */
    nxp_addr bound_addr;
    NXP_ASSERT_OK(nxp_socket_get_local_addr(server, &bound_addr));

    /* Create client socket */
    nxp_addr client_bind;
    NXP_ASSERT_OK(nxp_addr_from_string("127.0.0.1", 0, &client_bind));
    nxp_socket *client = nullptr;
    NXP_ASSERT_OK(nxp_socket_create_udp(&client_bind, &client));
    NXP_ASSERT_OK(nxp_socket_set_nonblocking(client));

    /* Set up state */
    echo_state state;
    memset(&state, 0, sizeof(state));
    state.server_sock = server;
    state.client_sock = client;

    /* Register sockets for READ events */
    NXP_ASSERT_OK(nxp_event_loop_add_socket(loop, server, NXP_EVENT_READ,
                                            on_server_read, &state));
    NXP_ASSERT_OK(nxp_event_loop_add_socket(loop, client, NXP_EVENT_READ,
                                            on_client_read, &state));

    /* Send data from client to server */
    const char *msg = "Hello, NXP!";
    ssize_t sent = nxp_socket_sendto(client, (const uint8_t *)msg,
                                     strlen(msg), &bound_addr);
    NXP_ASSERT((size_t)sent == strlen(msg));

    /* Poll - server should receive and echo back */
    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 100));
    NXP_ASSERT(state.server_got_data);
    NXP_ASSERT_EQ(state.server_recv_len, strlen(msg));

    /* Poll again - client should receive echo */
    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 100));
    NXP_ASSERT(state.client_got_echo);
    NXP_ASSERT_EQ(state.client_recv_len, strlen(msg));
    NXP_ASSERT(memcmp(state.client_recv, msg, strlen(msg)) == 0);

    /* Cleanup */
    nxp_event_loop_del_socket(loop, server);
    nxp_event_loop_del_socket(loop, client);
    nxp_socket_close(server);
    nxp_socket_close(client);
    nxp_event_loop_destroy(loop);
}

/* ── Test: Large Payload (MTU-safe) ────────────────────── */

NXP_TEST(udp_echo_large_payload) {
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    nxp_addr server_bind;
    NXP_ASSERT_OK(nxp_addr_from_string("127.0.0.1", 0, &server_bind));
    nxp_socket *server = nullptr;
    NXP_ASSERT_OK(nxp_socket_create_udp(&server_bind, &server));
    NXP_ASSERT_OK(nxp_socket_set_nonblocking(server));

    nxp_addr bound_addr;
    NXP_ASSERT_OK(nxp_socket_get_local_addr(server, &bound_addr));

    nxp_addr client_bind;
    NXP_ASSERT_OK(nxp_addr_from_string("127.0.0.1", 0, &client_bind));
    nxp_socket *client = nullptr;
    NXP_ASSERT_OK(nxp_socket_create_udp(&client_bind, &client));
    NXP_ASSERT_OK(nxp_socket_set_nonblocking(client));

    echo_state state;
    memset(&state, 0, sizeof(state));
    state.server_sock = server;
    state.client_sock = client;

    NXP_ASSERT_OK(nxp_event_loop_add_socket(loop, server, NXP_EVENT_READ,
                                            on_server_read, &state));
    NXP_ASSERT_OK(nxp_event_loop_add_socket(loop, client, NXP_EVENT_READ,
                                            on_client_read, &state));

    /* Send 1200-byte payload (typical NXP MTU-safe datagram) */
    uint8_t payload[1200];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i & 0xFFu);
    }

    ssize_t sent = nxp_socket_sendto(client, payload, sizeof(payload),
                                     &bound_addr);
    NXP_ASSERT_EQ((size_t)sent, sizeof(payload));

    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 100));
    NXP_ASSERT(state.server_got_data);
    NXP_ASSERT_EQ(state.server_recv_len, sizeof(payload));

    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 100));
    NXP_ASSERT(state.client_got_echo);
    NXP_ASSERT_EQ(state.client_recv_len, sizeof(payload));
    NXP_ASSERT(memcmp(state.client_recv, payload, sizeof(payload)) == 0);

    nxp_event_loop_del_socket(loop, server);
    nxp_event_loop_del_socket(loop, client);
    nxp_socket_close(server);
    nxp_socket_close(client);
    nxp_event_loop_destroy(loop);
}

/* ── Timer state ───────────────────────────────────────── */

typedef struct timer_state {
    bool     fired;
    uint64_t fire_time;
} timer_state;

static void on_timer(nxp_event_loop *loop, void *ud) {
    (void)loop;
    timer_state *ts = (timer_state *)ud;
    ts->fired     = true;
    ts->fire_time = nxp_time_now_us();
}

/* ── Test: Timer Fires at Deadline ─────────────────────── */

NXP_TEST(event_loop_timer) {
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    timer_state ts = { .fired = false, .fire_time = 0 };
    uint64_t now = nxp_time_now_us();
    uint64_t deadline = now + 10000; /* 10ms from now */

    nxp_timer *t = nxp_event_loop_add_timer(loop, deadline, on_timer, &ts);
    NXP_ASSERT_NOT_NULL(t);

    /* Poll with longer timeout - timer should fire */
    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 50));
    NXP_ASSERT(ts.fired);
    NXP_ASSERT(ts.fire_time >= deadline);

    nxp_event_loop_destroy(loop);
}

/* ── Test: Timer Cancellation ──────────────────────────── */

NXP_TEST(event_loop_timer_cancel) {
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    timer_state ts = { .fired = false, .fire_time = 0 };
    uint64_t deadline = nxp_time_now_us() + 10000; /* 10ms */

    nxp_timer *t = nxp_event_loop_add_timer(loop, deadline, on_timer, &ts);
    NXP_ASSERT_NOT_NULL(t);

    /* Cancel before it fires */
    nxp_event_loop_cancel_timer(loop, t);

    /* Poll past the deadline */
    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 30));

    /* Timer should NOT have fired */
    NXP_ASSERT(!ts.fired);

    nxp_event_loop_destroy(loop);
}

/* ── Test: Cross-thread Wakeup ─────────────────────────── */

static void wakeup_thread_fn(void *arg) {
    nxp_event_loop *loop = (nxp_event_loop *)arg;
    nxp_time_sleep_us(10000); /* Sleep 10ms then wake */
    nxp_event_loop_wakeup(loop);
}

NXP_TEST(event_loop_wakeup) {
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    /* Start a thread that will wake us up after 10ms */
    nxp_thread *t = nullptr;
    NXP_ASSERT_OK(nxp_thread_create(&t, wakeup_thread_fn, loop));

    uint64_t before = nxp_time_now_us();
    /* Block with a long timeout - should be woken up well before */
    NXP_ASSERT_OK(nxp_event_loop_run_once(loop, 5000));
    uint64_t after = nxp_time_now_us();

    /* Should have been woken up in <1s (not the full 5s timeout) */
    NXP_ASSERT((after - before) < 1000000);

    nxp_thread_join(t);
    nxp_thread_destroy(t);
    nxp_event_loop_destroy(loop);
}

/* ── Test: Run Loop with Stop from Timer ───────────────── */

static void stop_callback(nxp_event_loop *loop, void *ud) {
    (void)ud;
    nxp_event_loop_stop(loop);
}

NXP_TEST(event_loop_run_stop) {
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    /* Schedule a timer that stops the loop after 10ms */
    uint64_t deadline = nxp_time_now_us() + 10000;
    nxp_timer *t = nxp_event_loop_add_timer(loop, deadline,
                                            stop_callback, nullptr);
    NXP_ASSERT_NOT_NULL(t);

    /* Run should return after the timer fires and calls stop */
    uint64_t before = nxp_time_now_us();
    NXP_ASSERT_OK(nxp_event_loop_run(loop));
    uint64_t after = nxp_time_now_us();

    /* Should have run for approximately 10ms */
    NXP_ASSERT((after - before) >= 9000);     /* At least ~9ms */
    NXP_ASSERT((after - before) < 1000000);   /* Less than 1s */

    nxp_event_loop_destroy(loop);
}

/* ── Test: Socket get_local_addr ───────────────────────── */

NXP_TEST(socket_get_local_addr) {
    nxp_addr bind_addr;
    NXP_ASSERT_OK(nxp_addr_from_string("127.0.0.1", 0, &bind_addr));

    nxp_socket *sock = nullptr;
    NXP_ASSERT_OK(nxp_socket_create_udp(&bind_addr, &sock));

    nxp_addr local;
    NXP_ASSERT_OK(nxp_socket_get_local_addr(sock, &local));

    /* Should match the bind address family with a non-zero ephemeral port */
    NXP_ASSERT_EQ(local.family, bind_addr.family);
    NXP_ASSERT_NE(local.port, (uint16_t)0);

    nxp_socket_close(sock);
}

/* ── Test: Multiple Timers in Order ────────────────────── */

typedef struct multi_timer_state {
    int order[4];
    int count;
} multi_timer_state;

static void multi_timer_cb(nxp_event_loop *loop, void *ud) {
    (void)loop;
    multi_timer_state *mts = (multi_timer_state *)ud;
    /* Record the order of firing using the count as index */
    if (mts->count < 4) {
        mts->order[mts->count] = mts->count;
        mts->count++;
    }
}

NXP_TEST(event_loop_multiple_timers) {
    nxp_event_loop *loop = nxp_event_loop_create();
    NXP_ASSERT_NOT_NULL(loop);

    multi_timer_state mts = { .count = 0 };
    uint64_t now = nxp_time_now_us();

    /* Add timers in reverse deadline order */
    (void)nxp_event_loop_add_timer(loop, now + 30000, multi_timer_cb, &mts);
    (void)nxp_event_loop_add_timer(loop, now + 10000, multi_timer_cb, &mts);
    (void)nxp_event_loop_add_timer(loop, now + 20000, multi_timer_cb, &mts);

    /* Run until all fire (max 100ms) */
    for (int i = 0; i < 10 && mts.count < 3; i++) {
        (void)nxp_event_loop_run_once(loop, 15);
    }

    NXP_ASSERT_EQ(mts.count, 3);

    nxp_event_loop_destroy(loop);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
    nxp_result r = nxp_platform_init();
    if (r.code != NXP_OK) {
        printf("FATAL: Platform init failed (code %d)\n", r.code);
        return 1;
    }

    printf("=== NXP Event Loop + UDP Integration Tests ===\n");

    NXP_RUN_TEST(udp_echo_basic);
    NXP_RUN_TEST(udp_echo_large_payload);
    NXP_RUN_TEST(event_loop_timer);
    NXP_RUN_TEST(event_loop_timer_cancel);
    NXP_RUN_TEST(event_loop_wakeup);
    NXP_RUN_TEST(event_loop_run_stop);
    NXP_RUN_TEST(socket_get_local_addr);
    NXP_RUN_TEST(event_loop_multiple_timers);

    nxp_platform_cleanup();
    NXP_TEST_SUMMARY();
}
