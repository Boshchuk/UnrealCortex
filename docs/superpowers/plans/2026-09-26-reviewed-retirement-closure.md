# Reviewed Retirement Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `retire_entries` safely retire a caller-reviewed closed obsolete event island in a mixed Widget Blueprint without deleting live cosmetic behavior or hiding unsafe ownership.

**Architecture:** Extend the existing `CortexGraph` partition and retirement planner; retain its journal, native readback, generated-binding verification, and explicit save coordinator. Add one optional source GUID list and a complete native approval inventory; keep MCP's independent response-size refusal. A node class is admitted only after fixture evidence shows native removal and rollback are safe; refusal is a valid result when the real graph exceeds the proof.

**Tech Stack:** Unreal Engine 5.8 editor C++, K2 graphs, UE automation tests, Python MCP bridge and pytest.

**Spec:** `docs/superpowers/specs/2026-09-26-reviewed-retirement-closure-design.md`, approved 2026-09-26.

## Global Constraints

- Work only on feature/task branches; no worktrees; one mutating agent at a time. Prefix shell commands with `rtk`. Read `D:/UnrealProjects/RipperGame/docs/unreal-coding-standards.md` before C++ edits.
- Do not touch the unrelated RipperMirror Editor. No automatic loading of foreign Blueprint referencers or unreviewed asset saves.
- `migration.source.additional_node_guids` is optional, unique, graph-scoped, non-entry and explicitly reviewed; absent field preserves existing retirement behavior.
- Approval is discovery preview, reviewed preview with exact approved GUIDs, then one apply with the reviewed validation hash and unchanged patch ID/fingerprint/source.
- A native-trimmed identity/edge inventory is never approval-eligible; MCP's 40,000-character response ceiling remains a separate refusal.
- No compiled-but-unsaved result is persistence proof. A committed file is never rolled back by an in-memory journal.
- No Ripper Email page mutation until plugin implementation, isolated verification, focused review, deployment/build, and a fresh complete real-page preview.

## Review Focus

1. Late-retained data producer with an execution edge to a candidate: its exec consumer stays retained or approval refuses; Task 1 fixture.
2. Retained cosmetic entry feeding a requested body: request refuses without losing its link; Task 2 fixture.
3. Duplicate, foreign-graph, entry, and outside-island additional GUIDs: all refuse before mutation; Task 2 fixture.
4. More than 15 boundary/shared entries and an MCP response exceeding 40,000 characters: neither can generate an approval-eligible partial list; Task 3 native/Python fixtures.
5. Save failure versus post-save verification failure: distinguish dirty verified memory from committed-but-unverified disk without claiming rollback; Task 2 existing save tests plus injected fault fixture.

---

### Task 1: Stable retained/removable partition

**Files:**
- Modify: `Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.cpp:5018-5302`.
- Test: `Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp` (extend `FFixture` and add a named automation test near `RefusesSharedExecutionBody`).

**Interfaces:** Consumes existing `ComputeOwnedIslandPartition(UBlueprint*, UEdGraph*, const TArray<UEdGraphNode*>&, bool, FPrunePartition&, FCortexCommandResult&)`. Produces the same signature and deterministic `Removable`, `Shared`, `Blocked`, `ExternalEdges` partition; prune and retire both continue using it.

- [ ] **Step 1: Add failing native fixture.** Build an old entry -> body `A` -> exec body `B` chain. Supply `A`'s data output to a retained cosmetic body's data input. Assert `A` is retained, `B` is not removable, and the retained `A -> B` exec link persists if a different independent selected entry is retired. Add a cycle between removable nodes and assert bounded termination. Follow the existing `FFixture::AddCall`, `PrepareApprovedRequest` and `MakeRetirementInventory` patterns; do not assert incidental array length alone.
- [ ] **Step 2: Run RED focused automation.** Close the Sandbox Editor before UBT. From the Sandbox root: `export DOTNET_ROOT="$UE_58_PATH/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64" DOTNET_MULTILEVEL_LOOKUP=0 DOTNET_ROLL_FORWARD=LatestMajor && rtk proxy "$UE_58_PATH/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" CortexSandboxEditor Win64 Development -Project="$(pwd -W)/CortexSandbox.uproject" -WaitMutex -FromMsBuild`, then `rtk proxy powershell.exe -NoProfile -ExecutionPolicy Bypass -File cli/Testing/RunTests.ps1 "Cortex.Graph.Authoring.Migration.Retire+" -Timeout 900`. The added test must fail on the old one-pass candidate propagation for the specific preserved exec edge; report any preexisting failures.
- [ ] **Step 3: Implement fixed-point cut.** Replace the one-time incoming-exec sweep and backward-only retained propagation with a monotone worklist: seed graph terminators, intrinsically blocked and data-shared nodes; each newly retained node checks input producers for retained consumers **and** execution output consumers for incoming retained exec. Remove a candidate at most once and enqueue it, bounded by the existing visited scan and graph GUID ownership. Recompute edges from the final candidates and refuse unresolved links; retain the same public partition shape and no cross-graph traversal.
- [ ] **Step 4: Run focused Retire+ and Prune+ groups and commit** only `Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.cpp` and `Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp` (`rtk git add Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.cpp Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp && rtk git commit -m "fix(graph): stabilize owned retirement partition"`). Existing prune semantics must remain fail-closed.

