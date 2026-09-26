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
- [ ] **Step 3: Cover atomicity and persistence.** On a supported fixture, use the existing discovery -> approved-preview -> apply helper and assert exact removed GUIDs, preserved cosmetic binding/tree/links, generated binding removal after `compile=true`, rollback after injected removal/readback failure and all-absent versus mixed replay. Exercise existing save-fault seams: failed save retains verified dirty memory; post-save verification failure reports committed disk requiring reopen, not rollback. Verify `compile=false,save=false` cannot claim runtime safety.
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
- [ ] **Step 3: Run focused tests, full `Cortex.Graph+` and MCP suite.** From the Sandbox root use `rtk proxy powershell.exe -NoProfile -ExecutionPolicy Bypass -File cli/Testing/RunTests.ps1 "Cortex.Graph+" -Timeout 900` and from plugin root use `rtk uv run pytest -q --basetemp=Saved/pytest-reviewed-retirement MCP/tests/test_graph_retire_entries_scenario.py MCP/tests/test_graph_retirement_response_boundary.py`; report each real result. Start an isolated Sandbox Editor, confirm project identity/port, then smoke discovery -> reviewed preview -> approved apply -> compile -> readback -> save on a disposable fixture. Stop that Editor; never connect to RipperMirror.
- [ ] **Step 4: Independent review and deployment gate.** Request a focused read-only reviewer for graph ownership, identity/response completeness, rollback and save semantics. Correct material findings before deploying the plugin commit to RipperGame. Update the system docs and verification evidence surgically, commit owned plugin changes, build RipperEditor while its Editor is closed, then inspect a *fresh read-only* Email page preview in the correct Ripper Editor. The existing RIP-44 integration plan owns any subsequent page/detail/host asset mutation and actual-PC acceptance; a blocked fresh preview stops rather than approving a subset.

## Self-review / handoff

Check each spec section against Tasks 1-3: fixed-point ownership (1), optional class-specific selection and existing journal/save (2), complete approval and MCP bounds (3), with no new general deletion operation. Check old behavior when `additional_node_guids` is absent and the distinct save-failure states. No plugin implementation or Ripper asset mutation is authorized by this plan until the human reviews it and confirms the previously chosen subagent-driven execution method.
