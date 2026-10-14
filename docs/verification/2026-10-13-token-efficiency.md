# Issue 180 — token-efficiency integrated acceptance record

Integrated candidate: plugin branch `feat/issue-180-token-efficiency`, 24 commits over the review base
`89611e195007e999febf9bd061de26ec01bc7988` (Task 3 completion), ending at the Task 17 integration and
graph-read-policy commits. Per-task records live beside this file as
`2026-10-13-issue180-token-efficiency-task{1..16}.md`. No push, PR, or merge was performed.

## Task coverage and independent review

| Task | Status | Per-task review |
|---|---|---|
| 1 Public output/metrics | complete | clean |
| 2 Discovery/toolsets | complete | clean (fix round 1) |
| 3 Explicit pagination | complete | clean (fix rounds 1–2) |
| 4 Asset selectors/search | complete | clean (fix round 1) |
| 5 Selected values (Data + Material) | complete | clean (fix round 1) |
| 6 Blueprint defaults discovery | complete | clean (fix round 1) |
| 7 Inherited delta + Core diff helper | complete | clean (fix round 1) |
| 8 Catalog sections + capability facade | complete | clean |
| 9 Level component inventory | complete | clean |
| 10 UMG tree bounds | complete | clean (fix round 1) |
| 11 Bounded log windows + QA coverage | complete | clean (fix round 1) |
| 12 Reflect lean views | complete | clean (fix round 1) |
| 13 Gen prompt gating | complete | clean (fix round 1) |
| 14 Class-default echoes | complete | clean (fix round 1) |
| 15 Offline catalog linking | complete | clean |
| 16 Frontend context dedupe | complete | clean |
| 17 Integration/acceptance | complete (one non-reproducible residual GC abort recorded) | pending integrated review |

Each task is a separate review package (`review-<base>..<head>.diff` under
`.superpowers/sdd/2026-10-13-token-efficiency-impl/`) with verdicts recorded in the matching
`task-N-review*.md`.

## Python marker matrix (integrated candidate)

| Marker | Command | Result |
|---|---|---|
| unit | `pytest tests/ -m "not e2e and not scenario and not stress"` | 1086 passed, 265 deselected (no editor) |
| e2e | `pytest tests/ -m e2e` (rendering editor) | 263 passed, 2 skipped, 1087 deselected |
| scenario | `pytest tests/ -m scenario` | 59 passed, 1293 deselected |
| stress | `pytest tests/ -m stress` | 14 passed, 1338 deselected |

The same non-e2e marker run while a live editor was up reports 4 additional `test_tcp_client.py`
port-discovery failures (`assert None is not None`); with no editor running it is 1086/1086, so those
are live-editor environment interactions, not regressions.

## Native acceptance

- Targeted families, each run serially after a rebuild: `Cortex.Data.SearchAssets+` 12/12,
  `Cortex.Data.DataAsset+` 14/14, `Cortex.Level.Component+` 10/10, `Cortex.UMG.TreeBounds+` 7/7,
  `Cortex.UMG.PropertyBinding+` 13/13, `Cortex.Core.Serializer+` 52/52, `Cortex.Blueprint.TokenRead+` 8/8,
  `Cortex.Blueprint.ClassDefaults+` 30/30, `Cortex.Blueprint.Discovery+` 6/6,
  `Cortex.Blueprint.ClassDefaults.Get+` 8/8, `Cortex.Reflect+` 63/63, `Cortex.Gen+` 54/54,
  `Cortex.Data.Catalog+` 7/7, `Cortex.Editor.Utility.LogCapture+` 7/7, `Cortex.Core.Capabilities+` 3/3,
  `Cortex.Blueprint+` 261/261, `Cortex.Frontend.InputArea.Resolution` 9/9 (rendering editor), and
  `Cortex.Graph.Authoring.Migration.Retire+` 55/55. Raw severity scans of every accepted run are clean.
- Full root command `RunTests.ps1 'Cortex+'`: the integrated candidate now reports **1875/1875, zero
  failures** (`Saved/TestLogs/AutomationTest_2026-10-14_045958.log`, raw severity scan clean), a 217 s
  rendering run. The pre-Task-17 attempt reported 1873/1875, the only two failures being capability-shape
  pins that Tasks 11/12 intentionally changed (`reflect.class_hierarchy` 5→6 params,
  `editor.get_recent_logs` 4→7 params); commit `45bc832d` migrates the two C++ pins and only the
  `editor.get_recent_logs` entry of the checked-in `mcp_tool_signature_snapshot.json` fixture. The rest of
  that fixture (for example the Python `get_class_defaults` signature) and the checked-in
  `capabilities_cache_full.json` were still stale at that commit; the follow-up fixture migration recorded
  below corrects them.

### Residual risk — two earlier full-suite attempts aborted in engine GC (not reproducible)

Between the migration and the final green run, two full `Cortex+` attempts aborted at a **Blueprint
compile** step with an engine crash, at a different test each time:

```
LogThreadingWindows: Error: Runnable thread Background Worker #0 crashed.
LogWindows: Error: Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x000000000000000c
  UE::GC::TBatchDispatcher<...>::FlushQueuedReferences()   GarbageCollection.cpp:3286
  UE::GC::TFastReferenceCollector<...>::ProcessObjectArray() FastReferenceCollector.h:967
  UE::GC::FRealtimeGC::CollectReferencesForGC<...>
```

