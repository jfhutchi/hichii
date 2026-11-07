# Five fixes: final verification and publication

## Source and publication

- Starting fork: `9fa4a71fa051aa857e2d8f370aa31739a4f062c2`.
- Reviewed, tested and packaged source:
  `3fc0a6ad257ab99a53fa62348463bd92c5a114b6`.
- Direct Lichii parent integrated with history:
  `71268a75e806a92f55c21f2577aaa19c6e9d6692`.
- Published branch: <https://github.com/jfhutchi/hichii/tree/fix/amd-five-issues>.
- Matching source:
  <https://github.com/jfhutchi/hichii/commit/3fc0a6ad257ab99a53fa62348463bd92c5a114b6>.

Authenticated git transport created the fix branch without force. A fresh
`git ls-remote --heads origin refs/heads/fix/amd-five-issues` returned the exact
tested source SHA. This closing report and the completed plan are documentation
only; their final remote SHA is verified again after publication. No upstream
issue comments or closures are required for this result.

The direct-parent update includes 59 upstream commits, including PR432 and
PR353, rather than duplicating their already-merged fixes. The separate broader
Eglantine head `aff8b2569b9` was surveyed and was not merged wholesale. The
user-requested controller task replaces the separate planned #401 task; the
latest parent happens to include its texture-statistics correction as well.

## Completed selected fixes

| Fix | Implementation and evidence |
|---|---|
| #357 storage-buffer fog bounds | Backend-reported Vulkan/OpenGL limits, continuous read-only table reach, alignment and descriptor rounding, saturating sizes and allocation-width rejection. Commits `f7e87d594d` and `c77eed544a`; preserved through the direct-parent merge. |
| Controller/profile persistence | Correct handheld slot, remembered successful profile identity, read/write failure propagation, per-game guards, deleted identity cleanup and handheld style preservation. Commits `9bb48385d0a`, `b89521f5536`, `2e7fcfdbed7`. Real temporary INI files and fresh-process reloads cover mappings and explicit clears. |
| #325 NaN-defined shaders and cache migration | Adopted PR432; extended scalar/half/atomic emission checks, v53-before-v52 environment migration with matching usage history, and actual cached-native retranslation. Commit `12087d36dd1`. Opaque driver-cache copying remains a distinct existing policy. |
| #323 fragment output masks/state | Adopted broader PR353 component masks and blit invalidation, shared static/dynamic policy, real compiler metadata and state-tracker regressions, and dual-source separation. Commit `0da3a4c3ae9`. |
| #293 bounded pipeline retention | Physical-memory-derived soft budget, strict 9000-frame floor, exact oldest eligible quota, protected current/building entries and GPU-safe retirement. Commits `5c074085606` and `3fc0a6ad257`; the followup freezes candidate identities and ages against worker completion between passes. |

The upstream prerequisite retains scheduler ownership, quiet-aware split-thread
drain and portable migration behavior. Merge integration exposed a guest-memory
allocation failure crash; `41fd0500fe8` shares checked allocation between initial
setup and lightweight restoration. Its regression failed before the correction
and passes afterward.

Each task received independent spec and code-quality review. A separate final
cross-task source review approved `3fc0a6ad257` without actionable findings.

## Fresh final verification by the primary agent

The final verification runs were performed after the task reviews, against the
clean final source. All prior baseline video/NUP failures are resolved by the
adopted parent updates; none is excluded from the final suite.

| Check | Final result | Private local evidence |
|---|---|---|
| Main test/CLI build and full suite | Exit 0; **874 cases / 70,026,834 assertions**, seed `3165333734`, zero failures | `.local-tools/final-main-{build,tests}.log` |
| Fresh Qt GUI/CLI production build | Exit 0, 12-job Release build; SCM revision exactly `3fc0a6ad257ab99a53fa62348463bd92c5a114b6` | `.local-tools/final-gui-{configure,build}.log` |
| Post-build CTest | **4/4 pass**: SDL state, updater, input profiles, config | `.local-tools/final-gui-ctest.log` |
| Controller caller checks | **7/7 pass** | `tools/nx2/test_controller_profile_wiring.py` |
| Fragment/blit caller checks | **5/5 pass**, including negative source mutations | `tools/nx2/test_fragment_color_mask_wiring.py` |
| Eviction/retirement caller checks | **6/6 pass**, including candidate snapshot and negative source mutations | `tools/nx2/test_pipeline_eviction_wiring.py` |
| SPIR-V validation | **14/14 generated test modules pass**, all SPIR-V 1.5, `--target-env vulkan1.2` | `.local-tools/final-spirv-validation.log` |
| Entire source range whitespace check | Pass | `git diff --check 9fa4a71..3fc0a6a` |

