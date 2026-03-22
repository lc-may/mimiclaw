#pragma once

#include "linux/linux_compat.h"
#include "llm/llm_proxy.h"

#include "cJSON.h"

#include <stdbool.h>

/**
 * Append one JSON line to ~/.mimiclaw/logs/agent_trace.jsonl (see MIMI_AGENT_TRACE_REL).
 */

void agent_trace_turn_start(const char *channel, const char *chat_id, const char *user_text);

void agent_trace_llm_iteration(const char *channel, const char *chat_id, int iteration,
                              const char *system_prompt, const cJSON *messages);

void agent_trace_llm_response(const char *channel, const char *chat_id, int iteration,
                              const llm_response_t *resp);

void agent_trace_tool_call(const char *channel, const char *chat_id, int iteration,
                           const char *tool_name, const char *input_json, const char *output);

void agent_trace_llm_failed(const char *channel, const char *chat_id, int iteration, esp_err_t err);

void agent_trace_turn_end(const char *channel, const char *chat_id, const char *final_text);

void agent_trace_turn_error(const char *channel, const char *chat_id, const char *message);
