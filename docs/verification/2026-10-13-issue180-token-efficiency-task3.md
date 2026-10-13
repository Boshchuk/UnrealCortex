# Issue 180 — Task 3 verification record (explicit native/cached pagination, no cursor skipping)

Date: 2026-10-13. Status: implemented; controller-observed RED, GREEN and native-backed public
smoke recorded below; the selector-owning oversize proof script is prepared and awaiting the
controller's execution. Plugin branch `feat/issue-180-token-efficiency`, base
`ce303dd7a436d0d04e47f3dc95c5da2c96d7eb3c` (the Task-2 state). The commit identity is recorded in
the append-only follow-up docs commit.

## Contract implemented

* `tools/routers.py`
  * `NATIVE_LIMIT_COMMANDS` (`(domain, command)`): `data.query_datatable`,
    `data.search_datatable_content`, `data.search_assets`, `level.list_actors`,
    `reflect.find_overrides`, `reflect.find_usages`, `reflect.get_dependencies`,
    `reflect.get_referencers`, `reflect.search`, `statetree.list_assets`, `editor.list_cvars`,
    `anim.list_assets`, `gen.list_jobs`. Dispatched **before** limit validation: `limit`/`offset`
    reach the owner unchanged (native `limit=0` unlimited and `limit>200` keep their native
    meaning), no `_pagination`/Python cursor is added, a `cursor` refuses `INVALID_CURSOR` before
    dispatch, and the data is formatted by `format_native_page`. `_should_forward_limit_to_cpp`
    (the previous per-command special case) was removed.
  * `CACHED_PAGE_ARRAYS` (qualified read → owner array, identities verified in the C++ owners):
    `data.list_datatables`→`datatables`, `data.list_data_assets`→`data_assets`,
    `data.list_curve_tables`→`curve_tables`, `data.list_string_tables`→`string_tables`,
    `data.list_gameplay_tags`→`tags`, `level.find_actors`→`matches`,
    `level.list_actor_classes`/`level.list_component_classes`→`classes`,
    `graph.list_graphs`→`graphs`, `graph.search_nodes`→`results`,
    `graph.list_event_handlers`/`graph.find_event_handler`/`graph.find_function_calls`→`nodes`,
    `blueprint.list`→`blueprints`, `blueprint.list_scs_components`→`components`,
    `material.list_materials`→`materials`, `material.list_instances`→`instances`,
    `material.list_collections`→`collections`, `material.list_nodes`→`nodes`. Largest-list guessing
    is gone; a limit on a non-native read outside the mapping is validated 1–200 and dropped so the
    read stays complete.
  * Cached pages emit the largest prefix whose complete compact text **including** `_pagination`
    fits; `returned`/`next_cursor` come from the emitted rows with an absolute offset (no skipped
    rows). If no row fits, the page refuses (`RESPONSE_TOO_LARGE`, `reader_complete: false`) with no
    `_pagination` and no advancing cursor. A `cursor` carries the owning command: a cursor for
    another command or for a non-mapped read refuses `INVALID_CURSOR` before dispatch;
    missing/expired snapshots refuse `CURSOR_EXPIRED`.
  * Guarded order preserved: core specials → `graph.apply_patch` → property-binding read/write →
    animation authoring → UMG binding inspect/remove → native → cached cursor/limit → normal.
* `pagination.py`: cursors carry `command`/`array` (optional kwargs, so existing 3-argument callers
  and forged-cursor tests keep working); entries keep command/array/template provenance; a cursor
  whose key/offsets are not the expected types refuses `INVALID_CURSOR` instead of failing while
  slicing.
* `response.py`: `format_native_page(data, qualified_command, params)` is complete-or-refuse — the
  full page, or a refusal that keeps the window/count scalars, echoes `request_params`, sets
  `reader_complete=false`/`_native_page_omitted=true`, exposes no continuation and names a smaller
  limit/field selection. `format_response` gained `qualified_command`/`request_params` (the short
  `command`/`detail` arguments are retained for the dedicated animation/detail/binding policies) and
  a selector policy table that runs after the protected branches and before generic truncation:
  `data.get_data_asset`→`fields`, `material.get_node`→`properties`, `umg.get_tree`→
  `depth`/`max_nodes`/`names_only` + `get_widget`, blueprint
  `get_class_defaults`/`list_settable_defaults`/`list_inherited_properties`→`properties` +
  `include_values`/`discovery_limit`, `get_capabilities`→`detail=names`,
  `get_data_catalog`/`data.get_data_catalog`→`sections` + `include_asset_paths`,
  `query_class_detail`/`reflect.class_detail`→`detail=summary`,
  `query_class_context`/`reflect.class_context`→`detail=summary`/`properties`. Refusals keep scalar
  selector/identity context, never advertise `limit`, and a native **error envelope is never
  rewritten** into a selector refusal.
