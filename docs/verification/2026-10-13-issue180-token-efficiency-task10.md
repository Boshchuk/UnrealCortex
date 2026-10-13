# Issue 180 — Token efficiency, Task 10: honest native UMG tree bounds, names projection, independent binding inventory

**Date:** 2026-10-13
**Repository:** `Plugins/UnrealCortex` (plugin repo; `MCP/` is part of this repo)
**Branch:** `feat/issue-180-token-efficiency`
**Base revision:** `6e7421c5d998c2a5746821418b04f192996808ed` (Task 4 completion; this is the plugin
`HEAD` at the time of the task, with the Task 1–16 work uncommitted in the working tree)
**Owner:** Task 10 implementer (`ImplTask10UmgTreeBounds`) — RED test staging, production
implementation, the task report and this record; all builds, test runs and the registered live smoke
are controller-owned.

## Scope

`umg.get_tree` keeps full depth by default, but now honours an explicit traversal budget and a
names-only projection, reports how complete the walk actually was, and refuses an unsafe combined
tree + property-binding read instead of emitting a complete-looking partial payload.

Changed files (this task, all uncommitted):

| File | Change |
|---|---|
| `Source/CortexUMG/Private/Operations/CortexUMGWidgetTreeOps.h` | Private `FTreeReadBudget` (the one traversal budget: `MaxDepth`, `RemainingNodes`, `bNamesOnly` plus the accounting `ReturnedWidgets`/`bDepthCut`/`bNodeCut`) and the bounded `BuildWidgetTreeJson(UWidget*, int32 Depth, FTreeReadBudget&)` signature. |
| `Source/CortexUMG/Private/Operations/CortexUMGWidgetTreeOps.cpp` | Strict optional param reads (`ReadBoundedTreeIntParam`/`ReadBoundedTreeBoolParam`, cpp:201-238); budget-inspected, names-projected builder (cpp:241-321); `GetTree` (cpp:444-525) parses the bounds, counts the asset total without property expansion, emits the completeness fields and applies the combined-read refusal. |
| `Source/CortexUMG/Private/CortexUMGCommandHandler.cpp` | The live `get_tree` command info (cpp:186-191) advertises the optional `depth`, `max_nodes` and `names_only`; the description names the bounded/names-only contract. |
| `Source/CortexUMG/Private/Tests/CortexUMGTreeBoundsTest.cpp` | Staged RED: six consumer-visible regressions `Cortex.UMG.TreeBounds.*` (untracked; not modified by the implementation round). |
| `Plugins/UnrealCortex/docs/verification/2026-10-13-issue180-token-efficiency-task10.md` | This record. |

Not part of this task (deliberately): the generated MCP fallback and capabilities fixtures, the
toolkit resources and the parent-repo/toolkit UMG documents — Task 17 owns those.

`git status --porcelain -- Source/CortexUMG` lists exactly the three modified files above plus the
already-staged untracked test file; nothing else in the module was touched.

## Contract implemented

`umg.get_tree`

* Params: `require asset_path`; optional `depth` (integer `>= 0`, root is `0`; omitted keeps full
  depth), optional `max_nodes` (integer `>= 1`, counts the root and every emitted node; omitted is
  unlimited), optional `names_only` (boolean, default `false`), optional `include_property_bindings`
  (boolean, unchanged).
* A present bound must be an actual JSON number/boolean, integral and in range; JSON strings,
  fractional numbers, negative `depth`, `max_nodes < 1`, non-finite values and `max_nodes` beyond
  `int32` refuse with `INVALID_FIELD` before any asset work. Booleans are strictly typed, so a numeric
  `names_only` refuses rather than coercing.
* Response: `root` (or `null` for an asset with no root widget), `total_widgets` (count-only traversal
  of the whole asset tree — it never expands widget properties, so it stays exact under a cut),
  `returned_widgets` (nodes actually emitted), `tree_complete`, and `reasons` (array; empty on a
  complete read, otherwise it names the applied bound that cut the walk).
