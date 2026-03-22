#!/usr/bin/env python3
"""
Render ~/.mimiclaw/logs/agent_trace.jsonl (or a given path) as a readable HTML report.
No third-party dependencies (stdlib only).
"""

from __future__ import annotations

import argparse
import html
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


DEFAULT_REL = ".mimiclaw/logs/agent_trace.jsonl"


def default_input_path() -> Path:
    home = Path(os.environ.get("HOME", ""))
    return home / DEFAULT_REL


def load_records_from_lines(lines: list[str]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line_no, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as e:
            records.append(
                {
                    "event": "parse_error",
                    "line_no": line_no,
                    "error": str(e),
                    "raw": line[:2000],
                }
            )
    return records


def load_records(path: Path) -> list[dict[str, Any]]:
    if str(path) == "-":
        text = sys.stdin.read()
        return load_records_from_lines(text.splitlines())
    with path.open("r", encoding="utf-8", errors="replace") as f:
        return load_records_from_lines(f.readlines())


def pretty_json_maybe(s: str | None) -> str:
    if not s:
        return ""
    try:
        return json.dumps(json.loads(s), ensure_ascii=False, indent=2)
    except (json.JSONDecodeError, TypeError):
        return s


def esc(s: Any) -> str:
    if s is None:
        return ""
    return html.escape(str(s), quote=True)


def event_class(ev: str) -> str:
    return {
        "turn_start": "ev-turn_start",
        "llm_iteration": "ev-llm_iteration",
        "llm_response": "ev-llm_response",
        "tool_call": "ev-tool_call",
        "turn_end": "ev-turn_end",
        "error": "ev-error",
        "parse_error": "ev-error",
    }.get(ev, "ev-default")


def block_pre(label: str, content: Any) -> str:
    if content is None:
        text = ""
    elif isinstance(content, str):
        text = content
    else:
        text = json.dumps(content, ensure_ascii=False, indent=2)

    if label in ("messages_json", "input_json", "raw"):
        pretty = pretty_json_maybe(text)
    else:
        pretty = text
    return (
        f'<div class="block"><div class="block-label">{esc(label)}</div>'
        f'<pre class="content">{esc(pretty)}</pre></div>'
    )


def block_pre_json(pretty: str) -> str:
    return f'<pre class="content json">{esc(pretty)}</pre>'


def render_record(rec: dict[str, Any], idx: int) -> str:
    ev = rec.get("event", "unknown")
    cls = event_class(str(ev))
    ts = esc(rec.get("ts", ""))
    ts_ms = esc(rec.get("ts_ms", ""))
    channel = esc(rec.get("channel", ""))
    chat_id = esc(rec.get("chat_id", ""))
    iteration = rec.get("iteration", "")
    iter_s = esc(iteration) if iteration != "" else ""

    parts: list[str] = [
        f'<article class="card {cls}" id="e{idx}">',
        '<header class="card-hd">',
        f'<span class="badge {cls}">{esc(ev)}</span>',
        f'<span class="meta">{ts}</span>',
    ]
    if ts_ms:
        parts.append(f'<span class="meta dim">ts_ms={ts_ms}</span>')
    parts.append("</header>")
    parts.append('<div class="card-sub">')
    parts.append(f'<span class="pill">{channel}</span>')
    parts.append(f'<span class="pill dim">{chat_id}</span>')
    if iter_s:
        parts.append(f'<span class="pill iter">iter {iter_s}</span>')
    parts.append("</div>")
    parts.append('<div class="card-body">')

    if ev == "turn_start":
        parts.append(block_pre("user_text", rec.get("user_text")))

    elif ev == "llm_iteration":
        parts.append(f'<p class="kv"><strong>iteration</strong> {iter_s}</p>')
        parts.append(block_pre("system_prompt", rec.get("system_prompt")))
        mal = rec.get("messages_array_len")
        if mal is not None:
            parts.append(f'<p class="kv"><strong>messages_array_len</strong> {esc(mal)}</p>')
        if rec.get("messages_json_truncated"):
            parts.append('<p class="warn">messages_json was truncated at source</p>')
        mj = rec.get("messages_json")
        if mj:
            parts.append(
                '<details open><summary>messages_json</summary>'
                + block_pre_json(pretty_json_maybe(str(mj)))
                + "</details>"
            )

    elif ev == "llm_response":
        parts.append(f'<p class="kv"><strong>tool_use</strong> {esc(rec.get("tool_use"))}</p>')
        parts.append(block_pre("text", rec.get("text")))
        calls = rec.get("calls")
        if isinstance(calls, list) and calls:
            parts.append("<p><strong>calls</strong></p>")
            parts.append('<div class="calls">')
            for i, c in enumerate(calls):
                if not isinstance(c, dict):
                    parts.append(f"<pre>{esc(c)}</pre>")
                    continue
                parts.append('<div class="call-item">')
                parts.append(
                    f'<div class="call-title">#{i+1} <code>{esc(c.get("name"))}</code> '
                    f'<span class="dim">{esc(c.get("id"))}</span></div>'
                )
                inp = c.get("input")
                parts.append(
                    '<details><summary>input</summary>'
                    + block_pre_json(pretty_json_maybe(str(inp) if inp is not None else ""))
                    + "</details>"
                )
                parts.append("</div>")
            parts.append("</div>")

    elif ev == "tool_call":
        parts.append(f'<p class="kv"><strong>name</strong> <code>{esc(rec.get("name"))}</code></p>')
        parts.append(
            '<details><summary>input_json</summary>'
            + block_pre_json(pretty_json_maybe(str(rec.get("input_json") or "")))
            + "</details>"
        )
        parts.append(
            '<details open><summary>output</summary>'
            + block_pre("output", rec.get("output"))
            + "</details>"
        )

    elif ev == "turn_end":
        parts.append(block_pre("final_text", rec.get("final_text")))

    elif ev == "error":
        parts.append(f'<p class="kv"><strong>phase</strong> {esc(rec.get("phase"))}</p>')
        if rec.get("phase") == "llm_chat_tools":
            parts.append(f'<p class="kv"><strong>esp_err</strong> {esc(rec.get("esp_err"))}</p>')
            parts.append(block_pre("llm_error", rec.get("llm_error")))
        else:
            parts.append(block_pre("message", rec.get("message")))

    elif ev == "parse_error":
        parts.append(
            f'<p class="kv parse-err"><strong>line {esc(rec.get("line_no"))}</strong>: {esc(rec.get("error"))}</p>'
        )
        parts.append(block_pre("raw", rec.get("raw")))

    else:
        parts.append(block_pre_json(json.dumps(rec, ensure_ascii=False, indent=2)))

    parts.append("</div></article>")
    return "\n".join(parts)


def build_html(records: list[dict[str, Any]], title: str, source_path: str) -> str:
    body = "\n".join(render_record(r, i) for i, r in enumerate(records))
    nav = "".join(
        f'<a href="#e{i}" class="nav-dot {event_class(str(r.get("event", "")))}" title="{esc(r.get("event"))}"></a>'
        for i, r in enumerate(records)
    )
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>{esc(title)}</title>
<link rel="preconnect" href="https://fonts.googleapis.com"/>
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin/>
<link href="https://fonts.googleapis.com/css2?family=DM+Sans:ital,opsz,wght@0,9..40,400;0,9..40,600;1,9..40,400&family=IBM+Plex+Mono:wght@400;500&display=swap" rel="stylesheet"/>
<style>
:root {{
  --bg: #0f1218;
  --surface: #171b24;
  --border: #2a3142;
  --text: #e8ecf4;
  --muted: #8b95a8;
  --accent: #6ee7b7;
  --turn: #34d399;
  --iter: #60a5fa;
  --resp: #a78bfa;
  --tool: #fbbf24;
  --end: #2dd4bf;
  --err: #f87171;
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  font-family: "DM Sans", system-ui, sans-serif;
  background: radial-gradient(1200px 800px at 10% -10%, #1a2234 0%, var(--bg) 55%);
  color: var(--text);
  line-height: 1.45;
  min-height: 100vh;
}}
.wrap {{
  max-width: 960px;
  margin: 0 auto;
  padding: 28px 20px 80px;
}}
h1 {{
  font-size: 1.35rem;
  font-weight: 600;
  margin: 0 0 6px;
  letter-spacing: -0.02em;
}}
.sub {{
  color: var(--muted);
  font-size: 0.9rem;
  margin-bottom: 22px;
  word-break: break-all;
}}
.timeline {{
  position: relative;
  padding-left: 18px;
  border-left: 2px solid var(--border);
}}
.card {{
  position: relative;
  margin-bottom: 18px;
  padding: 14px 16px 16px;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 12px;
  box-shadow: 0 8px 24px rgba(0,0,0,.25);
}}
.card::before {{
  content: "";
  position: absolute;
  left: -21px;
  top: 18px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--muted);
  border: 2px solid var(--bg);
}}
.ev-turn_start::before {{ background: var(--turn); }}
.ev-llm_iteration::before {{ background: var(--iter); }}
.ev-llm_response::before {{ background: var(--resp); }}
.ev-tool_call::before {{ background: var(--tool); }}
.ev-turn_end::before {{ background: var(--end); }}
.ev-error::before {{ background: var(--err); }}
.card-hd {{
  display: flex;
  flex-wrap: wrap;
  align-items: baseline;
  gap: 10px 14px;
  margin-bottom: 8px;
}}
.badge {{
  display: inline-block;
  padding: 3px 10px;
  border-radius: 999px;
  font-size: 0.78rem;
  font-weight: 600;
  letter-spacing: 0.02em;
  border: 1px solid var(--border);
  background: rgba(255,255,255,.04);
}}
.ev-turn_start .badge {{ color: var(--turn); border-color: rgba(52,211,153,.35); }}
.ev-llm_iteration .badge {{ color: var(--iter); border-color: rgba(96,165,250,.35); }}
.ev-llm_response .badge {{ color: var(--resp); border-color: rgba(167,139,250,.35); }}
.ev-tool_call .badge {{ color: var(--tool); border-color: rgba(251,191,36,.35); }}
.ev-turn_end .badge {{ color: var(--end); border-color: rgba(45,212,191,.35); }}
.ev-error .badge {{ color: var(--err); border-color: rgba(248,113,113,.35); }}
.meta {{ font-size: 0.82rem; color: var(--muted); font-family: "IBM Plex Mono", monospace; }}
.meta.dim {{ opacity: 0.85; }}
.card-sub {{
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 12px;
}}
.pill {{
  font-size: 0.78rem;
  padding: 2px 8px;
  border-radius: 6px;
  background: rgba(255,255,255,.06);
  border: 1px solid var(--border);
  color: var(--text);
}}
.pill.dim {{ color: var(--muted); }}
.pill.iter {{ color: var(--iter); border-color: rgba(96,165,250,.3); }}
.kv {{ margin: 6px 0; font-size: 0.92rem; }}
.kv strong {{ color: var(--muted); font-weight: 600; margin-right: 6px; }}
.warn {{
  margin: 8px 0;
  padding: 8px 10px;
  border-radius: 8px;
  background: rgba(251,191,36,.12);
  border: 1px solid rgba(251,191,36,.35);
  color: #fcd34d;
  font-size: 0.88rem;
}}
.parse-err {{ color: var(--err); }}
details {{
  margin: 10px 0;
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 6px 10px 10px;
  background: rgba(0,0,0,.15);
}}
details > summary {{
  cursor: pointer;
  font-weight: 600;
  color: var(--muted);
  list-style: none;
}}
details > summary::-webkit-details-marker {{ display: none; }}
.block {{ margin: 10px 0; }}
.block-label {{
  font-size: 0.75rem;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--muted);
  margin-bottom: 4px;
}}
pre.content {{
  margin: 0;
  padding: 12px 14px;
  border-radius: 8px;
  background: #0c0f14;
  border: 1px solid var(--border);
  font-family: "IBM Plex Mono", ui-monospace, monospace;
  font-size: 0.8rem;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 480px;
  overflow: auto;
}}
pre.content.json {{ border-color: rgba(96,165,250,.25); }}
.calls {{ display: flex; flex-direction: column; gap: 10px; }}
.call-item {{
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 10px 12px;
  background: rgba(0,0,0,.12);
}}
.call-title {{
  font-size: 0.9rem;
  margin-bottom: 6px;
}}
.call-title code {{
  font-family: "IBM Plex Mono", monospace;
  font-size: 0.85rem;
  color: var(--tool);
}}
.dim {{ color: var(--muted); font-size: 0.82rem; }}
.nav {{
  position: fixed;
  right: 14px;
  top: 50%;
  transform: translateY(-50%);
  display: flex;
  flex-direction: column;
  gap: 5px;
  z-index: 10;
}}
.nav-dot {{
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--muted);
  opacity: 0.45;
}}
.nav-dot:hover {{ opacity: 1; }}
.ev-turn_start.nav-dot {{ background: var(--turn); }}
.ev-llm_iteration.nav-dot {{ background: var(--iter); }}
.ev-llm_response.nav-dot {{ background: var(--resp); }}
.ev-tool_call.nav-dot {{ background: var(--tool); }}
.ev-turn_end.nav-dot {{ background: var(--end); }}
.ev-error.nav-dot {{ background: var(--err); }}
@media (max-width: 1100px) {{ .nav {{ display: none; }} }}
</style>
</head>
<body>
<div class="nav" aria-hidden="true">{nav}</div>
<div class="wrap">
  <h1>{esc(title)}</h1>
  <p class="sub">Source: {esc(source_path)} · {len(records)} events</p>
  <div class="timeline">
{body}
  </div>
</div>
</body>
</html>
"""


def main() -> int:
    ap = argparse.ArgumentParser(description="Render agent_trace.jsonl as HTML.")
    ap.add_argument(
        "input",
        nargs="?",
        type=Path,
        default=None,
        help=f"JSONL file, or - for stdin (default: {DEFAULT_REL} under $HOME)",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("agent_trace_view.html"),
        help="Output HTML path (default: ./agent_trace_view.html)",
    )
    ap.add_argument(
        "--open",
        action="store_true",
        help="Try to open the generated file in a browser (xdg-open)",
    )
    args = ap.parse_args()

    in_path = args.input if args.input is not None else default_input_path()
    if str(in_path) != "-" and not in_path.is_file():
        print(f"error: file not found: {in_path}", file=sys.stderr)
        return 1

    records = load_records(in_path)
    src = "<stdin>" if str(in_path) == "-" else str(in_path.resolve())
    html_doc = build_html(records, title="Agent trace", source_path=src)
    out = args.output
    out.write_text(html_doc, encoding="utf-8")
    print(f"Wrote {out.resolve()} ({len(records)} events)")

    if args.open:
        try:
            subprocess.run(["xdg-open", str(out.resolve())], check=False)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
