# Five emulator issue fixes implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Follow the tasks below in order, with spec and code review after each implementation stage.

**Goal:** Correct Lichii #357, #325, #323, #293 and #401, validate their source behavior, and publish the tested changes to jfhutchi/hichii.

**Architecture:** Keep the fork's existing behavior and integrate the reviewed upstream prerequisite at its pinned revision. Apply attributed, bounded ports with real production-path regressions; retain a separate portable test package and distinguish automated verification from hardware gameplay results.

**Tech Stack:** C++20, Vulkan/OpenGL, Catch2 3.13, clang-cl 23.1.5 with VS2022 MSVC14.44, CMake/Ninja, Python.

## Task 1: Reviewed upstream prerequisite

Files: README.md; src/core/core.cpp; src/common/fs/*; src/yuzu/user_data_migration.cpp;
src/video_core/{buffer_cache/buffer_cache.h,engines/kepler_compute.h,gpu_split.cpp,gpu_split.h,gpu_thread.cpp};
src/tests/video_core/{gpu_split_idle.cpp,buffer_cache_ports.cpp}; src/tests/CMakeLists.txt.

- [ ] Integrate the reviewed pinned prerequisite. The November 7 head aff8b2569
  changes 233 files with approximately 88 conflicts, including unrelated shader-lab,
  audio, CI and unbounded pipeline-retention changes. It was surveyed, not adopted
  wholesale. The approved design explicitly pins the smaller prerequisite:

```powershell
git merge --no-commit --no-ff 0857e8b4a792ac99b87af6bb6f329127aa7df683
```

- [ ] Resolve only the four known conflicts. Keep fork README branding and its
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
- [ ] Implement the real held-idle-hook regression before resolving the Drain
  behavior; use a friend fixture following existing test-access patterns, not a
  stand-alone fake lock. The test starts the real split worker in a scoped system
  context, holds its idle callback with a condition variable, consumes ring work,
  calls Drain on the producer, and asserts Drain cannot complete before the
  callback is released. All failure paths must release/join worker threads.
- [ ] Require both Drain return paths to satisfy:

```cpp
consumed_cache == write_position && BackQuiet()
```

  After consumed catches up, request the idle hook once if not quiet. Publish
  quiet only after callback completion. Keep Backoff's consumed-only sleep check;
  sleeping on quiet without a corresponding wake would deadlock.
- [ ] Cover no-hook/empty, requested-hook, new-work and stop/wakeup behavior;
  unauthorized CPU/service callers cannot execute either WriteMemory scheduler
  path. Check DLSS versus ordinary native versus table-pointer sizing.
- [ ] Build production and tests, run targeted split/buffer/DLSS tests and the
  full suite, comparing the exact three preexisting failures in the baseline.
- [ ] Commit the merge with conflict-resolution rationale and upstream references.

## Task 2: #357 read-only table storage reach

Files: src/video_core/buffer_cache/buffer_cache.h;
src/video_core/renderer_vulkan/vk_buffer_cache.{h,cpp};
src/video_core/renderer_opengl/gl_buffer_cache.h; gl_device.{h,cpp};
src/tests/video_core/buffer_cache_ports.cpp.

- [ ] Read immutable PR351 diff fd4141e48c35e7436abc36b0031e2d05782bf4bf.
  Add runtime GetMaxStorageBufferRange queries for both backends, with the GL
  query using glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE).
- [ ] Extend the actual BufferCache fixture's Runtime with a configurable limit.
  Exercise StorageBufferBinding with table=true and read-only mapping beyond
  128MiB; the old 64MiB implementation must fail the new expected interval.
- [ ] Add unaligned starts, limits below the prefix, lengths above u32, continuous
  mapping breaks, null mappings and unchanged writable/native policies. Run the
  new cases against old production code and record the failures.
- [ ] Adapt the attributed port with saturating u64 arithmetic. Derive available
  bytes from the backend limit, base-alignment prefix and actual end rounding;
  do not use the original fixed 64KiB reservation. Clamp the final bound interval,
  not merely the requested payload. Preserve mapping continuity and 1GiB ceiling.
- [ ] Run buffer regressions and compile both Vulkan and OpenGL consumers.
- [ ] Commit with source PR/commit attribution and exact verification results.

## Task 3: #325 NaN-defined min/max/clamp

Files: src/shader_recompiler/backend/spirv/{emit_spirv_floating_point.cpp,spirv_emit_context.cpp};
src/video_core/pipeline_cache_files.h; renderer_vulkan/vk_pipeline_cache.cpp;
src/tests/video_core/{spirv_nan_safe_min_max.cpp,pipeline_cache_files.cpp}; tests/CMakeLists.txt.

- [ ] Confirm existing #295 VectorViewIsExact and global scalar-word fallback.
- [ ] Import tests from PR432 commit4437a12176299f71fe957d21d30ded4b5f9b5261
  before its production changes; extend them to f16x2 as well as f32x2 atomic loops.
  Run the emitted-instruction tests red against original FMin/FMax/FClamp.
- [ ] Port attributed commits d3697a8fbbb37ab5527ed580b0e3eb8db06ce5e2 and
  4437a12176299f71fe957d21d30ded4b5f9b5261, adapting overlapping test lists:

```cpp
return ctx.OpNMin(type, ctx.OpNMax(type, value, zero), one);
```

  Plain float min/max and floating CAS helpers use OpNMin/OpNMax. Qualify comments
  to match evidence: documented NVIDIA behavior is not a direct guest capture.
- [ ] Advance cache version once; migrate newest compatible environment plus
  matching used list (v53 then v52), preserving originals/existing destination.
  Existing cached environments must retranslate through the corrected emitter.
  Treat driver-blob compatibility separately and test its chosen migration policy.
- [ ] Validate generated SPIR-V using spirv-val when available; run floating,
  storage-alignment and pipeline-cache regressions, and commit with attribution.

## Task 4: #323 no-export fragment masks

Files: renderer_vulkan/{fragment_color_mask.h,vk_graphics_pipeline.cpp,
vk_graphics_pipeline.h,vk_rasterizer.cpp,vk_rasterizer.h,blit_image.cpp};
src/tests/video_core/fragment_color_mask.cpp; both affected CMakeLists.txt.

- [ ] Import PR339 tests and production helper from
  64d9962846b48a377f98321df5d7c861fd89c14a, then demonstrate the tests fail when
  applying the old unconditional guest masks or omitting state transitions.
- [ ] Port the narrow no-exports policy, preserving absent fragment stages and
  partial/dual-source exports. Static and EDS3 paths share the tested policy.
- [ ] Use UpdateDynamicStates' existing draw_pipeline argument rather than
  querying CurrentGraphicsPipeline again. A DrawTexture internal blit must not
  inherit guest no-export masks.
- [ ] Adapt only PR353's BlitColor state-invalidation correction. Exercise the
  production mask/state path through no-export guest -> internal blit -> same
  guest state without register changes, in addition to helper tests.
- [ ] Run mask/state regressions and compile all affected production units;
  commit with original PR attribution and recorded runtime limitations.

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

## Task 6: #401 texture debug bookkeeping

Files: src/video_core/texture_cache/{texture_cache.h,texture_cache_base.h};
tools/nx2/test_texture_debug_stats.py if a structural regression is needed.

- [ ] Capture the failing source behavior with a focused structural check,
  covering constructor initialization before image creation and guarded shape
  insertion. Do not fake a full Vulkan runtime just to test this simple policy.
- [ ] Adapt PR428 atf39d00ca9a040114c5bc911ccce9c3b27d4d1d9e:

```cpp
bool debug_stats{};
// At the start of TextureCache's constructor, before any image creation:
debug_stats = std::getenv("EDEN_NX2_TC_LOG") != nullptr &&
              Common::Log::IsLevelEnabled(Common::Log::Class::HW_GPU,
                                          Common::Log::Level::Debug);
```

- [ ] Check all environment/log-level combinations and existing periodic
  reset/clear behavior. Compile the actual texture-cache consumers and commit.

## Task 7: Full verification, review and separate package

- [ ] Run the tests and CLI production build, then a separate bundled-Qt GUI
  build (YUZU_TESTS=OFF because bundled Qt lacks Qt6Test). Use 12 jobs.
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
