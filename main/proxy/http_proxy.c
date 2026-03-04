#include "http_proxy.h"
#include "mimi_config.h"
#include "linux/linux_compat.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "proxy";

__attribute__((constructor)) static void proxy_log_level(void)
{
    esp_log_level_set(TAG, ESP_LOG_WARN);
}

static char     s_proxy_host[64] = {0};
static uint16_t s_proxy_port     = 0;
static char     s_proxy_type[8]  = "http";

esp_err_t http_proxy_init(void)
{
    if (MIMI_SECRET_PROXY_HOST[0] != '\0' && MIMI_SECRET_PROXY_PORT[0] != '\0') {
        strncpy(s_proxy_host, MIMI_SECRET_PROXY_HOST, sizeof(s_proxy_host) - 1);
        s_proxy_port = (uint16_t)atoi(MIMI_SECRET_PROXY_PORT);
        if (MIMI_SECRET_PROXY_TYPE[0] != '\0') {
            strncpy(s_proxy_type, MIMI_SECRET_PROXY_TYPE, sizeof(s_proxy_type) - 1);
        }
    }

    nvs_handle_t nvs;
    if (nvs_open(MIMI_NVS_PROXY, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[64] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, MIMI_NVS_KEY_PROXY_HOST, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_proxy_host, tmp, sizeof(s_proxy_host) - 1);
            uint16_t port = 0;
            if (nvs_get_u16(nvs, MIMI_NVS_KEY_PROXY_PORT, &port) == ESP_OK && port) {
                s_proxy_port = port;
            }
            len = sizeof(tmp);
            memset(tmp, 0, sizeof(tmp));
            if (nvs_get_str(nvs, "proxy_type", tmp, &len) == ESP_OK && tmp[0]) {
                strncpy(s_proxy_type, tmp, sizeof(s_proxy_type) - 1);
            }
        }
        nvs_close(nvs);
    }

    if (s_proxy_host[0] && s_proxy_port) {
        ESP_LOGW(TAG, "Proxy is configured but ignored in the Linux build");
    } else {
        ESP_LOGI(TAG, "No proxy configured (direct connection)");
    }
    return ESP_OK;
}

esp_err_t http_proxy_set(const char *host, uint16_t port, const char *type)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_PROXY, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MIMI_NVS_KEY_PROXY_HOST, host));
    ESP_ERROR_CHECK(nvs_set_u16(nvs, MIMI_NVS_KEY_PROXY_PORT, port));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "proxy_type", type));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_proxy_host, host, sizeof(s_proxy_host) - 1);
    s_proxy_port = port;
    strncpy(s_proxy_type, type, sizeof(s_proxy_type) - 1);
    ESP_LOGW(TAG, "Proxy saved but ignored in the Linux build: %s:%u (%s)",
             s_proxy_host, (unsigned)s_proxy_port, s_proxy_type);
    return ESP_OK;
}

esp_err_t http_proxy_clear(void)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(MIMI_NVS_PROXY, NVS_READWRITE, &nvs));
    nvs_erase_key(nvs, MIMI_NVS_KEY_PROXY_HOST);
    nvs_erase_key(nvs, MIMI_NVS_KEY_PROXY_PORT);
    nvs_erase_key(nvs, "proxy_type");
    nvs_commit(nvs);
    nvs_close(nvs);

    s_proxy_host[0] = '\0';
    s_proxy_port = 0;
    strcpy(s_proxy_type, "http");
    ESP_LOGI(TAG, "Proxy cleared");
    return ESP_OK;
}

bool http_proxy_is_enabled(void)
{
    return false;
}

struct proxy_conn {
    int unused;
};

proxy_conn_t *proxy_conn_open(const char *host, int port, int timeout_ms)
{
    (void)host;
    (void)port;
    (void)timeout_ms;
    ESP_LOGW(TAG, "Proxy tunneling is not implemented in the Linux build");
    return NULL;
}

int proxy_conn_write(proxy_conn_t *conn, const char *data, int len)
{
    (void)conn;
    (void)data;
    (void)len;
    return -1;
}

int proxy_conn_read(proxy_conn_t *conn, char *buf, int len, int timeout_ms)
{
    (void)conn;
    (void)buf;
    (void)len;
    (void)timeout_ms;
    return -1;
}

void proxy_conn_close(proxy_conn_t *conn)
{
    (void)conn;
}
