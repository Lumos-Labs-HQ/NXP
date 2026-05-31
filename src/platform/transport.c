/*
 * NXP Transport Registry — Backend registration and factory dispatch.
 */
#include "transport.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Backend registry entry ────────────────────────────── */

typedef struct {
    nxp_transport_type type;
    bool               (*can_connect)(void);
    nxp_result         (*connect_impl)(const char *url, nxp_transport **out);
    bool               (*can_listen)(void);
    nxp_result         (*listen_impl)(const char *url,
                                      void (*on_new_conn)(nxp_transport *, void *),
                                      void *user_data,
                                      nxp_transport_listener **out);
    bool  registered;
} nxp_transport_backend;

static nxp_transport_backend g_backends[NXP_TRANSPORT_COUNT];
static bool g_initialized = false;

/* ── Registration ──────────────────────────────────────── */

void nxp_transport_register(
    nxp_transport_type type,
    bool (*can_connect)(void),
    nxp_result (*connect_impl)(const char *, nxp_transport **),
    bool (*can_listen)(void),
    nxp_result (*listen_impl)(const char *,
                               void (*)(nxp_transport *, void *),
                               void *, nxp_transport_listener **))
{
    if (type >= NXP_TRANSPORT_COUNT) return;
    g_backends[type] = (nxp_transport_backend){
        .type         = type,
        .can_connect  = can_connect,
        .connect_impl = connect_impl,
        .can_listen   = can_listen,
        .listen_impl  = listen_impl,
        .registered   = true,
    };
}

/* ── URL Parsing ───────────────────────────────────────── */

static nxp_transport_type parse_scheme(const char *url) {
    if (url == nullptr) return NXP_TRANSPORT_UDP;

    if (strncmp(url, "nxp://", 6) == 0)  return NXP_TRANSPORT_UDP;
    if (strncmp(url, "ws://", 5) == 0)   return NXP_TRANSPORT_WS;
    if (strncmp(url, "wss://", 6) == 0)  return NXP_TRANSPORT_WS;
    if (strncmp(url, "ntc://", 6) == 0)  return NXP_TRANSPORT_TCP;
    if (strncmp(url, "nrtc://", 7) == 0) return NXP_TRANSPORT_RTC;

    /* Legacy: bare "host:port" → assume NXP native UDP */
    return NXP_TRANSPORT_UDP;
}

/* ── Factory ───────────────────────────────────────────── */

nxp_result nxp_transport_connect(const char *url, nxp_transport **out) {
    if (!g_initialized || out == nullptr) {
        return NXP_ERROR(NXP_ERR_INTERNAL);
    }

    nxp_transport_type type = parse_scheme(url);
    if (type >= NXP_TRANSPORT_COUNT || !g_backends[type].registered) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    if (g_backends[type].can_connect && !g_backends[type].can_connect()) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    return g_backends[type].connect_impl(url, out);
}

nxp_result nxp_transport_listen(
    const char *url,
    void (*on_new_conn)(nxp_transport *, void *),
    void *user_data,
    nxp_transport_listener **out)
{
    if (!g_initialized || out == nullptr) {
        return NXP_ERROR(NXP_ERR_INTERNAL);
    }

    nxp_transport_type type = parse_scheme(url);
    if (type >= NXP_TRANSPORT_COUNT || !g_backends[type].registered) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    if (g_backends[type].can_listen && !g_backends[type].can_listen()) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    return g_backends[type].listen_impl(url, on_new_conn, user_data, out);
}

/* ── Init ──────────────────────────────────────────────── */

void nxp_transport_init(void) {
    if (g_initialized) return;

    /* Each backend constructor registers itself.
     * These are weak symbols — only linked if the backend is compiled in. */
    extern void nxp_transport_udp_register(void);
    extern void nxp_transport_ws_register(void);
    extern void nxp_transport_tcp_register(void);
    nxp_transport_udp_register();
    nxp_transport_ws_register();
    nxp_transport_tcp_register();

    g_initialized = true;
}
