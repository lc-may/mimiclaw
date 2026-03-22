#include "session_mgr.h"
#include "mimi_config.h"

#include "linux/linux_compat.h"
#include "linux/linux_paths.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include "cJSON.h"

static const char *TAG = "session";

static void session_path(const char *chat_id, char *buf, size_t size)
{
    char rel_path[64];
    snprintf(rel_path, sizeof(rel_path), "sessions/tg_%s.jsonl", chat_id);
    mimi_get_full_path(rel_path, buf, size);
}

esp_err_t session_mgr_init(void)
{
    /* Ensure data directories exist on Linux */
    mimi_ensure_data_dirs();

    char session_dir[256];
    mimi_get_full_path("sessions/", session_dir, sizeof(session_dir));
    ESP_LOGI(TAG, "Session manager initialized at %s", session_dir);
    return ESP_OK;
}

esp_err_t session_append(const char *chat_id, const char *role, const char *content)
{
    char path[256];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open session file %s", path);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        fprintf(f, "%s\n", line);
        free(line);
    }

    fclose(f);
    return ESP_OK;
}

esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[256];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No history yet */
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Read all lines into a ring buffer of cJSON objects */
    cJSON *messages[MIMI_SESSION_MAX_MSGS];
    int count = 0;
    int write_idx = 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* Ring buffer: overwrite oldest if full */
        if (count >= max_msgs) {
            cJSON_Delete(messages[write_idx]);
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % max_msgs;
        if (count < max_msgs) count++;
    }
    fclose(f);

    /* Build JSON array with only role + content */
    cJSON *arr = cJSON_CreateArray();
    int start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % max_msgs;
        cJSON *src = messages[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        if (role && content) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
        }
        cJSON_AddItemToArray(arr, entry);
    }

    /* Cleanup ring buffer */
    int cleanup_start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (cleanup_start + i) % max_msgs;
        cJSON_Delete(messages[idx]);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_clear(const char *chat_id)
{
    char path[256];
    session_path(chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Session %s cleared", chat_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

/** Listed in session_list: canonical tg_<id>.jsonl or ws_<id>.jsonl (no tg_ prefix). */
static bool session_list_match(const char *name)
{
    if (!strstr(name, ".jsonl")) {
        return false;
    }
    if (strstr(name, "tg_")) {
        return true;
    }
    return strncmp(name, "ws_", 3) == 0;
}

void session_list(void)
{
    char session_dir[256];
    mimi_get_full_path("sessions/", session_dir, sizeof(session_dir));

    DIR *dir = opendir(session_dir);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open sessions directory");
        printf("  (cannot open sessions directory)\n");
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!session_list_match(entry->d_name)) {
            continue;
        }
        printf("  %s\n", entry->d_name);
        ESP_LOGI(TAG, "  Session: %s", entry->d_name);
        count++;
    }
    closedir(dir);

    if (count == 0) {
        printf("  (no session files)\n");
        ESP_LOGI(TAG, "  No sessions found");
    }
}
