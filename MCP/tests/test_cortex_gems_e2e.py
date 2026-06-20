"""End-to-End self-test for the editor + animation additions.

Covers, against a LIVE Unreal Editor with the UnrealCortex plugin:
  - editor.run_python                          (execute Python in the editor)
  - editor.get_cvar / set_cvar / list_cvars    (console-variable access)
  - the `anim` domain                          (CortexAnimation inspect + authoring)

The anim tests SKIP cleanly if the CortexAnimation domain isn't registered, and
authoring tests are self-cleaning round-trips, so this file is safe to re-run.

Run:
    cd <plugin>/MCP && uv run pytest tests/test_cortex_gems_e2e.py -v
"""

import json
import pathlib
import re
import uuid

import pytest

from cortex_mcp.tcp_client import UECommandError, UEConnection


def _find_saved_dir() -> pathlib.Path | None:
    """Walk up from this file to the project root (the dir containing a .uproject)
    and return its Saved/ directory. Layout-agnostic — works regardless of how
    deep the plugin is nested under Plugins/."""
    for parent in pathlib.Path(__file__).resolve().parents:
        if list(parent.glob("*.uproject")):
            return parent / "Saved"
    return None


def _discover_port() -> int | None:
    saved = _find_saved_dir()
    if saved is None:
        return None
    candidates = sorted(saved.glob("CortexPort-*.txt"), key=lambda p: p.stat().st_mtime, reverse=True)
    candidates += [saved / "CortexPort.txt"] if (saved / "CortexPort.txt").exists() else []
    for port_file in candidates:
        raw = port_file.read_text().strip()
        match = re.search(r'"port"\s*:\s*(\d+)', raw)
        if match:
            return int(match.group(1))
        if raw.isdigit():
            return int(raw)
    return None


@pytest.fixture(scope="module")
def conn():
    port = _discover_port()
    if port is None:
        pytest.skip("No CortexPort file found — start the Unreal Editor with UnrealCortex, then re-run.")
    c = UEConnection(port=port)
    try:
        c.connect()
    except ConnectionError:
        # Port file present but socket dead (editor closed, stale file) — skip rather than error.
        pytest.skip(f"Editor not reachable on port {port} (stale port file?) — start the editor and re-run.")
    try:
        yield c
    finally:
        c.disconnect()


def _uniq(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:8]}"


def _has_anim(conn) -> bool:
    resp = conn.send_command("get_capabilities", {})
    return "anim" in (resp.get("data", {}).get("domains", {}) or {})


_ANIM_SKIP = "anim domain not registered — CortexAnimation not loaded in this editor."


def _first_asset(conn, asset_type: str):
    resp = conn.send_command("anim.list_assets", {"asset_type": asset_type, "limit": 1})
    assets = resp.get("data", {}).get("assets", [])
    return assets[0]["path"] if assets else None


# ── execute_python ──

def test_run_python_basic(conn):
    data = conn.send_command("editor.run_python", {"code": "import unreal; unreal.log('cortex selftest ok')"})["data"]
    assert data.get("ok") is True and "output" in data


def test_run_python_captures_output(conn):
    data = conn.send_command("editor.run_python", {"code": "print('hello-from-selftest')"})["data"]
    assert "hello-from-selftest" in (str(data.get("output", "")) + str(data.get("result", "")))


def test_run_python_defer_path(conn):
    data = conn.send_command("editor.run_python", {"code": "import unreal; unreal.log('deferred ok')", "defer": True})["data"]
    assert data.get("ok") is True


def test_run_python_error_is_reported(conn):
    with pytest.raises((UECommandError, RuntimeError)):
        conn.send_command("editor.run_python", {"code": "raise RuntimeError('boom')"})


def test_run_python_missing_code_param(conn):
    with pytest.raises((UECommandError, RuntimeError)):
        conn.send_command("editor.run_python", {})


# ── CVars ──

def test_get_cvar(conn):
    data = conn.send_command("editor.get_cvar", {"name": "r.ScreenPercentage"})["data"]
    assert data["name"] == "r.ScreenPercentage" and "value" in data and "type" in data


