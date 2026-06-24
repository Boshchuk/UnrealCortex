# POC: in-editor MCP-over-HTTP transport (no Python bridge)

**Status: PROVEN end-to-end on UE 5.6.1 (2026-06-24).** Branch `feat/http-mcp-transport-poc`.

Inspired by UE 5.8's native `ModelContextProtocol` plugin: have the editor speak MCP
**directly over HTTP** instead of the current `Python (FastMCP) ⇄ TCP + CortexPort-*.txt ⇄ C++`
chain.

## What was built
`FCortexHttpServer` (`Source/CortexCore/Public|Private/CortexHttpServer.{h,cpp}`):
- Hosts `POST /mcp` via UE `IHttpRouter` (HTTPServer module) on a fixed port.
- Speaks MCP JSON-RPC (MCP "Streamable HTTP", single `application/json` responses):
  - `initialize` → protocolVersion + capabilities + serverInfo
  - `tools/list` → one `<domain>_cmd` tool per registered domain, **generated live** from
    `FCortexCommandRouter::GetRegisteredDomains()` + each handler's `GetSupportedCommands()`
    (command `enum` + params schema).
  - `tools/call` → reconstructs `<ns>.<subcommand>` and dispatches into the **existing**
    `FCortexCommandRouter::Execute(...)`; wraps the result as MCP `content[].text` + `isError`.
  - `ping`, notifications (202).
- Runs **alongside** the TCP server; opt-in via `cortex.http.port` CVar (default 8127 on this
  POC branch; production would default 0/off). Module wires it in `CortexCoreModule` Startup/Shutdown.

## Verified (live, curl → editor)
- `initialize` → valid handshake.
- `tools/list` → **12 tools**: core/data/graph/blueprint/umg/material/editor/level/qa/reflect/statetree/anim `_cmd`.
- `tools/call` `anim_cmd.list_assets` → real Lyra data (`total_before_limit: 686`), `isError:false`.
- Compile + link clean on UE 5.6.1; isolated (Paradox stays on TCP, untouched).

## Key findings
- **The schemas/dispatch were already 100% in C++** — the Python server was a thin bridge, so a
  Python-less HTTP server needed only transport + JSON-RPC framing + a `FCortexCommandInfo`→
  JSON-Schema converter. Confirmed.
- **Editor-environment gotchas (not code)**: `FHttpServerModule` dispatches on the game-thread
  tick. A modal dialog (e.g. "Restore Packages" after an unclean shutdown) or background-tick
  throttling ("Use Less CPU when in Background") starves that tick → requests hang. Any in-editor
  HTTP server inherits this.
- **`MakeError` name clash**: a local helper named `MakeError` collides with UE's global
  `MakeError()` (ValueOrError.h); renamed to `MakeJsonRpcError`.

## Not done (would need productionization)
- **SSE streaming** — responses are single JSON bodies; deferred/long-running tools would need a
  `text/event-stream` path (and the deferred-callback bridge). `run_python defer=true` etc. run
  inline here (nullptr deferred callback).
- **`Mcp-Session-Id` / session lifecycle** — stateless; validated with curl, not a full MCP client
  (Claude Code may require session headers from `initialize`).
- **Auth / origin checks** — none (loopback only); native MCP rejects non-loopback origins.
- **5.4.4 port** — `IHttpRouter` exists in 5.4.4 with the same API surface used here; expected to
  port directly, but not yet built on 5.4.4.
- Built/tested on the Lyra host only; not wired as the Paradox transport.

## Bottom line
The "editor speaks MCP over HTTP directly" approach is **feasible and working** — the Python bridge
and port-file are removable. Remaining work to ship is SSE + sessions + auth + the 5.4.4 build,
none of which are blockers to the concept.
