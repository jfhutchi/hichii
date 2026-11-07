# Fragment component masks (#323 / PR353)

## Scope and provenance

This task verifies and factors the broader upstream PR353 implementation at
`79c151694a6`, already adopted by the requested direct-parent merge. It does not
replace that policy with the earlier, narrower PR339. The starting HEAD is
`12087d36dd1d7aae8b7a1906e3d92d9a1f8b0676` on `fix/amd-five-issues`.

`fragment_color_mask.h` contains the production packing/intersection policy used
by `GraphicsPipeline` and `RasterizerVulkan`, plus the existing dynamic blending
gates extracted into `FragmentColorMaskState`. The extraction preserves initial
mask zero, the `TouchBlending`/`TouchColorMask` short circuits, and updating the
remembered mask only when a color-mask update is requested. No additional
renderer abstraction or runtime switch is introduced.

The existing raw `VkImageView` `BlitColor` overload still invalidates scheduler
state on the calling thread after its `Record` call. Its implementation was not
changed. Normal draws pass the selected pipeline through `UpdateDynamicStates`
to `UpdateBlending`; internal `DrawTexture` passes null and uses unrestricted
output mask `~0U`. An absent guest fragment stage continues to use zero export
metadata and therefore zero color writes.

## Behavioral coverage

Six Catch2 cases in `src/tests/video_core/fragment_color_mask.cpp` exercise:

- The real Maxwell fragment `EXIT` translator and `CollectShaderInfoPass` for
  all 8 targets and all 16 component masks. Mixed outputs `{1,2,4,8,3,5,10,15}`
  pack to `0xFA538421`; multiple exits accumulate component bits with OR.
- All 2,048 combinations of target, guest mask and output mask, with other
  targets enabled to catch nibble leakage; absent-stage zero and internal
  all-enabled masks are also checked for every target/guest mask.
- No color, depth-only, sample-only and combined depth/sample outputs. The
  compiler's depth and sample metadata remain independent of color metadata.
  This IR has no fragment stencil export; real depth/stencil state dirty flags
  are checked separately and remain untouched by the mask policy.
- Real `StateTracker::InvalidateCommandBufferState`, followed by unchanged
  no-export draws, repeated binds, another invalidation, and pipeline transitions
  `0 -> 5 -> ~0U -> 5` without guest register changes. The two invalidations each
  request zero color writes; repeat binds request none, and the three pipeline
  transitions bring the update count to five. Depth/stencil, blend-enable and
  blend-equation flags remain dirty. A separate case checks both gate short
  circuits, including a pending color flag when blending is clean.
- Actual SPIR-V emission for primary R and secondary A outputs in dual-source
  mode. Both outputs have Location 0 and distinct Index 0/1 decorations. Metadata
  stays `{1,8,0,0,0,0,0,0}` after backend initialization/emission, and the primary
  attachment mask is 1, never the OR of both sources. No attachment-1 remapping
  policy is introduced.

`tools/nx2/test_fragment_color_mask_wiring.py` adds five structural tests for
the caller wiring and exact raw-view blit overload. Its small parser strips
comments and strings and balances delimiters to require top-level invalidation
after a completed top-level `Record`. It rejects mutations of the actual source
that remove the call, move it into the worker lambda, or put it only in the
`ImageView` overload. Additional parser cases reject earlier/nested calls and
comment/string decoys, including raw strings. The existing vertex-input fallback
pipeline lookup is outside the blending path and is preserved.

## Red/green evidence

These are mutation regressions against an already adopted fix, not claims that
the pristine upstream implementation failed.

- Initial old-policy stubs (unconditional guest masks and no pipeline-change
  detection): 6 cases, 4 failed; 1,532 failing assertions. Log:
  `.local-tools/fragment-red-tests.log`.
- After the production callers were wired to the shared helper, changing only
  its intersection to `return guest_mask` failed 3 of 6 cases / 1,525 assertions.
  Log: `.local-tools/fragment-mask-mutation-tests.log`.
- Independently forcing the shared state's `output_changed` to false failed
  2 of 6 cases / 9 assertions, including missing transitions and the expected
  five updates falling to two. Log:
  `.local-tools/fragment-transition-mutation-tests.log`.
- Mutation scripts kept a private exact-byte backup and restored the header in
  `finally`. Restoration writes the bytes afresh so Ninja rebuilds all consumers,
  avoiding stale mutated objects. The final restored build log explicitly
  records both `vk_graphics_pipeline.cpp` and `vk_rasterizer.cpp` compilation,
  the test object rebuild and executable relinks.

## Final validation

- Restored main Release build of `tests` and `yuzu-cmd`: pass with clang-cl
  23.1.5, VS2022 headers, Ninja and 12 jobs. The actual graphics-pipeline and
  rasterizer translation units were rebuilt after mutation restoration.
  `.local-tools/fragment-restored-green-build.log`.
- Focused restored run: **6 cases / 4,714 assertions, zero failures**, seed
  4279258831, with private module output enabled.
  `.local-tools/fragment-restored-green-tests.log`.
- Full main suite: **864 cases / 70,030,538 assertions, zero failures**, seed
  2711437559. All 858 pre-task cases remain, with six additional cases. The full
  suite includes timing/concurrency-dependent assertion counts.
  `.local-tools/fragment-full-tests.log`; its build log reports no pending work.
- Structural regressions: **5 of 5 pass**, including the actual-source and
  parser negative mutations. `.local-tools/fragment-wiring.log`.
- Fresh restored dual-source module: **1 of 1 passes**
  `spirv-val --target-env vulkan1.2`. Its header is SPIR-V 1.5; output lives at
  `.local-tools/fragment-modules/dual-source.spv` and the result at
  `.local-tools/fragment-spirv-validation.log`. The compiler's emission policy
  was not modified, so Task 3's separately validated 13 NaN/atomic modules need
  no regeneration for this extraction.
- Static Qt 6.11.1 GUI Release targets `yuzu`, `yuzu-cmd`, `input_profile_tests`
  and `config_tests` build successfully. GUI CTest: **4 of 4 pass**
  (`input_profile_tests`, `config_tests`, `update_checker_tests`,
  `sdl_window_state_tests`). `.local-tools/fragment-gui-build.log` and
  `.local-tools/fragment-gui-ctest.log`. Final cache refresh after restored-header
  timestamps: `.local-tools/fragment-final-gui-build.log`.
- `git diff --check` passes. Source self-review found no behavior change relative
  to the adopted upstream policy; root-managed independent spec/quality review
  follows the implementation handoff.

## Validation limits

Tests execute the actual shader compiler, mask policy, and command-state tracker.
They do not instantiate a Vulkan device or run the full draw/blit/draw sequence;
that requires device resources and a GPU runtime. Structural coverage checks the
remaining wiring and blit ordering. No Radeon visual or FPS claim follows from
these tests.

No emulator or GUI application was started/stopped, no running-game session was
accessed, and no live configuration, installed emulator, user shader cache, keys,
or game data was changed. No changes were pushed. Task 5 retention is untouched.
