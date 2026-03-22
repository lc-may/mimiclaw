#include "tools/tool_run_javascript.h"
#include "linux/linux_paths.h"

#include "linux/linux_compat.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON.h"

static const char *TAG = "tool_js";

#define MAX_SCRIPT_BYTES (256 * 1024)
#define MAX_FILENAME_LEN 128

static bool is_safe_js_basename(const char *name)
{
    size_t n;

    if (!name || !name[0])
        return false;
    n = strlen(name);
    if (n < 4 || n > MAX_FILENAME_LEN)
        return false;
    if (strcmp(name + n - 3, ".js") != 0)
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c) || c == '_' || c == '-' || c == '.')
            continue;
        return false;
    }
    if (name[0] == '.' && name[1] == '.')
        return false;
    return true;
}

static bool resolve_mqjs_binary(char *out, size_t out_sz)
{
    const char *env = getenv("MIMICLAW_MQJS");
    if (env && env[0] && access(env, X_OK) == 0) {
        snprintf(out, out_sz, "%s", env);
        return true;
    }

    static const char *candidates[] = {
        "mquickjs/mqjs",
        "./mquickjs/mqjs",
        "../mquickjs/mqjs",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (access(candidates[i], X_OK) != 0)
            continue;
        if (realpath(candidates[i], out) != NULL)
            return true;
        snprintf(out, out_sz, "%s", candidates[i]);
        return true;
    }

    return false;
}

static int ensure_llm_js_path(char *dir_out, size_t dir_sz)
{
    const char *base = mimi_get_data_dir();

    snprintf(dir_out, dir_sz, "%s/llm_tools", base);
    if (mkdir(dir_out, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir llm_tools: %s", strerror(errno));
        return -1;
    }
    snprintf(dir_out, dir_sz, "%s/llm_tools/js", base);
    if (mkdir(dir_out, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir llm_tools/js: %s", strerror(errno));
        return -1;
    }
    return 0;
}

esp_err_t tool_run_javascript_execute(const char *input_json, char *output, size_t output_size)
{
    char mqjs_bin[PATH_MAX];
    char js_dir[512];
    char js_path[768];
    cJSON *root = NULL;
    const cJSON *jname = NULL;
    const cJSON *jscript = NULL;
    esp_err_t err = ESP_FAIL;

    if (!input_json || !output || output_size == 0) {
        snprintf(output, output_size, "Error: invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_Parse(input_json);
    if (!root || !cJSON_IsObject(root)) {
        snprintf(output, output_size, "Error: invalid JSON input");
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }

    jname = cJSON_GetObjectItemCaseSensitive(root, "filename");
    if (!cJSON_IsString(jname) || !jname->valuestring || !jname->valuestring[0]) {
        snprintf(output, output_size, "Error: missing or invalid 'filename' (basename, must end with .js)");
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }

    if (!is_safe_js_basename(jname->valuestring)) {
        snprintf(output, output_size,
                 "Error: filename must be a safe basename ending in .js (letters, digits, _ - . only)");
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }

    jscript = cJSON_GetObjectItemCaseSensitive(root, "script");
    if (jscript != NULL && !cJSON_IsString(jscript)) {
        snprintf(output, output_size, "Error: 'script' must be a string if present");
        err = ESP_ERR_INVALID_ARG;
        goto done;
    }

    if (ensure_llm_js_path(js_dir, sizeof(js_dir)) != 0) {
        snprintf(output, output_size, "Error: cannot create llm_tools/js directory");
        err = ESP_FAIL;
        goto done;
    }

    snprintf(js_path, sizeof(js_path), "%s/%s", js_dir, jname->valuestring);

    if (cJSON_IsString(jscript) && jscript->valuestring) {
        size_t slen = strlen(jscript->valuestring);
        if (slen > MAX_SCRIPT_BYTES) {
            snprintf(output, output_size, "Error: script exceeds max size (%u bytes)",
                     (unsigned)MAX_SCRIPT_BYTES);
            err = ESP_ERR_INVALID_SIZE;
            goto done;
        }
        FILE *wf = fopen(js_path, "wb");
        if (!wf) {
            snprintf(output, output_size, "Error: cannot write %s: %s", js_path, strerror(errno));
            err = ESP_FAIL;
            goto done;
        }
        if (slen > 0 && fwrite(jscript->valuestring, 1, slen, wf) != slen) {
            fclose(wf);
            snprintf(output, output_size, "Error: short write to %s", js_path);
            err = ESP_FAIL;
            goto done;
        }
        fclose(wf);
    } else {
        if (access(js_path, R_OK) != 0) {
            snprintf(output, output_size,
                     "Error: file not found: %s (provide 'script' to create it, or write the file first)",
                     js_path);
            err = ESP_ERR_NOT_FOUND;
            goto done;
        }
    }

    if (!resolve_mqjs_binary(mqjs_bin, sizeof(mqjs_bin))) {
        snprintf(output, output_size,
                 "Error: mqjs binary not found. Build with: make -C mquickjs "
                 "or set MIMICLAW_MQJS to the mqjs executable path");
        err = ESP_ERR_NOT_FOUND;
        goto done;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        snprintf(output, output_size, "Error: pipe: %s", strerror(errno));
        err = ESP_FAIL;
        goto done;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(output, output_size, "Error: fork: %s", strerror(errno));
        err = ESP_FAIL;
        goto done;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0)
            _exit(126);
        close(pipefd[1]);
        execl(mqjs_bin, "mqjs", js_path, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    size_t total = 0;
    bool truncated = false;
    while (total < output_size - 1) {
        ssize_t n = read(pipefd[0], output + total, output_size - 1 - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    {
        char discard;
        if (read(pipefd[0], &discard, 1) == 1)
            truncated = true;
    }

    close(pipefd[0]);

    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) < 0) {
        snprintf(output, output_size, "Error: waitpid: %s", strerror(errno));
        err = ESP_FAIL;
        goto done;
    }

    output[total] = '\0';
    if (truncated) {
        const char *msg = "\n... (output truncated)\n";
        size_t ml = strlen(msg);
        if (total + ml + 1 <= output_size) {
            memcpy(output + total, msg, ml + 1);
        } else if (output_size > ml + 1) {
            memcpy(output + output_size - 1 - ml, msg, ml + 1);
        }
    }

    if (WIFEXITED(wstatus)) {
        int code = WEXITSTATUS(wstatus);
        if (code != 0) {
            size_t len = strlen(output);
            int left = (int)(output_size - len - 1);
            if (left > 0)
                snprintf(output + len, (size_t)left, "\n[exit code %d]\n", code);
        }
        err = ESP_OK;
    } else {
        size_t len = strlen(output);
        int left = (int)(output_size - len - 1);
        if (left > 0)
            snprintf(output + len, (size_t)left, "\n[child did not exit normally]\n");
        err = ESP_FAIL;
    }

done:
    cJSON_Delete(root);
    return err;
}
