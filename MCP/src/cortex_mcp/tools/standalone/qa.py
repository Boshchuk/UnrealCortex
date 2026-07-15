"""Explicit registration for standalone QA tools."""

from __future__ import annotations

import sys
from pathlib import Path

_MCP_ROOT = Path(__file__).resolve().parents[4]
if str(_MCP_ROOT) not in sys.path:
    sys.path.insert(0, str(_MCP_ROOT))

from cortex_mcp.response import format_response
from tools.qa.composites import register_qa_composite_tools


def register_qa_standalone_tools(mcp, connection) -> None:
    """Register QA standalone tools."""

    # --- Performance sampling / assertion (deferred windowed ops in CortexQAPerfOps) ---
    # These pass an explicit timeout > the sampling window so the deferred result is not
    # cut off by the client recv timeout on longer windows.
    @mcp.tool(name="qa_sample_performance")
    def qa_sample_performance(duration: float = 3.0) -> str:
        try:
            response = connection.send_command(
                "qa.sample_performance", {"duration": duration}, timeout=duration + 10.0
            )
            return format_response(response.get("data", {}), "qa_sample_performance")
        except (RuntimeError, ConnectionError) as e:
            return f"Error: {e}"

    @mcp.tool(name="qa_assert_fps")
    def qa_assert_fps(min_fps: float, duration: float = 3.0, message: str = "") -> str:
        try:
            params: dict = {"min_fps": min_fps, "duration": duration}
            if message:
                params["message"] = message
            response = connection.send_command(
                "qa.assert_fps", params, timeout=duration + 10.0
            )
            return format_response(response.get("data", {}), "qa_assert_fps")
        except (RuntimeError, ConnectionError) as e:
            return f"Error: {e}"

    @mcp.tool(name="qa_assert_frametime")
    def qa_assert_frametime(max_ms: float, duration: float = 3.0, message: str = "") -> str:
        try:
            params: dict = {"max_ms": max_ms, "duration": duration}
            if message:
                params["message"] = message
            response = connection.send_command(
                "qa.assert_frametime", params, timeout=duration + 10.0
            )
            return format_response(response.get("data", {}), "qa_assert_frametime")
        except (RuntimeError, ConnectionError) as e:
            return f"Error: {e}"

    # _CaptureMCP: intercept legacy registration, re-export under consolidated names.
    captured: dict[str, callable] = {}

    class _CaptureMCP:
        def tool(self, name=None, description=None, **_kwargs):
            def decorator(fn):
                captured[name or fn.__name__] = fn
                return fn

            return decorator

    register_qa_composite_tools(_CaptureMCP(), connection)

    @mcp.tool(name="qa_test_step")
    def qa_test_step(
        action: dict,
        wait: dict | None = None,
        assertion: dict | None = None,
        screenshot_name: str = "qa_step.png",
    ) -> str:
        return captured["test_step"](
            action=action,
            wait=wait,
            assertion=assertion,
            screenshot_name=screenshot_name,
        )
