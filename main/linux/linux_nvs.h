/**
 * @file linux_nvs.h
 * @brief NVS-to-JSON compatibility layer for Linux
 *
 * Stores MimiClaw runtime configuration in a JSON file backend.
 * All configuration is stored in ~/.mimiclaw/config.json
 */

#pragma once

#include "linux_compat.h"

/* Initialize the NVS/JSON config system */
esp_err_t linux_nvs_init(void);

/* Get the config file path */
const char *linux_nvs_get_config_path(void);
