"""End-to-End self-test for the Cortex additions ported from VibeUE/UnrealClaude.

Covers, against a LIVE Unreal Editor with the rebuilt UnrealCortex plugin:
  - editor.run_python                 (#1 execute_python, incl. defer + error path)
  - editor.get_cvar / set_cvar / list_cvars   (#4 CVar commands)
  - the whole `anim` domain            (#2 CortexAnimation Phase A inspect + Phase B authoring)

Design notes:
  - Talks to CortexCore over the same TCP `tcp_connection` fixture the other
    e2e suites use (auto-discovers Saved/CortexPort-*.txt).
  - The `anim` domain only exists in a binary built from commits bc63607 / 859e978.
    If the running editor predates that build, every anim test SKIPS with a clear
    "restart the editor" message instead of failing — so this file is a valid
    regression check both before and after the editor is relaunched.
  - Authoring tests are round-trips (create -> verify -> delete) and clean up after
    themselves, so the file is safe to re-run.

Run:
    cd Plugins/Developer/UnrealCortex/MCP && uv run pytest tests/test_cortex_gems_e2e.py -v
"""

import json
import pathlib
import re
import uuid

import pytest

from cortex_mcp.tcp_client import UECommandError, UEConnection


# This fork lives at <Project>/Plugins/Developer/UnrealCortex/MCP/tests, so the
# project root is parents[5]. (The shared conftest fixture assumes the plugin is
# one level higher, at Plugins/UnrealCortex/MCP, and resolves the wrong root for
# this layout — hence this module-local override, which pytest prefers.)
_PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[5]
_SAVED_DIR = _PROJECT_ROOT / "Saved"


