# Controller profile persistence validation

## Scope and root causes

The controller configuration UI used player slot 0 for handheld profile creation,
loading, and selection lookup, although handheld bindings live in slot 8. Saving
already resolved slot 8 separately. All four paths now use the same tested
`InputProfiles::GetPlayerIndex` policy through the UI's `GetProfilePlayerIndex`.
Changing the active controller refreshes the profile list without activating or
loading a different profile.

Successful create, load, and save operations now assign `profile_name` to the
active settings bank and player slot. Existing Config serialization already
persists that field. Without the identity, reopening the dialog loses its
selection and per-game configuration treats the bindings as inherited global
settings. Successful deletion clears matching identities in both in-memory banks
without modifying the bindings or changing which bank is active.

Profile saves now propagate the actual SimpleIni/stdio write and close result.
Failed creation removes its new map entry; failed operations preserve the previous
identity. Profile loading rereads the file, preventing an unsuccessful save's
cached values from being loaded as though they had been saved. This selected-player
reload propagates read failures before changing settings, and never creates or
truncates a missing/unreadable profile as a fallback. Existing general Config
initialization retains its default file creation behavior.

No default-binding fallbacks, device schema, or string-default metadata behavior
were changed. Cleared bindings retain the existing `[empty]` representation.

## Isolation and build setup

All commands ran in `C:/Users/JHutc/Desktop/hichii-issue-fixes` on
`fix/amd-five-issues`, following the reviewed pinned-upstream prerequisite.
No GUI/emulator/game was launched, stopped, or attached. Tests use QCoreApplication,
real InputProfiles/QtConfig, and newly created temporary config folders. The fresh
process test passes its temporary path to each child before constructing any
config. No real user config was opened or modified.

The new Catch2 `input_profile_tests` executable requires Qt Core but not QtTest.
`YUZU_QT_COMPATIBILITY_TESTS` defaults ON, preserving the existing compatibility
test requirement. This machine's bundled static Qt 6.11.1 lacks QtTest, so the GUI
test build explicitly sets that option OFF rather than silently dropping tests.

Build helper: `.local-tools/build.ps1`, absolute clang-cl 23.1.5, VS 2022 headers
14.44, Ninja, Release, production WERROR enabled, 12 jobs. The existing GUI cache's
newer MSVC library search path was retained. The existing local warning include
suppresses only the pre-existing core-timing test unused variable; no production
warning policy was weakened.

## Red evidence

Tests were compiled and run against the original production profile/config code
before the fixes:

- `controller-red-tests.log`: 4 cases, 59 assertions, 10 failures. Create/save/load
  did not assign identity; deletion retained stale identities; the fresh reader
  process exited 2; per-game reload inherited global bindings instead of its saved
  custom mapping.
- `controller-red-write-tests.log`: failed write regression, 13 assertions,
  3 failures. Saving into a temporary directory in place of the INI returned true,
  as did failed creation, which also left an entry in the profile map.
- `controller-red-reload-tests.log`: after adding actual save failure propagation,
  recovery exposed the cached-value bug: 19 assertions, one failure, loading the
  unsuccessful new key 66 instead of the last saved key 65.
- `controller-red-read-tests.log`: the failed-load regression exposed false success
  and default-binding overwrite when a profile path was a directory: 30 assertions,
  6 failures. Read status propagation fixed this before handoff.

Logs remain under the ignored `.local-tools` directory.

## Final verification

GUI test configuration:

```powershell
& ./.local-tools/build.ps1 -BuildDirectory build-amd-gui -Configure `
  -Targets input_profile_tests `
  -CMakeOptions @('-DENABLE_QT=ON','-DYUZU_TESTS=ON','-DYUZU_QT_COMPATIBILITY_TESTS=OFF') `
  -LogName controller-red
```

Production compilation and final targeted checks:

```powershell
& ./.local-tools/build.ps1 -BuildDirectory build-amd-gui `
  -Targets input_profile_tests,yuzu,config_tests,update_checker_tests `
  -LogName controller-final-gui
& ./.local-tools/build.ps1 -BuildDirectory build-amd-gui `
  -Targets input_profile_tests -LogName controller-final-profile
& build-amd-gui/bin/input_profile_tests.exe --reporter compact `
  --out .local-tools/controller-final-input-tests.log
& 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' `
  --test-dir build-amd-gui `
  -R '^(input_profile_tests|config_tests|update_checker_tests)$' --output-on-failure
& ./.local-tools/build.ps1 -Targets tests,yuzu-cmd,config_tests,update_checker_tests `
  -LogName controller-main
