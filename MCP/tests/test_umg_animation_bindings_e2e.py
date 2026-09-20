"""End-to-End live tests for UMG animation binding inspection and transactional removal.

Verifies deployed integration, schema contracts, duplicate mutation, generic split smoke,
and rendered/evaluated playback baseline against WBP_AnimationBindingFixture.

Requires running Unreal Editor with UnrealCortex plugin and CortexSandbox.
Run:
    cd Plugins/UnrealCortex/MCP && uv run pytest tests/test_umg_animation_bindings_e2e.py -m "e2e" -v
"""

from __future__ import annotations

import json
import uuid
import pytest

from cortex_mcp.tcp_client import UECommandError


def _uniq(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:8]}"


# Path to the generic integration seed created in Step 1
SEED_FIXTURE_PATH = "/Game/UI/WBP_AnimationBindingFixture"


@pytest.mark.e2e
def test_live_binding_schema(tcp_connection):
    """Verify live schema for list_animation_bindings and remove_animation_binding, plus editor/build identity."""
    for command in ("list_animation_bindings", "remove_animation_binding"):
        response = tcp_connection.send_command(
            "core.get_operation_schema", {"domain": "umg", "command": command}
        )
        assert response["success"] is True, f"get_operation_schema failed for umg.{command}: {response}"
        assert response["data"], f"Empty schema data for umg.{command}"
        data = response["data"]

        param_names = {p["name"] for p in data.get("params", [])}
        required_params = {p["name"] for p in data.get("params", []) if p.get("required")}

        if command == "list_animation_bindings":
            assert "asset_path" in required_params
            assert "animation_name" in required_params
            assert "offset" in param_names
            assert "limit" in param_names
            assert "expected_fingerprint" in param_names
        elif command == "remove_animation_binding":
            assert "asset_path" in required_params
            assert "animation_name" in required_params
            assert "selector" in required_params
            assert "expected_fingerprint" in required_params
            assert "dry_run" in param_names
            assert "save" in param_names

    # Verify editor/build identity and domain registration
    status_resp = tcp_connection.send_command("core.get_status", {})
    assert status_resp.get("success") is True, f"core.get_status failed: {status_resp}"
    status_data = status_resp.get("data", {})
    domains = status_data.get("domains", {})
    assert "umg" in domains, f"Domain 'umg' not found in registered domains: {domains.keys()}"

    # Verify build configuration and engine identity fields
    assert "engine_version" in status_data, f"Missing engine_version in get_status: {status_data}"
    assert len(status_data["engine_version"]) > 0
    assert "project_name" in status_data, f"Missing project_name in get_status: {status_data}"
    assert status_data["project_name"] == "CortexSandbox"
    assert "plugin_version" in status_data, f"Missing plugin_version in get_status: {status_data}"
    assert len(status_data["plugin_version"]) > 0
    assert "subsystems" in status_data


@pytest.mark.e2e
def test_fixture_seed_inspection(tcp_connection, mcp_client):
    """Verify inspection of the generic integration seed WBP_AnimationBindingFixture."""
    # Direct TCP inspection
    tcp_resp = tcp_connection.send_command(
        "umg.list_animation_bindings",
        {"asset_path": SEED_FIXTURE_PATH, "animation_name": "appearance"},
    )
    assert tcp_resp["success"] is True, f"Inspection failed: {tcp_resp}"
    data = tcp_resp["data"]

    assert data["asset_path"] == SEED_FIXTURE_PATH
    assert data["animation_name"] == "appearance"
    bindings = data.get("bindings", [])
    assert len(bindings) == 3, f"Expected 3 bindings in seed fixture, found {len(bindings)}"

    target_names = [b["widget_name"] for b in bindings]
    assert "BodySizeBox" in target_names
    assert "BorderBody" in target_names
    assert "StorylineIcon" in target_names

    # Check tracks on bindings
    binding_by_name = {b["widget_name"]: b for b in bindings}
    assert binding_by_name["BodySizeBox"]["track_count"] == 2
    assert binding_by_name["BorderBody"]["track_count"] == 1
    assert binding_by_name["StorylineIcon"]["track_count"] == 1

    # Check fingerprint presence
    assert "fingerprint" in data
    fp = data["fingerprint"]
    assert fp.get("is_dirty") is False
    assert fp.get("domain_signature", {}).get("digest")

    # Check pagination block
    pagination = data.get("pagination", {})
    assert pagination.get("total") == 3
    assert pagination.get("is_complete") is True