* `src/tools/reflect/{referencers,dependencies,overrides,usages}.py`: the standalone native-owned
  readers now format through `format_native_page` with their exact qualified command, so their
  oversized native pages cannot fall into the generic array truncator either.
* Consumers: `tests/test_mcp_scenarios.py` (the `e2e`+`stress` subgraph case) reads the wanted nodes
  through the native `node_ids` selector in one complete-or-refuse response instead of paging
  `nodes` while keeping the whole edge topology. `graph.get_subgraph` stays out of cache ownership
  (`RunTrace`/`GetSubgraph` emit `nodes` **and** `edges`). `editor.get_recent_logs` is not
  advertised before Task 11.

## Controller-observed RED (pre-implementation)

Named tests: `tests/test_pagination_contract.py tests/test_routers.py tests/test_response.py` →
**40 failed, 101 passed in 3.85 s** (artifact `artifact://361`). Failure layer: missing
`NATIVE_LIMIT_COMMANDS`/`CACHED_PAGE_ARRAYS`/`format_native_page`, native limits and the 7-item
cached page intercepted by Python pagination, an oversized cached page returning 20 rows whose
continuation then skipped to row 20, and object reads refusing without `reader_complete`.

Native-backed public baseline on the same commit (`smoke-task3-native.py --baseline`, owned editor
PID 39252 / port 8743, 80-row disposable table + 7 owned tables + 150 owned TargetPoints, runtime
10.50 s) reproduced the same consumer failures: `data` 60/offset 7 returned the native default 25
rows plus Python pagination `total=25`; `level.list_actors` limit 150 returned 100 rows plus Python
pagination `total=100`; `editor.list_cvars` limit 500 returned `INVALID_LIMIT`; the cached 7-item
read with `limit=2` returned all 7 in one page. Cleanup 150/150 actors, 7/7 assets.

## GREEN (controller)

```
rtk proxy uv run pytest tests/test_pagination_contract.py tests/test_routers.py tests/test_response.py -v
```
**125 passed in 0.64 s.**

Full-file regression (all guard suites preserved):
```
rtk proxy uv run pytest tests/ -m "not e2e and not scenario and not stress" -v
```
**1043 passed, 265 deselected in 36.56 s** (artifact `artifact://390`), including
`test_graph_prune_response_boundary.py`, `test_graph_retirement_response_boundary.py`,
`test_graph_rewire_response_boundary.py`, `test_graph_authoring_contract.py`,
`test_blueprint_safe_patch.py`, `test_umg_animation_bindings.py`, `test_umg_property_bindings.py`
and `test_response.py` — the guarded graph/UMG/property-binding refusals and authoring outcomes are
unchanged.

## Native-backed public smoke (controller), candidate

Same fixture, script `smoke-task3-native.py` without `--baseline`, runtime 11.98 s:

* `data_cmd(query_datatable, {limit: 60, offset: 7})` — rows exactly the native oracle window
  `[7:67]`, `total_count=80`, **no** `_pagination`/Python cursor.
* `level_cmd(list_actors, {folder, limit: 150})` — exactly 150 rows, no Python cursor.
* `data_cmd(list_datatables, {path_filter: <owned folder>, limit: 2})` — four contiguous pages,
  7 unique paths.
* Cleanup 150/150 actors, 7/7 assets.

### CVar limit follow-up (controller)

* `editor_cmd(list_cvars, {pattern: 'r.Shadow.CSM', limit: 500})` — 11 variables, 0 commands;
  the public payload equals the actual native payload for that selector, no Python cursor
  (PASS, 3.90 s). This is the populated native-limit proof.
* `editor_cmd(list_cvars, {pattern: 'r.Shadow', limit: 500})` — the native answer is 225 variables,
  whose compact JSON exceeds 40000 characters, so the public route returns the truthful refusal
  `_error=RESPONSE_TOO_LARGE`, `reader_complete=false`, `_native_page_omitted=true`, the request
  selector/window metadata, `returned_count=225`, `total_matched=225` and **no** Python cursor
  (`smoke-task3-cvars.py`, whose initial expectation of an equality comparison was a fixture
  expectation bug, not a product defect).
* The earlier `pattern='r.*'` probe returned 0 variables: the native pattern is a substring match,
  so that probe is not counted as populated-limit evidence.

### Gen limitation (explicit)

