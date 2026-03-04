/**
 * @file linux_http.c
 * @brief MimiClaw HTTP client implementation using libcurl
 *
 * Provides the HTTP client interface used by MimiClaw on Linux.
 */

#include "linux_http.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <strings.h>

/* System CA bundle path */
#define LINUX_CA_BUNDLE_PATH "/etc/ssl/certs/ca-certificates.crt"

/* ESP error codes - must match linux_compat.h */
#define ESP_OK              0
#define ESP_FAIL            -1
#define ESP_ERR_NO_MEM      -2
#define ESP_ERR_TIMEOUT     -3
#define ESP_ERR_INVALID_ARG -5
#define ESP_ERR_NOT_FOUND   -4
#define ESP_ERR_HTTP_CONNECT    -100
#define ESP_ERR_HTTP_WRITE_DATA -101

/* Internal client structure */
struct esp_http_client {
    CURL *curl;
    struct curl_slist *headers;
    char *url;
    char *post_data;
    size_t post_len;
    esp_http_client_method_t method;
    int timeout_ms;
    int status_code;
    int64_t content_length;

    /* Response buffer */
    char *resp_data;
    size_t resp_len;
    size_t resp_cap;

    /* Header buffer for capturing headers */
    char *header_data;
    size_t header_len;
    size_t header_cap;

    /* Callbacks */
    http_event_handle_cb event_handler;
    void *user_data;
};

/* Global initialization flag */
static int s_curl_initialized = 0;

/* Write callback for response body */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct esp_http_client *client = (struct esp_http_client *)userdata;
    size_t total = size * nmemb;

    /* Grow buffer if needed */
    if (client->resp_len + total >= client->resp_cap) {
        size_t new_cap = client->resp_cap * 2;
        if (new_cap < client->resp_len + total + 1) {
            new_cap = client->resp_len + total + 1;
        }
        char *tmp = realloc(client->resp_data, new_cap);
        if (!tmp) return 0;
        client->resp_data = tmp;
        client->resp_cap = new_cap;
    }

    memcpy(client->resp_data + client->resp_len, ptr, total);
    client->resp_len += total;
    client->resp_data[client->resp_len] = '\0';

    /* Fire event handler */
    if (client->event_handler) {
        struct esp_http_client_event evt = {
            .event_id = HTTP_EVENT_ON_DATA,
            .client = client,
            .data = ptr,
            .data_len = total,
            .user_data = client->user_data,
            .header_key = NULL,
            .header_value = NULL,
        };
        client->event_handler(&evt);
    }

    return total;
}

