# UMG Widget Rename Animation Binding Correction Design

**Date:** 2026-10-01  
**Status:** Regression verified; overall acceptance pending  
**Domain:** UMG (`CortexUMG`)  
**Scope:** `umg.rename_widget` / `FCortexUMGWidgetTreeOps::RenameWidget`

---

## 1. Problem Statement & Verified Defects

### 1.1 Animation Binding Premature Loop Termination
In Unreal Engine 5.8 (`Engine/Source/Editor/UMGEditor/Private/WidgetBlueprintOperationUtils.cpp:727-751`), `FWidgetBlueprintOperationUtils::RenameWidget` iterates over `WidgetAnimation->AnimationBindings` to update widget names and possessable targets. When it encounters a slot binding (`SlotWidgetName != NAME_None`), an `else { break; }` branch executes immediately after renaming that single binding.

This premature `break` terminates the animation binding loop. Consequently:
1. Any subsequent bindings for the same widget (such as a direct widget binding where `SlotWidgetName == NAME_None`) retain the stale `OldObjectName`.
2. Runtime object resolution (`FWidgetAnimationBinding::FindRuntimeObject`) fails for those subsequent bindings because the old widget name no longer exists in `WidgetTree`.
3. MovieScene possessables associated with subsequent direct bindings are never renamed.
4. The command previously reported success despite leaving stale animation references.

### 1.2 Case-Insensitive String Comparison Defect
Live smoke testing demonstrated that calling `umg.rename_widget` with `widget_name='StatusLabel'` and `new_name='statuslabel'` returned success with `changed=false`, leaving the widget unchanged on disk and in memory rather than refusing with `InvalidOperation` as documented.

In `FCortexUMGWidgetTreeOps::RenameWidget`:
1. `Widget->GetName() != WidgetName` uses `FString::operator!=`, which in Unreal Engine is case-insensitive. A wrong-case `widget_name` (e.g. `'bodysizebox'` for `'BodySizeBox'`) is accepted as matching instead of returning `WidgetNotFound`.
2. `bChanged = OldName != NewName` is likewise case-insensitive. When the new name differs only in case, `bChanged` evaluates to `false`, bypassing `if (bChanged)` and the `Widget->GetFName() == FName(*NewName)` case-only guard entirely, returning success without mutation.

---

## 2. Architecture & Transaction Design

### 2.1 Transactional Animation Repair
The engine utility `FWidgetBlueprintOperationUtils::RenameWidget` opens an internal `FScopedTransaction` that destructs and commits when the utility returns. Modifying animations only in a trailing post-repair loop leaves those mutations outside the transaction; conversely, wrapping only the post-repair would snapshot bindings after the slot binding was already mutated by the engine.

To achieve atomic transactional behavior:
1. **Outer Scoped Transaction:** Surround the operation in an outer `FScopedTransaction` using standard Cortex transaction conventions (`FText::FromString(FString::Printf(TEXT("Cortex: Rename Widget %s to %s"), *OldName, *NewName))`). In Unreal Engine, the utility's inner transaction will nest into this active outer transaction.
2. **Pre-Engine Snapshot:** Before calling the engine utility, iterate through `WBP->Animations` and call `WidgetAnimation->Modify()` and `WidgetAnimation->MovieScene->Modify()` on any animation containing bindings referencing `OldFName`. This snapshots the pristine pre-rename state of all animations and possessables within the outer transaction.
3. **Invoke Engine Rename:** Execute `FWidgetBlueprintOperationUtils::RenameWidget(WBP, Widget, NewName)` to perform reference-aware renaming across widget trees, delegate bindings, focus, navigation, and child variables. On false-return, call `Transaction.Cancel()` to avoid pushing an empty or partial transaction entry.
4. **Post-Rename Repair:** Within the same outer transaction scope, update any remaining `AnimBinding` still matching `OldFName` to `NewFName`. For direct bindings (`SlotWidgetName == NAME_None`), locate the corresponding `FMovieScenePossessable` in `WidgetAnimation->MovieScene` by `AnimBinding.AnimationGuid` and rename it to `NewName`.
5. **State Preservation:** Slot names, animation GUIDs, tracks, sections, channels, keys, and playback ranges are preserved intact across both forward execution and undo/redo cycles.

### 2.2 Case-Sensitive Comparison (Implemented)
1. Exact lookup: `if (!Widget || !Widget->GetName().Equals(WidgetName, ESearchCase::CaseSensitive))` returns `WidgetNotFound`.
2. Exact change detection: `const bool bChanged = !OldName.Equals(NewName, ESearchCase::CaseSensitive);` ensures case-only new names trigger `bChanged = true`.
3. The existing `Widget->GetFName() == FName(*NewName)` check then catches case-only renames and rejects them with `InvalidOperation`, maintaining documented identifier invariants.

---

## 3. Contract & Safety Invariants

- **Identifier Discipline:** `new_name` must be a literal identifier (starting with a letter or underscore, alphanumeric/underscore characters only, non-empty, below `NAME_SIZE`). Sanitizing, slugging, and case-only renames are rejected.
- **Fingerprint Precondition:** Requires complete widget tree `expected_fingerprint` containing `compiled_signature_crc`. A mismatching supplied fingerprint returns `StalePrecondition`.
- **Containment Safeguards:** Loaded child Blueprints or external Blueprints referencing the target widget member return `InvalidOperation` before any mutation.
- **Persistence Boundary:** Memory-only mutation with skeleton refresh (`skeleton_regenerated=true`, `saved=false`). Package saving and full recompilation remain separate caller concerns.
- **Test Namespace Hierarchy:** Tested under sibling namespace `Cortex.UMG.RenameWidgetSlotFirstAnimationBinding` (sibling naming prevents Unreal's hierarchical automation tree from masking the existing leaf test `Cortex.UMG.RenameWidget`). Sibling dual-registration was verified via `RIP48-rename-sibling-registration-green-20261001.log` (`RunTests Cortex.UMG.RenameWidget` executed both `Cortex.UMG.RenameWidget` and `Cortex.UMG.RenameWidgetSlotFirstAnimationBinding` with 2/2 Success, QueueEmpty 2 / status 0, Warnings/Errors/Fatals 0/0/0).
- **Verification Boundary:** Regression verified via historical `RIP48-rename-animation-green-20261001.log` (under former namespace: 1/1 Success, 0/0/0; all forward, runtime object resolution, authored key/track/range, and Undo/Redo assertions passed), sibling registration `RIP48-rename-sibling-registration-green-20261001.log` (2/2 Success, 0/0/0), and final case-sensitivity verification `RIP48-rename-case-sensitive-green-20261001.log` (both original `Cortex.UMG.RenameWidget` and `Cortex.UMG.RenameWidgetSlotFirstAnimationBinding` 2/2 Success, QueueEmpty 2 / status 0, Warnings/Errors/Fatals 0/0/0). Live MCP smoke verification in `Saved/RIP48Validation/live-case-corrected-20261001.json` (Editor PID 38904, bound to Source SHA256 `1fbac25cac8de59d102ed2cb6d5cc5d31cf7217e04034dbfee5528287ed0c541`) confirmed wrong-case lookup refusal (`WIDGET_NOT_FOUND`), case-only new name refusal (`INVALID_OPERATION`), collision/identifier/stale-fingerprint refusals with clean state preservation, exact no-op behavior, and live FastMCP `umg_cmd` rename execution (`changed=true`, `saved=false`, compile 0 errors/0 warnings, save, `core.reload_asset`, and readback clean). Full Graph suite acceptance is independently blocked by a reproduced GC crash; this rename correction does not establish its cause or resolution.
