# Task 5: bounded Switch 2 pipeline retention

## Provenance and scope

Adapted [Lichii PR313](https://github.com/zortos293/Lichii/pull/313) for
[issue 293](https://github.com/zortos293/Lichii/issues/293), cumulatively from
base `588bfa611be9853729682dd78416c66e8d56385a` through
`8ca6ee9e26c21cdc2658b6d03fc26e3d12497e4f`. The reviewed chain is:

- `fc31275785a7adf6cfa80f927cffc5ffb1dbe45b`: original keep-all / boot-all approach.
  Those unbounded behaviors are not adopted.
- `90879c10e7102503e4679b13a7720e7ec1aff643`: physical-RAM retention budget and
  restored boot preload caps. Its unrelated `LoadDiskResources` failed-pipeline
  exclusion is deliberately omitted; the existing `if (pipeline)` remains.
- `c3bc77b21c27b95c9849a18cd7a1cf64e691369e`: restores the strict 9,000-frame
  stale-age floor after upstream AMD device-loss observations.
- `8ca6ee9e26c21cdc2658b6d03fc26e3d12497e4f`: exact oldest selection across ties.

Original code credit: Jordan <madebyjordan@users.noreply.github.com> and
monsterlag <64719697+monsterlag@users.noreply.github.com>. The source commits
also credit Claude Opus 5.5 <noreply@anthropic.com>. Commit trailers preserve
these credits. The selector's algorithm is unchanged; local adaptation adds
small production-used budget, eligibility and quota functions plus a typed
candidate snapshot for headless tests and asynchronous-build safety.

The implementation started from `0da3a4c3ae92ae8182b151eda01d3f4ab4e92196` on
`fix/amd-five-issues`, including the reviewed latest-parent integration and four
prior fixes. No branch switch occurred. Boot caps, NaN cache migration, storage
layout changes, shader emission and live configuration remain unchanged.

## Policy and ownership

Switch 2 retains all lookup entries at or below
`max(3000, physicalRAM / 8 / (450 * 1024))`: 8 GiB gives 3,000, 16 GiB gives
4,660, and 32 GiB gives 9,320. A function-local static caches the budget derived
from `Common::GetMemInfo().TotalPhysicalMemory`. A static assertion ties its
minimum to the unchanged 3,000-entry boot limit.

Above that budget, only nonnull, noncurrent, built pipelines with
`last_use < frame_count - 9000` can retire, after the explicit
`frame_count > 9000` guard. Collection uses a null short circuit before accessing
pipeline members and captures each eligible entry's iterator and last-use frame.
Selection and retirement operate only on that snapshot. The retirement
quota is exactly `min(lookup_count - budget, eligible_count)`. Oldest selection
uses the cutoff value itself and a separate tie count, without `cutoff + 1`.
Switch 1 retires all eligible stale pipelines. MoltenVK retains its early return.

The budget counts every lookup entry, including null/current/building entries;
it excludes the retired ownership queue. Young or protected entries can keep the
lookup above budget. Deferred ownership and actual driver allocation also mean
this is not a hard RAM cap.

Retirement retains the existing sequence: obtain `scheduler.CurrentTick()`, erase
the pipeline from `used_this_session` and record its key hash when necessary,
move its owning pointer into `evicted_pipelines`, then erase the lookup entry.
After any retirement every remaining nonnull pipeline clears both transition
vectors. `TickFrame` is unchanged: advance the frame, pop retired owners only
inside the nonempty queue loop guarded by `scheduler.IsFree(front().first)`, and
check eviction every 600 frames. No test-only lifetime framework was introduced.

## Red/green evidence

First extracted the existing age-only eligibility and quota behavior into the
production-used helpers. Before changing that policy, the regression required
zero retirements below and exactly at the Switch 2 budget. The compiled old
behavior returned 20 for both assertions:

- RED: **2 cases, 6 assertions; 1 case / 2 assertions fail**, both the intended
  under/equal-budget failures. Age-floor checks passed. Main tests and the actual
  `vk_pipeline_cache.cpp` consumer compiled and CLI linked for this red stage.
  `.local-tools/task5-age-only-red-{build,tests}.log`.
- GREEN retention: **9 cases / 997 assertions**, zero failures, seed 3781035945.
  `.local-tools/task5-retention-only-tests.log`.
- GREEN cache/build coverage: **37 cases / 1,559 assertions**, zero failures,
  seed 3194417832; tags include pipeline eviction, pipeline cache, cache files,
  pipeline build and prebuild. `.local-tools/task5-focused-tests.log`.

Coverage includes zero/small/8/16/32-GiB and maximum-u64 memory, budget quotient
boundaries, under/equal/one-over/many-over quotas, zero eligible, Switch 1,
frames 0/8999/9000/9001 and maximum-u64 strict age boundaries, and null/current/
building protection. Selector tests independently permute construction and
traversal orders against a sorted-prefix oracle for every quota from zero through
count plus one. Cases include `{100,200,200}`, duplicates, all-equal values,
empty inputs and `{u64max,u64max,u64max-1}`. Mixed metadata uses the actual
production helpers and checks the expected retired ages for both console modes.

Structural coverage reuses `code_only`, delimiter balancing and method extraction
from `test_fragment_color_mask_wiring.py`. **5 of 5 checks pass** in
`.local-tools/task5-wiring.log`. In-memory mutations of the actual methods reject
missing/negated completion checks, unconditional queue pops, wrong retirement
ticks, removed owning moves, erase-before-move, missing transition cleanup or
either transition vector, missing MoltenVK guard, missing null short circuit,
different collection eligibility, unfiltered `Take`, and quota bypass. Comments,
strings and unrelated-method decoys cannot satisfy the lifetime checks. A checker
fixture initially selected an earlier MoltenVK occurrence and a formatting-sensitive
anchor; both were corrected to target the eviction method before final validation.
These mutations never rewrote production files or produced mutated build objects.

## Initial validation before quality-review follow-up

- Main Release `tests` and `yuzu-cmd` pass with clang-cl 23.1.5, VS2022 MSVC14.44
  headers, WERROR, Ninja and 12 jobs. The final focused build explicitly recompiles
  the retention test and `vk_pipeline_cache.cpp` and relinks tests/CLI.
  `.local-tools/task5-focused-build.log`. Existing generated-version compiler
  warning `-fsized-deallocation` remains unrelated to this change.
- Full main suite: **873 cases / 70,028,114 assertions**, zero failures, seed
  1719836940. All 864 pre-task cases remain with nine added retention cases;
  concurrency-dependent assertion totals vary between runs.
  `.local-tools/task5-full-{build,tests}.log`.
- Static Qt 6.11.1 GUI targets `yuzu`, `yuzu-cmd`, `input_profile_tests`, and
  `config_tests` build successfully; the GUI cache also compiles the changed
  Vulkan production unit. `.local-tools/task5-gui-build.log`.
- GUI CTest: **4 of 4 pass** (`input_profile_tests`, `config_tests`,
  `update_checker_tests`, `sdl_window_state_tests`).
  `.local-tools/task5-gui-ctest.log`.
- `git diff --check` passes. Source self-review confirms no unrelated boot,
  preload, migration, shader-emitter or retirement-ownership changes. Independent
  spec and quality reviews are coordinated by the parent before publication.

No shader emitter changed, so the previously validated 13 NaN/atomic modules and
one dual-source module were not regenerated. Structural checks verify wiring,
not execution of a live Vulkan device's GPU completion. No Radeon gameplay,
frame-rate improvement or hard memory ceiling is claimed. No emulator/GUI was
started or stopped, no game/keys/configuration were touched, and nothing was pushed.

## Quality-review follow-up: freeze candidate membership

The initial source-spec review passed under a stable-candidate assumption, but
quality review found a P2 race in commit `5c074085606b978a1970121737e6e5ccbbb15b09`.
`PipelineBuild::IsBuilt()` reads `state->complete` atomically; a worker can set it
between collection and retirement. With collected ages `{100, 200}`, quota two,
and second-pass order `200, newly-built 150, 100`, reevaluating the same predicate
admits 150, exhausts the quota, and leaves 100. The original green tests and
same-predicate structural checks were insufficient to establish a stable set.

The follow-up captures typed cache iterators and ages in
`PipelineEvictionCandidate`. `SelectOldestPipelineCandidates` invokes the same
`OldestPipelineSelection` only on those captured ages and removes unselected
records. Retirement traverses the selected vector directly. A newly completed
pipeline waits until a later eviction check; neither its identity nor changed
last-use metadata can enter the current selection. Switch 1 also retires from
the captured eligible set, and all existing quota, age, ownership and cleanup
rules remain in force.

`Common::unordered_map` is Boost 1.92 `unordered_flat_map`. Its local
`detail/foa/table.hpp` erase forwards to `detail/foa/core.hpp`, which destroys
only the designated element and recovers that slot; no other entry is moved.
There are no insertions or rehashes between capture and retirement. The new
deterministic test uses that actual Common/Boost container and erases selected
iterators, rather than relying on a std::unordered_map lifetime assumption.

Before the production correction, the added method-scoped snapshot requirement
failed on the actual prior source: **1 of 6 structural checks failed**, with the
diagnostic that retirement must traverse captured identities instead of rescanning
live build flags. `.local-tools/task5-snapshot-red.log`. The new C++ case captures
ages 200 and 100, completes the previously building middle entry at age 150, 200,
or 50, and checks quotas one and two. It also changes a live age after capture to
ensure selection uses the recorded value. Both quotas preserve the new entry;
quota one retires identity 100, and quota two retires the original 100/200 pair.
This exercises the shared production snapshot selector without constructing
fake Vulkan pipelines or copying its selection algorithm into the test.

Final follow-up validation supersedes the initial results above:

- Focused cache/build/retention: **38 cases / 1,583 assertions**, zero failures,
  seed 2618141366. `.local-tools/task5-snapshot-focused-tests.log`; its build log
  records fresh test and Vulkan cache production compilation plus CLI relink.
- Full main: **874 cases / 70,029,655 assertions**, zero failures, seed 1215636218.
  `.local-tools/task5-snapshot-full-tests.log`. This adds one deterministic case
  to the prior 873; concurrency-dependent assertion totals still vary.
- Structural checks: **6/6 pass**, with a new negative mutation replacing
  snapshot traversal with a live cache rescan. The checker also requires a
  single original candidate-collection site and no eligibility/age reads in
  retirement. `.local-tools/task5-snapshot-wiring.log`.
- GUI targets `yuzu`, `yuzu-cmd`, `input_profile_tests`, `config_tests`: pass;
  changed Vulkan cache production unit rebuilt. GUI CTest: **4/4 pass**.
  `.local-tools/task5-snapshot-gui-build.log` and
  `.local-tools/task5-snapshot-gui-ctest.log`.
- `git diff --check`: pass. No other policy or attribution changes. Independent
  source-spec and quality review are requested again for this correction.
