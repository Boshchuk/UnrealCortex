"""Public MCP output and size-metric boundary (one representation, honest metrics).

These tests exercise REAL registration, not the mock forwarding client in
``conftest.py``:

* the shipped server is spawned over stdio (``uv run python -m cortex_mcp.server``)
  and driven by an actual ``mcp`` ``ClientSession``;
* the in-process cases register through the real ``ToolOutputRegistrar`` onto a real
  ``FastMCP`` instance and call it through ``FastMCP.call_tool``.

Nothing here simulates the Unreal TCP provider: the stdio cases only use local
schema/cache reads, and the Gen cases never submit a job — the unsupported modality is
refused inside the awaited coroutine, and a supported modality hits an explicit tripwire
instead of a faked provider reply.
"""

from __future__ import annotations

import asyncio
import json
import os
import shutil
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

import pytest
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.server.fastmcp import FastMCP
from mcp.types import CallToolResult, TextContent

from cortex_mcp.tcp_client import UEConnection

# ``compact_json``/``ToolOutputRegistrar`` belong to the Task-1 boundary and are imported at
# their point of use so the shipped-server cases below still RUN (and fail) while the
# boundary is missing, instead of erroring during collection.

MCP_ROOT = Path(__file__).resolve().parents[1]

# A real Unicode project directory name: the emitted schema path must round-trip and its
# UTF-8 byte length must differ from its character length.
UNICODE_PROJECT_NAME = "CortexTokenTest_\u7de8\u96c6_\u03b1\u03b2\u03b3"


def _uv_executable() -> str:
    """Resolve the uv launcher for the interpreter running these tests."""
    override = os.environ.get("CORTEX_TEST_UV")
    if override:
        return override
    found = shutil.which("uv")
    if found:
        return found
    for candidate in (
        Path(os.environ.get("LOCALAPPDATA", "")) / "hermes" / "bin" / "uv.exe",
        Path.home() / ".local" / "bin" / "uv.exe",
    ):
        if candidate.is_file():
            return str(candidate)
    return "uv"


@asynccontextmanager
async def open_test_client(project_dir, response_mode="text", toolset="all", metrics=False):
    # The MCP venv already has the editable cortex_mcp install and mcp 1.26.0, so the child
    # must never re-sync: a concurrent sync would fight the caller's environment lock (and
    # would install nothing new anyway). --no-sync plus UV_NO_SYNC keeps this deterministic.
    child_env = dict(os.environ, CORTEX_PROJECT_DIR=str(project_dir),
                     CORTEX_RESPONSE_MODE=response_mode, CORTEX_TOOLSET=toolset,
                     CORTEX_RESPONSE_METRICS="1" if metrics else "0",
                     UV_NO_SYNC="1")
    params = StdioServerParameters(command=_uv_executable(),
        args=["run", "--no-sync", "python", "-m", "cortex_mcp.server"], env=child_env,
        cwd=str(MCP_ROOT))
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            yield session


def _write_project(tmp_path, *, gen=False) -> Path:
    """Create a disposable project so the shipped server reads local files only."""
    project = tmp_path / UNICODE_PROJECT_NAME
    (project / "Saved" / "Cortex").mkdir(parents=True)
    (project / ".cortex" / "schema").mkdir(parents=True)
    (project / f"{UNICODE_PROJECT_NAME}.uproject").write_text("{}", encoding="utf-8")
    domains: dict[str, Any] = {"gen": {"commands": []}} if gen else {}
    cache = {"version": 1, "domains": domains}
    (project / "Saved" / "Cortex" / "capabilities-cache.json").write_text(
        json.dumps(cache), encoding="utf-8",
    )
    (project / ".cortex" / "schema" / "_catalog.md").write_text(
        "# schema catalog\n", encoding="utf-8",
    )
    return project


def _single_text(result) -> str:
    blocks = [block for block in result.content if block.type == "text"]
    assert len(blocks) == 1, f"expected exactly one text block, got {result.content!r}"
    return blocks[0].text


def _run(coro_factory):
    asyncio.run(coro_factory())


