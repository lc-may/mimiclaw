/**
 * @file linux_compat.h
 * @brief Linux compatibility layer used by MimiClaw
 *
 * This header provides the minimal API surface MimiClaw expects for
 * logging, tasks, queues, timers, storage, and HTTP integration on Linux.
 */

#pragma once

/* Standard C headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <inttypes.h>  /* For PRId64, PRIu64, etc. */

/* ============== Error Code Mapping ============== */

typedef int esp_err_t;

#define ESP_OK              0
#define ESP_FAIL            -1
#define ESP_ERR_NO_MEM      -2
#define ESP_ERR_TIMEOUT     -3
#define ESP_ERR_NOT_FOUND   -4
#define ESP_ERR_INVALID_ARG -5
#define ESP_ERR_INVALID_STATE -6
#define ESP_ERR_NOT_SUPPORTED -7
#define ESP_ERR_INVALID_SIZE -8

/* HTTP-specific errors */
#define ESP_ERR_HTTP_CONNECT    -100
#define ESP_ERR_HTTP_WRITE_DATA -101

/* NVS-specific errors */
#define ESP_ERR_NVS_NO_FREE_PAGES   -200
#define ESP_ERR_NVS_NEW_VERSION_FOUND -201

/* TLS-specific errors */
#define ESP_TLS_ERR_SSL_WANT_READ   -300
#define ESP_TLS_ERR_SSL_WANT_WRITE  -301

/* Error checking macro - maps to assert-like behavior */
#define ESP_ERROR_CHECK(x) do { \
    esp_err_t _err = (x); \
    if (_err != ESP_OK) { \
        fprintf(stderr, "ESP_ERROR_CHECK failed: %s:%d (%s) returned 0x%x\n", \
                __FILE__, __LINE__, #x, _err); \
        exit(1); \
    } \
} while(0)

/* Error code to name - returns static string */
static inline const char *esp_err_to_name(esp_err_t err) {
    switch (err) {
        case ESP_OK:              return "ESP_OK";
        case ESP_FAIL:            return "ESP_FAIL";
        case ESP_ERR_NO_MEM:      return "ESP_ERR_NO_MEM";
        case ESP_ERR_TIMEOUT:     return "ESP_ERR_TIMEOUT";
        case ESP_ERR_NOT_FOUND:   return "ESP_ERR_NOT_FOUND";
            case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
            case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
            case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
            case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
            default:                  return "UNKNOWN";
    }
}

/* ============== Log Level Mapping ============== */

typedef enum {
    ESP_LOG_NONE    = 0,
    ESP_LOG_ERROR   = 1,
    ESP_LOG_WARN    = 2,
    ESP_LOG_INFO    = 3,
    ESP_LOG_DEBUG   = 4,
    ESP_LOG_VERBOSE = 5,
} esp_log_level_t;

/* Current log level - can be adjusted at runtime */
extern int g_esp_log_level;

static inline void esp_log_level_set(const char *tag, esp_log_level_t level) {
    (void)tag;
    g_esp_log_level = level;
}

