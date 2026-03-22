/**
 * @file serial_cli.c
 * @brief Linux CLI using stdio
 *
 * Commands are dispatched via a simple string-based lookup table.
 */

#include "serial_cli.h"
#include "mimi_config.h"
#if MIMI_ENABLE_TELEGRAM
#include "telegram/telegram_bot.h"
#endif
#include "llm/llm_proxy.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "tools/tool_web_search.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"
#include "skills/skill_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

#include "linux/linux_compat.h"
#include "linux/linux_paths.h"

static const char *TAG = "cli";

/* Volatile flag for graceful shutdown */
static volatile int g_cli_running = 1;

/* Forward declarations of command handlers */
static int cmd_help(int argc, char **argv);
#if MIMI_ENABLE_TELEGRAM
static int cmd_set_tg_token(int argc, char **argv);
#endif
static int cmd_set_api_url(int argc, char **argv);
static int cmd_set_api_key(int argc, char **argv);
static int cmd_set_model(int argc, char **argv);
static int cmd_set_model_provider(int argc, char **argv);
static int cmd_memory_read(int argc, char **argv);
static int cmd_memory_write(int argc, char **argv);
static int cmd_session_list(int argc, char **argv);
static int cmd_session_clear(int argc, char **argv);
static int cmd_heap_info(int argc, char **argv);
static int cmd_set_proxy(int argc, char **argv);
static int cmd_clear_proxy(int argc, char **argv);
static int cmd_set_search_key(int argc, char **argv);
static int cmd_config_show(int argc, char **argv);
static int cmd_config_reset(int argc, char **argv);
static int cmd_heartbeat_trigger(int argc, char **argv);
static int cmd_cron_start(int argc, char **argv);
static int cmd_tool_exec(int argc, char **argv);
static int cmd_skill_list(int argc, char **argv);
static int cmd_skill_show(int argc, char **argv);
static int cmd_skill_search(int argc, char **argv);
static int cmd_exit(int argc, char **argv);

/* Command table entry */
typedef struct {
    const char *name;
    const char *help;
    int (*handler)(int argc, char **argv);
    int min_args;  /* Minimum arguments required (excluding command name) */
} cli_cmd_t;

/* Command table - sorted alphabetically */
static const cli_cmd_t g_commands[] = {
    { "config_reset",       "Clear all NVS overrides, revert to build-time defaults", cmd_config_reset, 0 },
    { "config_show",        "Show current configuration (build-time + NVS)", cmd_config_show, 0 },
    { "cron_start",         "Start cron scheduler timer now", cmd_cron_start, 0 },
    { "exit",               "Exit the CLI (Ctrl+D also works)", cmd_exit, 0 },
    { "heap_info",          "Show heap memory usage", cmd_heap_info, 0 },
    { "heartbeat_trigger",  "Manually trigger a heartbeat check", cmd_heartbeat_trigger, 0 },
    { "help",               "Show this help message", cmd_help, 0 },
    { "memory_read",        "Read MEMORY.md", cmd_memory_read, 0 },
    { "memory_write",       "Write to MEMORY.md", cmd_memory_write, 1 },
    { "session_clear",      "Clear a session", cmd_session_clear, 1 },
    { "session_list",       "List all sessions", cmd_session_list, 0 },
    { "set_api_url",        "Set LLM API URL override", cmd_set_api_url, 1 },
    { "set_api_key",        "Set LLM API key", cmd_set_api_key, 1 },
    { "set_model",          "Set LLM model (default: " MIMI_LLM_DEFAULT_MODEL ")", cmd_set_model, 1 },
    { "set_model_provider", "Set LLM model provider (anthropic|openai)", cmd_set_model_provider, 1 },
    { "set_proxy",          "Set proxy (e.g. set_proxy 192.168.1.83 7897 [http|socks5])", cmd_set_proxy, 2 },
    { "set_search_key",     "Set Brave Search API key for web_search tool", cmd_set_search_key, 1 },
#if MIMI_ENABLE_TELEGRAM
    { "set_tg_token",       "Set Telegram bot token", cmd_set_tg_token, 1 },
#endif
    { "skill_list",         "List installed skills from " MIMI_SKILLS_PREFIX, cmd_skill_list, 0 },
    { "skill_search",       "Search skill files by keyword (filename + content)", cmd_skill_search, 1 },
    { "skill_show",         "Print full content of one skill file", cmd_skill_show, 1 },
    { "tool_exec",          "Execute a registered tool: tool_exec <name> '{...json...}'", cmd_tool_exec, 1 },
    { "clear_proxy",        "Remove proxy configuration", cmd_clear_proxy, 0 },
};
static const int g_num_commands = sizeof(g_commands) / sizeof(g_commands[0]);

/* ============== Utility Functions ============== */