/* Write callback for response headers */
static size_t header_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct esp_http_client *client = (struct esp_http_client *)userdata;
    size_t total = size * nmemb;

    /* Store headers for potential access */
    if (client->header_len + total >= client->header_cap) {
        size_t new_cap = client->header_cap * 2;
        if (new_cap < client->header_len + total + 1) {
            new_cap = client->header_len + total + 1;
        }
        char *tmp = realloc(client->header_data, new_cap);
        if (!tmp) return total;  /* Non-fatal */
        client->header_data = tmp;
        client->header_cap = new_cap;
    }

    memcpy(client->header_data + client->header_len, ptr, total);
    client->header_len += total;
    client->header_data[client->header_len] = '\0';

    /* Parse header line and fire event */
    /* Header format: "Key: Value\r\n" or "HTTP/1.1 200 OK\r\n" */
    char *colon = memchr(ptr, ':', total);
    if (colon && client->event_handler) {
        /* Extract key and value */
        size_t key_len = colon - ptr;
        size_t val_start = key_len + 1;

        /* Skip leading whitespace in value */
        while (val_start < total && (((char*)ptr)[val_start] == ' ' || ((char*)ptr)[val_start] == '\t')) {
            val_start++;
        }

        /* Remove trailing \r\n from value */
        size_t val_len = total - val_start;
        while (val_len > 0 && (((char*)ptr)[val_start + val_len - 1] == '\r' || ((char*)ptr)[val_start + val_len - 1] == '\n')) {
            val_len--;
        }

        /* Null-terminate key and value temporarily */
        char save_key = ((char*)ptr)[key_len];
        char save_val = '\0';
        char save_val_after = '\0';
        if (val_start + val_len < total) {
            save_val_after = ((char*)ptr)[val_start + val_len];
            ((char*)ptr)[val_start + val_len] = '\0';
        }
        if (val_start < total) {
            save_val = ((char*)ptr)[val_start + val_len];
        }
        ((char*)ptr)[key_len] = '\0';

        struct esp_http_client_event evt = {
            .event_id = HTTP_EVENT_ON_HEADER,
            .client = client,
            .data = NULL,
            .data_len = 0,
            .user_data = client->user_data,
            .header_key = (char*)ptr,
            .header_value = (char*)ptr + val_start,
        };
        client->event_handler(&evt);

        /* Restore */
        ((char*)ptr)[key_len] = save_key;
        if (val_start < total) {
            ((char*)ptr)[val_start + val_len] = save_val;
        }
        if (val_start + val_len < total) {
            ((char*)ptr)[val_start + val_len] = save_val_after;
        }
    }

    return total;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    if (!s_curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        s_curl_initialized = 1;
    }

    struct esp_http_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;

    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        return NULL;
    }

    /* Initialize buffers */
    client->resp_cap = config->buffer_size > 0 ? config->buffer_size : 4096;
    client->resp_data = calloc(1, client->resp_cap);
    if (!client->resp_data) {
        curl_easy_cleanup(client->curl);
        free(client);
        return NULL;
    }

    client->header_cap = 2048;
    client->header_data = calloc(1, client->header_cap);
    if (!client->header_data) {
        free(client->resp_data);
        curl_easy_cleanup(client->curl);
        free(client);
        return NULL;
    }

    /* Copy configuration */
    if (config->url) {
        client->url = strdup(config->url);
    }
    client->method = config->method;
    client->timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000;
    client->event_handler = config->event_handler;
    client->user_data = config->user_data;

    /* Set up basic curl options */
    curl_easy_setopt(client->curl, CURLOPT_URL, client->url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, client);
    curl_easy_setopt(client->curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(client->curl, CURLOPT_HEADERDATA, client);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT_MS, client->timeout_ms);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, 10L);

    /* Use system CA bundle */
    curl_easy_setopt(client->curl, CURLOPT_CAINFO, LINUX_CA_BUNDLE_PATH);

    /* Set method */
    if (config->method == HTTP_METHOD_POST) {
        curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
    } else if (config->method == HTTP_METHOD_HEAD) {
        curl_easy_setopt(client->curl, CURLOPT_NOBODY, 1L);
    } else if (config->method == HTTP_METHOD_PUT) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (config->method == HTTP_METHOD_DELETE) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    return client;
}

