# PR #182 independent review — remediation

**Date:** 2026-10-14
**Review:** external read-only review published on PR #182 (findings B1–B3, N1) at head `b6b880eb`
**Subject:** the declared-contract boundary plus the response/pagination formatter

## Verdicts and fixes

### B1 — the router silently removed an undeclared `limit` before dispatch (confirmed, wider than reported)

`make_router` stripped `limit` for every command outside the page-owner maps, so
`umg_cmd("set_text", {…, "limit": 1})` reached C++ without it and the strict declared-contract
validator never saw the undeclared key. The same branch also stripped the **declared** `limit` of
`umg.list_animation_bindings`, whose C++ handler validates it and emits its own
`next_offset`/`is_complete` continuation — so that command's documented contract ("forwards `offset`,
`limit` … directly to C++ without generic pagination wrapping or parameter mutation",
`docs/systems/python-mcp.md`) was false, and an oversized page would have been silently re-cut in
Python with a continuation computed for a larger page.

Fix: only cache-owned page owners consume `limit`/`cursor`; every other command keeps its parameters
verbatim so the native validator can refuse them. `umg.list_animation_bindings` is registered as a
native page owner (complete-or-refuse formatting, `limit` reaching the handler).
`umg.remove_animation_binding`'s documented refusal of reserved pagination parameters now actually
receives `limit`.

### B2 — an oversized native-page refusal republished an advancing continuation (confirmed)

`format_native_page` promises a refusal that "exposes no advancing continuation", but
`_selector_context` copied the native `next_offset`/`next_cursor` into it, so a client advancing on
that continuation would skip the rejected page. Fix: those two keys are dropped from the refusal
while the applied window start, totals and completeness flag remain.

The existing test asserted the absence of fields its input never contained; it now feeds a
native-shaped continuation, making those assertions discriminating.

### B3 — the standalone hierarchy projection dropped the native completeness signal (confirmed)

`_flatten_hierarchy` returned only classes/total_classes/cpp_count/blueprint_count, so the native
walk's authoritative `truncated` flag was lost and a budget-cut hierarchy was indistinguishable from
a complete one. Fix: the flag is forwarded when the native reports it and documented in the tool
docstring.

### N1 — an explicitly empty alignment token was treated as omission (confirmed, fixed)

`set_alignment` refused only the both-empty case and skipped validation of an empty string, so
`{horizontal: "", vertical: "Bottom"}` applied the vertical axis while discarding the horizontal
request. Decision: a supplied token is judged on what was sent, so `""` refuses with
`INVALID_PROPERTY_VALUE` like any other unusable token. Mirrored in the toolkit documentation.

## Tests executed

| Target | Result |
|---|---|
| `CortexSandboxEditor Win64 Development` | build exit 0, no errors |
| `Cortex.UMG+` | 149/149 |
| `Cortex.UMG.SetterContract+` | 1/1 |
| Python `-m "not e2e and not scenario and not stress"` | 1103 passed (baseline 1093 + 10 new) |

Every new assertion was watched failing first:

- nine Python failures before the fixes — six strict setters carrying an undeclared `limit`, the
  `umg.list_animation_bindings` limit, the strengthened native-page refusal, and two hierarchy cases;
- four native failures for N1 (`supplied empty alignment token must be refused`, the
  `INVALID_PROPERTY_VALUE` code, and both axes unchanged), produced by temporarily reverting only the
  N1 token checks and rebuilding.

One existing assertion was removed rather than re-pinned:
`test_mixed_node_and_edge_read_is_never_cursor_paginated` asserted `"limit" not in params` for
`graph.get_subgraph`, which pinned the strip mechanism. That command declares no `limit` and no
handler reads one, so forwarding it changes no behaviour.

## Remaining risk

- `MCP/src/tools/data/data_assets.py` still constructs `class_filter`. It is not registered by the
  current server, so it is unreachable; it would refuse if it were re-registered.
- `umg.remove_animation_binding` refuses a `cursor` with `INVALID_CURSOR` (the router's cursor guard)
  while `docs/systems/python-mcp.md` describes reserved-pagination refusals as `INVALID_FIELD`. The
  refusal itself matches that documentation; only the error code differs.