/* Trim leading and trailing whitespace in place */
static char *trim(char *str)
{
    if (!str) return NULL;

    /* Leading */
    while (*str && isspace((unsigned char)*str)) str++;

    if (*str == '\0') return str;

    /* Trailing */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    return str;
}

/* Split a line into argc/argv (max 16 args) */
static int split_args(char *line, char **argv, int max_argv)
{
    if (!line || !argv || max_argv < 1) return 0;

    int argc = 0;
    char *p = line;
    bool in_quotes = false;
    char *token_start = NULL;

    while (*p && argc < max_argv - 1) {
        /* Skip leading whitespace unless in quotes */
        while (*p && !in_quotes && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        token_start = p;
        char *write_ptr = p;

        while (*p) {
            if (*p == '"' && (p == token_start || *(p-1) != '\\')) {
                in_quotes = !in_quotes;
                p++;  /* Skip the quote character */
                continue;
            }
            if (!in_quotes && isspace((unsigned char)*p)) {
                break;
            }
            /* Handle escaped characters */
            if (*p == '\\' && *(p+1)) {
                p++;
                *write_ptr++ = *p++;
            } else {
                *write_ptr++ = *p++;
            }
        }

        *write_ptr = '\0';
        if (*token_start) {
            argv[argc++] = token_start;
        }

        if (*p) p++;  /* Skip the space */
    }

    argv[argc] = NULL;
    return argc;
}

/* Find command by name */
static const cli_cmd_t *find_command(const char *name)
{
    for (int i = 0; i < g_num_commands; i++) {
        if (strcmp(g_commands[i].name, name) == 0) {
            return &g_commands[i];
        }
    }
    return NULL;
}

/* ============== Command Handlers ============== */

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Available commands:\n");
    for (int i = 0; i < g_num_commands; i++) {
        printf("  %-20s %s\n", g_commands[i].name, g_commands[i].help);
    }
    printf("\nType 'exit' or Ctrl+D to quit.\n");
    return 0;
}

static int cmd_exit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Goodbye!\n");
    g_cli_running = 0;
    return 0;
}

#if MIMI_ENABLE_TELEGRAM
static int cmd_set_tg_token(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: set_tg_token <token>\n");
        return 1;
    }
    telegram_set_token(argv[1]);
    printf("Telegram bot token saved.\n");
    return 0;
}
#endif

static int cmd_set_api_url(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: set_api_url <url>\n");
        return 1;
    }
    llm_set_api_url(argv[1]);
    printf("API URL override saved.\n");
    return 0;
}

static int cmd_set_api_key(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: set_api_key <key>\n");
        return 1;
    }
    llm_set_api_key(argv[1]);
    printf("API key saved.\n");
    return 0;
}

static int cmd_set_model(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: set_model <model>\n");
        return 1;
    }
    llm_set_model(argv[1]);
    printf("Model set.\n");
    return 0;
}

static int cmd_set_model_provider(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: set_model_provider <anthropic|openai>\n");
        return 1;
    }
    llm_set_provider(argv[1]);
    printf("Model provider set.\n");
    return 0;
}

static int cmd_memory_read(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *buf = malloc(4096);
    if (!buf) {
        printf("Out of memory.\n");
        return 1;
    }
    if (memory_read_long_term(buf, 4096) == ESP_OK && buf[0]) {
        printf("=== MEMORY.md ===\n%s\n=================\n", buf);
    } else {
        printf("MEMORY.md is empty or not found.\n");
    }
    free(buf);
    return 0;
}

static int cmd_memory_write(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: memory_write <content>\n");
        return 1;
    }
    memory_write_long_term(argv[1]);
    printf("MEMORY.md updated.\n");
    return 0;
}

static int cmd_session_list(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Sessions:\n");
    session_list();
    return 0;
}

static int cmd_session_clear(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: session_clear <chat_id>\n");
        return 1;
    }
    if (session_clear(argv[1]) == ESP_OK) {
        printf("Session cleared.\n");
    } else {
        printf("Session not found.\n");
    }
    return 0;
}

