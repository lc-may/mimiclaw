# MimiClaw TODO

This backlog tracks Linux / WSL improvements for the native MimiClaw runtime.

## High Priority

- Add an allowlist for Telegram user IDs to avoid exposing the bot to arbitrary senders.
- Let the agent persist durable memories through tool use instead of relying on CLI-only memory writes.
- Improve Telegram formatting so Markdown-heavy answers do not fail delivery.
- Add startup validation for required secrets and emit actionable errors before background services start.

## Medium Priority

- Expand built-in tools beyond file access, web search, time, and cron.
- Support richer Telegram inputs such as images, voice, and documents.
- Add more provider coverage behind the existing provider switch.
- Improve WebSocket protocol ergonomics for local clients.
- Add tests for path resolution, session persistence, and cron file handling.

## Low Priority

- Add structured session metadata instead of plain JSONL-only message history.
- Add export / backup helpers for `~/.mimiclaw/`.
- Improve CLI discoverability and help text.