### Task 2: Explicit class-guarded additional nodes

**Files:**
- Modify: `Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.h:269-304`, `Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.cpp:6842-7276` and the local eligibility predicates at `:4859-4974`.
- Modify: `Source/CortexGraph/Private/CortexGraphCommandHandler.cpp:575-585` (migration schema description).
- Test: `Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp` (existing `FGuardedEntryFixture`, class-specific/refusal/recovery and save-failure fixtures).

**Interfaces:** `migration.source.additional_node_guids: string[]` is optional; plan carries canonical requested GUIDs and their exact class/reason so normalized hash/readback cannot silently change the selection. `PlanRetirement` continues to produce `FCortexGraphMigrationRetirePlan`; `approved_node_guids` must equal the full computed removable set. Keep `FBlueprintEditorUtils::RemoveNode` and existing journal/readback; do not introduce a raw deletion API.

- [ ] **Step 1: Add RED tests for the request contract.** Use the existing `FFixture::Migration` helper to construct `source.additional_node_guids`; assert duplicate, selected entry, other graph, outside island, unsupported class, live PIE and retained consumer refuse before mutation with unchanged fingerprint/dirty state. Add one explicit eligible obsolete knot/delegate/latent/macro fixture **only when native engine ownership and deletion can be proven**, otherwise assert that class remains refused. Assert a cosmetic hook and its edges survive the supported path.
- [ ] **Step 2: Run Retire+ RED; implement strict parsing and durable plan.** Extend source `HasOnlyFields` with `additional_node_guids`, parse unique valid GUIDs, resolve in named graph and same asset exactly once, require island membership, and add the canonical list/class capture to `FCortexGraphMigrationRetirePlan::ToJson/FromJson` and inventory. Gate class-specific exception to explicitly listed nodes only; preserve old block rules for every unlisted node and all existing custom-event external-caller checks. A bound subgraph, unproved latent state or retained consumer must still refuse. Never convert a blocked node to removable solely because its GUID appears in the request.
- [ ] **Step 3: Cover atomicity and persistence.** On a supported fixture, use the existing discovery -> approved-preview -> apply helper and assert exact removed GUIDs, preserved cosmetic binding/tree/links, generated binding removal after `compile=true`, rollback after injected removal/readback failure and all-absent versus mixed replay. Exercise existing save-fault seams: failed save retains verified dirty memory; post-save verification failure reports committed disk and blocks mutation for the Editor process, requiring Editor restart, asset reopen and disk reconciliation without rollback. Verify `compile=false,save=false` cannot claim runtime safety.
- [ ] **Step 4: Run Retire+ and full Graph+ suites, then commit** only the owned C++ files (`rtk git add Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.h Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.cpp Source/CortexGraph/Private/CortexGraphCommandHandler.cpp Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp && rtk git commit -m "feat(graph): guard explicitly reviewed retirement nodes"`). If macro/latent/delegate semantics cannot be proven in the fixture, do not weaken the predicate; document the exact refusal for the later real-asset preview.

### Task 3: Complete approval response, MCP boundary and isolated release

**Files:**
- Modify: `Source/CortexGraph/Private/Operations/CortexGraphMigrationOps.cpp:7279-7344`, `Source/CortexGraph/Private/CortexGraphCommandHandler.cpp:309-349` and native plan status as needed.
- Modify: `MCP/src/cortex_mcp/graph_patch_boundary.py:135-158,421-495` only if native completeness signal or additional-request identity requires an explicit MCP check.
- Test: `Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp`; `MCP/tests/test_graph_retire_entries_scenario.py` and existing boundary tests under `MCP/tests/`.
- Docs: `D:/UnrealProjects/CortexSandbox/docs/systems/cortex-graph.md` and `D:/UnrealProjects/CortexSandbox/docs/verification/` only for actually observed evidence; preserve user-modified Sandbox root files.

