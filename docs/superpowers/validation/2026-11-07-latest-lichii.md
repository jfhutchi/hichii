# Direct Lichii parent integration

## Source and scope

The user requested the latest direct-parent Lichii source in the existing fork.
Merge `549bbeb7942` combines the reviewed local controller/fog/prerequisite head
`2e7fcfdbed7821d77e82b11c8e778ab0f61d5815` with direct parent `lichii/nx2`
at `71268a75e806a92f55c21f2577aaa19c6e9d6692`, preserving both histories.
The parent adds 59 commits since the starting fork revision and includes changes
after the published build 95 release (tag `build-94`). The broader divergent
Eglantine head `aff8b2569b9` is not merged.

The separate integration worktree first advanced to the reviewed local head.
After the merge commit it was detached, freeing `integration/lichii-latest` for
the development checkout with existing build caches. The fix branch stays at
the reviewed local head until the root's independent reviews and fast-forward.
No emulator installation or real user configuration was changed.

## Merge decisions

- `buffer_cache.h`: retain the reviewed local read-only-table range policy:
  backend limit in the binding-cache key, alignment prefix, continuous mapping,
  page-rounded descriptor clamp, saturating wide sizes and allocator-width
  rejection. The overlapping parent PR351 uses a fixed 64 KiB reserve instead.
- `gl_buffer_cache.h`, `gl_device.{h,cpp}`, `vk_buffer_cache.h`: retain the local
  APIs and the 64-bit OpenGL driver query without replacing small/zero reported
  limits with an assumed 128 MiB minimum. Both backend consumers remain present.
- `maxwell_fast_method.cpp`: retain all local real-worker idle/quiet/ownership
  tests and append the three parent macro handback/upload tests. Parent unsigned
  comparison corrections remain present.
- `buffer_cache_ports.cpp` merged textually but introduced a duplicate runtime
  range method. Remove the parent's fixed-return duplicate, keeping the local
  configurable runtime and the parent's unsigned download-count comparison.
- Preserve whole-branch `WriteMemory` scheduler ownership, the DLSS-only pointer
  sizing accessor and the split worker's sequentially consistent wake protocol,
  quiet publication, held-idle-hook guarantee and stop notification. Parent GPU
  thread pinning additions compose with these changes.
- Reviewed controller persistence and portable migration production files have
  no changes relative to `2e7fcfdbed7`. All three controller fix commits remain
  ancestors. The expected root-authored spec update `31747b5cbfe` is retained.

Parent PR432 NaN-defined SPIR-V operations and cache version 54, PR353's broader
fragment-component mask policy and BlitColor invalidation, and incidental texture
bookkeeping changes are adopted intact. Additional NaN migration/regression and
fragment-mask verification work remains in the next planned stages; this merge
does not claim those stages or bounded pipeline retention are complete.

Only `build-check.yml` and `release-binaries.yml` change under CI. The former uses
read-only repository permissions for production builds/tests; the latter retains
its existing release workflow with parent build/packaging changes. The previously
excluded PR-retarget workflow stays absent. No workflow was dispatched, release
published, issue commented on, or branch pushed during this stage.

## Allocation integration correction

The first full test invocation exited before a Catch2 summary. A compact duration
report and then a focused successful-assertion report isolated an access violation
(`0xC0000005`) in the new `System rejects missing guest memory before kernel
initialization and retries` test, after its first failed `Initialize` and before
the first `Load` result. The original full wrapper reported exit 1; the direct
diagnostic and focused invocations exposed the native access-violation exit code.

The textual merge had kept the prerequisite's direct `device_memory.emplace()`
inside `InitializeKernel`. That path restores memory released at Switch 2 shutdown
without rerunning full system initialization. The latest parent separately added
checked allocation inside `Initialize`; the direct restore bypassed that check,
leaving the optional engaged with a null backing pointer before `kernel.Initialize`.

Commit `41fd0500fe8` factors the parent's existing allocation/check into
`AllocateGuestMemory`, used by both callers. The kernel path only considers full
reinitialization if allocation succeeded; otherwise it returns the parent's
`ErrorMemoryAllocation` after exactly one retry. Normal lightweight restoration,
memory-layout reinitialization and guest-RAM release remain intact. The existing
new parent regression supplies the red/green test: it crashes before this change
and all 5 allocation cases / 52 assertions pass afterward, including both initial
failure and changed-memory-layout sections. No tests are disabled or weakened.

## Verification environment

