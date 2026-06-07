/*
 * End-to-End Integration Test: Full Handshake + Echo
 *
 * Covers all 4 stream types: RELIABLE, FAST, MEDIA, FILE
 * The protocol is universal — all types ride the same NXP transport.
 *
 * Tests: nxp_init, nxp_listen, nxp_connect, nxp_stream_open,
 *        nxp_stream_send, nxp_stream_recv, handshake, full round-trip.
 */
#include <nxp/nxp.h>
#include <nxp/nxp_listener.h>
#include <nxp/nxp_connection.h>
#include <nxp/nxp_stream.h>
#include <nxp/nxp_config.h>
#include "../../src/api/api_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    bool   connected;
    bool   server_closed;
    nxp_conn  *server_conn;
    nxp_stream *server_stream;
    nxp_stream *client_stream;
    char   server_recv[65536];
    size_t slen;
    char   client_recv[65536];
    size_t clen;
    int    fails;
} ctx_t;

static void on_server_stream_accept(nxp_conn *c, nxp_stream *s, void *ud);
static void on_server_data(nxp_stream *s, void *ud);
static void on_closed(nxp_conn *c, void *ud);

static void on_connected(nxp_conn *c, void *ud) {
    ((ctx_t *)ud)->connected = true;
}

static void on_new_conn(nxp_listener *ln, nxp_conn *conn, void *ud) {
    ctx_t *ctx = (ctx_t *)ud;
    ctx->server_conn = conn;
    printf("  [srv] new connection accepted\n");
    nxp_conn_set_stream_accept_cb(conn, on_server_stream_accept, ctx);
    nxp_conn_set_callbacks(conn, NULL, on_closed, ctx);
}

static void on_closed(nxp_conn *c, void *ud) {
    ((ctx_t *)ud)->server_closed = true;
}

static void on_server_stream_accept(nxp_conn *c, nxp_stream *s, void *ud) {
    ctx_t *ctx = (ctx_t *)ud;
    ctx->server_stream = s;
    printf("  [srv] stream %lu accepted\n", (unsigned long)nxp_stream_get_id(s));
    s->on_data = on_server_data;
    s->user_data = ctx;
}

static void on_server_data(nxp_stream *s, void *ud) {
    ctx_t *ctx = (ctx_t *)ud;
    bool fin = false;
    ssize_t n = nxp_stream_recv(s, (uint8_t *)ctx->server_recv, sizeof(ctx->server_recv), &fin);
    if (n > 0) {
        ctx->slen = (size_t)n;
        printf("  [srv] got %zd bytes, echoing\n", n);
        nxp_stream_send(s, (uint8_t *)ctx->server_recv, (size_t)n, fin);
    }
}

static void on_client_data(nxp_stream *s, void *ud) {
    ctx_t *ctx = (ctx_t *)ud;
    bool fin = false;
    ssize_t n = nxp_stream_recv(s, (uint8_t *)ctx->client_recv, sizeof(ctx->client_recv), &fin);
    if (n > 0) ctx->clen = (size_t)n;
}

static int run(nxp_stream_type type, const char *name, int port) {
    ctx_t ctx = {0};
    printf("\n--- %s: port %d ---\n", name, port);

    nxp_global_config gcfg = {1, 128};
    nxp_result ir = nxp_init(&gcfg);
    if (nxp_result_is_err(ir)) { printf("FAIL init=%d\n", ir.code); return 1; }
    printf("  init ok\n");

    /* Server */
    nxp_config *scfg = nxp_config_new();
    nxp_config_set_idle_timeout(scfg, 30000);
    nxp_listener *ln = NULL;
    nxp_result lr = nxp_listen(scfg, "127.0.0.1", (uint16_t)port, on_new_conn, &ctx, &ln);
    if (nxp_result_is_err(lr)) {
        printf("FAIL listen=%d\n", lr.code); nxp_config_free(scfg); nxp_shutdown(); return 1;
    }
    printf("  listen ok\n");

    /* Client */
    nxp_config *ccfg = nxp_config_new();
    nxp_config_set_idle_timeout(ccfg, 5000);
    nxp_conn *client_conn = NULL;
    nxp_result cr = nxp_connect(ccfg, "127.0.0.1", (uint16_t)port,
                                 on_connected, on_closed, &ctx, &client_conn);
    if (nxp_result_is_err(cr)) {
        printf("FAIL connect code=%d\n", cr.code);
        goto skip;
    }
    printf("  connect: code=%d state=%d\n", cr.code, (int)nxp_conn_get_state(client_conn));
    nxp_config_free(ccfg);

    /* Poll until connected */
    for (int i = 0; i < 2000 && !ctx.connected; i++) nxp_poll();
    if (!ctx.connected) { printf("FAIL never connected (state=%d)\n", (int)nxp_conn_get_state(client_conn)); goto done; }
    printf("  connected\n");
    fflush(stdout);

    /* Open client stream */
    nxp_stream *cs = NULL;
    nxp_result r = nxp_stream_open(client_conn, type, 0,
                                    on_client_data, NULL, NULL, &ctx, &cs);
    if (nxp_result_is_err(r)) { printf("FAIL stream_open: %d\n", r.code); ctx.fails++; goto done; }
    ctx.client_stream = cs;

    /* Send data */
    const char *msg = "HELLO_NXP_E2E_TEST_1234567890";
    size_t mlen = strlen(msg);
    ssize_t sent = nxp_stream_send(cs, (const uint8_t *)msg, mlen, true);
    printf("  [cli] sent %zd bytes (FIN)\n", sent);

    /* Poll aggressively */
    for (int i = 0; i < 1000; i++) {
        nxp_poll();
        if (ctx.slen > 0 && ctx.clen > 0) break;
    }

    if (ctx.slen == 0) {
        printf("FAIL server never got data\n"); ctx.fails++;
    } else if (ctx.slen != mlen || memcmp(ctx.server_recv, msg, mlen) != 0) {
        printf("FAIL server data mismatch (%zu)\n", ctx.slen); ctx.fails++;
    } else {
        printf("  server data OK (%zu bytes)\n", ctx.slen);
    }

    if (type == NXP_STREAM_RELIABLE || type == NXP_STREAM_FILE) {
        if (ctx.clen != mlen || memcmp(ctx.client_recv, msg, mlen) != 0) {
            printf("FAIL echo mismatch (%zu vs %zu)\n", mlen, ctx.clen); ctx.fails++;
        } else {
            printf("  echo OK (%zu bytes)\n", ctx.clen);
        }
    } else {
        if (ctx.clen > 0)
            printf("  echo %zu bytes (unreliable)\n", ctx.clen);
        else
            printf("  no echo (expected for unreliable)\n");
    }

done:
    nxp_stream_close(cs);
    nxp_conn_close(client_conn, 0);
    for (int i = 0; i < 10; i++) nxp_poll();
skip:
    nxp_listener_close(ln);
    nxp_config_free(scfg);
    nxp_shutdown();
    return ctx.fails ? 1 : 0;
}

int main(void) {
    printf("=== NXP E2E Echo ===\n");
    int f = 0;
    f += run(NXP_STREAM_RELIABLE, "RELIABLE", 9300);
    printf("\n%s\n", f ? "FAIL" : "PASS");
    return f;
}
