/**
 * @file linux_ws.c
 * @brief libwebsockets-based WebSocket server for Linux
 */

#include "linux_compat.h"
#include "linux_ws.h"
#include "mimi_config.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdlib.h>
#include <libwebsockets.h>
#include <pthread.h>
#include "cJSON.h"

static const char *TAG = "ws";

/* Server context */
static struct lws_context *s_context = NULL;
static pthread_t s_service_thread = 0;
static volatile bool s_running = false;
static int s_port = MIMI_WS_PORT;
static int s_max_clients = MIMI_WS_MAX_CLIENTS;

/* Per-session data */
struct per_session_data {
    int id;
    char chat_id[32];
    bool active;
};

/* Client tracking */
typedef struct {
    struct lws *wsi;
    char chat_id[32];
    bool active;
    pthread_mutex_t send_mutex;
    char *pending_msg;      /* Message waiting to be sent */
    size_t pending_len;     /* Length of pending message */
} ws_client_t;

static ws_client_t s_clients[MIMI_WS_MAX_CLIENTS];
static pthread_mutex_t s_clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_next_client_id = 0;

/* Forward declarations */
static int callback_ws(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);
static void *service_thread(void *arg);

/* Protocol definition */
static const struct lws_protocols protocols[] = {
    {
        .name = "mimi-ws-protocol",
        .callback = callback_ws,
        .per_session_data_size = sizeof(struct per_session_data),
        .rx_buffer_size = 4096,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 0,
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }  /* terminator */
};

/* Find client by WSI pointer */
static ws_client_t *find_client_by_wsi(struct lws *wsi)
{
    for (int i = 0; i < s_max_clients; i++) {
        if (s_clients[i].active && s_clients[i].wsi == wsi) {
            return &s_clients[i];
        }
    }
    return NULL;
}

/* Find client by chat_id */
static ws_client_t *find_client_by_chat_id(const char *chat_id)
{
    for (int i = 0; i < s_max_clients; i++) {
        if (s_clients[i].active && strcmp(s_clients[i].chat_id, chat_id) == 0) {
            return &s_clients[i];
        }
    }
    return NULL;
}

/* Add a new client */
static ws_client_t *add_client(struct lws *wsi)
{
    pthread_mutex_lock(&s_clients_mutex);

    for (int i = 0; i < s_max_clients; i++) {
        if (!s_clients[i].active) {
            s_clients[i].wsi = wsi;
            s_clients[i].active = true;
            s_clients[i].pending_msg = NULL;
            s_clients[i].pending_len = 0;
            pthread_mutex_init(&s_clients[i].send_mutex, NULL);

            int id = s_next_client_id++;
            snprintf(s_clients[i].chat_id, sizeof(s_clients[i].chat_id), "ws_%d", id);

            ESP_LOGI(TAG, "Client connected: %s", s_clients[i].chat_id);

            pthread_mutex_unlock(&s_clients_mutex);
            return &s_clients[i];
        }
    }

    pthread_mutex_unlock(&s_clients_mutex);
    ESP_LOGW(TAG, "Max clients reached, rejecting connection");
    return NULL;
}

/* Remove a client */
static void remove_client(struct lws *wsi)
{
    pthread_mutex_lock(&s_clients_mutex);

    for (int i = 0; i < s_max_clients; i++) {
        if (s_clients[i].active && s_clients[i].wsi == wsi) {
            ESP_LOGI(TAG, "Client disconnected: %s", s_clients[i].chat_id);
            s_clients[i].active = false;
            s_clients[i].wsi = NULL;
            if (s_clients[i].pending_msg) {
                free(s_clients[i].pending_msg);
                s_clients[i].pending_msg = NULL;
            }
            pthread_mutex_destroy(&s_clients[i].send_mutex);
            break;
        }
    }

    pthread_mutex_unlock(&s_clients_mutex);
}