**Interfaces:** The native retirement inventory must expose complete `selected_entries`, requested additional nodes, `removable`, `shared`, `blocked_nodes`, and `external_edges` or explicitly refuse approval. `complete=true` must mean both scan and review-critical display are complete for this migration. MCP's `_complete_approval_preview` remains fail-closed on `complete != True` and oversize response; it must never drop an approval-critical array while reporting success.

- [ ] **Step 1: Add RED native and Python boundary tests.** Generate >15 shared/blocked and boundary records; assert the caller gets all exact GUIDs/edges or a refusal with `complete=false`, never an omission marker with `complete=true`. Check discovery hash cannot authorize apply, approved-preview hash can, changed additional list refuses, and the MCP 40,000-character refusal applies independently of native completeness. Use existing `test_graph_retire_entries_scenario.py` envelope and `graph_patch_boundary.py` fake-connection patterns.
- [ ] **Step 2: Remove native review-list trimming, or refuse before publishing when a full inventory cannot fit.** Do not conflate `preexisting_diagnostics` (already explicitly truncated/non-critical) with selected/shared/blocked/edge identity lists. If the prepared full inventory cannot fit the transport, set `complete=false` and approval-ineligible before returning any token; the MCP bridge repeats the size check on preview and apply preflight.
- [ ] **Step 3: Run native and MCP verification against the right Editor.** With the Sandbox Editor closed for UBT, build and run `rtk proxy powershell.exe -NoProfile -ExecutionPolicy Bypass -File cli/Testing/RunTests.ps1 "Cortex.Graph+" -Timeout 900` from the Sandbox root. After that runner exits, start an isolated Sandbox Editor and confirm its project identity/port before invoking any live MCP test. From `Plugins/UnrealCortex/MCP/`, run `rtk uv run pytest -q --basetemp=Saved/pytest-reviewed-retirement tests/test_graph_retire_entries_scenario.py tests/test_graph_retirement_response_boundary.py`, then `rtk uv run pytest -q --basetemp=Saved/pytest-reviewed-retirement-full tests/` for the full MCP suite, keeping the identified Editor active. Smoke discovery -> reviewed preview -> approved apply -> compile -> readback -> save on a disposable fixture. Stop only that Editor afterward; never connect to RipperMirror. Report each real result.
- [ ] **Step 4: Independent review, two-repo documentation, deployment gate.** Request focused read-only review for ownership, inventory completeness, rollback and save semantics; correct material findings. Commit only owned plugin paths on the plugin feature branch. In the Sandbox root feature branch, update `docs/systems/cortex-graph.md` surgically and add only observed `docs/verification/` evidence; stage/commit **only authored hunks** and the approved plugin submodule pointer in a separate root commit. Because the root is already user-dirty, inspect the staged diff and leave pre-existing edits/untracked files unstaged; if the owned hunk cannot be isolated, leave it uncommitted and report the exact conflict rather than sweeping user changes into the commit. Deploy the reviewed plugin commit to RipperGame, build RipperEditor with its own Editor closed, then inspect a *fresh read-only* Email page preview in the correct Ripper Editor. The existing RIP-44 plan owns subsequent page/detail/host asset mutation and actual-PC acceptance; a blocked preview stops instead of approving a subset.

## Self-review / handoff

Check each spec section against Tasks 1-3: fixed-point ownership (1), optional class-specific selection and existing journal/save (2), complete approval and MCP bounds (3), with no new general deletion operation. Check old behavior when `additional_node_guids` is absent and distinct save-failure states. The user authorized subagent-driven implementation after plan review; plugin implementation proceeds only after resolving material reviewer findings, and Ripper asset mutation remains gated by the fresh real-page preview.

## Fixture-isolation acceptance correction (2026-10-01)

The prerequisite retains the original 30 commits ending at `fac9f808e9963a017dd6e47d2dfff3ef79b1c97e`; these commits are not attributed to RIP-48. The acceptance correction is limited to:

- `Source/CortexGraph/Private/Tests/CortexGraphTestContentRoot.h`
- `Source/CortexGraph/Private/Tests/CortexGraphMigrationRetireTest.cpp`
- `Source/CortexGraph/Private/Tests/CortexGraphMigrationRewireTest.cpp`
- `MCP/tests/conftest.py`
- `MCP/tests/test_graph_authoring_contract.py`
- `MCP/tests/test_operation_schema.py`
- `MCP/tests/test_umg_animation_bindings.py`
- `MCP/tests/test_level_e2e.py` (separately authorized test-only caller migration)

