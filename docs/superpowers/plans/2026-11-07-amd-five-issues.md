# Five emulator issue fixes implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Follow the tasks below in order, with spec and code review after each implementation stage.

**Goal:** Correct Lichii #357, #325, #323, #293 and controller/profile persistence,
validate their source behavior, and publish the tested changes to jfhutchi/hichii.
The user's additional controller requirement replaces #401 and is implemented
immediately after the completed #357 fix. Remaining numbered tasks retain their
original identifiers for traceability.

**Architecture:** Keep the fork's existing behavior and integrate the reviewed upstream prerequisite at its pinned revision. Apply attributed, bounded ports with real production-path regressions; retain a separate portable test package and distinguish automated verification from hardware gameplay results.

**Tech Stack:** C++20, Vulkan/OpenGL, Catch2 3.13, clang-cl 23.1.5 with VS2022 MSVC14.44, CMake/Ninja, Python.

## User-directed latest Lichii synchronization

The user subsequently requested the latest upstream releases while controller
review was underway. Keep the user's fork and integrate its direct Lichii parent;
do not replace it with Eglantine or discard the local fixes.

- [x] Fetch and inspect direct parent `lichii/nx2` at
  `71268a75e806a92f55c21f2577aaa19c6e9d6692`. This adds 59 parent commits since
  the starting fork revision. Its latest published release is named build 95
  (tag build-94); the fetched source also includes subsequent commits.
- [x] Create separate `integration/lichii-latest` worktree at
  `C:/Users/JHutc/Desktop/hichii-upstream-sync`. No emulator installation changes.
- [x] Finish controller spec/quality review, fast-forward the integration worktree
  to the reviewed controller commit, then merge the pinned direct-parent head.
  Resolve the six surveyed conflicts by preserving tested fog allocation/descriptor
  bounds and backend limits, combining the unsigned-literal and prerequisite test
  changes, and retaining the split-worker scheduler ownership/idle guarantees.
- [x] Review and build the merged result on `integration/lichii-latest`. Merge
  `549bbeb7942` retains local fixes; `41fd0500fe8` corrects the interaction between
  guest-RAM restoration and upstream allocation validation. CLI and Qt GUI builds
  pass, full main suite passes 855 cases / 69,930,850 assertions, focused buffer/
  split tests pass 30 cases / 442 assertions, and four configuration/updater/SDL
  CTest targets pass. The three prior video/NUP failures are gone; baseline is
  zero failures. Evidence: `../validation/2026-11-07-latest-lichii.md`.
- [x] Complete independent integration spec/quality reviews, then fast-forward
  `fix/amd-five-issues` to the verified integration commit. Source spec review
  and quality review passed at `41fd0500fe8`, and both reviewers verified the final
  GUI relink and four passing CTest targets. The fix branch was fast-forwarded to
  the reviewed integration, including its documentation-only followups.
- [x] Reconcile tasks 3 and 4 with upstream's already-merged NaN PR432 and
  fragment-output PR353. Preserve those adopted fixes, add missing regressions and
  migration corrections in the still-open tasks below, and record provenance
  rather than porting duplicate code. Reconciliation does not complete those tasks.

Eglantine `upstream/nx2` was re-fetched and remains `aff8b2569b9`. Its much broader
divergent integration remains outside this direct-parent synchronization. The new
user request supersedes the earlier decision to defer the direct Lichii update.

## Task 1: Reviewed upstream prerequisite

Completed in afb21b57873ac1ab2cd6fe479c2da1f3df762382. Independent spec and
quality reviews passed. Full suite 784/787, three exact baseline failures;
focused regressions 101 assertions/9 cases, ten fresh processes passed.

