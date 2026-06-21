# Verifying the editor + CortexAnimation additions on UE 5.6

This branch adds three things (PR: editor `run_python` + CVar commands + the new
`anim` domain). They were authored and compile/link-verified on **UE 5.4.4** and
run 14/14 against a live 5.4.4 editor. This doc is the repeatable procedure to
confirm the same on **UE 5.6** — a clean compile + a live functional pass.

---

## ⚡ Quick run (one command, auto-syncs results)

On the 5.6 machine: clone this branch into a 5.6 project's `Plugins/Developer/`,
then run the bundled task script. It builds the editor target, launches+waits for
the editor, runs the e2e suite, writes a report under `verify-results/`, and
commits+pushes that report back to this branch so the result **syncs to you with
no manual reporting**:

```powershell
cd Plugins/Developer/UnrealCortex/MCP/tests
./verify_ue56.ps1 -Engine "C:\EpicGames\UE_5.6" -Project "C:\dev\<HostProj>\<HostProj>.uproject" -LaunchEditor
```

- Add `-SafeDDC` if the editor hangs on launch (ZenServer DDC); `-SkipBuild` if
  already built; `-NoPush` to keep the report local.
- Outcome lands in `verify-results/UE-<host>-<stamp>.md` with a one-line **Verdict**
  (`PASS` / `BUILD FAILED` / `TESTS FAILED` / `EDITOR NOT REACHABLE`). Raw build/pytest
  logs stay local (gitignored); only the report syncs.
- After it pushes, the result is on this branch — `git pull` elsewhere to read it.

The manual steps below are the same thing unrolled, for when you want to drive it
by hand or the script can't launch the editor on your setup.

---

## Prerequisites

- A UE **5.6** engine (source or launcher).
- Any UE 5.6 **project** to host the plugin (a blank C++ or Blueprint project is
  fine — the plugin is `Type: Editor`, stripped from shipping).
- [`uv`](https://docs.astral.sh/uv/) for the Python MCP test runner.
- An interpreter for `uv` to use (uv will fetch one if none is on PATH).

## 1. Install the plugin into a 5.6 project

```bash
# from the host project root
mkdir -p Plugins/Developer
git clone -b upstream-pr/cortex-gems https://github.com/Boshchuk/UnrealCortex.git Plugins/Developer/UnrealCortex
```

Enable it in the host `.uproject` (or let the editor prompt on first open):

```json
{ "Plugins": [ { "Name": "UnrealCortex", "Enabled": true } ] }
```

## 2. Build the editor target (the real compile check)

```bash
"<UE5.6>/Engine/Build/BatchFiles/Build.bat" \
  <ProjectName>Editor Win64 Development \
  "-Project=<abs path>/<ProjectName>.uproject" -WaitMutex
```

**Pass criteria:** build succeeds. The modules of interest are `CortexCore`
(new `CortexDeferredExec`), `CortexEditor` (run_python + CVar ops, new
`PythonScriptPlugin` dep), and the new `CortexAnimation` module. If anything
needs a 5.6 API tweak it will surface here — see "If the build breaks" below.

## 3. Launch the editor, then run the live self-test

Open the project in the 5.6 editor (CortexCore writes `Saved/CortexPort-*.txt`),
then:

```bash
cd Plugins/Developer/UnrealCortex/MCP
uv sync
uv run pytest tests/test_cortex_gems_e2e.py -v
```

**Expected:** `14 passed`. Notes:
- The `anim.get_*` / authoring tests need at least one **Skeleton** and one
  **AnimSequence** in the project; if the project has none, those tests **skip**
  with a clear message (not a failure). Use a project with a character/anim
  content pack for full coverage, or migrate one skeletal mesh in.
- If **all** tests skip with "editor not reachable", the editor isn't running or
  the plugin didn't load — recheck step 2.
- If only the `anim_*` tests skip with "anim domain not registered", the
  `CortexAnimation` module didn't load — confirm it built and is in the
  `.uplugin` Modules list.

## What each section proves

| Test group | Exercises |
|---|---|
| `run_python_*` | `editor.run_python` — exec, output capture, `defer=true` (next-tick path), error reporting, missing-param guard |
| `*_cvar*` | `editor.get_cvar` / `set_cvar` (round-trip + restore) / `list_cvars` |
| `anim_get_*` | Phase A inspection — list_assets, skeleton/sequence info |
| `anim_*_round_trip` | Phase B authoring — socket and float-curve create→verify→delete (undo-wrapped, self-cleaning) |

## If the build breaks on 5.6

The code targets APIs present in both 5.4 and 5.6, but a couple of animation
APIs are version-sensitive. Most likely suspects and their locations:

- **Curve identifier / controller** — `CortexAnimAuthorOps.cpp` uses
  `UAnimSequenceBase::GetController()` + `FAnimationCurveIdentifier(FName,
  ERawCurveTrackTypes)` + `IAnimationDataController::AddCurve/SetCurveKeys`.
  If 5.6 changed these, adjust there.
- **Notify authoring** — direct `Notifies.Add()` + `FAnimLinkableElement::Link`
  + `SortNotifies` (no controller path for notifies in 5.4; verify 5.6 still
  exposes the same).
- **AnimBP state-machine introspection** — `CortexAnimInspectOps.cpp` uses
  `UAnimGraphNode_StateMachineBase::EditorStateMachineGraph` and
  `UAnimStateNode::GetStateName()` from the `AnimGraph` editor module.

Report any 5.6 build error (file:line + message) back and it can be patched
behind a small version shim, mirroring how the toolkit already handles
`StateTree` across 5.4/5.5/5.6.
