/*
 * NEXUS Protocol (NXP) - Configuration API
 * Copyright (c) 2026 NXP Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef NXP_CONFIG_H
#define NXP_CONFIG_H

#include "nxp_types.h"
#include "nxp_error.h"

[[nodiscard]] nxp_config *nxp_config_new(void);
void nxp_config_free(nxp_config *config);

[[nodiscard]] nxp_result nxp_config_set_cert_file(nxp_config *c, const char *path);
[[nodiscard]] nxp_result nxp_config_set_key_file(nxp_config *c, const char *path);
[[nodiscard]] nxp_result nxp_config_set_alpn(nxp_config *c, const char **alpns, size_t count);
[[nodiscard]] nxp_result nxp_config_set_max_streams(nxp_config *c, uint64_t max_bidi, uint64_t max_uni);
[[nodiscard]] nxp_result nxp_config_set_idle_timeout(nxp_config *c, uint64_t ms);
[[nodiscard]] nxp_result nxp_config_set_max_udp_payload(nxp_config *c, uint16_t size);
[[nodiscard]] nxp_result nxp_config_set_heartbeat_interval(nxp_config *c, uint64_t ms);

#endif /* NXP_CONFIG_H */
