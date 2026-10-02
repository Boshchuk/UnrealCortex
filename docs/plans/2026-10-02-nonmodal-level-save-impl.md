# Non-modal level saving implementation (issue #148)

1. Add `Cortex.Level.Streaming.SaveLevel.NeverSaved` before changing production
   behavior. Verify the old command returns the wrong error without blocking.
2. Add `CortexErrorCodes::LevelNotSaved`. Guard the persistent package and editor
   filename in `SaveLevel`, then scope unattended-script mode to the engine save.
3. Preserve the existing saved-map response. Change the existing save tests'
   exclusive-write probe to append mode so it does not truncate map content.
4. Build and run rendering-enabled native Level automation, inspect raw logs,
   and run the non-live Python suite. Record actual results and platform limits
   in `docs/verification/2026-10-02-issue-148.md`.
5. Create a PR, obtain independent subagent review, resolve actionable findings,
   merge after checks, verify issue closure, and remove task-owned temporary
   scripts/branches. Return the plugin and sandbox checkouts to current `main`.
