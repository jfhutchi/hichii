# Five emulator fixes, with Radeon and Ryzen priority

Status: concrete design for user review; implementation has not started.

## Scope and isolation

Update the user's fork, https://github.com/jfhutchi/hichii, with five independently
traceable issue fixes. Work in `fix/amd-five-issues` in the separate checkout
`C:/Users/JHutc/Desktop/hichii-issue-fixes`, starting at
`9fa4a71fa051aa857e2d8f370aa31739a4f062c2`.

The installed emulator, graphics-test package, controller settings, saves, game
dump, and separate native-port experiment remain outside this checkout. Do not
start or stop the emulator. Do not publish game data or captures. Publish verified
source changes to a fix branch in the user's fork and report their exact status.

## Selected issues

### 1. Radeon overbright fog: Lichii #357

Report: https://github.com/zortos293/Lichii/issues/357

Read-only storage bindings reached through pointer tables stop at 64 MiB in
`BufferCache::StorageBufferBinding`. Zelda's light-probe records can lie roughly
130 MiB into the mapped pool. These out-of-range reads produce incorrect fog.

Adapt https://github.com/zortos293/Lichii/pull/351 at
`fd4141e48c35e7436abc36b0031e2d05782bf4bf`, with attribution. Bound read-only
table-pointer pools by the continuous mapped range, the actual backend storage
buffer limit, and 1 GiB. Saturate mapping lengths before narrowing. Account for
alignment and final binding rounding using actual requirements. Preserve writable
pool limits and the existing OpenGL path, with its own queried limit.

Tests cover reads beyond 64/128 MiB, small device limits, unaligned pointers,
rounded final descriptor sizes, mapping discontinuities, lengths above u32, null
bindings, and unchanged writable pools. Reported RX 9070 XT measurements support
the mechanism; local Radeon gameplay verification remains required. #422 has a
matching symptom but is not a sixth fix or independently proven duplicate.

### 2. Radeon NaN haze and green water: Lichii #325

Report: https://github.com/zortos293/Lichii/issues/325

SPIR-V FMin/FMax/FClamp leave NaN behavior undefined. Use NMin/NMax for the guest's
floating min/max behavior and compose clamp as NMin(NMax(value, low), high),
including float atomic compare-exchange helpers. Adapt the reviewed source and
tests from https://github.com/zortos293/Lichii/pull/432, heads
`d3697a8fbbb37ab5527ed580b0e3eb8db06ce5e2` and
`4437a12176299f71fe957d21d30ded4b5f9b5261`, preserving original attribution.

Exercise emitted SPIR-V for 32/64-bit min/max, saturation, clamp, both clamp
profiles, and float atomic loops; validate generated modules where possible.
Advance the pipeline cache version and audit migration so shader environments
are retranslated rather than reusing obsolete generated output. Preserve prior
files and copy only structurally compatible caches. This fix contains NaN spread;
it does not replace the independent pointer-range correction in issue 1.

### 3. Radeon mountain-ridge color artifacts: Lichii #323

Report: https://github.com/zortos293/Lichii/issues/323

A present fragment program with no color exports currently leaves guest color
write masks enabled. Undefined Vulkan outputs then corrupt bound attachments.
Adapt https://github.com/zortos293/Lichii/pull/339 at
`64d9962846b48a377f98321df5d7c861fd89c14a`: suppress color writes only when all
fragment color exports are absent, in both static and EDS3 dynamic state. When
pipeline exports change, invalidate blending and color-mask state even if guest
registers did not change. Preserve absent fragment stages, partial outputs,
dual-source behavior, and depth/stencil operations.

Tests exercise every guest component mask, all attachments, absent versus
present fragment programs, partial/dual outputs, repeated bindings, transitions
in both directions, and unrelated dirty flags. The affected production Vulkan
translation units must compile as well as the policy tests.

### 4. Repeat area-entry shader stutter on Ryzen: Lichii #293

Report: https://github.com/zortos293/Lichii/issues/293

The report uses a Ryzen 7 9800X3D. Pipelines are destroyed solely because they
were unused for 9000 frames, even when memory capacity permits retaining them.
Returning to an area rebuilds hundreds at once. This is CPU/GPU-vendor-neutral
and therefore relevant to the friend's Ryzen/Radeon setup, without promising a
measured frame-rate gain on that unavailable machine.

