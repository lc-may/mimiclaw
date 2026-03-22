/**
 * @file agent_trace.c
 * @brief JSON Lines trace log for agent / LLM / tool interactions
 */

#include "agent/agent_trace.h"
#include "mimi_config.h"
#include "linux/linux_paths.h"

#include "linux/linux_compat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "agent_trace";

static int ensure_logs_dir(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/logs", mimi_get_data_dir());
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static void trace_path(char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "%s/%s", mimi_get_data_dir(), MIMI_AGENT_TRACE_REL);
}

static void add_ts(cJSON *obj)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long ms = (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
    cJSON_AddNumberToObject(obj, "ts_ms", (double)ms);

    struct tm tm_buf;
    time_t sec = tv.tv_sec;
    gmtime_r(&sec, &tm_buf);
    char iso[40];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    cJSON_AddStringToObject(obj, "ts", iso);
}

static void add_common(cJSON *obj, const char *event, const char *channel, const char *chat_id)
{
    add_ts(obj);
    cJSON_AddStringToObject(obj, "event", event);
    cJSON_AddStringToObject(obj, "channel", channel ? channel : "");
    cJSON_AddStringToObject(obj, "chat_id", chat_id ? chat_id : "");
}

static void add_truncated_string(cJSON *obj, const char *key, const char *val)
{
    char flag_key[96];
    snprintf(flag_key, sizeof(flag_key), "%s_truncated", key);

    if (!val) {
        cJSON_AddNullToObject(obj, key);
        return;
    }
    size_t len = strlen(val);
    if (len <= MIMI_AGENT_TRACE_MAX_FIELD) {
        cJSON_AddStringToObject(obj, key, val);
        return;
    }
    char *tmp = malloc(MIMI_AGENT_TRACE_MAX_FIELD + 1);
    if (!tmp) {
        cJSON_AddStringToObject(obj, key, "(alloc failed)");
        return;
    }
    memcpy(tmp, val, MIMI_AGENT_TRACE_MAX_FIELD);
    tmp[MIMI_AGENT_TRACE_MAX_FIELD] = '\0';
    cJSON_AddStringToObject(obj, key, tmp);
    cJSON_AddBoolToObject(obj, flag_key, 1);
    free(tmp);
}

static void append_line(cJSON *obj)
{
    if (ensure_logs_dir() != 0) {
        cJSON_Delete(obj);
        return;
    }

    char path[512];
    trace_path(path, sizeof(path));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!line) {
        return;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGW(TAG, "fopen %s: %s", path, strerror(errno));
        free(line);
        return;
    }
    fprintf(f, "%s\n", line);
    free(line);
    fflush(f);
    fclose(f);
}

void agent_trace_turn_start(const char *channel, const char *chat_id, const char *user_text)
{
    cJSON *o = cJSON_CreateObject();
    add_common(o, "turn_start", channel, chat_id);
    add_truncated_string(o, "user_text", user_text);
    append_line(o);
}

void agent_trace_llm_iteration(const char *channel, const char *chat_id, int iteration,
                               const char *system_prompt, const cJSON *messages)
{
    cJSON *o = cJSON_CreateObject();
    add_common(o, "llm_iteration", channel, chat_id);
    cJSON_AddNumberToObject(o, "iteration", iteration);
    add_truncated_string(o, "system_prompt", system_prompt ? system_prompt : "");

    if (messages && cJSON_IsArray(messages)) {
        int alen = cJSON_GetArraySize((cJSON *)messages);
        cJSON_AddNumberToObject(o, "messages_array_len", (double)alen);

        char *mj = cJSON_PrintUnformatted((cJSON *)messages);
        if (mj) {
            size_t len = strlen(mj);
            if (len <= MIMI_AGENT_TRACE_MAX_FIELD) {
                cJSON_AddStringToObject(o, "messages_json", mj);
            } else {
                char *tmp = malloc(MIMI_AGENT_TRACE_MAX_FIELD + 1);
                if (tmp) {
                    memcpy(tmp, mj, MIMI_AGENT_TRACE_MAX_FIELD);
                    tmp[MIMI_AGENT_TRACE_MAX_FIELD] = '\0';
                    cJSON_AddStringToObject(o, "messages_json", tmp);
                    cJSON_AddBoolToObject(o, "messages_json_truncated", 1);
                    free(tmp);
                }
            }
            free(mj);
        }
    } else {
        cJSON_AddNullToObject(o, "messages_json");
    }

    append_line(o);
}

void agent_trace_llm_response(const char *channel, const char *chat_id, int iteration,
                              const llm_response_t *resp)
{
    if (!resp) {
        return;
    }

    cJSON *o = cJSON_CreateObject();
    add_common(o, "llm_response", channel, chat_id);
    cJSON_AddNumberToObject(o, "iteration", iteration);
    cJSON_AddBoolToObject(o, "tool_use", resp->tool_use ? 1 : 0);
    add_truncated_string(o, "text", resp->text ? resp->text : "");

    cJSON *calls = cJSON_CreateArray();
    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *c = &resp->calls[i];
        cJSON *one = cJSON_CreateObject();
        cJSON_AddStringToObject(one, "id", c->id);
        cJSON_AddStringToObject(one, "name", c->name);
        add_truncated_string(one, "input", c->input ? c->input : "{}");
        cJSON_AddItemToArray(calls, one);
    }
    cJSON_AddItemToObject(o, "calls", calls);

    append_line(o);
}

void agent_trace_tool_call(const char *channel, const char *chat_id, int iteration,
                           const char *tool_name, const char *input_json, const char *output)
{
    cJSON *o = cJSON_CreateObject();
    add_common(o, "tool_call", channel, chat_id);
    cJSON_AddNumberToObject(o, "iteration", iteration);
    cJSON_AddStringToObject(o, "name", tool_name ? tool_name : "");
    add_truncated_string(o, "input_json", input_json ? input_json : "{}");
    add_truncated_string(o, "output", output ? output : "");
    append_line(o);
}

void agent_trace_llm_failed(const char *channel, const char *chat_id, int iteration, esp_err_t err)
{
    cJSON *o = cJSON_CreateObject();
    add_common(o, "error", channel, chat_id);
    cJSON_AddNumberToObject(o, "iteration", iteration);
    cJSON_AddStringToObject(o, "phase", "llm_chat_tools");
    cJSON_AddStringToObject(o, "esp_err", esp_err_to_name(err));
    const char *detail = llm_get_last_error();
    add_truncated_string(o, "llm_error", detail ? detail : "");
    append_line(o);
}

void agent_trace_turn_end(const char *channel, const char *chat_id, const char *final_text)
{
    cJSON *o = cJSON_CreateObject();
    add_common(o, "turn_end", channel, chat_id);
    add_truncated_string(o, "final_text", final_text ? final_text : "");
    append_line(o);
}

void agent_trace_turn_error(const char *channel, const char *chat_id, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    add_common(o, "error", channel, chat_id);
    cJSON_AddStringToObject(o, "phase", "turn");
    add_truncated_string(o, "message", message ? message : "");
    append_line(o);
}