* Node shape: `name` always; `class` plus the `is_user_widget`/`widget_blueprint` breadcrumb for
  nested user widgets unless `names_only=true`; `child_count` = the node's **real** child total
  whenever it has children (complete or cut); `incomplete=true` **only** on a node whose walk stopped
  early; `children` only when at least one child was actually retained.
* A cut branch therefore declares `child_count` + `incomplete` and omits `children` — it is never
  presented as a leaf, and a complete node never claims a cut. Actual leaves omit both `children` and
  `child_count`. The single depth/node budget is inspected before any child JSON is allocated.
* `include_property_bindings=true` remains an independent complete asset-wide inventory appended by
  its unchanged owner (`FCortexUMGPropertyBindingOps::AppendInspection`); a tree cut never shortens it,
  and orphan diagnostics still resolve against the full tree with case-sensitive lookup.
* The one exception: when the inventory is attached and the estimated ASCII-pretty size of the whole
  response exceeds the module ceiling `CortexUMGAnimationTrackUtils::MaxDetailedResponseChars` (40000),
  the combined read refuses with the module's bounded envelope — `_error = "RESPONSE_TOO_LARGE"`,
  `reader_complete = false`, retained `asset_path` / `total_widgets` / `returned_widgets` /
  `max_response_chars`, and no `root`, `property_binding_state` or `tree_complete` — instead of
  returning partial orphan state as empty.
* A tree-only oversized read is deliberately not refused natively: the tree is caller-boundable via
  `depth`/`max_nodes` (and its completeness is declared by `tree_complete`), and bounded tree-only
  responses are owned by the Python layer's own `RESPONSE_TOO_LARGE` refusal.

## Controller-observed RED (before implementation)

