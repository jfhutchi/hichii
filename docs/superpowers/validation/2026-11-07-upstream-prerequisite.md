# Pinned upstream prerequisite validation

Task 1 merges reviewed upstream `0857e8b4a792ac99b87af6bb6f329127aa7df683`
into `fix/amd-five-issues`, starting from `f7b0486fb`. Later upstream integration
and the five selected issue ports remain separate work.

## Resolution decisions

- Preserve Lichii branding, split batching, `DrainSite` profiling, writer/sync
  positions, the sequentially consistent front wake handshake, strict
  `back_running` ownership diagnostics, and shutdown notification.
- Integrate upstream `df92fc6c95461d2fc07ab6357301e5a212e0e03f`: both Drain return
  paths require consumption and quiet. Quiet is published after the idle hook,
  including the no-hook path; a caught-up front requests that hook once. Keep
  Backoff's consumption-only sleep predicate so quiet publication needs no new wake.
- Apply scheduler ownership to the entire page-preservation branch, including
  the fork's inline upload and the upstream download. Preserve write notifications,
  range subtraction, and fast paths.
- Integrate upstream `8820bb79a` DLSS sizing scope via `IsDlssDispatch`, retaining
  the fork's indirect-table sizing, binding cache, and Fire Emblem hash helpers.
- Retain upstream filesystem defaults and legacy Eden migration. README now
  documents the `eglantine` default, portable priority, and explicit migration.
  GUI migration was reviewed in source, not executed against user data.
- Omit the new upstream `retarget_prs.yml` workflow: its automatic retargeting
  to `nx2-experimental` and PR comments are upstream policy, not this fork's policy.

`RequestIdleHook` requests pending idle work, not an unconditional callback when
already quiet. The production caller audit found only `Scheduler::WaitSubmitted`
(fence and buffer waits) besides Drain. `DeferFlush` sets pending work only on
the back; the idle hook flushes it before quiet publication. A redundant request
while already quiet therefore needs no second callback. The header and tests
state this contract explicitly. Stop/restart retains the existing position reset;
quiet is ignored when no back runs and is cleared before each restarted batch.

## Tests before integration

The initial focused run built successfully and failed 4 of 5 cases, with 12 failed
assertions. It reproduced both premature Drain returns with a real BackLoop
worker held inside its idle hook, the next-batch quiet race, CPU upload/download
through WriteMemory, and ordinary native compute using an oversized binding.
The initial written indirect-table assertion was corrected to preserve the
existing active-DLSS 8 MiB policy; the other red failures were unchanged.
Evidence: `.local-tools/upstream-red-build.log` and `upstream-red-tests.log`.

## Final checks

Build: `pwsh -File .local-tools/build.ps1 -LogName upstream-full -RunTests`.
The VS2022/clang-cl 23.1.5 build succeeds for `tests` and `yuzu-cmd`, with 12 jobs,
production warnings-as-errors, and the same test-only baseline warning exception.
The test executable uses `build-amd-vs2022/bin/user` for portable isolation.

Full core suite: **787 cases, 784 passed, 3 failed**; 69,878,885 assertions,
69,878,882 passed, 3 failed; random seed 1408555494. The three failure identities
match the baseline exactly:

1. `NX2 video refreshes cached scaler ranges from full to limited and back`,
   `src/tests/video_core/nx2_video_render.cpp:151`, frame byte comparison.
2. `NX2 full-range NV12 preserves same-size chroma and converts resized frames`,
   `src/tests/video_core/nx2_video_render.cpp:210`, frame byte comparison.
3. `NUP edits default-language metadata in place`,
   `src/tests/core/nup_writer.cpp:671`, return value 3 versus Success (0).

No new failures. Evidence: `.local-tools/upstream-full-build.log`,
`upstream-full-tests.log`, and `upstream-full-test-launch.log`.

The final focused filter `[split_quiet],[split_owner],[dlss_sizing]` passes
**101 assertions in 9 cases** on ten consecutive fresh process runs. These cover
both Drain return paths, empty and no-hook work, a requested hook between entries,
redundant requests while quiet, new work during quiet publication, sleeping-worker
stop, real back/GPU/CPU/service ownership, unauthorized CPU/service upload and
download prevention, owner preservation of neighboring GPU bytes, ordinary native
versus active/inactive DLSS sizing, bogus sizes, and the separate indirect-table
policy. The existing split restart and dependency tests also pass in the full run.
Repeat evidence: `.local-tools/upstream-repeat-1.log` through `upstream-repeat-10.log`.

The tests use the existing friend access to start the actual worker, scoped System
test contexts, bounded observations, and release/join cleanup for held hooks.
No emulator, game, real save, or hardware gameplay test was run. The GUI and its
migration dialog are not covered by this headless build. The full core suite is
not all green; its three documented baseline failures remain unresolved.
