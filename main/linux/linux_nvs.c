/**
 * @file linux_nvs.c
 * @brief NVS-to-JSON implementation for Linux
 *
 * Provides NVS-compatible API backed by a JSON configuration file.
 */

#include "linux_nvs.h"
#include "linux_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <cJSON.h>

#define TAG "nvs"

/* Config file path - ~/.mimiclaw/config.json */
static char s_config_path[256] = {0};
static cJSON *s_config_root = NULL;
static pthread_mutex_t s_nvs_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Forward declarations */
static cJSON *get_or_create_namespace(cJSON *root, const char *namespace);
static void ensure_config_loaded(void);
static esp_err_t save_config(void);

/* Get home directory */
static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home) return home;

    struct passwd *pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;

    return "/tmp";
}

/* Initialize NVS - load or create config file */
esp_err_t linux_nvs_init(void) {
    const char *home = get_home_dir();
    snprintf(s_config_path, sizeof(s_config_path), "%s/.mimiclaw", home);

    /* Create directory if it doesn't exist */
    mkdir(s_config_path, 0755);

    /* Create subdirectories */
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/config", s_config_path);
    mkdir(subdir, 0755);
    snprintf(subdir, sizeof(subdir), "%s/memory", s_config_path);
    mkdir(subdir, 0755);
    snprintf(subdir, sizeof(subdir), "%s/sessions", s_config_path);
    mkdir(subdir, 0755);
    snprintf(subdir, sizeof(subdir), "%s/skills", s_config_path);
    mkdir(subdir, 0755);

    /* Append config filename */
    strncat(s_config_path, "/config.json", sizeof(s_config_path) - strlen(s_config_path) - 1);

    /* Load or create config */
    ensure_config_loaded();

    ESP_LOGI(TAG, "NVS config initialized: %s", s_config_path);
    return ESP_OK;
}

const char *linux_nvs_get_config_path(void) {
    return s_config_path;
}

/* Ensure config is loaded from file */
static void ensure_config_loaded(void) {
    if (s_config_root) return;

    /* Try to load existing config */
    FILE *f = fopen(s_config_path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size > 0 && size < 65536) {
            char *buf = malloc(size + 1);
            if (buf) {
                size_t n = fread(buf, 1, size, f);
                buf[n] = '\0';
                s_config_root = cJSON_Parse(buf);
                free(buf);
            }
        }
        fclose(f);
    }

    /* Create new config if needed */
    if (!s_config_root) {
        s_config_root = cJSON_CreateObject();
    }
}

