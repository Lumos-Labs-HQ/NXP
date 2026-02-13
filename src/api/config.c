/*
 * NXP Configuration API - Implementation
 *
 * Phase 11: Builder pattern for connection/listener configuration.
 */
#include "api_internal.h"

/* ── Create / Destroy ────────────────────────────────── */

nxp_config *nxp_config_new(void) {
    nxp_config *c = (nxp_config *)calloc(1, sizeof(nxp_config));
    if (c == nullptr) return nullptr;

    /* Sensible defaults */
    c->max_streams_bidi     = NXP_MAX_STREAMS_DEFAULT;
    c->max_streams_uni      = NXP_MAX_STREAMS_DEFAULT;
    c->idle_timeout_ms      = NXP_IDLE_TIMEOUT_DEFAULT;
    c->max_udp_payload      = NXP_MAX_UDP_PAYLOAD;
    c->heartbeat_interval_ms = 0;  /* disabled by default */

    return c;
}

void nxp_config_free(nxp_config *c) {
    if (c == nullptr) return;

    free(c->cert_file);
    free(c->key_file);

    for (size_t i = 0; i < c->alpn_count; i++) {
        free(c->alpns[i]);
    }
    free(c->alpns);
    free(c);
}

/* ── Setters ─────────────────────────────────────────── */

nxp_result nxp_config_set_cert_file(nxp_config *c, const char *path) {
    if (c == nullptr || path == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    free(c->cert_file);
    c->cert_file = strdup(path);
    return c->cert_file != nullptr ? NXP_SUCCESS : NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
}

nxp_result nxp_config_set_key_file(nxp_config *c, const char *path) {
    if (c == nullptr || path == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    free(c->key_file);
    c->key_file = strdup(path);
    return c->key_file != nullptr ? NXP_SUCCESS : NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
}

nxp_result nxp_config_set_alpn(nxp_config *c, const char **alpns, size_t count) {
    if (c == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    if (count > 0 && alpns == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    /* Free existing ALPNs */
    for (size_t i = 0; i < c->alpn_count; i++) {
        free(c->alpns[i]);
    }
    free(c->alpns);
    c->alpns = nullptr;
    c->alpn_count = 0;

    if (count == 0) return NXP_SUCCESS;

    c->alpns = (char **)calloc(count, sizeof(char *));
    if (c->alpns == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    for (size_t i = 0; i < count; i++) {
        c->alpns[i] = strdup(alpns[i]);
        if (c->alpns[i] == nullptr) {
            /* Cleanup on failure */
            for (size_t j = 0; j < i; j++) free(c->alpns[j]);
            free(c->alpns);
            c->alpns = nullptr;
            return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);
        }
    }
    c->alpn_count = count;
    return NXP_SUCCESS;
}

nxp_result nxp_config_set_max_streams(nxp_config *c, uint64_t max_bidi,
                                       uint64_t max_uni) {
    if (c == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    c->max_streams_bidi = max_bidi;
    c->max_streams_uni  = max_uni;
    return NXP_SUCCESS;
}

nxp_result nxp_config_set_idle_timeout(nxp_config *c, uint64_t ms) {
    if (c == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    c->idle_timeout_ms = ms;
    return NXP_SUCCESS;
}

nxp_result nxp_config_set_max_udp_payload(nxp_config *c, uint16_t size) {
    if (c == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    if (size < 1200 || size > 1500) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    c->max_udp_payload = size;
    return NXP_SUCCESS;
}

nxp_result nxp_config_set_heartbeat_interval(nxp_config *c, uint64_t ms) {
    if (c == nullptr) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    c->heartbeat_interval_ms = ms;
    return NXP_SUCCESS;
}
