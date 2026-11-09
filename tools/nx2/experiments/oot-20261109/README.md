# OoT Windows evaluation checkpoint, 9 November 2026

Work paused at the owner's request to conserve usage. This is an experiment
archive, not a selected performance fix or a portable benchmark release.
No game files, keys, saves, caches, profiles, screenshots or raw process logs
are included. The original working installation and progress remain preserved.

## Code and validation

- Evaluated integration source:1cb8dd2649a48cbd0a0ce387570e56aa34aebc7d,
  including officialf1932bc, worker cleanup and paired pipeline diagnostics.
- Worker cleanup:PR1105, forward port of PR939 with additional admission coverage.
  A correctness fix; no isolated gameplay speedup is claimed.
- Paired diagnostics:draft PR1113. Trace-enabled runtime format validation remains
  unfinished. Disabled traces preserve the existing rendering path.
- Public benchmark input-preservation fix:PR1072,702c50ade269977bcb9aa79d7ee281b4bddf7dc6;
  17 metrics tests and9 audit tests passed, including Windows case/hardlink aliases.
- Old-profile1cb full suite:1166 cases/70909147 assertions passed.
- Fresh-PGO1cb full suite:1166 cases/70887130 assertions passed.
  Fresh executable SHA256:FEB903E1F68B749B0791540D4FC331B56EF4A5C7E3EAC028C65290B8AD361A32.
  Fresh-profile training used only the main gameplay process; startup probe data
  was excluded. No hot gameplay profile mismatches were reported.
- State-dispatch542840553ee2bf17761476b2fdeb2307784671a9 is preserved on its
  evaluation branch, excluded from1cb and not selected: no repeatable Town gain.

## Benchmark status and limits

Hardware:i9-14900F,RTX4080SUPER,approximately32GiB;2560x1440 at143.998Hz.
Vulkan,docked1x,native upscaler path,frame generation off,no explicit FPS limiter.
Current Field1:41p.m. autosave and Town1:58p.m. manual checkpoint were copied.

V3 reloads the checkpoint and performs1920 scripted intervals: jump, walk forward
and back, sword, then rapid right/left camera rotation with translation stopped.
It verifies exact cyclic ring/stable counters, loaded module identity/hash and
foreground focus. Input first eligible at T selects timestamps T through T+N,
not T-1 through T+N-1. Historicalv2 has that one-interval boundary error and
endpoint drift; its raw evidence remains local, labeled and excluded from v3.
The ring measures guest VI presentation requests, not unique rendered frames,
host presentation, physical scanout or input-to-photon latency.

Only reference0ab7736df9dba2b396d2c7a26bd26fbb9b7b0dc0 completed three valid
v3 Town repeats before the pause:47.2341/52.0940/50.6173 guest presents/s.
Pooled49.8975;median19.4314ms,p9525.9152ms,p9928.4677ms,max82.6071ms;
11.5278%within16.6667ms;12/4/0 intervals above33.3/50/100ms (5760 total).
The Field warm-up lost foreground focus and is invalid for acceptance. Current
old-profile and fresh-profile v3 performance comparisons are unfinished.
Do not compare instrumented training FPS with these reference results.

System commit approached its limit during reference Town sampling, while several
GiB of physical RAM remained available. GetPerformanceInfo and an independent
Windows memory-counter spot check agreed; a spot page-read sample was low, which
does not rule out transient paging. No allocation failure was observed in that
reference log. Grouped test order and shared NVIDIA driver cache are further
limits. Small improvements cannot be attributed causally to individual patches.
Sustained real60FPS and complete visual correctness are NOT demonstrated.

The exact main NSO build ID is E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348,
zero-padded to32bytes. Earlier1.1.1 labeling needs qualification: the NUP loader's
2.0.0 log is synthesized from numeric revision131072, not an authenticated display
version. Current native-project version documentation has conflicting input-family
metadata; preserve the exact executable identity instead of inferring a retail
version from the directory or that numeric conversion.

## Archived helpers

workspace-tools preserves the local experiment helpers byte-for-byte; hashes are
in tool-hashes.json. They assume their ORIGINAL placement under the desktop
workspace's tools directory, local diagnostics layout and Windows toolchain.
They are not runnable from this archive path without restoring that layout and
reviewing machine-specific paths and parameters. Build-OotFreshPgo defaults to
Plan; the actual1cb experiment passed its revision explicitly. Do not run its
older default7ab revision and mistake it for the evaluated build.

Pure window/input tests:python -m unittest discover -s workspace-tools
-p test_present_ring_locator.py (seven tests). No save/config/cache restoration or
runtime replacement should be performed from this archive without reviewing the
preserved local manifests. Source is GPL-3.0-or-later as part of this repository.