def _discover_port() -> int | None:
    """Read the newest CortexPort-*.txt and return its TCP port, or None."""
    candidates = sorted(
        _SAVED_DIR.glob("CortexPort-*.txt"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for port_file in candidates:
        raw = port_file.read_text().strip()
        match = re.search(r'"port"\s*:\s*(\d+)', raw)  # JSON form
        if match:
            return int(match.group(1))
        if raw.isdigit():  # legacy plain-number form
            return int(raw)
    return None


@pytest.fixture(scope="module")
def tcp_connection():
    """Module-local connection that targets this fork's project root explicitly.

    Overrides the shared conftest fixture (whose project-root math is wrong for the
    Developer/ subfolder layout). Skips the whole module if no editor is running.
    """
    port = _discover_port()
    if port is None:
        pytest.skip(
            f"No CortexPort-*.txt under {_SAVED_DIR} — start the Unreal Editor with "
            "the UnrealCortex plugin, then re-run."
        )
    conn = UEConnection(port=port)
    conn.connect()
    try:
        yield conn
    finally:
        conn.disconnect()


# ──────────────────────────── helpers ────────────────────────────

def _uniq(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:8]}"


def _registered_domains(tcp_connection) -> set[str]:
    """Domain namespaces the live editor reports via get_capabilities."""
    resp = tcp_connection.send_command("get_capabilities", {})
    return set((resp.get("data", {}).get("domains", {}) or {}).keys())


def _has_anim(tcp_connection) -> bool:
    return "anim" in _registered_domains(tcp_connection)


_ANIM_SKIP = (
    "anim domain not registered in the running editor — it was launched before the "
    "Phase A/B build (commits bc63607/859e978). Restart the editor (or Live Coding) "
    "to load CortexAnimation, then re-run."
)


def _first_asset(tcp_connection, asset_type: str):
    """Return the object path of the first asset of a given anim type, or None."""
    resp = tcp_connection.send_command("anim.list_assets", {"asset_type": asset_type, "limit": 1})
    assets = resp.get("data", {}).get("assets", [])
    return assets[0]["path"] if assets else None


# ──────────────────────── #1 execute_python ────────────────────────

def test_run_python_basic(tcp_connection):
    resp = tcp_connection.send_command(
        "editor.run_python", {"code": "import unreal; unreal.log('cortex selftest ok')"}
    )
    data = resp["data"]
    assert data.get("ok") is True
    assert "output" in data


def test_run_python_captures_output(tcp_connection):
    resp = tcp_connection.send_command("editor.run_python", {"code": "print('hello-from-selftest')"})
    blob = str(resp["data"].get("output", "")) + str(resp["data"].get("result", ""))
    assert "hello-from-selftest" in blob


def test_run_python_defer_path(tcp_connection):
    # defer=true routes through FCortexDeferredExec (next-tick). Should still succeed.
    resp = tcp_connection.send_command(
        "editor.run_python", {"code": "import unreal; unreal.log('deferred ok')", "defer": True}
    )
    assert resp["data"].get("ok") is True


def test_run_python_error_is_reported(tcp_connection):
    # A Python exception must surface as a command error, not a silent success.
    with pytest.raises((UECommandError, RuntimeError)):
        tcp_connection.send_command("editor.run_python", {"code": "raise RuntimeError('boom')"})


def test_run_python_missing_code_param(tcp_connection):
    with pytest.raises((UECommandError, RuntimeError)):
        tcp_connection.send_command("editor.run_python", {})


# ──────────────────────────── #4 CVars ────────────────────────────

def test_get_cvar(tcp_connection):
    resp = tcp_connection.send_command("editor.get_cvar", {"name": "r.ScreenPercentage"})
    data = resp["data"]
    assert data["name"] == "r.ScreenPercentage"
    assert "value" in data and "type" in data


def test_get_cvar_unknown_errors(tcp_connection):
    with pytest.raises((UECommandError, RuntimeError)):
        tcp_connection.send_command("editor.get_cvar", {"name": "r.ThisCVarDoesNotExist_xyz"})


def test_set_cvar_round_trip(tcp_connection):
    name = "r.ScreenPercentage"
    original = tcp_connection.send_command("editor.get_cvar", {"name": name})["data"]["value"]
    try:
        resp = tcp_connection.send_command("editor.set_cvar", {"name": name, "value": "75"})
        data = resp["data"]
        assert "old_value" in data
        assert tcp_connection.send_command("editor.get_cvar", {"name": name})["data"]["value"] == "75"
    finally:
        tcp_connection.send_command("editor.set_cvar", {"name": name, "value": original})


def test_list_cvars(tcp_connection):
    # Many cvars contain "ScreenPercentage" (.Default, .Editor, ...), so don't assume the
    # bare name is in the first N — assert the filter works: non-empty, every hit matches.
    resp = tcp_connection.send_command("editor.list_cvars", {"pattern": "ScreenPercentage", "limit": 20})
    data = resp["data"]
    assert "cvars" in data and len(data["cvars"]) > 0
    assert all("screenpercentage" in c.get("name", "").lower() for c in data["cvars"])


# ───────────────────── #2 anim — Phase A inspect ─────────────────────

def test_anim_list_assets(tcp_connection):
    if not _has_anim(tcp_connection):
        pytest.skip(_ANIM_SKIP)
    resp = tcp_connection.send_command("anim.list_assets", {"asset_type": "all", "limit": 10})
    data = resp["data"]
    assert "assets" in data and "count" in data


def test_anim_get_skeleton_info(tcp_connection):
    if not _has_anim(tcp_connection):
        pytest.skip(_ANIM_SKIP)
    path = _first_asset(tcp_connection, "skeleton")
    if path is None:
        pytest.skip("no Skeleton assets in this project to inspect")
    data = tcp_connection.send_command("anim.get_skeleton_info", {"asset_path": path})["data"]
    assert "bones" in data and data["bone_count"] >= 1
    assert "sockets" in data


def test_anim_get_sequence_info(tcp_connection):
    if not _has_anim(tcp_connection):
        pytest.skip(_ANIM_SKIP)
    path = _first_asset(tcp_connection, "sequence")
    if path is None:
        pytest.skip("no AnimSequence assets in this project to inspect")
    data = tcp_connection.send_command("anim.get_sequence_info", {"asset_path": path})["data"]
    assert "play_length" in data
    assert "notifies" in data
    assert "frame_rate_fps" in data


# ───────────────────── #2 anim — Phase B authoring ─────────────────────

def test_anim_socket_round_trip(tcp_connection):
    """add_socket -> verify via get_skeleton_info -> remove_socket. Self-cleaning."""
    if not _has_anim(tcp_connection):
        pytest.skip(_ANIM_SKIP)
    skel = _first_asset(tcp_connection, "skeleton")
    if skel is None:
        pytest.skip("no Skeleton assets to author against")

    info = tcp_connection.send_command("anim.get_skeleton_info", {"asset_path": skel})["data"]
    if not info.get("bones"):
        pytest.skip("skeleton has no bones")
    bone = info["bones"][0]["name"]
    socket = _uniq("CortexSelfTestSocket")

    try:
        add = tcp_connection.send_command(
            "anim.add_socket",
            {"skeleton_path": skel, "socket_name": socket, "bone_name": bone,
             "location": [10, 0, 0]},
        )["data"]
        assert add["socket_name"] == socket

        after = tcp_connection.send_command("anim.get_skeleton_info", {"asset_path": skel})["data"]
        assert any(s["name"] == socket for s in after["sockets"]), "socket not present after add"
    finally:
        rm = tcp_connection.send_command(
            "anim.remove_socket", {"skeleton_path": skel, "socket_name": socket}
        )["data"]
        assert rm.get("removed", 0) >= 1
        final = tcp_connection.send_command("anim.get_skeleton_info", {"asset_path": skel})["data"]
        assert not any(s["name"] == socket for s in final["sockets"]), "socket not cleaned up"


def test_anim_curve_round_trip(tcp_connection):
    """add_curve / set_curve_keys / remove_curve on the first AnimSequence. Self-cleaning."""
    if not _has_anim(tcp_connection):
        pytest.skip(_ANIM_SKIP)
    seq = _first_asset(tcp_connection, "sequence")
    if seq is None:
        pytest.skip("no AnimSequence assets to author against")

    curve = _uniq("CortexSelfTestCurve")
    try:
        tcp_connection.send_command("anim.add_curve", {"asset_path": seq, "curve_name": curve})
        set_resp = tcp_connection.send_command(
            "anim.set_curve_keys",
            {"asset_path": seq, "curve_name": curve,
             "keys": [{"time": 0.0, "value": 0.0}, {"time": 1.0, "value": 1.0}]},
        )["data"]
        assert set_resp["key_count"] == 2
    finally:
        tcp_connection.send_command("anim.remove_curve", {"asset_path": seq, "curve_name": curve})