& 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' `
  --test-dir build-amd-vs2022 -R '^(config_tests|update_checker_tests)$' --output-on-failure
New-Item -ItemType Directory -Path build-amd-vs2022/bin/user -Force | Out-Null
& build-amd-vs2022/bin/tests.exe --reporter compact `
  --out .local-tools/controller-main-full-tests.log
git diff --check
& 'C:/Users/JHutc/Desktop/zelda-native-port/tools/clang+llvm-23.1.5-x86_64-pc-windows-msvc/bin/clang-format.exe' `
  --dry-run --Werror --style=file:src/.clang-format src/tests/input_common/input_profiles.cpp
```

Results:

- Full GUI executable linked successfully, including the affected configuration
  UI and QtConfig units. CLI and main test executables also rebuilt successfully.
- Controller profile tests after review follow-up: **7 cases, 164 assertions, all passed**. Coverage
  includes the production active-slot resolver and P1/handheld isolation,
  create/load/save selection, profile-list reopening, fresh-process config/profile
  reload, keyboard and gamepad button packages, GUID/port, analog deadzone/range,
  motion, controller type, connection state, `[empty]` bindings, failed operations,
  saved-file recovery, deletion, per-game overrides, and explicit return to global
  inheritance after clearing the custom Controls section.
- Existing config tests: **7 cases, 426 assertions, all passed**.
- GUI targeted CTest: **3/3 passed**. Main targeted CTest: **2/2 passed**.
- Main full suite: **791 cases, 788 passed, 3 failed**, exactly the known baseline
  failures at `video_core/nx2_video_render.cpp:151`,
  `video_core/nx2_video_render.cpp:210`, and `core/nup_writer.cpp:671`.
  Total assertions: 69,808,836, with those same 3 failures. Catch exit code: 42.
- Whitespace and new-test clang-format checks passed.

## Spec review follow-up

The first implementation's real config tests exercised the shared slot resolver,
but could not catch an accidental raw `player_index` argument in a QWidget caller.
`tools/nx2/test_controller_profile_wiring.py` adds seven explicitly structural
checks: all four UI operations must use the shared resolver, the resolver must read
the actual emulated Npad ID, and the per-game caller must guard failed loads before
assigning identity, connection state, or reloading its controller. The script strips
comments and literals before examining function bodies. It does not instantiate a
QWidget or claim to exercise the complete dialog.

The per-game caller also assigned identity before loading and ignored failure.
It now checks `LoadProfile`, logs the failed profile name and player number, and
continues before changing connection state or reloading. InputProfiles assigns the
identity only on success. A new real InputProfiles test demonstrates failed custom
bank loads preserve identity, all binding arrays, style, disconnected state, and the
global bank.

Red evidence uses old production source read from Git into temporary files, without
rewriting tracked files. The following reproduces it with ignored source copies:

```powershell
git show '9bb48385d0a^:src/yuzu/configuration/configure_input_player.cpp' |
  Set-Content -Encoding utf8 .local-tools/controller-old-player.cpp
git show '9bb48385d0a^:src/yuzu/configuration/configure_input_per_game.cpp' |
  Set-Content -Encoding utf8 .local-tools/controller-old-per-game.cpp
python -I tools/nx2/test_controller_profile_wiring.py `
  --player-source-file .local-tools/controller-old-player.cpp `
  --per-game-source-file .local-tools/controller-old-per-game.cpp -v
python -I tools/nx2/test_controller_profile_wiring.py -v
& ./.local-tools/build.ps1 -BuildDirectory build-amd-gui `
  -Targets input_profile_tests,yuzu -LogName controller-review-followup
& build-amd-gui/bin/input_profile_tests.exe --reporter compact `
  --out .local-tools/controller-review-followup-tests.log
```

Results: the pre-fix source fails **7/7 structural checks**. The first implementation
passes the five active-slot wiring checks but fails the two per-game failure checks.
The follow-up passes **7/7 structural checks** and **7 cases / 164 assertions** in the
real config executable. The GUI executable links successfully with the affected
per-game configuration unit compiled. No main full-suite rerun was needed for this
UI/test-only follow-up; the recorded baseline run remains unchanged.

## Limits

This verifies the identified profile identity/slot persistence defects and real
config serialization, not every possible controller or hardware driver issue.
No QWidget dialog, physical controller, or running game was exercised. UI caller
wiring is covered by compiled integration, source review, and structural checks.
Existing config writes are not replaced with an atomic-file protocol; successful
identity assignment reflects the checked file operation, while disk-level recovery
after a partially written file remains outside this change. Existing QtTest
compatibility tests were explicitly disabled only in this local GUI build.
