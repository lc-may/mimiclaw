/**
 * @file linux_paths.c
 * @brief Path utilities implementation
 */

#include "linux_paths.h"
#include "linux_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#define TAG "paths"

static char s_data_dir[256] = {0};

static bool strip_prefix(const char *prefix, const char **path)
{
    size_t prefix_len = strlen(prefix);
    if (strncmp(*path, prefix, prefix_len) != 0) {
        return false;
    }

    *path += prefix_len;
    if (**path == '/') {
        (*path)++;
    }
    return true;
}

const char *mimi_get_data_dir(void) {
    if (s_data_dir[0]) return s_data_dir;

    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) home = "/tmp";

    snprintf(s_data_dir, sizeof(s_data_dir), "%s/.mimiclaw", home);
    return s_data_dir;
}

int mimi_get_full_path(const char *rel_path, char *buf, size_t buf_size) {
    const char *base = mimi_get_data_dir();

    if (!rel_path || !buf || buf_size == 0) return -1;

    if (strncmp(rel_path, base, strlen(base)) == 0) {
        int ret = snprintf(buf, buf_size, "%s", rel_path);
        return (ret >= 0 && (size_t)ret < buf_size) ? 0 : -1;
    }

    if (strcmp(rel_path, "~/.mimiclaw") == 0 || strcmp(rel_path, ".mimiclaw") == 0) {
        int ret = snprintf(buf, buf_size, "%s", base);
        return (ret >= 0 && (size_t)ret < buf_size) ? 0 : -1;
    }

    strip_prefix("~/.mimiclaw", &rel_path);
    strip_prefix(".mimiclaw", &rel_path);
    while (*rel_path == '/') {
        rel_path++;
    }

    int ret = snprintf(buf, buf_size, "%s/%s", base, rel_path);
    return (ret >= 0 && (size_t)ret < buf_size) ? 0 : -1;
}

int mimi_ensure_data_dirs(void) {
    const char *base = mimi_get_data_dir();
    char path[512];

    /* Create base directory */
    if (mkdir(base, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create %s: %s", base, strerror(errno));
        return -1;
    }

    /* Create subdirectories */
    const char *subdirs[] = {"config", "memory", "sessions", "skills", "logs", NULL};
    for (int i = 0; subdirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", base, subdirs[i]);
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create %s: %s", path, strerror(errno));
            return -1;
        }
    }

    /* llm_tools/js for MicroQuickJS (mqjs) tool scripts */
    snprintf(path, sizeof(path), "%s/llm_tools", base);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create %s: %s", path, strerror(errno));
        return -1;
    }
    snprintf(path, sizeof(path), "%s/llm_tools/js", base);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create %s: %s", path, strerror(errno));
        return -1;
    }

    ESP_LOGI(TAG, "Data directories ready at %s", base);
    return 0;
}
