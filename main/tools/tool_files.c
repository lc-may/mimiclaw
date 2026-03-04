#include "tools/tool_files.h"
#include "mimi_config.h"

#include "linux/linux_compat.h"
#include "linux/linux_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include "cJSON.h"

static const char *TAG = "tool_files";

#define MAX_FILE_SIZE (32 * 1024)

static size_t append_paths_recursive(const char *dir_path, const char *prefix,
                                    char *output, size_t output_size, size_t offset,
                                    int *count)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return offset;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && offset < output_size - 1) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            offset = append_paths_recursive(full_path, prefix, output, output_size, offset, count);
            continue;
        }

        if (prefix && strncmp(full_path, prefix, strlen(prefix)) != 0) {
            continue;
        }

        int written = snprintf(output + offset, output_size - offset, "%s\n", full_path);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= output_size - offset) {
            offset = output_size - 1;
            break;
        }
        offset += (size_t)written;
        (*count)++;
    }

    closedir(dir);
    return offset;
}

/**
 * Validate that a path starts with the data directory and contains no ".." traversal.
 */
static bool validate_path(const char *path)
{
    if (!path) return false;

    const char *data_dir = mimi_get_data_dir();
    if (strcmp(path, "~/.mimiclaw") == 0 || strcmp(path, ".mimiclaw") == 0) {
        return true;
    }

    if (strncmp(path, "~/.mimiclaw/", 12) == 0 || strncmp(path, ".mimiclaw/", 10) == 0) {
        return strstr(path, "..") == NULL;
    }

    size_t base_len = strlen(data_dir);

    if (strncmp(path, data_dir, base_len) != 0) return false;

    /* Require a path separator after the base (unless base ends with '/') */
    if (base_len > 0 && data_dir[base_len - 1] != '/') {
        if (path[base_len] != '/' && path[base_len] != '\0') return false;
    }

    if (strstr(path, "..") != NULL) return false;
    return true;
}

static bool resolve_path(const char *input, char *output, size_t output_size)
{
    if (!validate_path(input)) {
        return false;
    }
    return mimi_get_full_path(input, output, output_size) == 0;
}

/* ── read_file ─────────────────────────────────────────────── */

esp_err_t tool_read_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    char resolved[512];
    if (!resolve_path(path, resolved, sizeof(resolved))) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", mimi_get_data_dir());
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(resolved, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", resolved);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t max_read = output_size - 1;
    if (max_read > MAX_FILE_SIZE) max_read = MAX_FILE_SIZE;

    size_t n = fread(output, 1, max_read, f);
    output[n] = '\0';
    fclose(f);

    ESP_LOGI(TAG, "read_file: %s (%d bytes)", resolved, (int)n);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── write_file ────────────────────────────────────────────── */

esp_err_t tool_write_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));
    char resolved[512];

    if (!resolve_path(path, resolved, sizeof(resolved))) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", mimi_get_data_dir());
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!content) {
        snprintf(output, output_size, "Error: missing 'content' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(resolved, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", resolved);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        snprintf(output, output_size, "Error: wrote %d of %d bytes to %s", (int)written, (int)len, resolved);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "OK: wrote %d bytes to %s", (int)written, resolved);
    ESP_LOGI(TAG, "write_file: %s (%d bytes)", resolved, (int)written);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── edit_file ─────────────────────────────────────────────── */

esp_err_t tool_edit_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "old_string"));
    const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "new_string"));
    char resolved[512];

    if (!resolve_path(path, resolved, sizeof(resolved))) {
        snprintf(output, output_size, "Error: path must start with %s/ and must not contain '..'", mimi_get_data_dir());
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!old_str || !new_str) {
        snprintf(output, output_size, "Error: missing 'old_string' or 'new_string' field");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* Read existing file */
    FILE *f = fopen(resolved, "r");
    if (!f) {
        snprintf(output, output_size, "Error: file not found: %s", resolved);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
        snprintf(output, output_size, "Error: file too large or empty (%ld bytes)", file_size);
        fclose(f);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Allocate buffer for the result (old content + possible expansion) */
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t max_result = file_size + (new_len > old_len ? new_len - old_len : 0) + 1;
    char *buf = malloc(file_size + 1);
    char *result = malloc(max_result);
    if (!buf || !result) {
        free(buf);
        free(result);
        fclose(f);
        snprintf(output, output_size, "Error: out of memory");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(buf, 1, file_size, f);
    buf[n] = '\0';
    fclose(f);

    /* Find and replace first occurrence */
    char *pos = strstr(buf, old_str);
    if (!pos) {
        snprintf(output, output_size, "Error: old_string not found in %s", resolved);
        free(buf);
        free(result);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    size_t prefix_len = pos - buf;
    memcpy(result, buf, prefix_len);
    memcpy(result + prefix_len, new_str, new_len);
    size_t suffix_start = prefix_len + old_len;
    size_t suffix_len = n - suffix_start;
    memcpy(result + prefix_len + new_len, buf + suffix_start, suffix_len);
    size_t total = prefix_len + new_len + suffix_len;
    result[total] = '\0';

    free(buf);

    /* Write back */
    f = fopen(resolved, "w");
    if (!f) {
        snprintf(output, output_size, "Error: cannot open file for writing: %s", resolved);
        free(result);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fwrite(result, 1, total, f);
    fclose(f);
    free(result);

    snprintf(output, output_size, "OK: edited %s (replaced %d bytes with %d bytes)", resolved, (int)old_len, (int)new_len);
    ESP_LOGI(TAG, "edit_file: %s", resolved);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── list_dir ──────────────────────────────────────────────── */

esp_err_t tool_list_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *prefix = NULL;
    char resolved_prefix[512];
    const char *effective_prefix = NULL;
    if (root) {
        cJSON *pfx = cJSON_GetObjectItem(root, "prefix");
        if (pfx && cJSON_IsString(pfx)) {
            prefix = pfx->valuestring;
            if (!resolve_path(prefix, resolved_prefix, sizeof(resolved_prefix))) {
                snprintf(output, output_size, "Error: invalid prefix %s", prefix);
                cJSON_Delete(root);
                return ESP_ERR_INVALID_ARG;
            }
            effective_prefix = resolved_prefix;
        }
    }

    const char *data_dir = mimi_get_data_dir();
    DIR *dir = opendir(data_dir);
    if (!dir) {
        snprintf(output, output_size, "Error: cannot open %s directory", data_dir);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t off = 0;
    int count = 0;
    closedir(dir);
    off = append_paths_recursive(data_dir, effective_prefix, output, output_size, off, &count);

    if (count == 0) {
        snprintf(output, output_size, "(no files found)");
    }

    ESP_LOGI(TAG, "list_dir: %d files (prefix=%s)", count,
             effective_prefix ? effective_prefix : "(none)");
    cJSON_Delete(root);
    return ESP_OK;
}