Files: README.md; src/core/core.cpp; src/common/fs/*; src/yuzu/user_data_migration.cpp;
src/video_core/{buffer_cache/buffer_cache.h,engines/kepler_compute.h,gpu_split.cpp,gpu_split.h,gpu_thread.cpp};
src/tests/video_core/{gpu_split_idle.cpp,buffer_cache_ports.cpp}; src/tests/CMakeLists.txt.

- [x] Integrate the reviewed pinned prerequisite. The November 7 head aff8b2569
  changes 233 files with approximately 88 conflicts, including unrelated shader-lab,
  audio, CI and unbounded pipeline-retention changes. It was surveyed, not adopted
  wholesale. The approved design explicitly pins the smaller prerequisite:

```powershell
git merge --no-commit --no-ff 0857e8b4a792ac99b87af6bb6f329127aa7df683
```

- [x] Resolve only the four known conflicts. Keep fork README branding and its
  GPU split batching, profiling, seq_cst wake handshake and shutdown notification.
  Preserve automatic upstream guest-RAM release and migration changes, reviewing
  their interaction with portable paths. In kepler_compute.h add:

```cpp
bool IsDlssDispatch() const { return native_mode && dlss_chain.active; }
```

  In BindComputeStorageBuffer use that accessor for native pointer sizing, retaining
  the separate indirect-table argument. In WriteMemory guard the entire branch
  that can inline-upload or download GPU bytes with GpuSplit::OwnsScheduler().
  Retain upstream MarkGpuThread/OwnsScheduler and quiet-state publication.
- [x] Implement the real held-idle-hook regression before resolving the Drain
  behavior; use a friend fixture following existing test-access patterns, not a
  stand-alone fake lock. The test starts the real split worker in a scoped system
  context, holds its idle callback with a condition variable, consumes ring work,
  calls Drain on the producer, and asserts Drain cannot complete before the
  callback is released. All failure paths must release/join worker threads.
- [x] Require both Drain return paths to satisfy:

```cpp
consumed_cache == write_position && BackQuiet()
```

  After consumed catches up, request the idle hook once if not quiet. Publish
  quiet only after callback completion. Keep Backoff's consumed-only sleep check;
  sleeping on quiet without a corresponding wake would deadlock.
- [x] Cover no-hook/empty, requested-hook, new-work and stop/wakeup behavior;
  unauthorized CPU/service callers cannot execute either WriteMemory scheduler
  path. Check DLSS versus ordinary native versus table-pointer sizing.
- [x] Build production and tests, run targeted split/buffer/DLSS tests and the
  full suite, comparing the exact three preexisting failures in the baseline.
- [x] Commit the merge with conflict-resolution rationale and upstream references.

## Task 2: #357 read-only table storage reach

Completed in f7e87d594d and c77eed544a. Independent spec and quality reviews
passed. Focused buffer regressions: 364 assertions / 18 cases. Full suite:
788 / 791 cases, with the exact three baseline failures unchanged. Production
Vulkan/OpenGL consumers compiled. Radeon gameplay remains unverified.

Files: src/video_core/buffer_cache/buffer_cache.h;
src/video_core/renderer_vulkan/vk_buffer_cache.{h,cpp};
src/video_core/renderer_opengl/gl_buffer_cache.h; gl_device.{h,cpp};
src/tests/video_core/buffer_cache_ports.cpp.

- [x] Read immutable PR351 diff fd4141e48c35e7436abc36b0031e2d05782bf4bf.
  Add runtime GetMaxStorageBufferRange queries for both backends, with the GL
  query using glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE).
- [x] Extend the actual BufferCache fixture's Runtime with a configurable limit.
  Exercise StorageBufferBinding with table=true and read-only mapping beyond
  128MiB; the old 64MiB implementation must fail the new expected interval.
- [x] Add unaligned starts, limits below the prefix, lengths above u32, continuous
  mapping breaks, null mappings and unchanged writable/native policies. Run the
  new cases against old production code and record the failures.
- [x] Adapt the attributed port with saturating u64 arithmetic. Derive available
  bytes from the backend limit, base-alignment prefix and actual end rounding;
  do not use the original fixed 64KiB reservation. Clamp the final bound interval,
  not merely the requested payload. Preserve mapping continuity and 1GiB ceiling.
- [x] Run buffer regressions and compile both Vulkan and OpenGL consumers.
- [x] Commit with source PR/commit attribution and exact verification results.

## Task 3: #325 NaN-defined min/max/clamp

Implemented with independent source spec/quality reviews reporting no actionable
findings. Full main suite passes 858 cases / 69,841,324 assertions; focused tests
pass 17 cases / 201,545 assertions; 13 emitted SPIR-V modules validate; CLI/GUI
builds and four GUI CTest targets pass. Exact red/green and evidence boundaries:
`../validation/2026-11-07-nan-cache.md`. Radeon gameplay remains unverified.

Files: src/shader_recompiler/backend/spirv/{emit_spirv_floating_point.cpp,spirv_emit_context.cpp};
src/video_core/pipeline_cache_files.h; renderer_vulkan/vk_pipeline_cache.cpp;
src/tests/video_core/{spirv_nan_safe_min_max.cpp,pipeline_cache_files.cpp}; tests/CMakeLists.txt.

- [x] Confirm existing #295 VectorViewIsExact and global scalar-word fallback.
- [x] Adopt the existing PR432 production changes and tests through direct-parent
  commit `2e69168aae8`, preserving ancestry and attribution to reviewed source
  commits `d3697a8fbbb37ab5527ed580b0e3eb8db06ce5e2` and
  `4437a12176299f71fe957d21d30ded4b5f9b5261`. Do not port the same code twice:

```cpp
return ctx.OpNMin(type, ctx.OpNMax(type, value, zero), one);
```

  Plain float min/max and floating CAS helpers use OpNMin/OpNMax.
- [x] Extend adopted emitted-instruction regressions to f16x2 as well as f32x2
  atomic loops and all planned min/max/clamp profiles. Demonstrate red results
  with isolated mutations restoring original FMin/FMax/FClamp. Qualify comments
  to match evidence: documented NVIDIA behavior is not a direct guest capture.
- [x] Adopt the parent's pipeline cache version 54 advance.
- [x] Correct migration to choose the newest compatible environment plus
  matching used list (v53 then v52), preserving originals/existing destination.
  Existing cached environments must retranslate through the corrected emitter.
  Treat driver-blob compatibility separately and test its chosen migration policy.
- [x] Validate generated SPIR-V using spirv-val when available; run floating,
  storage-alignment and pipeline-cache regressions, and commit with attribution.

## Task 4: #323 fragment component masks, including no exports

Files: renderer_vulkan/{fragment_color_mask.h,vk_graphics_pipeline.cpp,
vk_graphics_pipeline.h,vk_rasterizer.cpp,vk_rasterizer.h,blit_image.cpp};
src/tests/video_core/fragment_color_mask.cpp; both affected CMakeLists.txt.

- [x] Adopt PR353 at `79c151694a6` through the requested direct-parent merge:
  static and EDS3 dynamic masks intersect guest masks with exported components
  per render target. Preserve this broader policy, including no-export suppression,
  and its BlitColor state invalidation. Do not replace it with the earlier narrow
  PR339 port at `64d9962846b48a377f98321df5d7c861fd89c14a`.
- [x] Add production-policy regressions covering all guest masks and attachments,
  absent/present fragment stages, partial/dual-source exports and unchanged depth/
  stencil behavior. Demonstrate failure when restoring unconditional guest masks
  or omitting transitions; share the tested policy across static and EDS3 paths.
- [x] Verify UpdateDynamicStates uses the already selected pipeline for masks.
  Cover internal DrawTexture's unrestricted output masks and transitions without
  guest register changes, including repeated bindings and unrelated dirty flags.
- [x] Verify the adopted PR353 BlitColor state-invalidation correction. Exercise real
  StateTracker invalidation between unchanged no-export states, in addition to
  helper tests. Add a focused structural regression for the exact BlitColor
  overload's top-level invalidation after recording, with negative mutations for
  missing, worker-lambda and wrong-overload calls. Record that these tests do not
  execute the full Vulkan draw/blit/draw path, which requires device resources.
- [x] Run mask/state regressions and compile all affected production units;
  commit with original PR attribution and recorded runtime limitations.

Evidence: `docs/superpowers/validation/2026-11-07-fragment-masks.md`.
Restored focused 6 cases / 4,714 assertions; full main 864 cases / 70,030,538
assertions; structural 5/5; emitted dual-source SPIR-V 1/1; GUI CTest 4/4.

## Task 5: #293 bounded pipeline retention

Files: renderer_vulkan/{pipeline_eviction_policy.h,vk_pipeline_cache.cpp};
src/tests/video_core/pipeline_eviction_policy.cpp; affected CMakeLists.txt.

- [ ] Adapt PR313 at8ca6ee9e26c21cdc2658b6d03fc26e3d12497e4f. Exclude its
  unrelated all-title boot-failure-cache change. Preserve boot preload caps.
- [ ] Add red regressions exercising under-budget retention and exact excess
  selection via the production policy, including cutoff ties in every order,
  zero candidates, maximum frames, current/building/recent exclusion and GPU-safe
  deferred destruction. Demonstrate that age-only eviction fails retention.
- [ ] Keep Switch2 pipelines below max(3000, physicalRAM/8/(450*1024)). Above
  budget retire only min(excess, eligible stale count) oldest candidates; retain
  the 9000-frame floor, legacy Switch1 and MoltenVK behavior, and transitions.
- [ ] Run cache/build/eviction tests, compile vk_pipeline_cache.cpp and commit.

## Task 6: Controller/profile persistence (prioritized after task 2)

Completed in `9bb48385d0a`, `b89521f5536` and `2e7fcfdbed7`; independent spec and
quality reviews passed before latest-parent integration. The merged controller
tests pass 8 cases / 208 assertions and all 7 caller wiring checks pass. See
`docs/superpowers/validation/2026-11-07-controller-persistence.md` for red/green,
fresh-process and file-isolation evidence.

Files: src/yuzu/configuration/{configure_input_player.*,input_profiles.*};
an integrated active-slot helper if needed; focused temporary-config tests and
their CMake wiring. Preserve the existing serialized binding format.

- [x] Demonstrate the active handheld slot mismatch and missing selected-profile
  identity with regressions that fail before the fix.
- [x] Use one active-slot resolver for profile create/load/save/selection. Update
  identity only on success, clear deleted identities without resetting bindings,
  and preserve global/per-game isolation and existing cancellation semantics.
- [x] Exercise actual QtConfig/InputProfiles save/destroy/reopen and fresh-process
  round trips for gamepad GUID/port, keyboard keys, sticks, motion, style,
  connection state and `[empty]` clears. Keep all files in temporary directories.
- [x] Compile affected GUI production paths and run focused tests without starting
  the emulator or touching the user's configuration. Record limitations, obtain
  independent spec then quality review, and commit.

Canceled: the original #401 texture-debug-bookkeeping task is replaced by this
user-requested fix. No separate #401 port is performed or counted; its incidental
parent changes are now included by the user-requested latest Lichii merge.

## Task 7: Full verification, review and separate package

- [ ] Run the tests and CLI production build, then a separate bundled-Qt GUI
  build and focused controller-config tests. The bundled Qt lacks Qt6Test; keep
  compatibility tests requiring that optional component distinct. Use 12 jobs.
- [ ] Run the full suite and targeted regressions; compare exact baseline failure
  identities, not just counts. Resolve any new failures. Preserve all evidence.
- [ ] Request independent spec review, address gaps, then independent code-quality
  review. Review the complete diff and git diff --check, including merge effects.
- [ ] Package a separate portable build with its own data folder. Do not copy
  game data or keys into public artifacts; do not launch/stop the owner's emulator.
- [ ] Record exact build hashes, source commits, fixes, tests and remaining
  hardware-only scenarios in docs/superpowers/validation/2026-11-07-five-fixes.md.

## Task 8: Update the user's fork

- [ ] Verify fork identity/push permission, compare remote fix-branch state, and
  push the tested branch without force. If a GitHub App connector cannot push git
  objects, use authenticated git transport; never print credentials.
- [ ] Verify the remote branch SHA equals the tested local commit. Provide source
  and local package links. Do not post to upstream issues or close reports.
- [ ] Reconcile every checkbox against authoritative evidence. Mark the overall
  goal complete only when all five source fixes, verification and fork update are
  established. Report Radeon gameplay/60FPS as unverified where untested.