Observed stops: `Cortex.Graph.Authoring.Migration.Retire.CompileRecoveryAcceptsExactErrorState`
(1206 tests seen) and `Cortex.Blueprint.Cleanup.RemoveSCSGuard.CompileFailureRollback` (117 tests seen).
The identical binary/candidate then passed `Cortex.Blueprint+` 261/261 and the retire family 55/55
standalone and finally completed the full root command 1875/1875, so the abort is not reproducible and
is recorded as a **non-blocking residual risk**, not a certified defect.

Unverified suspect — now triaged and discounted: Task 14 added `FCortexClassDefaultValueSnapshot`, the
only new `FGCObject` participant in this program (`CortexBPClassDefaultsOps.cpp:1201`, with
`AddReferencedObjects` at `:1256`). Source inspection shows no plausible causal link: the snapshot's
lifetime (constructed `:2428`, released with the journal at `:2533`) ends **before** the explicit
`FKismetEditorUtilities::CompileBlueprint` at `:2540` whose synchronous `CollectGarbage` is what walks the
crashing collector, and it is built only for wide-integer roots.

The evidence instead points to a pre-existing, environment-level intermittent engine GC crash reachable
from `CompileBlueprint` → `CollectGarbage`: 37 crash contexts with this exact signature (`0x…0c` at
`GarbageCollection.cpp:3286`) appear in this project's logs from 2026-02-12 through 2026-10-14 across UE
5.6.1 / 5.8.2 / 5.8.3, including a 2026-10-03 instance whose stack runs `SetReplicationSettings` →
`CompileBlueprint` → `CollectGarbage` with no Task 14 code anywhere in the path. Both 2026-10-14 aborts
stopped in tests that never construct a snapshot, and the snapshot-path tests passed inside both aborted
runs.

Conclusion: **non-blocking residual risk, not a defect of this program**; no code change is warranted on
the available evidence. One secondary latent item stays on watch and is recorded with its reproduction
recipe in `docs/tech-debt/cortex-blueprint-tech-debt.md`: `CollectObjectReferences` models every
`FObjectPropertyBase` uniformly and has no weak/lazy/interface/delegate branch.

## Live capabilities / fallback

On owned editor PID 46772 `scripts/sync_fallback.py` regenerated `_fallback_generated.py` from the live
cache and the output changed; that shows only that the committed fallback differed from that live cache, not
that the committed fixtures or fallback matched the verified registrations. `TestFallbackDrift` declares
`capabilities_cache_full.json` the fixture of record and that fixture intentionally omits `anim`, so the
fallback was regenerated with `--from-fixture`. That restored fixture↔fallback agreement, but agreement
between the two generated artifacts is not evidence that either reflects the registrations this program
changed — both fixtures were still stale for those commands.

The follow-up migration is therefore source-verified per entry instead:

- `mcp_tool_signature_snapshot.json` (the public Python tool signatures): the two entries whose registered
  Python function signature actually changed were re-derived from the function source:
  `blueprint.get_class_defaults` (`tools/blueprint/class_defaults.py`) gains `include_values`,
  `include_members`, `discovery_offset` and `discovery_limit` after the restored positional
  `blueprint_path`;
  `blueprint.cleanup_blueprint_migration` (`tools/blueprint/analysis.py`) gains `save`. Every other entry is
  byte-identical. `editor.get_recent_logs` deliberately keeps the seven-parameter native shape, because
  `Cortex.Core.Capabilities.Snapshot` pins that fixture entry to the native `editor.get_recent_logs`
  capability; the legacy Python wrapper (`tools/editor/utilities.py`) still takes only four arguments.
- `capabilities_cache_full.json` (a mirror of the native `GetSupportedCommands` payload): the entries whose
  native command metadata this program changed were updated from the owning `GetSupportedCommands`
  registration and cross-checked against the live `Saved/Cortex/capabilities-cache.json` —
  `data.search_assets`, `data.get_data_asset`, `data.get_data_catalog`, `blueprint.get_class_defaults`,
  `blueprint.list_inherited_properties`, `blueprint.list_settable_defaults`, `blueprint.set_class_defaults`
  (adds `echo`), `material.get_node`, `level.list_components`, `reflect.class_hierarchy`,
  `reflect.class_detail`, `editor.get_recent_logs` and `umg.get_tree`. `data.list_data_assets` was already
  current, `reflect.search` needed no change (Task 12 changed response metadata, not its parameters), and
  `gen.list_jobs` `include_prompt` is out of coverage because the fixture intentionally has no `gen` domain.
- `_fallback_generated.py` was then regenerated from the corrected fixture (`scripts/sync_fallback.py
  --from-fixture`), so the offline fallback publishes the same migrated signatures as the fixture.

## Deferred to the remaining Task 17 tail

- Toolkit synchronization (`cortex-sync-toolkit`) and the `cortex-sync-docs` audit, both of which carry
  their own approval gates; the parent-repo `docs/systems/*.md` edits made by individual tasks remain
  local and uncommitted for that step.
- A bounded independent review of the integrated candidate (the whole-branch review).
- No push, PR, merge or branch deletion was performed; the delivery endpoint is the verified local
  implementation on `feat/issue-180-token-efficiency`.
