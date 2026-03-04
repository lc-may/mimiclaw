# MimiClaw

MimiClaw is a native Linux / WSL AI assistant written in C. It runs as a local process, talks through WebSocket by default, keeps memory on disk, and exposes a small CLI for runtime configuration.

## Quick Start

Create `main/mimi_secrets.h` first, because these are build-time macros:

```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

Then build:

Requirements:

- CMake >= 3.16
- a C compiler
- `libcurl`
- `cJSON`
- optional: `libwebsockets` for the WebSocket gateway

Ubuntu / WSL:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libcjson-dev libwebsockets-dev

cmake -S . -B build
cmake --build build -j
```

Set at least:

```c
#define MIMI_ENABLE_TELEGRAM        0
#define MIMI_SECRET_TG_TOKEN        ""
#define MIMI_SECRET_API_URL         ""
#define MIMI_SECRET_API_KEY         ""
#define MIMI_SECRET_MODEL           ""
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"
#define MIMI_SECRET_PROXY_HOST      ""
#define MIMI_SECRET_PROXY_PORT      ""
#define MIMI_SECRET_PROXY_TYPE      ""
#define MIMI_SECRET_SEARCH_KEY      ""
```

Run:

```bash
./build/mimiclaw
```

## WebSocket Protocol

The Linux build starts a WebSocket server on port `18789` by default.

Client to MimiClaw:

```json
{"type":"message","chat_id":"demo","content":"Hello"}
```

MimiClaw to client:

```json
{"type":"response","chat_id":"demo","content":"..."}
```

If `chat_id` is omitted on the first client message, the server assigns one like `ws_0`.

Minimal Python client example:

```bash
pip install websockets
python3 examples/ws_client.py
python3 examples/ws_client.py --message "Hello"
```

Runtime data is stored under `~/.mimiclaw/`:

- `config/`
- `memory/`
- `sessions/`
- `skills/`
- `cron.json`
- `HEARTBEAT.md`

## CLI

Common commands:

```text
mimi> help
mimi> set_api_url https://api.openai.com/v1/chat/completions
mimi> set_api_key sk-...
mimi> set_model_provider openai
mimi> set_model gpt-4o
mimi> set_search_key ...
mimi> config_show
mimi> config_reset
mimi> memory_read
mimi> memory_write "content"
mimi> session_list
mimi> session_clear 12345
mimi> heartbeat_trigger
mimi> cron_start
mimi> tool_exec get_current_time
```

## Notes

- The ESP32 / ESP-IDF firmware path has been removed from this repository.
- Telegram is disabled by default. To enable it, set `#define MIMI_ENABLE_TELEGRAM 1` in `main/mimi_secrets.h`.
- LLM support currently uses Anthropic Messages API or OpenAI Chat Completions style APIs. `MIMI_SECRET_API_URL` may be either a full endpoint or a provider base URL. For example, Anthropic-compatible providers can use a base like `https://.../api/anthropic`, and MimiClaw will append `/v1/messages`.
- Proxy settings are currently stored but ignored in the Linux build.
