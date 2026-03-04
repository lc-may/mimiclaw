/**
 * @file linux_ws.h
 * @brief libwebsockets wrapper for Linux port of MimiClaw
 *
 * This header provides the WebSocket server implementation using libwebsockets.
 */

#pragma once

#include "linux/linux_compat.h"

/* Opaque context - actual definition in linux_ws.c */
typedef struct lws_context lws_context_t;

/**
 * Initialize and start the WebSocket server.
 * @param port  Port to listen on (typically MIMI_WS_PORT = 18789)
 * @param max_clients  Maximum concurrent connections
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t linux_ws_start(int port, int max_clients);

/**
 * Stop the WebSocket server and free resources.
 * @return ESP_OK on success
 */
esp_err_t linux_ws_stop(void);

/**
 * Send a text message to a client by chat_id.
 * @param chat_id  Client identifier
 * @param text     Message text (null-terminated)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if client not found
 */
esp_err_t linux_ws_send(const char *chat_id, const char *text);

/**
 * Get the libwebsockets context (for advanced use).
 * @return Pointer to lws_context, or NULL if not running
 */
lws_context_t *linux_ws_get_context(void);