Use a full-GUID package directory per fixture while preserving the asset leaf name. Capture ownership through explicit unload and remove only owned disk output at final destruction. Preserve the existing mid-test unload/reload semantics; remove obsolete fixed-path predeletes, duplicated end-of-test deletion and incidental file-cleanup assertions. No production operation, additional GC invocation or global editor teardown is introduced.

Observed in `CortexSandboxMirror` on UE 5.8.3:

- Isolation-only diagnostic processor: `Saved/Logs/RIP48-baseline-graph-debug-gc-20261001.log`, verified `gc.ForceEnableGCProcessor=true`, 312/312 Success, completed queue/status 0, no warnings/errors/fatals. Diagnostic only.
- Isolation-only normal processor: `Saved/Logs/RIP48-baseline-graph-normal-gc-isolated-20261001.log`, 312/312 Success, completed queue/status 0, no warnings/errors/fatals.
- Final ownership cleanup: native build succeeded (11 actions, 43.66 seconds); `Saved/Logs/RIP48-baseline-graph-owned-fixtures-final-20261001.log`, fresh normal processor, 312/312 Success, completed queue/status 0, no warnings/errors/fatals. All 50 GUID directories observed in this log were absent afterward.
- Focused cleanup/retirement/rewire Python contracts: 47 passed. Non-live MCP suite: 930 passed, 265 deselected. Registered live retirement plus response-boundary checks: 23 passed, including both routes under asyncio and trio; native mirror PID 50652, port 8743, editor instance `AD3AE7FD47A4D99BA730E29AF80721C4`.

The retained earlier GC access-violation dumps and wrapper-copy failure remain separate historical evidence. These fresh runs do not prove their root cause or claim a historical fix. A full MCP attempt was invalidated by a normal `QUIT_EDITOR` exit during the run and cancelled; it is not a passing full-suite gate.

### Python task-state isolation and live persistence

The first stable full MCP attempt stopped after 407 passes because independent pytest cases shared the operation-schema correction budget. One contract test consumed the first UMG correction, the asyncio scenario consumed the second, and the trio scenario received the intentional exhausted-budget response instead of a fresh-task policy response. A function-scoped autouse fixture now resets the budget before and after each test; the within-case exhaustion test remains intact. The ordered contract/live-adapter/budget/animation regression run passed all 37 cases. Production budget semantics are unchanged.

The next full attempt stopped after 555 passes because existing raw-TCP Level tests used `class` for `level.spawn_actor`, whose live contract requires `class_name`. The operator authorized test-only caller migration. Update spawn requests in the Level E2E cases and shared actor fixture only; retain the valid `class` parameters for class description, component creation and actor filtering. The live Level suite passed 50 cases with its two existing skips. No native Level code or transport compatibility alias changes.

The registered retirement persistence smoke used mirror PID 49904, port 8743, editor instance `CAD3BF344079E3E52349F895CB01968D`. It discovered the complete four-node removable set, reviewed that exact set, applied with one target compile and matched readback, explicitly saved, then reloaded without discarding changes. Removed GUIDs remained absent; the retained override/body/shared-producer GUIDs and execution/data edges survived reload. The saved hash matched after reload and the package was clean. The disposable asset was deleted through its declared ownership record. Raw calls and responses: `Saved/RIP48Validation/prerequisite-retirement-save-smoke.json`.

## 2026-10-01 follow-up safety regressions

- Exercise matching foreign calls with unresolved owners and malformed delegate scopes; refuse approval without mutating the target.
- Exercise empty and nonempty unrelated call-link baselines, missing/malformed prepared inventory, readback refusal and verified restoration.
- After post-save verification failure, retain committed memory/disk and refuse a second public-route mutation; report the process-lifetime guard and Editor restart recovery.
- See CortexSandbox docs/plans/2026-10-01-graph-retirement-review-fixes-design.md, its implementation guide and verification report for the follow-up evidence.

### Intervening integration and complete pre-integration gate

Remote readback found PR151 already merged at `86d45d2c4fb93b612e40a3ada036b6a56f6eb660`, including external safety corrections `93a8ee0d` and `8ef12cf2`; PR149 was closed without merging. Preserve those changes. The ten-path test correction was captured in `cfa43c27` and merged with actual upstream main in `7eaa8857ae10e5626c64a0658ecbca002754f9dd`. Earlier native queues and persistence smoke do not validate the externally updated production DLL.