The 14 modules comprise the 13 NaN/native-cache/atomic modules from Task 3 and
one dual-source module from Task 4. Their emitter source remains unchanged by
the retention work. Task reports retain red/mutation/restoration evidence;
stale mutation objects were explicitly rebuilt before accepting green results.
Assertion totals vary with randomized/concurrent cases; the final count and seed
above identify this particular full run.

Toolchain: clang-cl 23.1.5, VS2022 MSVC 14.44 headers, CMake/Ninja and static Qt
6.11.1. The Qt build links MSVC 14.51 runtime libraries required by that bundle.
Production WERROR remains enabled. The documented pre-existing test-only
unused-global exception remains; third-party builds emit ignored-argument,
packing and static-getopt warnings, so this is not a warning-free build claim.
The optional QtTest compatibility target remains disabled because the bundled
Qt distribution lacks that component.

## Separate Windows package

Folder: `C:/Users/JHutc/Desktop/hichii-five-fixes-test-3fc0a6ad25`.

Archive: `C:/Users/JHutc/Desktop/hichii-five-fixes-test-3fc0a6ad25.zip`.
Size: **50,017,052 bytes**. ZIP SHA-256:

```text
B481A0C2F4C4EB4B487A17315EED6B9BB5FE5A4364FDBC386F8D26B2FDBF456A
```

| Executable | Bytes | SHA-256 |
|---|---:|---|
| `lichii.exe` | 65,370,624 | `80F7540C447E54CD525118575D8208C99F18E67B921205C236B05336D2C1CB77` |
| `eden-cli.exe` | 40,922,624 | `DDE817D96A831BF154BE3DD0D41386883801290F81CED801C8EAB191E34BB2F2` |

The archive contains 36 entries. Both executable copies match the built files.
It includes license texts, the build manifest, Microsoft runtime DLLs and the
optional shader-compiler DLLs used by the installed distribution. PE imports
resolve against the package or this Windows system, including API-set contracts;
this is not a guarantee of every dynamic dependency on another installation.

The included `user/README.txt` preserves a separate portable data directory when
extracting the ZIP. No game data, firmware, keys, saves or controller configuration
is bundled. Configure this copy before gameplay testing. It was not launched by
the agent. The package and ZIP checksum sidecar remain local and are not uploaded
with source commits.

## Preserved installation and remaining hardware checks

The installed executable at
`C:/Users/JHutc/Desktop/lichii-windows-x86_64/lichii.exe` remains byte-identical:

```text
C9AA37E8B52162E94E113E92CAEAD085C41C4937A2F815D02644D7DF56A863FD
```

All config tests use temporary paths. No emulator was started or stopped, and no
live game session, user shader cache or installed user configuration was changed.

The local machine is Intel/RTX; the friend's Ryzen 7 5800X3D / Radeon 7900 XTX is
unavailable. The evidence establishes the corrected source behavior, builds,
regressions and module validity. It does not establish Radeon pixels, a full
Vulkan draw/blit/draw execution, real GPU completion timing, physical controller
operation, all Switch features, complete audio behavior or sustained 60+ FPS.
The retention count is a memory heuristic and soft budget, not a hard bound on
driver memory or a measured frame-rate gain.

Hardware follow-up scenarios remain Hyrule Field fog, haze after area transitions,
mountain ridges, repeated-area frame times, and save/reopen of the user's actual
controller mappings. These limits are separate from completion of the five
source corrections, verified fork update and isolated test-package delivery.