# --------------------------------------------------------------------------------------
# Shipped server over stdio
# --------------------------------------------------------------------------------------

def test_default_text_mode_emits_one_compact_json_block(tmp_path):
    project = _write_project(tmp_path)

    async def scenario():
        async with open_test_client(project) as session:
            result = await session.call_tool("core_cmd", {"command": "schema_status", "params": {}})
            text = next(block.text for block in result.content if block.type == "text")
            assert result.structuredContent is None  # default text-only
            assert "\n" not in text
            assert json.loads(text)["exists"] is True
            # Compact separators, no ASCII escaping and no output-schema string wrapper.
            assert text.startswith('{"exists":')
            from cortex_mcp.response import compact_json
            assert text == compact_json(json.loads(text))
            assert not result.meta or "cortex" not in result.meta

            tools = {tool.name: tool for tool in (await session.list_tools()).tools}
            assert tools["core_cmd"].outputSchema is None
            assert tools["reflect_cache_status"].outputSchema is None

            # Wrapping must not replace the published input contracts with a generic wrapper.
            core_schema = tools["core_cmd"].inputSchema
            assert set(core_schema["properties"]) == {"command", "params"}
            assert core_schema["required"] == ["command"]
            assert core_schema.get("additionalProperties") is True
            profile_schema = tools["profile_operation_schema"].inputSchema
            assert set(profile_schema["properties"]) == {"profile", "domain", "command"}
            assert set(profile_schema["required"]) == {"profile", "domain", "command"}
            assert not ({"args", "kwargs"} & set(tools["reflect_cache_status"].inputSchema.get("properties") or {}))

    _run(scenario)


def test_structured_mode_adds_the_decoded_object_and_keeps_compact_text(tmp_path):
    project = _write_project(tmp_path)

    async def scenario():
        async with open_test_client(project, response_mode="structured") as session:
            result = await session.call_tool("core_cmd", {"command": "schema_status", "params": {}})
            text = _single_text(result)
            assert "\n" not in text
            assert isinstance(result.structuredContent, dict)
            assert not isinstance(result.structuredContent, str)
            assert result.structuredContent["exists"] is True
            assert result.structuredContent == json.loads(text)

    _run(scenario)


def test_strict_invocation_shape_is_refused_without_becoming_wrapped_output(tmp_path):
    project = _write_project(tmp_path)

    async def scenario():
        async with open_test_client(project) as session:
            result = await session.call_tool(
                "core_cmd",
                {"command": "schema_status", "params": {}, "unexpected_top_level": 1},
            )
            assert result.structuredContent is None
            payload = json.loads(_single_text(result))
            assert payload["_error"] == "INVALID_INVOCATION_SHAPE"
            assert "unexpected_top_level" in payload["_message"]

    _run(scenario)


def test_opaque_failure_text_is_preserved_verbatim(tmp_path):
    project = _write_project(tmp_path)

    async def scenario():
        async with open_test_client(project) as session:
            # No editor is reachable: the router returns a plain non-JSON failure string.
            result = await session.call_tool("core_cmd", {"command": "get_status", "params": {}})
            text = _single_text(result)
            assert result.structuredContent is None
            assert text.startswith("Error:")
            try:
                json.loads(text)
            except json.JSONDecodeError:
                pass
            else:  # pragma: no cover - a parsing failure must never become empty success
                raise AssertionError("opaque failure text was reported as JSON")

    _run(scenario)


def test_standalone_reflect_tool_uses_the_same_compact_representation(tmp_path):
    project = _write_project(tmp_path)

    async def scenario():
        async with open_test_client(project) as session:
            result = await session.call_tool("reflect_cache_status", {})
            text = _single_text(result)
            assert result.structuredContent is None
            assert "\n" not in text
            payload = json.loads(text)
            assert payload["cached"] is False

    _run(scenario)


def test_large_integer_is_emitted_exactly_without_newlines(tmp_path):
    project = _write_project(tmp_path)
    (project / "Saved" / "Cortex" / "reflect-cache.json").write_text(
        json.dumps({"data": {"total_classes": 9007199254740993}}), encoding="utf-8",
    )

    async def scenario():
        async with open_test_client(project) as session:
            result = await session.call_tool("reflect_cache_status", {})
            text = _single_text(result)
            assert "\n" not in text
            assert result.structuredContent is None
            payload = json.loads(text)
            assert isinstance(payload["class_count"], int)
            assert payload["class_count"] == 9007199254740993

    _run(scenario)


