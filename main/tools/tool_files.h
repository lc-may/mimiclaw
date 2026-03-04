#pragma once

#include "linux/linux_compat.h"
#include <stddef.h>

/**
 * Read a file from the data directory (~/.mimiclaw/).
 * Input JSON: {"path": "~/.mimiclaw/..."}
 */
esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * Write/overwrite a file in the data directory.
 * Input JSON: {"path": "~/.mimiclaw/...", "content": "..."}
 */
esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * Find-and-replace edit a file in the data directory.
 * Input JSON: {"path": "~/.mimiclaw/...", "old_string": "...", "new_string": "..."}
 */
esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size);

/**
 * List files in the data directory, optionally filtered by path prefix.
 * Input JSON: {"prefix": "~/.mimiclaw/..."} (prefix is optional)
 */
esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size);