void esp_http_client_cleanup(esp_http_client_handle_t client)
{
    if (!client) return;

    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    if (client->headers) {
        curl_slist_free_all(client->headers);
    }
    free(client->url);
    free(client->post_data);
    free(client->resp_data);
    free(client->header_data);
    free(client);
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url)
{
    if (!client || !url) return ESP_ERR_INVALID_ARG;

    free(client->url);
    client->url = strdup(url);
    if (!client->url) return ESP_ERR_NO_MEM;

    curl_easy_setopt(client->curl, CURLOPT_URL, client->url);
    return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method)
{
    if (!client) return ESP_ERR_INVALID_ARG;

    client->method = method;

    if (method == HTTP_METHOD_GET) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPGET, 1L);
    } else if (method == HTTP_METHOD_POST) {
        curl_easy_setopt(client->curl, CURLOPT_POST, 1L);
    } else if (method == HTTP_METHOD_HEAD) {
        curl_easy_setopt(client->curl, CURLOPT_NOBODY, 1L);
    } else if (method == HTTP_METHOD_PUT) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (method == HTTP_METHOD_DELETE) {
        curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    if (!client || !key) return ESP_ERR_INVALID_ARG;

    char header_line[512];
    if (value) {
        snprintf(header_line, sizeof(header_line), "%s: %s", key, value);
    } else {
        snprintf(header_line, sizeof(header_line), "%s:", key);
    }

    struct curl_slist *new_headers = curl_slist_append(client->headers, header_line);
    if (!new_headers) return ESP_ERR_NO_MEM;

    client->headers = new_headers;
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, size_t len)
{
    if (!client) return ESP_ERR_INVALID_ARG;

    /* Store the post data - we need to keep it alive until perform */
    free(client->post_data);
    if (data && len > 0) {
        client->post_data = malloc(len);
        if (!client->post_data) return ESP_ERR_NO_MEM;
        memcpy(client->post_data, data, len);
        client->post_len = len;
    } else {
        client->post_data = NULL;
        client->post_len = 0;
    }

    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, client->post_data);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, client->post_len);

    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    if (!client) return ESP_ERR_INVALID_ARG;

    /* Reset response buffer */
    client->resp_len = 0;
    client->header_len = 0;
    if (client->resp_data) client->resp_data[0] = '\0';
    if (client->header_data) client->header_data[0] = '\0';

    /* Set headers */
    if (client->headers) {
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, client->headers);
    }

    /* Perform the request */
    CURLcode res = curl_easy_perform(client->curl);

    if (res != CURLE_OK) {
        /* Fire error event */
        if (client->event_handler) {
            struct esp_http_client_event evt = {
                .event_id = HTTP_EVENT_ERROR,
                .client = client,
                .user_data = client->user_data,
                .data = NULL,
                .data_len = 0,
                .header_key = NULL,
                .header_value = NULL,
            };
            client->event_handler(&evt);
        }

        /* Map curl errors to ESP errors */
        switch (res) {
            case CURLE_COULDNT_CONNECT:
                return ESP_ERR_HTTP_CONNECT;
            case CURLE_WRITE_ERROR:
                return ESP_ERR_HTTP_WRITE_DATA;
            case CURLE_OPERATION_TIMEDOUT:
                return ESP_ERR_TIMEOUT;
            case CURLE_OUT_OF_MEMORY:
                return ESP_ERR_NO_MEM;
            default:
                return ESP_FAIL;
        }
    }

    /* Get status code */
    long status = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &status);
    client->status_code = (int)status;

    /* Get content length */
    curl_off_t content_len = 0;
    curl_easy_getinfo(client->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_len);
    client->content_length = (int64_t)content_len;

    /* Fire finish event */
    if (client->event_handler) {
        struct esp_http_client_event evt = {
            .event_id = HTTP_EVENT_ON_FINISH,
            .client = client,
            .user_data = client->user_data,
            .data = NULL,
            .data_len = 0,
            .header_key = NULL,
            .header_value = NULL,
        };
        client->event_handler(&evt);
    }

    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    if (!client) return -1;
    return client->status_code;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t client)
{
    if (!client) return -1;
    return client->content_length;
}

esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char *key, char *value)
{
    /* This is a simplified implementation - in practice, use the header event handler */
    if (!client || !key || !value) return ESP_ERR_INVALID_ARG;

    /* Search in stored headers */
    if (!client->header_data || client->header_len == 0) return ESP_ERR_NOT_FOUND;

    /* Simple search - case-insensitive */
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\n%s:", key);

    /* Check start of headers first */
    char *found = NULL;
    if (strncasecmp(client->header_data, key, strlen(key)) == 0 &&
        client->header_data[strlen(key)] == ':') {
        found = client->header_data;
    } else {
        found = strcasestr(client->header_data, search_key);
        if (found) found++; /* Skip newline */
    }

    if (!found) return ESP_ERR_NOT_FOUND;

    /* Skip key and colon */
    char *val_start = strchr(found, ':');
    if (!val_start) return ESP_ERR_NOT_FOUND;
    val_start++;

    /* Skip whitespace */
    while (*val_start == ' ' || *val_start == '\t') val_start++;

    /* Find end of value */
    char *val_end = strstr(val_start, "\r\n");
    if (!val_end) val_end = client->header_data + client->header_len;

    size_t val_len = val_end - val_start;
    memcpy(value, val_start, val_len);
    value[val_len] = '\0';

    return ESP_OK;
}