Windows Release builds use clang-cl 23.1.5, VS2022 14.44 headers, CMake/Ninja and
12 compile jobs via `.local-tools/build.ps1`. Production warnings remain errors.
The preexisting test-only `expected_callback` warning suppression remains scoped
to `core_timing.cpp`. The GUI cache uses bundled static Qt 6.11.1 and the existing
14.51 runtime library path; optional Qt6Test compatibility tests remain explicitly
disabled because the bundle lacks Qt6Test. OpenGL is enabled and Vulkan production
sources are compiled. Automatic update checking remains disabled in these caches.

## Verification results

| Check | Result | Local evidence |
| --- | --- | --- |
| Main production tests and CLI build, including Vulkan/OpenGL | Passed, rebuilt after allocation correction | `.local-tools/latest-lichii-main-build.log`, `latest-lichii-memory-fix-build.log` |
| Qt GUI, CLI, profiles/config, updater and SDL test targets | Passed; final build recompiles core and relinks GUI/CLI/profiles | `.local-tools/latest-lichii-gui-build.log`, `latest-lichii-gui-final-build.log` |
| Allocation failure/retry regression | 5 cases, 52 assertions, passed | `.local-tools/latest-lichii-memory-red.log`, `latest-lichii-memory-fix-tests.log` |
| Full main suite after correction | 855 cases, 69,930,850 assertions, zero failures | `.local-tools/latest-lichii-final-tests.log` |
| Buffer, scheduler ownership, split quiet and macro handback | 30 cases, 442 assertions, passed | `.local-tools/latest-lichii-buffer-split-tests.log` |
| Actual QtConfig/InputProfiles persistence | 8 cases, 208 assertions, passed | `.local-tools/latest-lichii-profile-tests.log` |
| SDL window-state, updater, profiles and configuration CTest targets | 4/4 passed, repeated after final relink | `.local-tools/latest-lichii-ctest.log`, `latest-lichii-ctest-final.log` |
| Controller caller structural checks | 7/7 passed | `.local-tools/latest-lichii-controller-wiring.log` |

The previous 791-case baseline had failures in `nx2_video_render.cpp` at lines
151 and 210 and `nup_writer.cpp` at line 671. The parent's video contract/test
corrections and NUP archive lifetime fix remove all three. The accepted baseline
for this integration is now **zero failures**, not the old three exceptions.

Representative commands (all from the development checkout):

```powershell
& ./.local-tools/build.ps1 -LogName latest-lichii-main -RunTests
& ./.local-tools/build.ps1 -LogName latest-lichii-memory-fix `
  -RunTests -Filter '[host_memory_failure]'
& ./build-amd-vs2022/bin/tests.exe --reporter compact `
  --out .local-tools/latest-lichii-final-tests.log
& ./build-amd-vs2022/bin/tests.exe `
  '[buffer_ports],[split_quiet],[split_owner],[gpu_split_dependencies]' `
  --reporter compact --out .local-tools/latest-lichii-buffer-split-tests.log
& ./.local-tools/build.ps1 -BuildDirectory build-amd-gui `
  -Targets yuzu,yuzu-cmd,input_profile_tests,config_tests,update_checker_tests,sdl_window_state_tests `
  -LogName latest-lichii-gui
# Repeat with -LogName latest-lichii-gui-final after the core integration fix.
& ./build-amd-gui/bin/input_profile_tests.exe --reporter compact `
  --out .local-tools/latest-lichii-profile-tests.log
& 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' `
  --test-dir build-amd-gui `
  -R '^(input_profile_tests|config_tests|update_checker_tests|sdl_window_state_tests)$' `
  --output-on-failure
python -I tools/nx2/test_controller_profile_wiring.py -v
git diff --check
```

The main suite and focused regressions ran against the final production source
in `41fd0500fe8`. The final GUI build explicitly recompiled `core.cpp` and linked
`lichii.exe`, `eden-cli.exe` and `input_profile_tests.exe` after the correction.
All four selected CTest executables then passed again. Source spec review passed
at that commit; independent quality review and the fix-branch fast-forward are
owned by the root after this handoff. The integration worktree remains detached
at the merge, and the development checkout remains on `integration/lichii-latest`.

## Limits

Only headless unit/configuration fixtures run. No GUI or game was launched or
stopped; no game data, keys, live configuration or captures were accessed. These
checks do not establish Radeon rendering correctness, controller hardware behavior,
FPS, or frame pacing. Updater-enabled release packaging and remote CI execution
are not exercised by these local builds.