Port the bounded Switch 2 retention policy from
https://github.com/zortos293/Lichii/pull/313, rather than the unbounded retention
in https://github.com/Djihads80/Eglantine/pull/696. Keep pipelines while under a
host-memory-derived budget (one eighth of physical RAM using the port's measured
450 KiB estimate, with its existing boot-limit floor). Above budget, evict only
the required number of oldest eligible stale pipelines. Keep current, building,
and recently used pipelines; retain deferred GPU-safe destruction and transition
cleanup. Preserve legacy Switch 1 and MoltenVK behavior. The budget is a heuristic
count, not a guarantee of actual AMD driver memory consumption.

Tests verify under-budget retention, exact excess counts, age eligibility,
ordering across all cutoff ties and iteration orders, zero candidates, maximum
frame values, and preservation of active/in-flight objects. Repeated-area live
frame-time checks are a separate requirement from policy unit tests.

### 5. Unrequested texture-cache bookkeeping: Lichii #401

Report: https://github.com/zortos293/Lichii/issues/401

`debug_stats` starts true and is only updated inside the TC_LOG-gated report.
Without the variable, normal gameplay continually inserts diagnostic shape keys
and never clears them. This is unnecessary CPU/memory work on Ryzen and other
processors; no isolated FPS benefit has been measured.

Adapt https://github.com/zortos293/Lichii/pull/428 at
`f39d00ca9a040114c5bc911ccce9c3b27d4d1d9e`: default off, initialize before image
creation from both TC_LOG presence and the HW_GPU debug log level, and preserve
the periodic update/clear behavior. Detailed collection is off without explicit
request, including when the log level is Debug but TC_LOG is absent.

Verify all four environment/log-level combinations, first-window behavior,
guarded shape-map insertion, and production compilation. A focused structural
regression is acceptable if constructing the complete runtime is impractical;
do not label it as a measured performance test.

## Upstream prerequisite and alternatives

Repository AGENTS requires upstream-first review. Fetched Eglantine nx2 is
`0857e8b4a792ac99b87af6bb6f329127aa7df683`. The branches have
diverged: 186 fork-only and 26 upstream-only commits. A non-mutating merge-tree
check reports conflicts in README.md, buffer_cache.h, kepler_compute.h, and
gpu_split.cpp. Integrate upstream before fixes, preserving fork behavior and the
upstream scheduler ownership/quiet-state changes; audit each resolution and run
the baseline again. Review filesystem migration effects and retain portable
test data isolation.

Recommended approach is attributed, reviewed ports with targeted regressions.
Writing five new fixes would duplicate existing investigations. Merging broad
performance forks would introduce unrelated shader, input, audio, and rendering
changes that make these five results hard to verify. Experimental behavior flags
do not ship on nx2, per repository AGENTS.

Other reports investigated but not selected:

- #236: TexturePass already contains the integer/depth guard at commit
  `68caa6c75e`; backend hardening is not counted as fixing a new defect.
- #285: #354 proposes a larger descriptor-array compatibility implementation;
  its 64-entry/clamping and unsupported-device limitations need separate work.
  It is not silently included as part of the fog fix.
- #379: the dump does not identify the page-table lock owner. Releasing the
  texture-cache mutex alone does not prove the GPU/guest wait cycle is broken.
  No speculative locking change is included.

## Verification and completion evidence

Establish unchanged baseline build/test results, then demonstrate meaningful
regressions fail before each fix and pass afterward. Run affected production
builds, targeted tests, full relevant suite, whitespace checks, and independent
spec/code review. Build a separate Windows test package for the owner; never
replace the working installation.

The local machine has an RTX 4080 SUPER and Intel CPU. Its tests can establish
code validity and NVIDIA regressions, not Radeon pixel correctness or Ryzen
frame times. Record exact unverified hardware scenarios, including Hyrule Field
fog, Kokiri-to-Deku-to-Kokiri haze, mountain ridges, and repeated area entry.
Use an owner-launched capture-enabled test session if needed; do not inject into
the existing emulator. Keep the overall goal active until the selected fixes,
verification, and fork update are actually established. Do not promise 60 FPS
or claim five gameplay issues resolved from compilation alone.
