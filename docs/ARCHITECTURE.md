# MimiClaw Architecture

MimiClaw is a native Linux / WSL service written in C. It runs as a single local process, persists state under `~/.mimiclaw/`, and communicates through Telegram, a local CLI, and an optional WebSocket gateway.

## Runtime Model

Main subsystems:

- `telegram/`: long-poll Telegram updates and send replies
- `gateway/`: optional WebSocket server for local clients
- `agent/`: build context, call the LLM, execute tools, assemble final reply
- `tools/`: built-in tools such as `web_search`, `get_current_time`, file access, and cron helpers
- `memory/`: persistent long-term memory and per-session history on disk
- `cron/` and `heartbeat/`: background triggers that enqueue internal messages
- `cli/`: stdin-based maintenance console for runtime configuration

## Data Flow

1. An inbound message arrives from Telegram, WebSocket, cron, heartbeat, or CLI.
2. `message_bus` pushes it into the inbound queue.
3. `agent_loop` loads prompt context from files under `~/.mimiclaw/`.
4. `llm_proxy` calls the configured provider API.
5. If the model requests tools, `tool_registry` dispatches them and the loop continues.
6. The final answer is pushed to the outbound queue.
7. `main.c` dispatches the result back to Telegram and/or WebSocket.

## Persistent Storage

Runtime data lives under `~/.mimiclaw/`:

- `config/SOUL.md`
- `config/USER.md`
- `memory/MEMORY.md`
- `memory/YYYY-MM-DD.md`
- `sessions/<chat_id>.jsonl`
- `skills/*.md`
- `cron.json`
- `HEARTBEAT.md`
- `nvs.json`

The Linux port resolves these logical paths through `main/linux/linux_paths.c`, so source code can use stable project-local paths while the actual files live in the user's home directory.

## Build Layout

The repository now has a single native build path:

- root `CMakeLists.txt`
- Linux compatibility shims in `main/linux/`
- executable entry point in `main/main.c`

There is no ESP-IDF component manifest, partition table, OTA slot layout, or board-specific boot path in this repository anymore.

## Compatibility Layer

`main/linux/linux_compat.h` provides small FreeRTOS / ESP-style API shims so the higher-level agent code can stay relatively unchanged. Those identifiers are an internal compatibility detail for the Linux build, not a separate firmware target.
