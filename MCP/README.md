# UnrealCortex MCP Server

Python MCP server that bridges Claude Code (and other MCP clients) to the UnrealCortex C++ plugin running inside Unreal Editor.

## Quick Start

```bash
cd Plugins/UnrealCortex/MCP
uv run python -m cortex_mcp
```

Port is auto-discovered from `Saved/CortexPort-{PID}.txt` (written by CortexCore on editor startup; one file per running editor instance). Override with `CORTEX_PORT=8742` env var.

## Directory Layout

```
MCP/
├── src/cortex_mcp/         # MCP server core
│   ├── server.py           # FastMCP server + tool discovery
│   ├── tcp_client.py       # TCP connection to CortexCore
│   ├── cache.py            # Response caching
│   └── response.py         # JSON formatting helpers
├── tools/                  # Domain tool modules (auto-discovered)
│   ├── blueprint/
│   ├── core/
│   ├── data/
│   ├── editor/
│   ├── graph/
│   ├── level/
│   ├── material/
│   ├── qa/
│   ├── reflect/
│   └── umg/
└── tests/                  # Unit + E2E tests
```

## Tool Discovery

`server.py` auto-discovers tools on startup. Rules:

1. **Scan:** Recursively scans `tools/` for `*.py` files.
2. **Skip:** Files whose name starts with `_` are skipped (`__init__.py`, `_helpers.py`, etc.).
3. **Register:** For each file, every function matching `register_*_tools` is called with `(mcp, connection)`.

### Example

`tools/blueprint/assets.py` defines:

```python
def register_blueprint_asset_tools(mcp, connection):
    @mcp.tool()
    def list_blueprints(...) -> str: ...

    @mcp.tool()
    def create_blueprint(...) -> str: ...
```

The server calls `register_blueprint_asset_tools(mcp, connection)` automatically.

### Adding a New Tool File

1. Create `tools/{domain}/my_feature.py` (no leading `_`).
2. Define `register_{name}_tools(mcp, connection)` — any name matching `register_*_tools`.
3. Decorate each tool with `@mcp.tool()`.
4. Restart the MCP server — tools are discovered on startup.

```python
# tools/mymodule/my_feature.py

def register_mymodule_feature_tools(mcp, connection):
    @mcp.tool()
    def my_tool(param: str) -> str:
        """Short description shown to LLM."""
        response = connection.send_command("mymodule.my_command", {"param": param})
        return format_response(response.get("data", {}), "my_tool")
```

> **Silent failure:** If the file is named `_my_feature.py` or the function is not named `register_*_tools`, it will never be discovered. No error is raised.

### Command Surface Discipline

Keep domain command surfaces focused on real workflows. Do not add per-domain diagnostic commands when the global built-ins already cover the need:

- `get_status` reports editor/server health.
- `get_capabilities` reports registered domains and command metadata.

Domain commands should do domain work, and composite tools should exist only when they reduce real multi-step workflows. Future placeholder commands or empty wrapper modules should wait until the underlying behavior is implemented and tested.

## TCP Protocol

Commands are line-delimited JSON sent to `127.0.0.1:{port}`:

```json
{"command": "data.list_datatables", "params": {}}
```

Response:

```json
{"success": true, "data": {...}}
```

Namespace prefix routes to the registered domain handler in C++. Built-in commands (`get_status`, `get_capabilities`) have no prefix.

## Testing

```bash
# From a standalone UnrealCortex checkout (use Plugins/UnrealCortex/MCP in a host project)
cd MCP
rtk proxy uv sync --group dev

# Offline tests: no editor or host .uproject required
rtk proxy uv run pytest tests/ -m "not e2e and not scenario and not stress" -v
rtk proxy uv run python scripts/sync_fallback.py --from-fixture --check

# E2E tests (requires running UE editor)
rtk proxy uv run pytest tests/ -m "e2e and not stress" -v
```

Use marker selection (`-m`), rather than test-name selection (`-k`), to exclude
editor tests. Live scenario modules also carry `e2e`, so
`-m "not e2e and not stress"` is an equivalent offline selection today.
An explicit `CORTEX_PROJECT_DIR` selects the project for connected tests; the
connection fixture temporarily supplies the containing workspace only when this
variable is absent and restores it even if discovery or connection fails.

Schema and project-discovery unit tests create temporary project layouts.
Checks of sibling `cortex-toolkit/` resources and CortexSandbox workspace docs
skip with a named prerequisite when those directories are absent. When present,
the checks still fail on missing files or invalid content. Toolkit example
scenarios require `cortex-toolkit/examples/typed-blueprint-authoring/` as well as
a live editor. Other live tests may require CortexSandbox assets and classes;
this offline isolation change does not make them generic host-project tests.
