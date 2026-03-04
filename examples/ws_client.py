#!/usr/bin/env python3
"""
Minimal WebSocket client for MimiClaw.

Examples:
  python3 examples/ws_client.py
  python3 examples/ws_client.py --message "Hello"
  python3 examples/ws_client.py --url ws://127.0.0.1:18789 --chat-id demo

Dependency:
  pip install websockets
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from typing import Optional

try:
    import websockets
    from websockets.exceptions import ConnectionClosed
except ImportError:  # pragma: no cover - runtime guidance
    print("Missing dependency: websockets", file=sys.stderr)
    print("Install it with: pip install websockets", file=sys.stderr)
    raise SystemExit(1)


class ClientState:
    def __init__(self, chat_id: Optional[str]) -> None:
        self.chat_id = chat_id
        self.stop = False


async def recv_loop(
    websocket: websockets.WebSocketClientProtocol,
    state: ClientState,
) -> None:
    try:
        async for raw in websocket:
            try:
                payload = json.loads(raw)
            except json.JSONDecodeError:
                print(f"\n[raw] {raw}")
                continue

            msg_type = payload.get("type", "")
            chat_id = payload.get("chat_id")
            content = payload.get("content", "")

            if chat_id and not state.chat_id:
                state.chat_id = chat_id
                print(f"\n[info] assigned chat_id={chat_id}")

            if msg_type == "response":
                print(f"\n[mimiclaw:{chat_id or '-'}] {content}")
            else:
                print(f"\n[{msg_type or 'message'}] {json.dumps(payload, ensure_ascii=False)}")
    except ConnectionClosed:
        if not state.stop:
            print("\n[info] connection closed by server")


async def send_message(
    websocket: websockets.WebSocketClientProtocol,
    state: ClientState,
    content: str,
) -> None:
    payload = {
        "type": "message",
        "content": content,
    }
    if state.chat_id:
        payload["chat_id"] = state.chat_id

    await websocket.send(json.dumps(payload, ensure_ascii=False))
    print(f"[you:{state.chat_id or 'auto'}] {content}")


async def interactive_loop(
    websocket: websockets.WebSocketClientProtocol,
    state: ClientState,
) -> None:
    while True:
        try:
            line = await asyncio.to_thread(input, "you> ")
        except EOFError:
            break
        except KeyboardInterrupt:
            print()
            break

        line = line.strip()
        if not line:
            continue
        if line in {"exit", "quit"}:
            break

        await send_message(websocket, state, line)


async def run(args: argparse.Namespace) -> int:
    state = ClientState(chat_id=args.chat_id)

    async with websockets.connect(args.url) as websocket:
        print(f"[info] connected to {args.url}")
        if state.chat_id:
            print(f"[info] using chat_id={state.chat_id}")

        receiver = asyncio.create_task(recv_loop(websocket, state))

        try:
            if args.message:
                await send_message(websocket, state, args.message)
                await asyncio.sleep(args.wait)
            else:
                await interactive_loop(websocket, state)
        finally:
            state.stop = True
            receiver.cancel()
            try:
                await receiver
            except asyncio.CancelledError:
                pass

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Minimal MimiClaw WebSocket client")
    parser.add_argument(
        "--url",
        default="ws://127.0.0.1:18789",
        help="MimiClaw WebSocket URL",
    )
    parser.add_argument(
        "--chat-id",
        default=None,
        help="Optional chat/session identifier. If omitted, MimiClaw assigns one.",
    )
    parser.add_argument(
        "--message",
        default=None,
        help="Send one message and exit after --wait seconds.",
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=5.0,
        help="How long to wait for responses in --message mode.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130
    except OSError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