def test_metrics_report_compact_chars_and_utf8_bytes_excluding_metadata(tmp_path):
    project = _write_project(tmp_path)

    async def scenario():
        async with open_test_client(project, metrics=True) as session:
            result = await session.call_tool("core_cmd", {"command": "schema_status", "params": {}})
            text = _single_text(result)
            assert result.structuredContent is None
            assert project.name in text  # the Unicode directory reached the body

            cortex = (result.meta or {}).get("cortex")
            assert isinstance(cortex, dict), "metrics must be published under _meta.cortex"
            assert cortex["json_chars"] == len(text)
            assert cortex["utf8_bytes"] == len(text.encode("utf-8"))
            # The Unicode directory makes the UTF-8 byte count strictly larger.
            assert cortex["utf8_bytes"] > cortex["json_chars"]

    _run(scenario)


def test_optional_gen_tool_is_registered_and_refuses_before_submitting_a_job(tmp_path):
    project = _write_project(tmp_path, gen=True)

    async def scenario():
        async with open_test_client(project) as session:
            names = {tool.name for tool in (await session.list_tools()).tools}
            assert "gen_compose" in names

            result = await session.call_tool("gen_compose", {"type": "unsupported_modality"})
            text = _single_text(result)
            assert "\n" not in text
            assert result.structuredContent is None
            payload = json.loads(text)
            assert payload["success"] is False
            assert "unsupported_modality" in payload["error"]

    _run(scenario)


# --------------------------------------------------------------------------------------
# Registration facade (real FastMCP, real registrar)
# --------------------------------------------------------------------------------------

class _SizeRecorder:
    """Minimal telemetry sink: records the per-return size contract."""

    def __init__(self):
        self.recorded: list[tuple[str, str, int, int]] = []

    def record_response_size(self, tool_name, qualified_command, json_chars, utf8_bytes):
        self.recorded.append((tool_name, qualified_command, json_chars, utf8_bytes))


def _probe_server():
    from cortex_mcp.tool_output import ToolOutputRegistrar

    mcp = FastMCP("cortex-output-probe")
    recorder = _SizeRecorder()
    registrar = ToolOutputRegistrar(mcp, recorder)
    return mcp, recorder, registrar


def test_public_output_preserves_null_unicode_and_large_integer():
    async def scenario():
        mcp, recorder, registrar = _probe_server()
        payload = {"nullable": None, "text": "caf\u00e9 \u7de8\u96c6", "big": 9007199254740993}

        @registrar.tool(name="probe_public_values", description="probe values")
        def probe_public_values() -> str:
            return json.dumps(payload)

        result = await mcp.call_tool("probe_public_values", {})
        text = _single_text(result)
        assert result.structuredContent is None
        assert "\n" not in text
        from cortex_mcp.response import compact_json
        assert text == compact_json(payload)
        decoded = json.loads(text)
        assert decoded["nullable"] is None
        assert decoded["text"] == payload["text"]
        assert decoded["big"] == 9007199254740993
        # Encoding alone must not touch the connection.
        assert recorder.recorded == []

    _run(scenario)


def test_public_output_opaque_string_is_never_an_empty_object():
    async def scenario():
        mcp, _recorder, registrar = _probe_server()
        opaque = "Error: editor not reachable\n  CORTEX_PROJECT_DIR: D:/x"

        @registrar.tool(name="probe_public_opaque", description="probe opaque")
        def probe_public_opaque() -> str:
            return opaque

        result = await mcp.call_tool("probe_public_opaque", {})
        assert result.structuredContent is None
        assert _single_text(result) == opaque

    _run(scenario)