/* Get timestamp for logs */
static inline uint32_t esp_log_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* Logging macros with timestamp and tag */
#define ESP_LOGE(tag, fmt, ...) \
    do { if (g_esp_log_level >= ESP_LOG_ERROR) \
        fprintf(stderr, "E (%u) %s: " fmt "\n", esp_log_timestamp(), tag, ##__VA_ARGS__); } while(0)

#define ESP_LOGW(tag, fmt, ...) \
    do { if (g_esp_log_level >= ESP_LOG_WARN) \
        fprintf(stdout, "W (%u) %s: " fmt "\n", esp_log_timestamp(), tag, ##__VA_ARGS__); } while(0)

#define ESP_LOGI(tag, fmt, ...) \
    do { if (g_esp_log_level >= ESP_LOG_INFO) \
        fprintf(stdout, "I (%u) %s: " fmt "\n", esp_log_timestamp(), tag, ##__VA_ARGS__); } while(0)

#define ESP_LOGD(tag, fmt, ...) \
    do { if (g_esp_log_level >= ESP_LOG_DEBUG) \
        fprintf(stdout, "D (%u) %s: " fmt "\n", esp_log_timestamp(), tag, ##__VA_ARGS__); } while(0)

#define ESP_LOGV(tag, fmt, ...) \
    do { if (g_esp_log_level >= ESP_LOG_VERBOSE) \
        fprintf(stdout, "V (%u) %s: " fmt "\n", esp_log_timestamp(), tag, ##__VA_ARGS__); } while(0)

/* ============== Memory Management ============== */

/* Ignore memory capability flags on Linux - just use standard malloc */
#define MALLOC_CAP_SPIRAM    0
#define MALLOC_CAP_INTERNAL  0
#define MALLOC_CAP_8BIT      0
#define MALLOC_CAP_32BIT     0

#define heap_caps_malloc(size, caps)        malloc(size)
#define heap_caps_calloc(n, size, caps)     calloc(n, size)
#define heap_caps_realloc(ptr, size, caps)  realloc(ptr, size)
#define heap_caps_free(ptr)                 free(ptr)

static inline size_t heap_caps_get_free_size(uint32_t caps) {
    (void)caps;
    /* Return a reasonable large value - on Linux we don't track this precisely */
    return 1024 * 1024 * 100;  /* 100 MB */
}

static inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
    (void)caps;
    /* Return a reasonable large value - on Linux we don't track this precisely */
    return 1024 * 1024 * 50;  /* 50 MB */
}

static inline uint32_t esp_get_free_heap_size(void) {
    return (uint32_t)heap_caps_get_free_size(0);
}

/* ============== Thread/Task Management ============== */

typedef pthread_t TaskHandle_t;

/* MimiClaw task function signature */
typedef void (*TaskFunction_t)(void*);

#define pdPASS  1
#define pdFAIL  0
#define pdTRUE  1
#define pdFALSE 0

/* Convert ms to ticks - on Linux we use ms directly */
#define pdMS_TO_TICKS(ms) (ms)

/* Port max delay */
#define portMAX_DELAY UINT32_MAX

/* Wrapper to adapt task signature to pthread */
typedef struct {
    TaskFunction_t task_func;
    void *pvParameters;
} task_wrapper_args_t;

static inline void* task_wrapper(void *arg) {
    task_wrapper_args_t *args = (task_wrapper_args_t *)arg;
    TaskFunction_t task_func = args->task_func;
    void *pvParameters = args->pvParameters;
    free(args);
    task_func(pvParameters);
    return NULL;
}

/* Thread creation wrapper */
static inline int xTaskCreate(TaskFunction_t pxTaskCode,
                              const char *pcName,
                              uint16_t usStackDepth,
                              void *pvParameters,
                              int uxPriority,
                              TaskHandle_t *pxCreatedTask) {
    (void)pcName;
    (void)usStackDepth;
    (void)uxPriority;

    task_wrapper_args_t *args = malloc(sizeof(*args));
    if (!args) return pdFAIL;
    args->task_func = pxTaskCode;
    args->pvParameters = pvParameters;

    pthread_t thread;
    int ret = pthread_create(&thread, NULL, task_wrapper, args);
    if (ret == 0) {
        if (pxCreatedTask) {
            *pxCreatedTask = thread;
        } else {
            pthread_detach(thread);
        }
        return pdPASS;
    }
    free(args);
    return pdFAIL;
}

/* Pinned to core - on Linux we ignore core affinity */
#define xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core) \
    xTaskCreate(fn, name, stack, arg, prio, handle)

/* Delete task - on Linux, thread should just return or pthread_exit */
static inline void vTaskDelete(TaskHandle_t task) {
    if (task) {
        pthread_detach(task);  /* Just detach, don't join */
    }
}

/* Delay in milliseconds */
static inline void vTaskDelay(uint32_t ms) {
    usleep(ms * 1000);
}

/* ============== Queue Implementation (pthread-based) ============== */

typedef struct queue_handle {
    void *buffer;
    size_t item_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} *QueueHandle_t;

static inline QueueHandle_t xQueueCreate(size_t queue_length, size_t item_size) {
    QueueHandle_t q = calloc(1, sizeof(*q));
    if (!q) return NULL;

    q->buffer = calloc(queue_length, item_size);
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    q->item_size = item_size;
    q->capacity = queue_length;
    q->head = 0;
    q->tail = 0;
    q->count = 0;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return q;
}

static inline void vQueueDelete(QueueHandle_t q) {
    if (!q) return;
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->buffer);
    free(q);
}

static inline int xQueueSend(QueueHandle_t q, const void *item, uint32_t timeout_ms) {
    if (!q || !item) return pdFAIL;

    struct timespec ts;
    if (timeout_ms != portMAX_DELAY) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
    }

    pthread_mutex_lock(&q->mutex);

    while (q->count >= q->capacity) {
        if (timeout_ms == portMAX_DELAY) {
            pthread_cond_wait(&q->not_full, &q->mutex);
        } else {
            int ret = pthread_cond_timedwait(&q->not_full, &q->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFAIL;
            }
        }
    }

    /* Copy item to tail */
    char *dst = (char *)q->buffer + (q->tail * q->item_size);
    memcpy(dst, item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);

    return pdPASS;
}

static inline int xQueueReceive(QueueHandle_t q, void *item, uint32_t timeout_ms) {
    if (!q || !item) return pdFAIL;

    struct timespec ts;
    if (timeout_ms != portMAX_DELAY) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
    }

    pthread_mutex_lock(&q->mutex);

    while (q->count == 0) {
        if (timeout_ms == portMAX_DELAY) {
            pthread_cond_wait(&q->not_empty, &q->mutex);
        } else {
            int ret = pthread_cond_timedwait(&q->not_empty, &q->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFAIL;
            }
        }
    }

    /* Copy item from head */
    char *src = (char *)q->buffer + (q->head * q->item_size);
    memcpy(item, src, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);

    return pdPASS;
}

/* ============== Timer API ============== */

/* Microsecond timestamp */
static inline int64_t esp_timer_get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* ============== Random Number Generation ============== */

#include <sys/random.h>

static inline uint32_t esp_random(void) {
    uint32_t val;
    if (getrandom(&val, sizeof(val), 0) == sizeof(val)) {
        return val;
    }
    /* Fallback to random() */
    return (uint32_t)random();
}

/* ============== System Functions ============== */

#include <signal.h>

static inline void esp_restart(void) {
    /* Graceful exit - main() should handle cleanup */
    exit(0);
}

/* ============== NVS Stub Declarations ============== */

/* NVS is replaced by JSON config - see linux_nvs.h for implementation */
typedef int nvs_handle_t;

#define NVS_READONLY  0
#define NVS_READWRITE 1

/* These are implemented in linux_nvs.c */
esp_err_t nvs_open(const char *namespace, int mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *buf, size_t *len);
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *val);
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *val);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *val);
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t val);
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t val);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

/* ============== Filesystem Stub Declarations ============== */
#define esp_vfs_spiffs_conf_t void
#define esp_vfs_spiffs_register(conf) ESP_OK
#define esp_vfs_spiffs_unregister(base) ((void)0)
#define esp_spiffs_info(label, total, used) ((void)0)

/* ============== Event Loop Stub ============== */

#define esp_event_loop_create_default() ESP_OK
#define esp_event_handler_instance_register(...) ESP_OK

/* ============== HTTP Client Types ============== */

/* HTTP client uses libcurl - full definitions in linux_http.h */
#include "linux_http.h"

/* ============== HTTP Server Stub Types ============== */

/* HTTP server is replaced by libwebsockets - see linux_ws.h */
typedef void* httpd_handle_t;
typedef void* httpd_req_t;

#define HTTP_GET    0
#define HTTP_POST   1

#define HTTPD_WS_TYPE_TEXT  1

typedef struct {
    uint8_t *payload;
    size_t len;
    int type;
} httpd_ws_frame_t;

/* ============== TLS Stub Types ============== */

/* TLS is replaced by OpenSSL - see linux_tls.h */
typedef struct esp_tls esp_tls_t;

#define ESP_TLS_CONNECTING  1

/* ============== CRT Bundle Stub ============== */

#define esp_crt_bundle_attach NULL

/* ============== Tick Type ============== */

typedef uint32_t TickType_t;

/* ============== BaseType_t ============== */

typedef int BaseType_t;

/* ============== Timer API (pthread-based) ============== */

/* Forward declaration - struct timer_handle is defined below */
typedef struct timer_handle *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);