A fresh `tools/list` on the running server shows `gen_cmd` **unavailable**, so the native
`gen.list_jobs` limit=0 behaviour has no live proof in this task. It stays source-owned/declared
until Task 13 provides the native fixture proof; no live Gen claim is made here.

## Selector-owning oversize proof (controller-executed)

`smoke-task3-oversize.py` (task-owned; removed by the controller once the evidence is recorded)
creates naturally oversized owned fixtures and asserts both layers:

| read | fixture | native check | public check |
|---|---|---|---|
| `data.get_data_asset` | `/Game/Temp/CortexTask3Oversize_<stamp>/DA_T3Oversize` (`CortexTestDataAsset`, `TestProperty` = 50 000 chars) | owner JSON > 40 000 compact chars | refusal, `reader_complete=false`, `asset_path` kept, no `properties`, no `_truncated`, guidance names `fields`, no `limit` |
| `material.get_node` | `M_T3Oversize` + `Constant` node with `Desc` = 50 000 chars | owner JSON > 40 000 compact chars | refusal, `reader_complete=false`, `node_id` kept, no `properties`, guidance names `properties`, no `limit` |
| `umg.get_tree` | `WBP_T3Oversize` with 420 `TextBlock`s under a `CanvasPanel` root | owner tree JSON > 40 000 compact chars | refusal, `reader_complete=false`, `asset_path` kept, no `root`, guidance names `depth`/`max_nodes`/`names_only` + `get_widget`, no `limit` |

A size-caused control follows the DataAsset case: shrinking `TestProperty` makes the same public
read return the complete properties again. All owned assets are deleted in `finally`. Source
preconditions verified before choosing the fixtures: `GetDataAsset` returns a deep `properties`
object; `GetNode` returns every `CPF_Edit` property of the expression and `UMaterialExpression::Desc`
is `EditAnywhere` (`MaterialExpression.h`); `BuildWidgetTreeJson` carries name/class/children only
(no widget text), so the ordinary tree must be oversized by widget count;
`umg.get_tree` without `include_property_bindings` adds no binding state.

Selector recovery itself remains Tasks 5/10: this proof asserts the refusal and never claims that
selected native values were recovered.

## Round 2 — real oversize smoke defects: RED and bounded refusal-context fix

The controller executed `smoke-task3-oversize.py`. Native thresholds passed (DA 50463, MAT 50146,
TREE 63081 chars with 421 widgets), the DA and MAT public refusals passed, DA shrink control
returned the complete read, and cleanup removed all 3 owned assets. Two real defects were found:

1. **`umg.get_tree` refusal lost the selector.** Native `umg.get_tree` returns
   `root`/`total_widgets` only — no `asset_path` — while `_refuse_oversized_read` built its context
   from `_scalar_context(data)` only and ignored the `request_params` the router already passes, so
   the public refusal carried no asset identity.
2. **The refusal budget was not guaranteed.** `_scalar_context` keeps arbitrary unbounded top-level
   strings and `format_native_page` only popped `request_params`. Reproduced live through stdio
   against the owned editor: `editor_cmd(list_cvars, {pattern: 'x'*50000, limit: 500})` returned a
   refusal of **50480 characters**, i.e. the refusal itself broke the 40 000-character contract
   (`smoke-task3-refusal-budget.py`, retained for GREEN).

Permanent RED regressions staged before any fix (production untouched): the material and UMG tree
policy tests now use native-realistic data (no `asset_path` in the native response) and require the
caller's selector in the refusal, and a new `TestRefusalContextBudget` class pins the CVar-shaped
native refusal (≤40 000 chars, `_omitted_context == ["pattern"]`,
`returned_count`/`total_matched`/`limit` retained), the direct `format_native_page` budget case, the
no-omission case, and the policy refusal that omits an oversized native scalar (`asset_class`) while
keeping `asset_path`.