static int cmd_heap_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Internal free: %d bytes\n",
           (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("PSRAM free:    %d bytes\n",
           (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("Total free:    %d bytes\n",
           (int)esp_get_free_heap_size());
    return 0;
}

static int cmd_set_proxy(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: set_proxy <host> <port> [http|socks5]\n");
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *type = (argc >= 4) ? argv[3] : "http";

    if (port <= 0 || port > 65535) {
        printf("Invalid port number.\n");
        return 1;
    }

    if (strcmp(type, "http") != 0 && strcmp(type, "socks5") != 0) {
        printf("Invalid proxy type: %s. Use http or socks5.\n", type);
        return 1;
    }

    http_proxy_set(host, (uint16_t)port, type);
    printf("Proxy set. Restart to apply.\n");
    return 0;
}

static int cmd_clear_proxy(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    http_proxy_clear();
    printf("Proxy cleared. Restart to apply.\n");
    return 0;
}

static int cmd_set_search_key(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: set_search_key <key>\n");
        return 1;
    }
    tool_web_search_set_key(argv[1]);
    printf("Search API key saved.\n");
    return 0;
}

/* config_show helper */
static void print_config(const char *label, const char *ns, const char *key,
                         const char *build_val, bool mask)
{
    char nvs_val[128] = {0};
    const char *source = "not set";
    const char *display = "(empty)";

    /* NVS takes highest priority */
    nvs_handle_t nvs;
    if (nvs_open(ns, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(nvs_val);
        if (nvs_get_str(nvs, key, nvs_val, &len) == ESP_OK && nvs_val[0]) {
            source = "NVS";
            display = nvs_val;
        }
        nvs_close(nvs);
    }

    /* Fall back to build-time value */
    if (strcmp(source, "not set") == 0 && build_val[0] != '\0') {
        source = "build";
        display = build_val;
    }

    if (mask && strlen(display) > 6 && strcmp(display, "(empty)") != 0) {
        printf("  %-14s: %.4s****  [%s]\n", label, display, source);
    } else {
        printf("  %-14s: %s  [%s]\n", label, display, source);
    }
}

static int cmd_config_show(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("=== Current Configuration ===\n");
#if MIMI_ENABLE_TELEGRAM
    print_config("TG Token",   MIMI_NVS_TG,     MIMI_NVS_KEY_TG_TOKEN, MIMI_SECRET_TG_TOKEN,   true);
#else
    printf("  %-14s: disabled  [build]\n", "Telegram");
#endif
    print_config("API URL",    MIMI_NVS_LLM,    MIMI_NVS_KEY_API_URL,  MIMI_SECRET_API_URL,    false);
    print_config("API Key",    MIMI_NVS_LLM,    MIMI_NVS_KEY_API_KEY,  MIMI_SECRET_API_KEY,    true);
    print_config("Model",      MIMI_NVS_LLM,    MIMI_NVS_KEY_MODEL,    MIMI_SECRET_MODEL,      false);
    print_config("Provider",   MIMI_NVS_LLM,    MIMI_NVS_KEY_PROVIDER, MIMI_SECRET_MODEL_PROVIDER, false);
    print_config("Proxy Host", MIMI_NVS_PROXY,  MIMI_NVS_KEY_PROXY_HOST, MIMI_SECRET_PROXY_HOST, false);
    print_config("Proxy Port", MIMI_NVS_PROXY,  MIMI_NVS_KEY_PROXY_PORT, MIMI_SECRET_PROXY_PORT, false);
    print_config("Search Key", MIMI_NVS_SEARCH, MIMI_NVS_KEY_API_KEY,  MIMI_SECRET_SEARCH_KEY, true);
    printf("=============================\n");
    return 0;
}

static int cmd_config_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const char *namespaces[] = {
#if MIMI_ENABLE_TELEGRAM
        MIMI_NVS_TG,
#endif
        MIMI_NVS_LLM,
        MIMI_NVS_PROXY,
        MIMI_NVS_SEARCH
    };
    for (size_t i = 0; i < (sizeof(namespaces) / sizeof(namespaces[0])); i++) {
        nvs_handle_t nvs;
        if (nvs_open(namespaces[i], NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    printf("All NVS config cleared. Build-time defaults will be used on restart.\n");
    return 0;
}

static int cmd_heartbeat_trigger(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Checking HEARTBEAT.md...\n");
    if (heartbeat_trigger()) {
        printf("Heartbeat: agent prompted with pending tasks.\n");
    } else {
        printf("Heartbeat: no actionable tasks found.\n");
    }
    return 0;
}

static int cmd_cron_start(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t err = cron_service_start();
    if (err == ESP_OK) {
        printf("Cron service started.\n");
        return 0;
    }

    printf("Failed to start cron service: %s\n", esp_err_to_name(err));
    return 1;
}

static int cmd_tool_exec(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: tool_exec <name> [json]\n");
        return 1;
    }

    const char *tool_name = argv[1];
    const char *input_json = (argc >= 3) ? argv[2] : "{}";

    enum { TOOL_EXEC_BUF = 64 * 1024 };
    char *output = calloc(1, TOOL_EXEC_BUF);
    if (!output) {
        printf("Out of memory.\n");
        return 1;
    }

    esp_err_t err = tool_registry_execute(tool_name, input_json, output, TOOL_EXEC_BUF);
    printf("tool_exec status: %s\n", esp_err_to_name(err));
    printf("%s\n", output[0] ? output : "(empty)");
    free(output);
    return (err == ESP_OK) ? 0 : 1;
}

static bool has_md_suffix(const char *name)
{
    size_t len = strlen(name);
    return (len >= 3) && strcmp(name + len - 3, ".md") == 0;
}

static bool build_skill_path(const char *name, char *out, size_t out_size)
{
    if (!name || !name[0]) return false;
    if (strstr(name, "..") != NULL) return false;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) return false;

    char rel_path[128];
    if (has_md_suffix(name)) {
        snprintf(rel_path, sizeof(rel_path), "skills/%s", name);
    } else {
        snprintf(rel_path, sizeof(rel_path), "skills/%s.md", name);
    }
    return mimi_get_full_path(rel_path, out, out_size) == 0;
}

static int cmd_skill_list(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *buf = malloc(4096);
    if (!buf) {
        printf("Out of memory.\n");
        return 1;
    }

    size_t n = skill_loader_build_summary(buf, 4096);
    if (n == 0) {
        printf("No skills found under " MIMI_SKILLS_PREFIX ".\n");
    } else {
        printf("=== Skills ===\n%s", buf);
    }
    free(buf);
    return 0;
}

static int cmd_skill_show(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: skill_show <name>\n");
        return 1;
    }

    char path[128];
    if (!build_skill_path(argv[1], path, sizeof(path))) {
        printf("Invalid skill name.\n");
        return 1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("Skill not found: %s\n", path);
        return 1;
    }

    printf("=== %s ===\n", path);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        fputs(line, stdout);
    }
    fclose(f);
    printf("\n============\n");
    return 0;
}