The complete 1,195-case pre-integration MCP run finished with 1,152 passed, two existing skips, seven failures and 34 setup errors (221.69 seconds). Setup errors all reported access denied to the Windows default `pytest-of-eugen` temporary root; use a fresh run-owned `--basetemp`, not permission changes or deletion of existing temporary artifacts. Failures were two remaining Level scenario spawn callers, two Graph stress pagination assertions, and three UMG live cases (status route, stale dirty fingerprint and invalid fixture compile). This is a failed full-suite gate, not an acceptance claim. Raw output: `Saved/RIP48Validation/prerequisite-full-mcp-pre-integration-20261002.log`.

### Authorized complete-gate corrections (2026-10-02)

The remaining direct Level scenario spawns now use canonical `level_cmd` envelopes; their unused legacy spawn adapter was removed. The actual Level/scenario/50-cycle lifecycle run passed 56 cases with two existing skips. Graph stress readback follows five-node cursor pages and checks every authored node identity rather than mistaking a 40,000-character truncated response for the complete graph. Both asyncio and trio stress cases passed.

The operator also authorized the pre-existing native UMG animation-GUID correction. `remove_animation` removes only the found animation's GUID-map entry inside its existing transaction. Its new real compiled-Blueprint regression failed before the production correction with the retained deleted-GUID assertion and matching compiler ensure during redo; after the one-line correction, the full 65-case UMG queue passed with no warnings/errors/fatals. Raw RED and GREEN logs are `Saved/Logs/RIP48-remove-animation-variable-identity-red-20261002.log` and `Saved/Logs/RIP48-remove-animation-umg-green-20261002.log`.

UMG live consumers now use the built-in status/capabilities routes and structured command exceptions. Rejection checks preserve the old token and verify no mutation. The duplicate and split fixtures perform actual compile/save/reload; whole-animation templates first remove only their obsolete playback consumer and retain sibling animations. Informational master-track presence is not a dangling binding, and persistence identity is compared after compilation/save rather than against a precompile token.

The integrated prerequisite build succeeded (12 actions, 101.65 seconds). Its fresh normal-GC Graph queue completed 314/314 Success using `CortexGraph-0002.dll`: `Saved/Logs/RIP48-integrated-prerequisite-graph-normal-20261002.log`. That raw log also contains two pre-queue UE Dataflow struct-initialization error records; do not claim the whole log is error-free. The isolated-temp verification passed all 197 selected Python cases, including the earlier 34 temporary-root setup errors, without changing the default temporary root or its permissions. No Toolkit command/parameter/response contract changed.

### Final follow-up candidate acceptance evidence

Frozen source `70a588cec1da3778da253abf6ab4fef2eb4ddbe5` completed the full MCP run: 1,195 collected, 1,193 passed and two existing Level skips (DataLayer subsystem unavailable; pre-existing unattended `save_all` skip), 266.51 seconds. Raw output: `Saved/RIP48Validation/prerequisite-full-mcp-70a588ce-20261002.log`.

Its fresh normal-GC `Cortex.Graph+` queue loaded `CortexGraph-0004.dll`, completed 314/314 Success, reached Queue Empty and exited with status zero; this final raw log contains no warnings/errors/fatals: `Saved/Logs/RIP48-prerequisite-70a588ce-graph-normal-20261002.log`. All 53 exact owned fixture directory candidates recovered from that run are absent after completion; no unrelated temporary files were deleted. Inventory: `Saved/RIP48Validation/prerequisite-70a588ce-owned-fixture-inventory.json`. This passes the current candidate gate without claiming a historical collector root-cause repair.

The actual registered retirement smoke on the owned mirror Editor (PID 17208, port 8743, UE 5.8.3 build 58210709) passed reviewed exact retirement, one post-removal compile and matched readback, explicit save and actual clean reload. Retained node GUIDs/classes, execution/data edges and saved hash survived unchanged; the owned fixture was deleted. `Saved/RIP48Validation/retirement-persistence-70a588ce.json` records those calls, served operation schemas, status/capabilities and loaded Graph/UMG DLL SHA-256 identities. The throwaway runner was removed. All five UMG live cases also passed in the full gate, including owned split host/child/four-template compile/save/reload and sibling-animation retention. Independent native UMG, consumer and prerequisite source reviews found no actionable issues; final review binds the completed Graph queue separately.