Controller RED for these regressions: **5 failed, 124 passed in 0.92 s** (artifact://426) —
missing request asset identity for material/tree, the unbounded 50 462-character native pattern
refusal, and the unbounded native scalar (`asset_class`) refusal.

Fix implemented (`response.py`, one shared helper; guard ordering untouched):
`_selector_context(data, request_params)` takes native scalars win-first and lets request selectors
fill only the gaps, so a refusal always reports the window the owner actually applied
(`level.list_actors` clamps the offset to >=0 and the limit to 1..1000 and emits the applied
values — request `limit=5000, offset=-7` must surface as `1000/0`); arrays/nested payloads and
`_`-prefixed telemetry keys are excluded, and the context is flattened to the top level.
`_bounded_refusal(payload, context)` keeps every refusal ≤ `MAX_RESPONSE_CHARS` by dropping the
largest remaining context value whole and naming its key in `_omitted_context` (sorted, present only
when something was omitted): a value is never truncated, no fixed refusal field is ever dropped, and
the native page array is never echoed or shortened. `_refuse_oversized_read` now receives the
router's `request_params`; the previous `_scalar_context` and the `request_params` echo were
removed. Regression added: `test_native_refusal_reports_the_authoritative_applied_window`
(requested 5000/-7 with native 1000/0/total 5000 → refusal metadata `1000/0/5000`).

## Round 2 correction — controller-observed GREEN and real native proof

Content identity of the plugin files committed for this task (SHA-1 of the file bytes; the commit
identity is recorded in the append-only follow-up ledger entry):

| file | blob |
|---|---|
| `MCP/src/cortex_mcp/pagination.py` | `b757eeba57df32297adf10d7361eaf354ab5fc61` |
| `MCP/src/cortex_mcp/response.py` | `0bab18d82bc81734a7faae257bca159c8ae9a591` |
| `MCP/src/cortex_mcp/tools/routers.py` | `013aaf176688366bad5e93e02d8fda90fda6140d` |
| `MCP/src/tools/reflect/dependencies.py` | `67bd26c9c7e8f1c45f98a591c86109ce8fd619f7` |
| `MCP/src/tools/reflect/overrides.py` | `30b71afef99d58e5758673bbaa337c6f5b9cfe66` |
| `MCP/src/tools/reflect/referencers.py` | `ca7206a0a574a8fc4829f814da6e4576c4fa5568` |
| `MCP/src/tools/reflect/usages.py` | `e6e1caa2615a2e4a8ccbce4c6f99d7f46eb42819` |
| `MCP/tests/test_mcp_scenarios.py` | `a98fec3cd9b92189f4357f693062cf0029c90709` |
| `MCP/tests/test_response.py` | `6d140f8433d01e84c1b479767147e2d403b2aca4` |
| `MCP/tests/test_pagination_contract.py` | `a422c32d991c2cff810d30a192a688d23e499815` |

Controller evidence after the fix (same owned editor PID 39252 / port 8743):

* Named suite `tests/test_pagination_contract.py tests/test_routers.py tests/test_response.py` →
  **130 passed in 0.46 s** (the 5-failure RED for this correction was artifact://426).
* Real native refusal-budget probe: `editor_cmd(list_cvars, {pattern: 'x'*50000, limit: 500})` →
  bounded refusal of **510 characters**, `_error=RESPONSE_TOO_LARGE`, `reader_complete=false`, no
  `pattern` echo (3.09 s). The 50 480-character refusal is gone.
* Real Data/material/tree oversize smoke (native 50 463 / 50 146 / 63 081 chars) → all public
  refusals pass with the caller's selector identity preserved and no false completeness; the DA
  shrink control returns the complete read; cleanup removed all 3 owned assets (12.14 s).
* The full sweep (1043 passed / 265 deselected, artifact://390) predates this scoped correction; the
  controller ruled the 130-test suite plus the real native regressions sufficient, so unchanged
  suites were not repeated.
* Tracked integration proofs (not missing Python contracts): native `gen_cmd` is unavailable in the
  running server → Gen `limit=0` fixture proof at Task 13; selected-value recovery for the
  object/tree reads at Tasks 5/10.
* Owned editor lifecycle: the controller verified `CORTEX_EDITOR_PID=39252` / project
  `CortexSandbox` (`connection._pid=39252`, port 8743) and issued `core.shutdown` with
  `force=false` (success); no default/mounted route to the user's Ripper editor was used. The raw
  task editor log severity scan found only the startup Zen recovery warnings (lines 1063–1064) with
  no fixture/authoring errors, ensures or fatal lines at scan time (the shutdown itself was not yet
  logged). This is not a zero-warning manual-session claim.

## Files changed

`MCP/src/cortex_mcp/tools/routers.py`, `MCP/src/cortex_mcp/pagination.py`,
`MCP/src/cortex_mcp/response.py`, `MCP/src/tools/reflect/{referencers,dependencies,overrides,usages}.py`,
`MCP/tests/test_pagination_contract.py` (new), `MCP/tests/test_response.py`,
`MCP/tests/test_mcp_scenarios.py`, and the parent-repo `docs/systems/python-mcp.md` §Pagination /
Response Guard sections (scoped local, like the Task-1/2 parent-doc changes).

`cortex-toolkit/resources/mcp-tool-reference.md` §Pagination still describes the pre-Task-3
"all list commands" contract and the `string_table_refs` limit-forwarding note; it needs the same
cutover in the toolkit repo (Task 17 synchronization) and was deliberately not edited without
toolkit authority.