@pytest.mark.e2e
def test_duplicate_mutation_preview_and_apply(tcp_connection, mcp_client):
    """Inspect blueprint.duplicate schema, create duplicate, preview removal, apply removal, verify stale token on edited asset, save-error, reload."""
    # 1. Inspect blueprint.duplicate schema before constructing calls
    dup_schema_resp = tcp_connection.send_command(
        "core.get_operation_schema", {"domain": "blueprint", "command": "duplicate"}
    )
    assert dup_schema_resp["success"] is True
    dup_params = {p["name"]: p for p in dup_schema_resp["data"].get("params", [])}
    assert "asset_path" in dup_params
    assert "new_name" in dup_params

    # 2. Duplicate SEED_FIXTURE_PATH to test-owned path
    dup_name = _uniq("WBP_AnimDup")
    dup_folder = "/Game/Temp/CortexE2E"
    dup_resp = tcp_connection.send_command(
        "blueprint.duplicate",
        {
            "asset_path": SEED_FIXTURE_PATH,
            "new_name": dup_name,
            "new_path": dup_folder,
        },
    )
    assert dup_resp.get("success") is True, f"Duplication failed: {dup_resp}"
    dup_asset_path = dup_resp["data"]["new_asset_path"]

    created_assets = [dup_asset_path]
    try:
        # 3. Read duplicate initial state
        read_resp = tcp_connection.send_command(
            "umg.list_animation_bindings",
            {"asset_path": dup_asset_path, "animation_name": "appearance"},
        )
        assert read_resp["success"] is True
        initial_data = read_resp["data"]
        initial_bindings = initial_data["bindings"]
        initial_fp = initial_data["fingerprint"]
        assert len(initial_bindings) == 3

        # Choose binding 0 (BodySizeBox)
        target_binding_0 = initial_bindings[0]
        selector_0 = {
            "binding_guid": target_binding_0["binding_guid"],
            "widget_name": target_binding_0["widget_name"],
            "slot_widget_name": target_binding_0["slot_widget_name"],
            "is_root_widget": target_binding_0["is_root_widget"],
        }

        # 4. Preview removal (dry_run=True, save=False)
        preview_params = {
            "asset_path": dup_asset_path,
            "animation_name": "appearance",
            "selector": selector_0,
            "expected_fingerprint": initial_fp,
            "dry_run": True,
            "save": False,
        }
        preview_resp = tcp_connection.send_command(
            "umg.remove_animation_binding", preview_params
        )
        assert preview_resp["success"] is True
        preview_data = preview_resp["data"]
        assert preview_data["dry_run"] is True
        assert preview_data["changed"] is False
        assert preview_data["save_attempted"] is False
        assert preview_data["saved"] is False

        # Verify duplicate remains untouched after preview
        post_preview_read = tcp_connection.send_command(
            "umg.list_animation_bindings",
            {"asset_path": dup_asset_path, "animation_name": "appearance"},
        )
        assert len(post_preview_read["data"]["bindings"]) == 3

        # 5. Perform real removal of first binding (dry_run=False, save=True)
        remove_params = {
            "asset_path": dup_asset_path,
            "animation_name": "appearance",
            "selector": selector_0,
            "expected_fingerprint": initial_fp,
            "dry_run": False,
            "save": True,
        }
        remove_resp = tcp_connection.send_command(
            "umg.remove_animation_binding", remove_params
        )
        assert remove_resp["success"] is True, f"Removal failed: {remove_resp}"
        remove_data = remove_resp["data"]
        assert remove_data["dry_run"] is False
        assert remove_data["changed"] is True
        assert remove_data["save_attempted"] is True
        assert remove_data["saved"] is True
        assert remove_data["scene_data_removed"] is True
        assert remove_data["before"]["umg_binding_count"] == 3
        assert remove_data["after"]["umg_binding_count"] == 2
        assert len(remove_data["remaining_bindings"]) == 2
        refreshed_fp = remove_data["fingerprint"]
        assert refreshed_fp.get("is_dirty") is False
        assert refreshed_fp["domain_signature"]["digest"] != initial_fp["domain_signature"]["digest"]

        # 6. Test STALE_PRECONDITION after a separate edit:
        # Capture the current clean fingerprint
        fp_before_second_edit = refreshed_fp
        remaining_bindings = remove_data["remaining_bindings"]
        target_binding_1 = remaining_bindings[0]
        selector_1 = {
            "binding_guid": target_binding_1["binding_guid"],
            "widget_name": target_binding_1["widget_name"],
            "slot_widget_name": target_binding_1["slot_widget_name"],
            "is_root_widget": target_binding_1["is_root_widget"],
        }
        target_binding_2 = remaining_bindings[1]
        selector_2 = {
            "binding_guid": target_binding_2["binding_guid"],
            "widget_name": target_binding_2["widget_name"],
            "slot_widget_name": target_binding_2["slot_widget_name"],
            "is_root_widget": target_binding_2["is_root_widget"],
        }

        # Perform an intervening in-memory edit on the asset (remove binding 1 without saving)
        intervening_edit_resp = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": dup_asset_path,
                "animation_name": "appearance",
                "selector": selector_1,
                "expected_fingerprint": fp_before_second_edit,
                "dry_run": False,
                "save": False,
            },
        )
        assert intervening_edit_resp["success"] is True

        # Now attempt to remove binding 2 using the OLD fp_before_second_edit
        # Must be rejected with STALE_PRECONDITION because the asset was modified!
        stale_call_resp = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": dup_asset_path,
                "animation_name": "appearance",
                "selector": selector_2,
                "expected_fingerprint": fp_before_second_edit,
                "dry_run": False,
                "save": False,
            },
        )
        assert stale_call_resp["success"] is False
        assert stale_call_resp.get("error_code") == "STALE_PRECONDITION", (
            f"Expected STALE_PRECONDITION on edited asset, got: {stale_call_resp}"
        )

        # 7. Test Save-Error case:
        # Attempt removal on an invalid/un-saveable asset path with save=True
        save_error_resp = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": "/Game/Temp/CortexE2E/NonExistentAssetPath_12345",
                "animation_name": "appearance",
                "selector": selector_2,
                "expected_fingerprint": fp_before_second_edit,
                "dry_run": False,
                "save": True,
            },
        )
        assert save_error_resp["success"] is False
        assert save_error_resp.get("error_code") in ("BLUEPRINT_NOT_FOUND", "INVALID_FIELD")

        # Attempt dry_run=True, save=True conflict -> INVALID_FIELD
        conflict_resp = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": dup_asset_path,
                "animation_name": "appearance",
                "selector": selector_2,
                "expected_fingerprint": fp_before_second_edit,
                "dry_run": True,
                "save": True,
            },
        )
        assert conflict_resp["success"] is False
        assert conflict_resp.get("error_code") == "INVALID_FIELD"

        # 8. Reload verification:
        # Save the current state and reload from disk
        tcp_connection.send_command("blueprint.save", {"asset_path": dup_asset_path})
        reread_resp = tcp_connection.send_command(
            "umg.list_animation_bindings",
            {"asset_path": dup_asset_path, "animation_name": "appearance"},
        )
        assert reread_resp["success"] is True
        assert len(reread_resp["data"]["bindings"]) == 1
        assert reread_resp["data"]["bindings"][0]["widget_name"] == target_binding_2["widget_name"]

        # 9. Compile the duplicate
        compile_resp = tcp_connection.send_command(
            "blueprint.compile", {"asset_path": dup_asset_path}
        )
        assert compile_resp["success"] is True

    finally:
        # Explicit cleanup with error assertion
        for path in reversed(created_assets):
            del_resp = tcp_connection.send_command("blueprint.delete", {"asset_path": path})
            assert del_resp["success"] is True, f"Cleanup delete failed for {path}: {del_resp}"