/* Save config to file */
static esp_err_t save_config(void) {
    if (!s_config_root) return ESP_FAIL;

    char *json_str = cJSON_Print(s_config_root);
    if (!json_str) return ESP_ERR_NO_MEM;

    FILE *f = fopen(s_config_path, "w");
    if (!f) {
        free(json_str);
        return ESP_FAIL;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fclose(f);
    free(json_str);

    return (written == len) ? ESP_OK : ESP_FAIL;
}

/* Get or create namespace object */
static cJSON *get_or_create_namespace(cJSON *root, const char *namespace) {
    cJSON *ns = cJSON_GetObjectItem(root, namespace);
    if (!ns) {
        ns = cJSON_CreateObject();
        cJSON_AddItemToObject(root, namespace, ns);
    }
    return ns;
}

/* ============== NVS API Implementation ============== */

/* Handle is just an index into a simple array for tracking open handles */
#define MAX_NVS_HANDLES 16

typedef struct {
    char namespace[32];
    int mode;  /* NVS_READONLY or NVS_READWRITE */
    bool in_use;
} nvs_handle_entry_t;

static nvs_handle_entry_t s_handles[MAX_NVS_HANDLES];

esp_err_t nvs_open(const char *namespace, int mode, nvs_handle_t *handle) {
    if (!namespace || !handle) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    /* Find free handle slot */
    int slot = -1;
    for (int i = 0; i < MAX_NVS_HANDLES; i++) {
        if (!s_handles[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&s_nvs_mutex);
        return ESP_ERR_NO_MEM;
    }

    s_handles[slot].in_use = true;
    strncpy(s_handles[slot].namespace, namespace, sizeof(s_handles[slot].namespace) - 1);
    s_handles[slot].mode = mode;
    *handle = slot;

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    if (handle >= 0 && handle < MAX_NVS_HANDLES) {
        pthread_mutex_lock(&s_nvs_mutex);
        s_handles[handle].in_use = false;
        pthread_mutex_unlock(&s_nvs_mutex);
    }
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *buf, size_t *len) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!key || !buf || !len) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    cJSON *ns = cJSON_GetObjectItem(s_config_root, s_handles[handle].namespace);
    if (!ns) {
        pthread_mutex_unlock(&s_nvs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *item = cJSON_GetObjectItem(ns, key);
    if (!item || !cJSON_IsString(item)) {
        pthread_mutex_unlock(&s_nvs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    const char *val = item->valuestring;
    size_t val_len = strlen(val) + 1;

    if (*len < val_len) {
        *len = val_len;
        pthread_mutex_unlock(&s_nvs_mutex);
        return ESP_ERR_INVALID_ARG;  /* Buffer too small */
    }

    memcpy(buf, val, val_len);
    *len = val_len;

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *val) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!key || !val) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    cJSON *ns = cJSON_GetObjectItem(s_config_root, s_handles[handle].namespace);
    if (!ns) {
        pthread_mutex_unlock(&s_nvs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *item = cJSON_GetObjectItem(ns, key);
    if (!item || !cJSON_IsNumber(item)) {
        pthread_mutex_unlock(&s_nvs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    *val = (int64_t)item->valuedouble;

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *val) {
    int64_t tmp;
    esp_err_t ret = nvs_get_i64(handle, key, &tmp);
    if (ret == ESP_OK) {
        *val = (uint16_t)tmp;
    }
    return ret;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *val) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_handles[handle].mode == NVS_READONLY) return ESP_ERR_INVALID_STATE;
    if (!key || !val) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    cJSON *ns = get_or_create_namespace(s_config_root, s_handles[handle].namespace);

    /* Remove existing key if present */
    cJSON_DeleteItemFromObject(ns, key);

    /* Add new value */
    cJSON_AddStringToObject(ns, key, val);

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t val) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_handles[handle].mode == NVS_READONLY) return ESP_ERR_INVALID_STATE;
    if (!key) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    cJSON *ns = get_or_create_namespace(s_config_root, s_handles[handle].namespace);

    /* Remove existing key if present */
    cJSON_DeleteItemFromObject(ns, key);

    /* Add new value */
    cJSON_AddNumberToObject(ns, key, (double)val);

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t val) {
    return nvs_set_i64(handle, key, (int64_t)val);
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&s_nvs_mutex);
    esp_err_t ret = save_config();
    pthread_mutex_unlock(&s_nvs_mutex);

    return ret;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_handles[handle].mode == NVS_READONLY) return ESP_ERR_INVALID_STATE;
    if (!key) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    cJSON *ns = cJSON_GetObjectItem(s_config_root, s_handles[handle].namespace);
    if (ns) {
        cJSON_DeleteItemFromObject(ns, key);
    }

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    if (handle < 0 || handle >= MAX_NVS_HANDLES || !s_handles[handle].in_use) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_handles[handle].mode == NVS_READONLY) return ESP_ERR_INVALID_STATE;

    pthread_mutex_lock(&s_nvs_mutex);
    ensure_config_loaded();

    /* Delete entire namespace */
    cJSON_DeleteItemFromObject(s_config_root, s_handles[handle].namespace);

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}

esp_err_t nvs_flash_init(void) {
    return linux_nvs_init();
}

esp_err_t nvs_flash_erase(void) {
    pthread_mutex_lock(&s_nvs_mutex);

    if (s_config_root) {
        cJSON_Delete(s_config_root);
        s_config_root = NULL;
    }

    /* Delete config file */
    if (s_config_path[0]) {
        unlink(s_config_path);
    }

    /* Create fresh config */
    s_config_root = cJSON_CreateObject();

    pthread_mutex_unlock(&s_nvs_mutex);
    return ESP_OK;
}