typedef struct timer_handle {
    pthread_t thread;
    uint32_t period_ms;
    bool auto_reload;
    bool running;
    TimerCallbackFunction_t callback;
    void *arg;
    pthread_mutex_t mutex;
    pthread_cond_t stop_cond;
} timer_handle_t;

/* Internal timer thread function */
static inline void* timer_thread_func(void *arg) {
    TimerHandle_t timer = (TimerHandle_t)arg;

    while (1) {
        pthread_mutex_lock(&timer->mutex);
        bool should_run = timer->running;
        bool is_auto_reload = timer->auto_reload;
        uint32_t period = timer->period_ms;
        pthread_mutex_unlock(&timer->mutex);

        if (!should_run) {
            break;
        }

        /* Wait for period (or stop signal) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += period / 1000;
        ts.tv_nsec += (period % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }

        pthread_mutex_lock(&timer->mutex);
        int ret = pthread_cond_timedwait(&timer->stop_cond, &timer->mutex, &ts);
        bool still_running = timer->running;
        pthread_mutex_unlock(&timer->mutex);

        if (!still_running) {
            break;
        }

        /* If timed out (not signaled to stop), fire callback */
        if (ret == ETIMEDOUT) {
            if (timer->callback) {
                timer->callback(timer);
            }

            /* If one-shot, exit after firing */
            if (!is_auto_reload) {
                pthread_mutex_lock(&timer->mutex);
                timer->running = false;
                pthread_mutex_unlock(&timer->mutex);
                break;
            }
        }
    }

    return NULL;
}

