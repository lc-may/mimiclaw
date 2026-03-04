/**
 * @file main.c
 * @brief Linux entry point for MimiClaw
 *
 * Native Linux entry point for MimiClaw.
 * Installs signal handlers and starts all runtime services.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "linux/linux_compat.h"
#include "linux/linux_paths.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#if MIMI_ENABLE_TELEGRAM
#include "telegram/telegram_bot.h"
#endif
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "gateway/ws_server.h"
#include "cli/serial_cli.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "skills/skill_loader.h"

static const char *TAG = "main";

/* Volatile flag for graceful shutdown */
static volatile int g_running = 1;

/* Forward declarations */
static void signal_handler(int sig);
static void outbound_dispatch_thread(void *arg);

/* Signal handler for graceful shutdown */
static void signal_handler(int sig)
{
    (void)sig;
    printf("\nShutting down...\n");
    g_running = 0;
    serial_cli_stop();
}

/* Outbound dispatch thread: reads from outbound queue and routes to channels */
static void outbound_dispatch_thread(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Outbound dispatch started");

    while (g_running) {
        mimi_msg_t msg;
        if (message_bus_pop_outbound(&msg, 1000) != ESP_OK) {
            continue;  /* Timeout, check g_running and retry */
        }

        ESP_LOGI(TAG, "Dispatching response to %s:%s", msg.channel, msg.chat_id);

        if (strcmp(msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
#if MIMI_ENABLE_TELEGRAM
            esp_err_t send_err = telegram_send_message(msg.chat_id, msg.content);
            if (send_err != ESP_OK) {
                ESP_LOGE(TAG, "Telegram send failed for %s: %s", msg.chat_id, esp_err_to_name(send_err));
            } else {
                ESP_LOGI(TAG, "Telegram send success for %s (%d bytes)", msg.chat_id, (int)strlen(msg.content));
            }
#else
            ESP_LOGW(TAG, "Dropping Telegram outbound message because Telegram is disabled at build time");
#endif
        } else if (strcmp(msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
            esp_err_t ws_err = ws_server_send(msg.chat_id, msg.content);
            if (ws_err != ESP_OK) {
                ESP_LOGW(TAG, "WS send failed for %s: %s", msg.chat_id, esp_err_to_name(ws_err));
            }
        } else if (strcmp(msg.channel, MIMI_CHAN_SYSTEM) == 0) {
            ESP_LOGI(TAG, "System message [%s]: %.128s", msg.chat_id, msg.content);
        } else {
            ESP_LOGW(TAG, "Unknown channel: %s", msg.channel);
        }

        free(msg.content);
    }

    ESP_LOGI(TAG, "Outbound dispatch stopped");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Set up signal handlers for graceful shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Silence noisy components */
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MimiClaw - Linux AI Agent");
    ESP_LOGI(TAG, "========================================");

    /* Print memory info (simulated on Linux) */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Initialize NVS (JSON-based on Linux) */
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Initialize storage directories */
    ESP_ERROR_CHECK(mimi_ensure_data_dirs() == 0 ? ESP_OK : ESP_FAIL);

    /* Initialize subsystems */
    ESP_ERROR_CHECK(message_bus_init());
    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(skill_loader_init());
    ESP_ERROR_CHECK(session_mgr_init());
    ESP_ERROR_CHECK(http_proxy_init());
#if MIMI_ENABLE_TELEGRAM
    ESP_ERROR_CHECK(telegram_bot_init());
#else
    ESP_LOGI(TAG, "Telegram integration disabled");
#endif
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(cron_service_init());
    ESP_ERROR_CHECK(heartbeat_init());
    ESP_ERROR_CHECK(agent_loop_init());

    /* Start Serial CLI */
    ESP_ERROR_CHECK(serial_cli_init());

    /* Start outbound dispatch thread */
    TaskHandle_t outbound_task;
    if (xTaskCreate(outbound_dispatch_thread, "outbound",
                    MIMI_OUTBOUND_STACK, NULL,
                    MIMI_OUTBOUND_PRIO, &outbound_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create outbound dispatch thread");
        return 1;
    }

    /* Start network-dependent services */
    ESP_ERROR_CHECK(agent_loop_start());
#if MIMI_ENABLE_TELEGRAM
    ESP_ERROR_CHECK(telegram_bot_start());
#endif
    cron_service_start();
    heartbeat_start();
    ESP_ERROR_CHECK(ws_server_start());

    ESP_LOGI(TAG, "All services started!");
    ESP_LOGI(TAG, "MimiClaw ready. Type 'help' for CLI commands.");

    /* Run CLI main loop (blocks until exit) */
    serial_cli_run();

    /* Graceful shutdown */
    g_running = 0;
    ESP_LOGI(TAG, "Stopping services...");
    ws_server_stop();
    heartbeat_stop();
    cron_service_stop();

    /* Stop the outbound dispatch thread by setting g_running = 0 (already done) */
    /* The thread will exit on its own after the next timeout */

    ESP_LOGI(TAG, "MimiClaw shutdown complete.");
    return 0;
}