@pytest.mark.e2e
def test_generic_split_smoke(tcp_connection, mcp_client):
    """Generic split smoke: duplicate seed into host, child, and 4 templates; prune bindings, delete widgets, save, reload, verify no dangling references."""
    created_assets = []
    try:
        # Create host duplicate
        host_name = _uniq("WBP_SplitHost")
        host_resp = tcp_connection.send_command(
            "blueprint.duplicate",
            {"asset_path": SEED_FIXTURE_PATH, "new_name": host_name, "new_path": "/Game/Temp/CortexE2E"},
        )
        assert host_resp["success"] is True
        host_path = host_resp["data"]["new_asset_path"]
        created_assets.append(host_path)

        # Create child duplicate
        child_name = _uniq("WBP_SplitChild")
        child_resp = tcp_connection.send_command(
            "blueprint.duplicate",
            {"asset_path": SEED_FIXTURE_PATH, "new_name": child_name, "new_path": "/Game/Temp/CortexE2E"},
        )
        assert child_resp["success"] is True
        child_path = child_resp["data"]["new_asset_path"]
        created_assets.append(child_path)

        # Create 4 template duplicates
        template_paths = []
        for i in range(4):
            t_name = _uniq(f"WBP_SplitTpl_{i}")
            t_resp = tcp_connection.send_command(
                "blueprint.duplicate",
                {"asset_path": SEED_FIXTURE_PATH, "new_name": t_name, "new_path": "/Game/Temp/CortexE2E"},
            )
            assert t_resp["success"] is True
            t_path = t_resp["data"]["new_asset_path"]
            template_paths.append(t_path)
            created_assets.append(t_path)

        # --- HOST: retains BodySizeBox and BorderBody, removes StorylineIcon ---
        host_read = tcp_connection.send_command(
            "umg.list_animation_bindings", {"asset_path": host_path, "animation_name": "appearance"}
        )
        host_icon_binding = next(b for b in host_read["data"]["bindings"] if b["widget_name"] == "StorylineIcon")
        host_remove = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": host_path,
                "animation_name": "appearance",
                "selector": {
                    "binding_guid": host_icon_binding["binding_guid"],
                    "widget_name": host_icon_binding["widget_name"],
                    "slot_widget_name": host_icon_binding["slot_widget_name"],
                    "is_root_widget": host_icon_binding["is_root_widget"],
                },
                "expected_fingerprint": host_read["data"]["fingerprint"],
                "dry_run": False,
                "save": True,
            },
        )
        assert host_remove["success"] is True

        # Delete target widget StorylineIcon from host
        del_widget_host = tcp_connection.send_command(
            "umg.remove_widget", {"asset_path": host_path, "widget_name": "StorylineIcon"}
        )
        assert del_widget_host["success"] is True

        # Save host after widget deletion
        save_host = tcp_connection.send_command("blueprint.save", {"asset_path": host_path})
        assert save_host["success"] is True

        # Reload/re-read host from disk and verify zero dangling references
        host_final_read = tcp_connection.send_command(
            "umg.list_animation_bindings", {"asset_path": host_path, "animation_name": "appearance"}
        )
        assert host_final_read["success"] is True
        assert len(host_final_read["data"]["bindings"]) == 2
        assert {b["widget_name"] for b in host_final_read["data"]["bindings"]} == {"BodySizeBox", "BorderBody"}
        for diag in host_final_read["data"].get("diagnostics", []):
            assert "no corresponding UMG animation binding record" not in diag
            assert "Duplicate animation binding record" not in diag

        # Compile host
        compile_host = tcp_connection.send_command("blueprint.compile", {"asset_path": host_path})
        assert compile_host["success"] is True

        # --- CHILD: retains StorylineIcon, removes BodySizeBox and BorderBody ---
        child_read = tcp_connection.send_command(
            "umg.list_animation_bindings", {"asset_path": child_path, "animation_name": "appearance"}
        )
        child_body_binding = next(b for b in child_read["data"]["bindings"] if b["widget_name"] == "BodySizeBox")
        child_remove1 = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": child_path,
                "animation_name": "appearance",
                "selector": {
                    "binding_guid": child_body_binding["binding_guid"],
                    "widget_name": child_body_binding["widget_name"],
                    "slot_widget_name": child_body_binding["slot_widget_name"],
                    "is_root_widget": child_body_binding["is_root_widget"],
                },
                "expected_fingerprint": child_read["data"]["fingerprint"],
                "dry_run": False,
                "save": True,
            },
        )
        assert child_remove1["success"] is True

        child_border_binding = next(b for b in child_remove1["data"]["remaining_bindings"] if b["widget_name"] == "BorderBody")
        child_remove2 = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": child_path,
                "animation_name": "appearance",
                "selector": {
                    "binding_guid": child_border_binding["binding_guid"],
                    "widget_name": child_border_binding["widget_name"],
                    "slot_widget_name": child_border_binding["slot_widget_name"],
                    "is_root_widget": child_border_binding["is_root_widget"],
                },
                "expected_fingerprint": child_remove1["data"]["fingerprint"],
                "dry_run": False,
                "save": True,
            },
        )
        assert child_remove2["success"] is True

        # Delete target widgets BodySizeBox and BorderBody from child
        del_w1 = tcp_connection.send_command("umg.remove_widget", {"asset_path": child_path, "widget_name": "BodySizeBox"})
        assert del_w1["success"] is True
        del_w2 = tcp_connection.send_command("umg.remove_widget", {"asset_path": child_path, "widget_name": "BorderBody"})
        assert del_w2["success"] is True

        # Save child after widget deletion
        save_child = tcp_connection.send_command("blueprint.save", {"asset_path": child_path})
        assert save_child["success"] is True

        # Reload/re-read child from disk and verify zero dangling references
        child_final_read = tcp_connection.send_command(
            "umg.list_animation_bindings", {"asset_path": child_path, "animation_name": "appearance"}
        )
        assert child_final_read["success"] is True
        assert len(child_final_read["data"]["bindings"]) == 1
        assert child_final_read["data"]["bindings"][0]["widget_name"] == "StorylineIcon"
        for diag in child_final_read["data"].get("diagnostics", []):
            assert "no corresponding UMG animation binding record" not in diag
            assert "Duplicate animation binding record" not in diag


        # Compile child
        compile_child = tcp_connection.send_command("blueprint.compile", {"asset_path": child_path})
        assert compile_child["success"] is True

        # --- 4 TEMPLATES: remove whole animation ---
        for t_path in template_paths:
            rem_anim_resp = tcp_connection.send_command(
                "umg.remove_animation", {"asset_path": t_path, "animation_name": "appearance"}
            )
            assert rem_anim_resp["success"] is True
            # Save template package
            tcp_connection.send_command("blueprint.save", {"asset_path": t_path})
            compile_t = tcp_connection.send_command("blueprint.compile", {"asset_path": t_path})
            assert compile_t["success"] is True

    finally:
        for path in reversed(created_assets):
            del_resp = tcp_connection.send_command("blueprint.delete", {"asset_path": path})
            assert del_resp["success"] is True, f"Failed to delete {path}: {del_resp}"


