#pragma once

#include "linux/linux_compat.h"
#include <stddef.h>

/**
 * Run a .js file with the host mqjs (MicroQuickJS) binary.
 * Scripts live under ~/.mimiclaw/llm_tools/js/.
 */
esp_err_t tool_run_javascript_execute(const char *input_json, char *output, size_t output_size);