def test_get_cvar_unknown_errors(conn):
    with pytest.raises((UECommandError, RuntimeError)):
        conn.send_command("editor.get_cvar", {"name": "r.ThisCVarDoesNotExist_xyz"})


def test_set_cvar_round_trip(conn):
    name = "r.ScreenPercentage"
    original = conn.send_command("editor.get_cvar", {"name": name})["data"]["value"]
    try:
        data = conn.send_command("editor.set_cvar", {"name": name, "value": "75"})["data"]
        assert "old_value" in data
        assert conn.send_command("editor.get_cvar", {"name": name})["data"]["value"] == "75"
    finally:
        conn.send_command("editor.set_cvar", {"name": name, "value": original})


def test_list_cvars(conn):
    data = conn.send_command("editor.list_cvars", {"pattern": "ScreenPercentage", "limit": 20})["data"]
    assert "cvars" in data and len(data["cvars"]) > 0
    assert all("screenpercentage" in c.get("name", "").lower() for c in data["cvars"])


# ── anim Phase A: inspection ──

def test_anim_list_assets(conn):
    if not _has_anim(conn):
        pytest.skip(_ANIM_SKIP)
    data = conn.send_command("anim.list_assets", {"asset_type": "all", "limit": 10})["data"]
    assert "assets" in data and "count" in data


def test_anim_get_skeleton_info(conn):
    if not _has_anim(conn):
        pytest.skip(_ANIM_SKIP)
    path = _first_asset(conn, "skeleton")
    if path is None:
        pytest.skip("no Skeleton assets to inspect")
    data = conn.send_command("anim.get_skeleton_info", {"asset_path": path})["data"]
    assert data["bone_count"] >= 1 and "sockets" in data


def test_anim_get_sequence_info(conn):
    if not _has_anim(conn):
        pytest.skip(_ANIM_SKIP)
    path = _first_asset(conn, "sequence")
    if path is None:
        pytest.skip("no AnimSequence assets to inspect")
    data = conn.send_command("anim.get_sequence_info", {"asset_path": path})["data"]
    assert "play_length" in data and "notifies" in data and "frame_rate_fps" in data


# ── anim Phase B: authoring (self-cleaning round-trips) ──

def test_anim_socket_round_trip(conn):
    if not _has_anim(conn):
        pytest.skip(_ANIM_SKIP)
    skel = _first_asset(conn, "skeleton")
    if skel is None:
        pytest.skip("no Skeleton assets to author against")
    info = conn.send_command("anim.get_skeleton_info", {"asset_path": skel})["data"]
    if not info.get("bones"):
        pytest.skip("skeleton has no bones")
    bone = info["bones"][0]["name"]
    socket = _uniq("CortexSelfTestSocket")
    try:
        add = conn.send_command("anim.add_socket",
                                {"skeleton_path": skel, "socket_name": socket, "bone_name": bone, "location": [10, 0, 0]})["data"]
        assert add["socket_name"] == socket
        after = conn.send_command("anim.get_skeleton_info", {"asset_path": skel})["data"]
        assert any(s["name"] == socket for s in after["sockets"])
    finally:
        rm = conn.send_command("anim.remove_socket", {"skeleton_path": skel, "socket_name": socket})["data"]
        assert rm.get("removed", 0) >= 1


def test_anim_curve_round_trip(conn):
    if not _has_anim(conn):
        pytest.skip(_ANIM_SKIP)
    seq = _first_asset(conn, "sequence")
    if seq is None:
        pytest.skip("no AnimSequence assets to author against")
    curve = _uniq("CortexSelfTestCurve")
    try:
        conn.send_command("anim.add_curve", {"asset_path": seq, "curve_name": curve})
        data = conn.send_command("anim.set_curve_keys",
                                 {"asset_path": seq, "curve_name": curve,
                                  "keys": [{"time": 0.0, "value": 0.0}, {"time": 1.0, "value": 1.0}]})["data"]
        assert data["key_count"] == 2
    finally:
        conn.send_command("anim.remove_curve", {"asset_path": seq, "curve_name": curve})
