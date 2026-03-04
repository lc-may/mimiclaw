/**
 * @file linux_http.h
 * @brief MimiClaw HTTP client compatibility layer using libcurl
 *
 * This provides the HTTP client interface used by MimiClaw, backed by libcurl.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations - these are defined in linux_compat.h */
typedef int esp_err_t;

/* HTTP Methods */
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
} esp_http_client_method_t;

/* HTTP Events */
typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADER_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED,
} esp_http_client_event_id_t;

/* Forward declaration */
struct esp_http_client;
typedef struct esp_http_client esp_http_client_t;
typedef esp_http_client_t* esp_http_client_handle_t;

/* Event structure forward declaration */
typedef struct esp_http_client_event esp_http_client_event_t;

/* Event handler callback type */
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);

/* Event structure */
struct esp_http_client_event {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client;
    void *data;
    size_t data_len;
    void *user_data;
    char *header_key;
    char *header_value;
};

/* Configuration structure */
typedef struct {
    const char *url;
    const char *host;
    const char *path;
    const char *query;
    const char *username;
    const char *password;
    esp_http_client_method_t method;
    int timeout_ms;
    bool disable_auto_redirect;
    int max_redirection_count;
    size_t buffer_size;
    size_t buffer_size_tx;
    void *user_data;
    http_event_handle_cb event_handler;
    void *crt_bundle_attach;  /* Ignored on Linux - use system CA bundle */
} esp_http_client_config_t;

/**
 * Initialize HTTP client with configuration
 * @param config Configuration structure
 * @return Handle on success, NULL on failure
 */
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);

/**
 * Cleanup HTTP client
 * @param client Client handle
 */
void esp_http_client_cleanup(esp_http_client_handle_t client);

/**
 * Perform the HTTP request
 * @param client Client handle
 * @return ESP_OK on success, error code on failure
 */
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);

/**
 * Set the URL for the request
 * @param client Client handle
 * @param url URL string
 * @return ESP_OK on success
 */
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);

/**
 * Set the HTTP method
 * @param client Client handle
 * @param method HTTP method
 * @return ESP_OK on success
 */
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);

/**
 * Add a header to the request
 * @param client Client handle
 * @param key Header name
 * @param value Header value
 * @return ESP_OK on success
 */
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);

/**
 * Set POST data
 * @param client Client handle
 * @param data POST body data
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, size_t len);

/**
 * Get HTTP status code from response
 * @param client Client handle
 * @return HTTP status code, or negative on error
 */
int esp_http_client_get_status_code(esp_http_client_handle_t client);

/**
 * Get content length from response
 * @param client Client handle
 * @return Content length, or negative if unknown
 */
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client);

/**
 * Get a response header value (not fully implemented - use event handler for headers)
 * @param client Client handle
 * @param key Header name
 * @param value Output buffer
 * @return ESP_OK on success
 */
esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char *key, char *value);

#ifdef __cplusplus
}
#endif