static inline TimerHandle_t xTimerCreate(const char *pcTimerName,
                                          TickType_t xTimerPeriodInTicks,
                                          BaseType_t xAutoReload,
                                          void *pvTimerID,
                                          TimerCallbackFunction_t pxCallbackFunction) {
    (void)pcTimerName;
    (void)pvTimerID;

    TimerHandle_t timer = calloc(1, sizeof(struct timer_handle));
    if (!timer) return NULL;

    timer->period_ms = xTimerPeriodInTicks;
    timer->auto_reload = (xAutoReload != 0);
    timer->running = false;
    timer->callback = pxCallbackFunction;
    timer->arg = pvTimerID;

    pthread_mutex_init(&timer->mutex, NULL);
    pthread_cond_init(&timer->stop_cond, NULL);

    return timer;
}

static inline BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xBlockTime) {
    (void)xBlockTime;

    if (!xTimer) return pdFAIL;

    pthread_mutex_lock(&xTimer->mutex);

    if (xTimer->running) {
        pthread_mutex_unlock(&xTimer->mutex);
        return pdPASS;  /* Already running */
    }

    xTimer->running = true;
    pthread_mutex_unlock(&xTimer->mutex);

    int ret = pthread_create(&xTimer->thread, NULL, timer_thread_func, xTimer);
    if (ret != 0) {
        pthread_mutex_lock(&xTimer->mutex);
        xTimer->running = false;
        pthread_mutex_unlock(&xTimer->mutex);
        return pdFAIL;
    }

    return pdPASS;
}

static inline BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xBlockTime) {
    (void)xBlockTime;

    if (!xTimer) return pdFAIL;

    pthread_mutex_lock(&xTimer->mutex);
    bool was_running = xTimer->running;
    xTimer->running = false;
    pthread_cond_signal(&xTimer->stop_cond);
    pthread_mutex_unlock(&xTimer->mutex);

    if (was_running) {
        pthread_join(xTimer->thread, NULL);
    }

    return pdPASS;
}

static inline void xTimerDelete(TimerHandle_t xTimer, TickType_t xBlockTime) {
    if (!xTimer) return;

    xTimerStop(xTimer, xBlockTime);

    pthread_mutex_destroy(&xTimer->mutex);
    pthread_cond_destroy(&xTimer->stop_cond);
    free(xTimer);
}

/* ============== Constructor Attribute ============== */

/* GCC constructor attribute is available */
#define __CONSTRUCTOR __attribute__((constructor))