def test_public_output_preserves_an_explicit_call_tool_result():
    async def scenario():
        mcp, _recorder, registrar = _probe_server()

        @registrar.tool(name="probe_public_explicit", description="probe explicit result")
        def probe_public_explicit() -> CallToolResult:
            return CallToolResult(content=[TextContent(type="text", text="explicit-body")])

        result = await mcp.call_tool("probe_public_explicit", {})
        assert _single_text(result) == "explicit-body"
        assert result.structuredContent is None

    _run(scenario)


class _ProviderTripwire:
    """Fails loudly if the optional Gen composite ever reaches a provider."""

    def __init__(self):
        self.send_calls: list[str] = []

    async def send(self, command, params=None, **kwargs):
        self.send_calls.append(command)
        raise AssertionError("the adapter test must not submit a generation job")


def test_optional_gen_async_registration_actually_runs_without_provider_contact():
    async def scenario():
        mcp, _recorder, registrar = _probe_server()
        from cortex_mcp.tools.composites.gen import register_gen_compose_tools

        connection = _ProviderTripwire()
        register_gen_compose_tools(registrar, connection)

        # Unsupported modality: refused inside the awaited coroutine, before any provider call.
        result = await mcp.call_tool("gen_compose", {"type": "unsupported_modality"})
        text = _single_text(result)
        assert "\n" not in text
        assert result.structuredContent is None
        payload = json.loads(text)
        assert payload["success"] is False
        assert "unsupported_modality" in payload["error"]
        assert connection.send_calls == []

        # Supported modality: the awaited body reaches the provider boundary, so the tripwire
        # refusing to fake a job proves the coroutine genuinely executed.
        with pytest.raises(Exception) as excinfo:
            await mcp.call_tool("gen_compose", {"type": "mesh", "prompt": "a barrel"})
        assert "must not submit a generation job" in str(excinfo.value)
        assert connection.send_calls == ["gen.start_mesh"]

    _run(scenario)


def test_connection_metrics_expose_the_documented_response_size_schema():
    connection = UEConnection(host="127.0.0.1", port=65000)
    connection.record_response_size("core_cmd", "core.schema_status", 120, 140)
    connection.record_response_size("core_cmd", "core.schema_status", 30, 60)

    sizes = connection.get_call_metrics()["response_sizes"]

    assert sizes == {
        "total": {"json_chars": 150, "utf8_bytes": 200, "calls": 2},
        "by_tool": {"core_cmd": {"json_chars": 150, "utf8_bytes": 200, "calls": 2}},
        "by_command": {"core.schema_status": {"json_chars": 150, "utf8_bytes": 200, "calls": 2}},
    }


def test_connection_metrics_omit_by_command_without_a_qualified_command():
    connection = UEConnection(host="127.0.0.1", port=65000)
    connection.record_response_size("apply_import_ops_json", "", 10, 12)

    sizes = connection.get_call_metrics()["response_sizes"]

    assert sizes == {
        "total": {"json_chars": 10, "utf8_bytes": 12, "calls": 1},
        "by_tool": {"apply_import_ops_json": {"json_chars": 10, "utf8_bytes": 12, "calls": 1}},
        "by_command": {},
    }


def test_metrics_are_reported_once_per_public_return_excluding_telemetry():
    async def scenario():
        from cortex_mcp.tool_output import ToolOutputRegistrar

        mcp = FastMCP("cortex-metrics-probe")
        recorder = _SizeRecorder()
        registrar = ToolOutputRegistrar(mcp, recorder, metrics=True)

        @registrar.tool(name="probe_metrics", description="probe metrics")
        def probe_metrics() -> str:
            return json.dumps({"text": "caf\u00e9 \u2728"})

        result = await mcp.call_tool("probe_metrics", {})
        text = _single_text(result)

        cortex = result.meta["cortex"]
        assert cortex["json_chars"] == len(text)
        assert cortex["utf8_bytes"] == len(text.encode("utf-8"))
        assert cortex["utf8_bytes"] > cortex["json_chars"]

        assert len(recorder.recorded) == 1
        tool_name, _qualified_command, json_chars, utf8_bytes = recorder.recorded[0]
        assert tool_name == "probe_metrics"
        assert (json_chars, utf8_bytes) == (len(text), len(text.encode("utf-8")))

    _run(scenario)
