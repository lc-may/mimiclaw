/**
 * @file ws_server.c
 * @brief Linux WebSocket gateway backed by libwebsockets.
 */

#include "ws_server.h"
#include "mimi_config.h"
#include "linux/linux_compat.h"

#ifdef MIMI_HAVE_LIBWEBSOCKETS
#include "linux/linux_ws.h"
#endif

static const char *TAG = "ws";

esp_err_t ws_server_start(void)
{
#ifdef MIMI_HAVE_LIBWEBSOCKETS
    return linux_ws_start(MIMI_WS_PORT, MIMI_WS_MAX_CLIENTS);
#else
    ESP_LOGW(TAG, "WebSocket server disabled: libwebsockets not available");
    return ESP_OK;
#endif
}

esp_err_t ws_server_send(const char *chat_id, const char *text)
{
#ifdef MIMI_HAVE_LIBWEBSOCKETS
    return linux_ws_send(chat_id, text);
#else
    (void)chat_id;
    (void)text;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t ws_server_stop(void)
{
#ifdef MIMI_HAVE_LIBWEBSOCKETS
    return linux_ws_stop();
#else
    return ESP_OK;
#endif
}