Rendering runner, serial, one filter at a time. Log
`Saved/TestLogs/AutomationTest_2026-10-13_233735.log` ("...Automation Test Queue Empty 6 tests
performed."): `Cortex.UMG.TreeBounds+` — 6 tests, **6 FAIL, 0 PASS**, 50 `LogAutomationController:
Error:` lines (the 6 per-test `Result={Fail}` completions plus 44 assertion failures), and zero
`Warning|Ensure|Fatal|Assertion` lines in the raw scan.

Per-test consumer-visible failures (test file line numbers as reported by the runner):

* `DepthZeroCut` — `tree_complete`, `child_count`, `returned_widgets` and `reasons` were all absent
  (`returned_widgets` read back as `-1`); `depth=0` was ignored entirely.
* `MaxNodesCut` — `max_nodes=2` was ignored: the read declared completeness and retained **2** children
  instead of 1, with no `incomplete` and no `child_count` on root or on the cut branch.
* `NamesOnlyHierarchy` — completeness/returned fields absent (`-1`); `class` still present on root and
  nested panel; `child_count` absent; the leaf still carried an empty `children` array.
* `BindingOrphanUnderCut` — the read claimed completeness, so the "tree really is cut" guard failed
  (the binding-inventory assertions themselves were already green and guard the inventory against a
  future cut).
* `OversizedCombinedReadRefused` — a plain complete `root` + `total_widgets` + `property_binding_state`
  success came back with no size bound at all.
* `FullTreeRecovery` — `tree_complete`/`returned_widgets` absent (`-1`) and leaves still serialized an
  empty `children` array.

## Controller-observed GREEN (implementation round)

| Filter | Result | Log | Raw diagnostic scan |
|---|---|---|---|
| `Cortex.UMG.TreeBounds+` | 6 tests performed, **6/6 GREEN** (0 Fail) | `Saved/TestLogs/AutomationTest_2026-10-13_235436.log` | 0 `Warning|Ensure|Fatal|Assertion` lines |
| `Cortex.UMG.PropertyBinding+` | 13 tests performed, **13/13 GREEN** (0 Fail) | `Saved/TestLogs/AutomationTest_2026-10-13_235524.log` | 0 `Warning|Ensure|Fatal|Assertion` lines |

Both logs end with a queue-empty line for their own filter (`6 tests performed.` and
`13 tests performed.`), i.e. the runs covered exactly the intended families. The runs are
controller-owned; the numbers above are read from those logs.

## Bindings proved unchanged

* `Source/CortexUMG/Private/Operations/CortexUMGPropertyBindingOps.{h,cpp}` has **zero** changes, and
  `GetTree` still calls the same `AppendInspection(WBP, Params, nullptr, Data, …)` before any tree
  work, so the tree budget cannot reach the inventory path.
* `Cortex.UMG.PropertyBinding+` — the full 13-test binding family, including
  `ExactWidgetNamePreservesCaseOnlyOrphans` and `Persistence.ExplicitLifecycle` — is 13/13 GREEN on the
  same source as the tree change (`235524.log`).
* `Cortex.UMG.TreeBounds.BindingOrphanUnderCut` additionally pins that a genuinely cut read
  (`tree_complete=false`) still returns `reader_complete=true`, `total=2`, `scope="asset"`, both
  serialized records (the case-only orphan keeping its exact raw name) and exactly one exact
  `Missing target widget: rootSibling` diagnostic.

## Consumer audit for real leaf omission

No consumer indexes a UMG tree node's `children` directly, so no consumer needed an edit:

* `MCP/src/cortex_mcp/verification/umg.py:12,23` and `MCP/src/tools/umg/composites.py:47,57,108` read
  `node.get("children", [])`.
* `MCP/src/tools/umg/widget_tree.py:73-75` only formats the payload.
* Native consumers `Source/CortexUMG/Private/Tests/CortexUMGAddWidgetTest.cpp:80-84` (`TryGetArrayField`)
  and `CortexUMGFullWidgetBuildTest.cpp:99-102` (`total_widgets`) tolerate an absent array.
* A workspace-wide search for `["children"]` finds only the unrelated reflect tool's own output
  (`MCP/src/tools/reflect/context.py:72,75`), and `MCP/tests/test_e2e.py::TestUMGCRUD::test_get_tree`
  asserts presence of `tree`/`root`/`widgets` only.

## Source-confirmed rulings and deliberate pins

* **Fail-closed bounds.** A malformed or out-of-range bound refuses with `INVALID_FIELD` rather than
  silently falling back to full depth; this mirrors the established `include_property_bindings`
  strictness already pinned by `Cortex.UMG.PropertyBinding.RefusesCoercedJsonTypes`.
* **One budget, one traversal.** `FTreeReadBudget` is passed by reference into the existing recursive
  builder; no second tree walk is introduced for the projection, and the count-only `total_widgets`
  traversal (pre-existing `CortexUMGUtils::CountWidgets`) is reused unchanged.
* **Honest projection.** `incomplete` is omitted (not `false`) on complete nodes to keep cut claims
  one-sided, and a null panel child can never make its parent look cut: `incomplete` is set only when
  the walk actually stopped early (`bChildrenCut`).
* **Ceiling reuse.** The combined-read refusal reuses the animation family's estimator
  (`EstimateAsciiPrettyChars`) and ceiling (`MaxDetailedResponseChars = 40000`,
  `CortexUMGAnimationTrackUtils.h:72`) instead of defining a second convention, and it measures the
  exact built response so the refusal decision and the shipped envelope cannot disagree.

## Task 17 handoff (not done here)

* `MCP/src/cortex_mcp/_fallback_generated.py` operation metadata and the generated capabilities cache
  still describe `umg.get_tree` without `depth`/`max_nodes`/`names_only` (and without the new
  `returned_widgets`/`tree_complete`/`reasons` response fields); regenerate from a live capabilities
  read. The native command info already advertises the three new optional params.
* The MCP `get_tree` tool (`MCP/src/tools/umg/widget_tree.py`) still takes `asset_path` only; the new
  bounds are reachable today through `umg_cmd`, so any signature snapshot stays unaffected until the
  tool surface is extended deliberately.
* Parent-repo `docs/systems/cortex-umg.md` and toolkit `resources/umg-patterns.md` /
  `templates/domains/umg.md` still describe the unbounded tree read; Task 17 owns their sync.
* The registered live `umg_cmd(get_tree)` shallow/names/full/cut/combined-bindings smoke and the
  zero-warning raw scans of that smoke are controller-owned and are **not** claimed here.
