#include "memory_store.h"
#include "mimi_config.h"

#include "linux/linux_compat.h"
#include "linux/linux_paths.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "memory";

static void get_date_str(char *buf, size_t size, int days_ago)
{
    time_t now;
    time(&now);
    now -= days_ago * 86400;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d", &tm);
}

esp_err_t memory_store_init(void)
{
    /* Ensure data directories exist on Linux */
    mimi_ensure_data_dirs();
    ESP_LOGI(TAG, "Memory store initialized at %s", mimi_get_data_dir());
    return ESP_OK;
}

esp_err_t memory_read_long_term(char *buf, size_t size)
{
    char path[256];
    mimi_get_full_path(MIMI_PATH_MEMORY, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t memory_write_long_term(const char *content)
{
    char path[256];
    mimi_get_full_path(MIMI_PATH_MEMORY, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s", path);
        return ESP_FAIL;
    }
    fputs(content, f);
    fclose(f);
    ESP_LOGI(TAG, "Long-term memory updated (%d bytes)", (int)strlen(content));
    return ESP_OK;
}

esp_err_t memory_append_today(const char *note)
{
    char date_str[16];
    get_date_str(date_str, sizeof(date_str), 0);

    char rel_path[64];
    snprintf(rel_path, sizeof(rel_path), "memory/%s.md", date_str);

    char path[256];
    mimi_get_full_path(rel_path, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        /* Try creating — if file doesn't exist yet, write header */
        f = fopen(path, "w");
        if (!f) {
            ESP_LOGE(TAG, "Cannot open %s", path);
            return ESP_FAIL;
        }
        fprintf(f, "# %s\n\n", date_str);
    }

    fprintf(f, "%s\n", note);
    fclose(f);
    return ESP_OK;
}

esp_err_t memory_read_recent(char *buf, size_t size, int days)
{
    size_t offset = 0;
    buf[0] = '\0';

    for (int i = 0; i < days && offset < size - 1; i++) {
        char date_str[16];
        get_date_str(date_str, sizeof(date_str), i);

        char rel_path[64];
        snprintf(rel_path, sizeof(rel_path), "memory/%s.md", date_str);

        char path[256];
        mimi_get_full_path(rel_path, path, sizeof(path));

        FILE *f = fopen(path, "r");
        if (!f) continue;

        if (offset > 0 && offset < size - 4) {
            offset += snprintf(buf + offset, size - offset, "\n---\n");
        }

        size_t n = fread(buf + offset, 1, size - offset - 1, f);
        offset += n;
        buf[offset] = '\0';
        fclose(f);
    }

    return ESP_OK;
}