/* WebSocket callback handler */
static int callback_ws(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    struct per_session_data *pss = (struct per_session_data *)user;
    ws_client_t *client;
    char buf[LWS_PRE + 4096];
    int n;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        /* New connection */
        client = add_client(wsi);
        if (client) {
            pss->active = true;
            strncpy(pss->chat_id, client->chat_id, sizeof(pss->chat_id) - 1);
        } else {
            /* Reject connection - max clients reached */
            return -1;
        }
        break;

    case LWS_CALLBACK_CLOSED:
        /* Connection closed */
        remove_client(wsi);
        break;

    case LWS_CALLBACK_RECEIVE:
        /* Received data from client */
        client = find_client_by_wsi(wsi);
        if (!client || len == 0) {
            break;
        }

        /* Make null-terminated copy */
        char *payload = calloc(1, len + 1);
        if (!payload) {
            ESP_LOGE(TAG, "Failed to allocate receive buffer");
            break;
        }
        memcpy(payload, in, len);

        /* Parse JSON message */
        cJSON *root = cJSON_Parse(payload);
        free(payload);

        if (!root) {
            ESP_LOGW(TAG, "Invalid JSON from client");
            break;
        }

        cJSON *type = cJSON_GetObjectItem(root, "type");
        cJSON *content = cJSON_GetObjectItem(root, "content");

        if (type && cJSON_IsString(type) && strcmp(type->valuestring, "message") == 0
            && content && cJSON_IsString(content)) {

            /* Determine chat_id */
            const char *chat_id = client->chat_id;
            cJSON *cid = cJSON_GetObjectItem(root, "chat_id");
            if (cid && cJSON_IsString(cid)) {
                chat_id = cid->valuestring;
                /* Update client's chat_id if provided */
                if (client) {
                    pthread_mutex_lock(&s_clients_mutex);
                    strncpy(client->chat_id, chat_id, sizeof(client->chat_id) - 1);
                    pthread_mutex_unlock(&s_clients_mutex);
                }
            }

            ESP_LOGI(TAG, "WS message from %s: %.40s...", chat_id, content->valuestring);

            /* Push to inbound bus */
            mimi_msg_t msg = {0};
            strncpy(msg.channel, MIMI_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
            strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
            msg.content = strdup(content->valuestring);
            if (msg.content) {
                message_bus_push_inbound(&msg);
            }
        }

        cJSON_Delete(root);
        break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
        /* Client is ready to receive data - send pending message */
        client = find_client_by_wsi(wsi);
        if (client && client->pending_msg) {
            pthread_mutex_lock(&client->send_mutex);

            size_t msg_len = client->pending_len;
            char *msg = client->pending_msg;
            client->pending_msg = NULL;
            client->pending_len = 0;

            pthread_mutex_unlock(&client->send_mutex);

            /* Prepare buffer with LWS_PRE padding */
            size_t buf_size = LWS_PRE + msg_len;
            char *send_buf = malloc(buf_size);
            if (send_buf) {
                memcpy(send_buf + LWS_PRE, msg, msg_len);
                n = lws_write(wsi, (unsigned char *)(send_buf + LWS_PRE), msg_len,
                              LWS_WRITE_TEXT);
                free(send_buf);
                if (n < 0) {
                    ESP_LOGW(TAG, "Failed to write to client %s", client->chat_id);
                }
            }
            free(msg);
        }
        break;

    default:
        break;
    }

    return 0;
}

/* Service thread - runs lws_service loop */
static void *service_thread(void *arg)
{
    (void)arg;

    while (s_running) {
        /* Service with 50ms timeout */
        lws_service(s_context, 50);
    }

    return NULL;
}

esp_err_t linux_ws_start(int port, int max_clients)
{
    if (s_context) {
        ESP_LOGW(TAG, "WebSocket server already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_port = port;
    s_max_clients = max_clients;

    /* Initialize client array */
    memset(s_clients, 0, sizeof(s_clients));

    /* Create context info */
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = s_port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;

    /* Create context */
    s_context = lws_create_context(&info);
    if (!s_context) {
        ESP_LOGE(TAG, "Failed to create libwebsockets context");
        return ESP_FAIL;
    }

    /* Start service thread */
    s_running = true;
    if (pthread_create(&s_service_thread, NULL, service_thread, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to create service thread");
        lws_context_destroy(s_context);
        s_context = NULL;
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WebSocket server started on port %d", s_port);
    return ESP_OK;
}

esp_err_t linux_ws_stop(void)
{
    if (!s_context) {
        return ESP_OK;
    }

    /* Signal service thread to stop */
    s_running = false;

    /* Cancel any blocking service call */
    lws_cancel_service(s_context);

    /* Wait for service thread */
    if (s_service_thread) {
        pthread_join(s_service_thread, NULL);
        s_service_thread = 0;
    }

    /* Clean up any pending messages */
    pthread_mutex_lock(&s_clients_mutex);
    for (int i = 0; i < s_max_clients; i++) {
        if (s_clients[i].pending_msg) {
            free(s_clients[i].pending_msg);
            s_clients[i].pending_msg = NULL;
        }
        if (s_clients[i].active) {
            pthread_mutex_destroy(&s_clients[i].send_mutex);
        }
        s_clients[i].active = false;
    }
    pthread_mutex_unlock(&s_clients_mutex);

    /* Destroy context */
    lws_context_destroy(s_context);
    s_context = NULL;

    ESP_LOGI(TAG, "WebSocket server stopped");
    return ESP_OK;
}

esp_err_t linux_ws_send(const char *chat_id, const char *text)
{
    if (!s_context) {
        return ESP_ERR_INVALID_STATE;
    }

    pthread_mutex_lock(&s_clients_mutex);
    ws_client_t *client = find_client_by_chat_id(chat_id);
    if (!client) {
        pthread_mutex_unlock(&s_clients_mutex);
        ESP_LOGW(TAG, "No WS client with chat_id=%s", chat_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Build response JSON */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "response");
    cJSON_AddStringToObject(resp, "content", text);
    cJSON_AddStringToObject(resp, "chat_id", chat_id);

    char *json_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!json_str) {
        pthread_mutex_unlock(&s_clients_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Store pending message (will be sent on writable callback) */
    pthread_mutex_lock(&client->send_mutex);

    /* Free any previous pending message */
    if (client->pending_msg) {
        free(client->pending_msg);
    }

    client->pending_msg = json_str;
    client->pending_len = strlen(json_str);

    pthread_mutex_unlock(&client->send_mutex);
    pthread_mutex_unlock(&s_clients_mutex);

    /* Request callback for write */
    lws_callback_on_writable(client->wsi);

    return ESP_OK;
}

struct lws_context *linux_ws_get_context(void)
{
    return s_context;
}