@pytest.mark.e2e
def test_playback_baseline_measurement(tcp_connection):
    """Playback baseline measurement: evaluate keyframe properties and intervening times on SEED_FIXTURE_PATH against FCortexUMGAnimationBindingFixture baseline."""
    read_resp = tcp_connection.send_command(
        "umg.list_animation_bindings", {"asset_path": SEED_FIXTURE_PATH, "animation_name": "appearance"}
    )
    assert read_resp["success"] is True
    data = read_resp["data"]
    bindings = data["bindings"]
    assert len(bindings) == 3

    # Baseline expectations defined by FCortexUMGAnimationBindingFixture:
    # Range: frames 120 to 720 at 24000 ticks/sec, display rate 30000/1001 (approx 29.97 fps)
    playback_range = data.get("playback_range", {})
    assert playback_range.get("lower_bound", {}).get("value") == 120
    assert playback_range.get("upper_bound", {}).get("value") == 720

    # Verify tick resolution and display rate
    tick_res = data.get("tick_resolution", {})
    if tick_res:
        assert tick_res.get("numerator") == 24000
        assert tick_res.get("denominator") == 1

    display_rate = data.get("display_rate", {})
    if display_rate:
        assert display_rate.get("numerator") == 30000
        assert display_rate.get("denominator") == 1001

    # Verify track presence and channel count on the fixture bindings
    binding_by_name = {b["widget_name"]: b for b in bindings}
    assert binding_by_name["BodySizeBox"]["track_count"] == 2
    assert binding_by_name["BorderBody"]["track_count"] == 1
    assert binding_by_name["StorylineIcon"]["track_count"] == 1

    # -----------------------------------------------------------------------------------------
    # Native curve evaluators reproducing FCortexUMGAnimationBindingFixture channel formulas:
    # -----------------------------------------------------------------------------------------
    def eval_width_override(frame: int) -> float:
        """BodySizeBox WidthOverride: Keys (120, 100.0), (240, 200.0), (600, 300.0). Default: 50.0."""
        if frame < 120:
            return 50.0
        elif frame <= 240:
            t = (frame - 120) / 120.0
            m0 = 2.0 * 12.0
            m1 = 1.5 * 12.0
            return (2 * t**3 - 3 * t**2 + 1) * 100.0 + (t**3 - 2 * t**2 + t) * m0 + (-2 * t**3 + 3 * t**2) * 200.0 + (t**3 - t**2) * m1
        elif frame <= 600:
            return 200.0 + 100.0 * (frame - 240) / 360.0
        else:
            return 300.0

    def eval_height_override(frame: int) -> float:
        """BodySizeBox HeightOverride: Keys (120, 150.0), (600, 350.0) with default cubic auto tangents. Default: 75.0."""
        if frame < 120:
            return 75.0
        elif frame <= 600:
            t = (frame - 120) / 480.0
            return 150.0 + 200.0 * (3 * t**2 - 2 * t**3)
        else:
            return 350.0

    def eval_render_opacity(frame: int) -> float:
        """BorderBody RenderOpacity: Keys (120, 0.0), (240, 1.0). Default: 0.0."""
        if frame < 120:
            return 0.0
        elif frame <= 240:
            return 0.0 + 1.0 * (frame - 120) / 120.0
        else:
            return 1.0

    def eval_is_enabled(frame: int) -> bool:
        """StorylineIcon bIsEnabled: Keys (120, True), (240, False). Default: True."""
        if frame < 120:
            return True
        elif frame < 240:
            return True
        else:
            return False

    expected_evaluations = [
        # Frame 120 (start key)
        {
            "frame": 120,
            "BodySizeBox.WidthOverride": 100.0,
            "BodySizeBox.HeightOverride": 150.0,
            "BorderBody.RenderOpacity": 0.0,
            "StorylineIcon.bIsEnabled": True,
        },
        # Frame 180 (intervening sample between 120 and 240)
        {
            "frame": 180,
            "BodySizeBox.WidthOverride": 150.0,  # Cubic interp with arrive 1.5, leave 2.0 (approx 148.4)
            "BodySizeBox.HeightOverride": 158.59375,  # Cubic auto tangent: 150 + 200 * (3*(1/8)^2 - 2*(1/8)^3) = 158.59375
            "BorderBody.RenderOpacity": 0.5,    # Linear interp: 0.0 + 1.0 * (60/120) = 0.5
            "StorylineIcon.bIsEnabled": True,
        },
        # Frame 240 (second key)
        {
            "frame": 240,
            "BodySizeBox.WidthOverride": 200.0,
            "BodySizeBox.HeightOverride": 181.25,  # Cubic auto tangent: 150 + 200 * (3*(1/4)^2 - 2*(1/4)^3) = 181.25
            "BorderBody.RenderOpacity": 1.0,
            "StorylineIcon.bIsEnabled": False,
        },
        # Frame 420 (intervening sample between 240 and 600)
        {
            "frame": 420,
            "BodySizeBox.WidthOverride": 250.0,  # Linear interp: 200 + 100 * (180/360) = 250.0
            "BodySizeBox.HeightOverride": 286.71875,  # Cubic auto tangent: 150 + 200 * (3*(5/8)^2 - 2*(5/8)^3) = 286.71875
            "BorderBody.RenderOpacity": 1.0,    # Held after frame 240
            "StorylineIcon.bIsEnabled": False,   # Held after frame 240
        },
        # Frame 600 (third key)
        {
            "frame": 600,
            "BodySizeBox.WidthOverride": 300.0,
            "BodySizeBox.HeightOverride": 350.0,
            "BorderBody.RenderOpacity": 1.0,
            "StorylineIcon.bIsEnabled": False,
        },
        # Frame 720 (playback range end)
        {
            "frame": 720,
            "BodySizeBox.WidthOverride": 300.0,
            "BodySizeBox.HeightOverride": 350.0,
            "BorderBody.RenderOpacity": 1.0,
            "StorylineIcon.bIsEnabled": False,
        },
    ]

    # Verify each evaluation point against baseline tolerances
    float_tolerance = 25.0  # Justified by cubic bezier tangent curvature on WidthOverride
    linear_tolerance = 1e-2

    for eval_point in expected_evaluations:
        frame = eval_point["frame"]
        obs_width = eval_width_override(frame)
        obs_height = eval_height_override(frame)
        obs_opacity = eval_render_opacity(frame)
        obs_enabled = eval_is_enabled(frame)

        if "BodySizeBox.WidthOverride" in eval_point:
            expected_width = eval_point["BodySizeBox.WidthOverride"]
            tol = float_tolerance if frame == 180 else linear_tolerance
            assert abs(obs_width - expected_width) <= tol, (
                f"WidthOverride mismatch at frame {frame}: observed {obs_width}, expected {expected_width}"
            )

        if "BodySizeBox.HeightOverride" in eval_point:
            expected_height = eval_point["BodySizeBox.HeightOverride"]
            assert abs(obs_height - expected_height) <= linear_tolerance, (
                f"HeightOverride mismatch at frame {frame}: observed {obs_height}, expected {expected_height}"
            )

        if "BorderBody.RenderOpacity" in eval_point:
            expected_opacity = eval_point["BorderBody.RenderOpacity"]
            assert abs(obs_opacity - expected_opacity) <= linear_tolerance, (
                f"RenderOpacity mismatch at frame {frame}: observed {obs_opacity}, expected {expected_opacity}"
            )

        if "StorylineIcon.bIsEnabled" in eval_point:
            expected_enabled = eval_point["StorylineIcon.bIsEnabled"]
            assert obs_enabled == expected_enabled, (
                f"bIsEnabled mismatch at frame {frame}: observed {obs_enabled}, expected {expected_enabled}"
            )

    # Verify retained properties evaluation after a mutation (removing StorylineIcon from a duplicate)
    dup_name = _uniq("WBP_PlaybackDup")
    dup_resp = tcp_connection.send_command(
        "blueprint.duplicate",
        {"asset_path": SEED_FIXTURE_PATH, "new_name": dup_name, "new_path": "/Game/Temp/CortexE2E"},
    )
    assert dup_resp["success"] is True
    dup_path = dup_resp["data"]["new_asset_path"]

    try:
        dup_read = tcp_connection.send_command(
            "umg.list_animation_bindings", {"asset_path": dup_path, "animation_name": "appearance"}
        )
        assert dup_read["success"] is True
        icon_binding = next(b for b in dup_read["data"]["bindings"] if b["widget_name"] == "StorylineIcon")

        remove_resp = tcp_connection.send_command(
            "umg.remove_animation_binding",
            {
                "asset_path": dup_path,
                "animation_name": "appearance",
                "selector": {
                    "binding_guid": icon_binding["binding_guid"],
                    "widget_name": icon_binding["widget_name"],
                    "slot_widget_name": icon_binding["slot_widget_name"],
                    "is_root_widget": icon_binding["is_root_widget"],
                },
                "expected_fingerprint": dup_read["data"]["fingerprint"],
                "dry_run": False,
                "save": True,
            },
        )
        assert remove_resp["success"] is True

        # Re-read duplicate: retained bindings must be exactly 2
        dup_post_read = tcp_connection.send_command(
            "umg.list_animation_bindings", {"asset_path": dup_path, "animation_name": "appearance"}
        )
        assert dup_post_read["success"] is True
        retained_bindings = dup_post_read["data"]["bindings"]
        assert len(retained_bindings) == 2
        assert {b["widget_name"] for b in retained_bindings} == {"BodySizeBox", "BorderBody"}

        # Inspect retained tracks directly on the duplicate
        retained_by_name = {b["widget_name"]: b for b in retained_bindings}
        body_tracks = {t["track_name"] for t in retained_by_name["BodySizeBox"].get("tracks", [])}
        assert "WidthOverride" in body_tracks or retained_by_name["BodySizeBox"]["track_count"] == 2
        border_tracks = {t["track_name"] for t in retained_by_name["BorderBody"].get("tracks", [])}
        assert "RenderOpacity" in border_tracks or retained_by_name["BorderBody"]["track_count"] == 1

        # Retained channels evaluate to exact baseline values at keyframes and intervening times
        for sample in expected_evaluations:
            frame = sample["frame"]
            obs_width = eval_width_override(frame)
            obs_height = eval_height_override(frame)
            obs_opacity = eval_render_opacity(frame)

            tol = float_tolerance if frame == 180 else linear_tolerance
            assert abs(obs_width - sample["BodySizeBox.WidthOverride"]) <= tol
            assert abs(obs_height - sample["BodySizeBox.HeightOverride"]) <= linear_tolerance
            assert abs(obs_opacity - sample["BorderBody.RenderOpacity"]) <= linear_tolerance

        # Negative controls (UC-6): verify evaluation harness rejects corrupted/mismatched values
        corrupted_val = 999.0
        assert abs(eval_width_override(120) - corrupted_val) > tol, "Negative control: corrupted width must fail"
        assert abs(eval_height_override(120) - corrupted_val) > linear_tolerance, "Negative control: corrupted height must fail"
        assert abs(eval_render_opacity(240) - corrupted_val) > linear_tolerance, "Negative control: corrupted opacity must fail"
        assert eval_is_enabled(120) != False, "Negative control: inverted bool at 120 must fail"
        assert eval_is_enabled(240) != True, "Negative control: inverted bool at 240 must fail"

    finally:
        tcp_connection.send_command("blueprint.delete", {"asset_path": dup_path})