static bool contains_nocase(const char *text, const char *keyword)
{
    if (!text || !keyword || !keyword[0]) return false;

    size_t key_len = strlen(keyword);
    for (const char *p = text; *p; p++) {
        size_t i = 0;
        while (i < key_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)keyword[i])) {
            i++;
        }
        if (i == key_len) return true;
    }
    return false;
}

static int cmd_skill_search(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: skill_search <keyword>\n");
        return 1;
    }

    const char *keyword = argv[1];
    char skills_dir[256];
    if (mimi_get_full_path("skills", skills_dir, sizeof(skills_dir)) != 0) {
        printf("Cannot resolve skills directory.\n");
        return 1;
    }

    DIR *dir = opendir(skills_dir);
    if (!dir) {
        printf("Cannot open %s.\n", skills_dir);
        return 1;
    }

    int matches = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t name_len = strlen(name);

        if (name_len < 4) continue;
        if (strcmp(name + name_len - 3, ".md") != 0) continue;

        char full_path[296];
        snprintf(full_path, sizeof(full_path), "%s/%s", skills_dir, name);

        bool file_matched = contains_nocase(name, keyword);
        int matched_line = 0;

        FILE *f = fopen(full_path, "r");
        if (!f) continue;

        char line[256];
        int line_no = 0;
        while (!file_matched && fgets(line, sizeof(line), f)) {
            line_no++;
            if (contains_nocase(line, keyword)) {
                file_matched = true;
                matched_line = line_no;
            }
        }
        fclose(f);

        if (file_matched) {
            matches++;
            if (matched_line > 0) {
                printf("- %s (matched at line %d)\n", full_path, matched_line);
            } else {
                printf("- %s (matched in filename)\n", full_path);
            }
        }
    }

    closedir(dir);
    if (matches == 0) {
        printf("No skills matched keyword: %s\n", keyword);
    } else {
        printf("Total matches: %d\n", matches);
    }
    return 0;
}

/* ============== Public API ============== */

esp_err_t serial_cli_init(void)
{
    ESP_LOGI(TAG, "Serial CLI started");
    return ESP_OK;
}

int serial_cli_run(void)
{
    char *line = NULL;
    size_t cap = 0;

    while (g_cli_running) {
        fputs("mimi> ", stdout);
        fflush(stdout);

        ssize_t line_len = getline(&line, &cap, stdin);
        if (line_len < 0) {
            break;
        }
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
        }

        char *trimmed = trim(line);

        if (trimmed && *trimmed) {
            /* Parse and execute */
            char *argv[16];
            int argc = split_args(line, argv, 16);

            if (argc > 0) {
                const cli_cmd_t *cmd = find_command(argv[0]);
                if (cmd) {
                    if (argc - 1 >= cmd->min_args) {
                        cmd->handler(argc, argv);
                    } else {
                        printf("Error: %s requires %d argument(s)\n", cmd->name, cmd->min_args);
                        printf("Usage: %s\n", cmd->help);
                    }
                } else {
                    printf("Unknown command: %s. Type 'help' for available commands.\n", argv[0]);
                }
            }
        }

    }

    /* getline returned NULL (EOF) or exit command was called */
    if (g_cli_running) {
        printf("\n");  /* newline after Ctrl+D */
    }

    free(line);
    return 0;
}

/* Signal handler for graceful shutdown */
void serial_cli_stop(void)
{
    g_cli_running = 0;
}
