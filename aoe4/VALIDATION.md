# AoE IV engine validation

Tested on an Apple M2 Max running macOS 27.2.1 (26B131), with Rosetta and D3DMetal at
2560×1440 display output. AoE IV 17.0.12303.0 was Steam build 26559323. Its executable SHA-256
was `e047891780ec9a01e39185803288578b8385419e93c97b2216ebd24ee2bc1469`. Live inspection
confirmed the pointer at RVA `0x7c9f9d8`, the 16 MiB ring and 32-byte slots.

Gameplay used a local engine assembled from the stock Highball r32 archive with rebuilt
Darwin and PE ntdll modules and the signed arm64 helper. The installed Wine 10 Sikarugir r28
engine reproduced an illegal instruction at `0x143EE5588` before the menu. Unpatched Wine 11 r32
reached the skirmish when the driver warning was acknowledged.

## Unpatched and patched r32

Both launches used the same benchmark bottle, renderer components, executable, graphics
settings and Nagari skirmish save at game time 4:55. Unpatched ran first. Metal System Trace
was requested five seconds after pressing Play. The display numbers use the same trace
window from seconds 2 through 25 and count distinct game frames presented to the 60 Hz display.

| Metric | Unpatched r32 | Patched r32, cache enabled |
| --- | ---: | ---: |
| Displayed FPS | 37.8 | 58.7 |
| Median display interval | 33.3 ms | 16.7 ms |
| 99th percentile display interval | 50.0 ms | 33.3 ms |
| Intervals of at least three display refreshes | 17 | 0 |
| Longest display interval | 133.3 ms | 33.3 ms |
| Time from `OnBeginLoad` to `OnEndLoad` | 49.5 s | 17.1 s |

The patched run recorded 31 gaps of about 33 ms in that window and a 50 ms gap in the first
second of capture. A separate longer patched trace had a 467 ms interval during its first
second and one 50 ms gap after the first two seconds. The matched results apply to one scene
and cover the full patch set with caching enabled.

## Cache-only comparison

Separate 30-second Game Performance Overview captures used the same save and both decoder
hooks, with caching disabled first and then enabled.

| Metric | Cache disabled | Cache enabled |
| --- | ---: | ---: |
| Average FPS | 55.2 | 58.6 |
| Lowest one-second FPS sample | 48.0 | 56.0 |
| Game-process CPU usage | 228.5% | 163.2% |
| Time from `OnBeginLoad` to `OnEndLoad` | 30.2 s | 17.5 s |

The cache-enabled trace added 1,354,730 hits, with zero new entries and zero recorded
fallbacks. The disabled process logged the override and produced no cache counters. Loading
times can include OS cache and run-order effects.

## Discovery across game versions

The same detector found the measured pointer RVA `0x7af7ad0` in 16.3.11308.0 and `0x7c9f9d8`
in 17.0.12303.0. The rebuilt engine launched 17.0 through Steam with an empty profile directory,
validated the live allocation at `0x149e00000` and recorded 898,714 cache hits with the game at
the graphics-driver check. Native and Rosetta tests covered signature reads across a file
chunk boundary, mismatched and duplicate references, a changed slot shift and a truncated
live allocation. A synthetic executable named `RelicCardinal.exe` with a different PE image
base completed through the stock Wine launch.

## Steam browser startup

On the same D3DMetal bottle, Steam's browser GPU process repeatedly exited on both unpatched
and patched r32. The patched build reproduced its exit code with the AoE hooks explicitly
disabled. Forcing software rendering stopped the GPU process exits, and the D3DMetal Steam
window still displayed black. A screen recording verified that Steam's Library loaded and
responded on both engines when Steam was started with DXMT, following Highball's existing
[Steam window behavior](https://github.com/gauthierpiarrette/highball/issues/416).

## Late Siege frame pacing

On the M2 Max with the 1512×982 built-in display, AoE IV 17.0.12303.0 still
hitched roughly twice per second in the village view. The Metal trace
showed the GPU waiting for submissions. In a separate CPU trace, the game's
main thread spent roughly two thirds of its running samples during long
frames in Wine's context restore. A typical long frame coincided with about
33,000 cache hits and zero insertions or fallbacks. Independent [FEX
analysis](https://github.com/tarikbc/ageofempires-android/blob/main/docs/how-it-works/INSTRUCTION-STEPPER.md)
identifies the same 16 MiB ring as the Aegis instruction stepper.

The r2 build reads the pointer and verifies the fragment in a single
Wine VM lock acquisition. It also reuses a previously relocated
fragment when an exact instruction prefix and absolute jump target match
across ring slots. The shortcut accounted for about 94% of cache hits by
the end of the run.

The following 25-second windows used the same isolated bottle, display,
graphics settings and game build. The fixed scene had 71 render encoders
and 3,419 to 3,420 fragment draws per frame. FPS is the Metal performance
trace value. The 99th percentile comes from the per-frame HUD log.

| Engine variant | Displayed FPS | P99 frame interval | Longest interval |
| --- | ---: | ---: | ---: |
| Published r1 | 44.4 | 166.7 ms | 183.3 ms |
| Batched memory reads | 49.2 | 120.8 ms | 141.7 ms |
| r2 profiling build | 49.7 | 108.3 ms | 125.0 ms |

During the jump-lookup run, panning away reduced the fragment draw count
to 1,644 while the stalls continued. In that lighter view, 829 of 1,739
running samples on the main thread during long frames included the
remaining protected memory read. Returning toward the village via the
minimap yielded 58.3 FPS and a 33.3 ms P99 over 18 seconds, with around
3,271 draws and zero new cache entries. A separate 19-second capture
after the player selected and moved units ran at 58.7 FPS with a 33.3 ms
P99 and around 3,620 draws. The game condition that schedules the bursts
remains to be identified.

## Build checks and remaining validation

- The [prototype Intel macOS build](https://github.com/himbeles/highball-engine-aoe4/actions/runs/43691361823)
  passed. Local gameplay used the patched modules described above.
- Native macOS and Rosetta portable tests passed. The freshly rebuilt arm64 helper passed
  strict signature verification and its Rosetta probe. All ten changed Wine source files in
  the prepared tree matched those used for the gameplay build.
- The [r1 build](https://github.com/himbeles/highball-engine-aoe4/actions/runs/43741809761)
  rebuilt both ntdll modules and the arm64 helper. A fresh Highball install of that artifact
  unpacked the helper and profiles, created a bottle and ran a Windows command in it.
- The r2 Late Siege tests ran a local build in a clone of the r1 Highball bottle with
  D3DMetal. The release module was rebuilt from the same Wine source with the r1
  deployment target of macOS 14.
- Extended late-game play, replay, Windows multiplayer and the clean Highball recipe-to-Play
  flow remain to be checked.

## Wine 11 integration checks

- Applied the series to the checksum-verified CrossOver 26.3.0 source. Patch 0032 also
  applied without fuzz after the pending fiber mirror patch 0031 (highball-engine #38).
- Built both x86-64 ntdll modules for macOS 14. Built the pinned helper as arm64,
  verified its ad-hoc signature and ran its Rosetta probe on an M2 Max with macOS 27.2.1.
- The native and Rosetta test suites and profile tooling passed. The Rosetta suite
  exercises cache refusal when the switch is unset, the executable name is wrong,
  verification fails, caching is disabled or the discovered allocation is too small.
- In a fresh throwaway prefix using the rebuilt modules and the intended archive layout,
  Windows `cmd.exe` ran with the switch both unset and set. A synthetic x64 PE named
  `RelicCardinal.exe` ran without attaching by default, then found the bundled helper
  and completed the cooperative handshake when launched with `HB_AOE4=1`. The fixture
  deliberately had no known cache ring; it ran successfully with the decoder alone.

These checks use rebuilt local modules and a synthetic executable. The full upstream
workflow archive and the Steam recipe-to-Play flow still need validation.


## r3 validated-page cache

On 2026-11-17, AoE IV 17.0.12303.0 (Steam build 26559323) ran the Late Siege tutorial
through Highball's normal Play action with D3DMetal and MSync on the M2 Max, macOS
27.2.1. The display and render resolution were 2560×1600; the game window was
1280×800. No unit selection or camera movement occurred between the six measured
windows. Thus unit selection did not warm away the workload being compared.

The same instrumented binary and process alternated the r2 locked reader and the
r3 page cache, with two seconds to settle and twenty seconds measured per window.
VM mutation tracking remained active in both modes. This isolates the reader change;
it is not a comparison against the byte-for-byte published r2 executable. Diagnostic
mode switches and per-read counters are absent from the production r3 module.

Apple's `metalperftrace` per-frame metrics supplied the on-glass drawable timeline.
No screen recording ran during these measurement windows. Counts use a 49.9 ms
threshold to include refresh-rounded 50 ms intervals; P99 is computed from individual
frame durations, not one-second FPS samples. Capture boundaries can affect a frame.

| Window | Reader | FPS | P99 | Worst interval | Frames ≥49.9 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| A | r2 | 36.60 | 83.33 ms | 91.67 ms | 41 |
| A | r3 | 38.72 | 41.67 ms | 50.00 ms | 7 |
| B | r2 | 37.02 | 83.33 ms | 83.33 ms | 40 |
| B | r3 | 38.70 | 41.67 ms | 58.33 ms | 7 |
| C | r2 | 37.04 | 75.00 ms | 91.67 ms | 40 |
| C | r3 | 39.17 | 41.67 ms | 50.00 ms | 4 |

All B/C windows had 3,540 fragment draws per frame. The first r2 window initially
varied from 3,517 to 3,540. Around 99.4% of reader attempts used the fast path (this
includes rejecting resumes outside the ring). Each r3 window had about two million
relocated-cache hits and zero new entries or fallbacks. Remaining 50–58 ms frames
show that r3 reduces the measured bottleneck without eliminating all frame drops.
The earlier observation that selecting units reduces the lookup workload remains
a clue about the game's scheduling; r3 does not synthesize selection or camera input.

### r3 safety and release checks

- Prepared Wine again from the checksum-verified original archive and patch series;
  the affected sources match the release build byte for byte.
- Portable profile, discovery, relocation, executable cache and packaging tests passed
  under Rosetta. The release module retains the macOS 14 deployment target.
- A separate test-only build of the production sources passed real Windows API tests:
  rewritten bytes, ring relocation, freed/no-access mappings, guarded ring and pointer
  pages, null/unmapped no-fault copies, 3,000 concurrent guard/protection changes and
  100 same-address release/reallocate/decommit/recommit cycles. Guards remained set
  and successful reads returned the expected data. See [fixture instructions](tests/VM-READER.md).
- The no-fault recovery is restricted to the exact private `rep movsb` instruction.
  Other faults retain Wine's normal handling. The release contains neither the test
  syscall bridge nor the lab-only environment variables and shared-memory switch.

This is a limited local validation, not a guarantee for every game update, extended
late-game simulation or Windows multiplayer. The optimization fails back to r2's
locked reader on invalidation; `AOE4_DISABLE_VM_CACHE=1` provides a process-start
fallback for regressions. The upstream port keeps `HB_AOE4=1` as an additional opt-in gate. The local
release and upstream proposal have different helper packaging and startup gates;
measurements above were collected with the fork release path.


The exact signed r3 release module (`df9af29ac3ea7122aee0e93b11161c0921bdfafff83231f4487a99cf5f3fc529`)
was installed from the final archive into a fresh Highball home and ran a Windows
command in a new D3DMetal bottle. It also launched AoE IV through the regular Games
bottle's Play action with MSync and the accepted profile. In a separate 20-second
Late Siege check, it recorded 38.63 FPS, 50.00 ms P99, 75.00 ms worst interval,
10 frames ≥49.9 ms and none ≥99.9 ms, with 3,542 fragment draws per frame and
1,987,788 cache hits, no insertions or fallbacks. This was a new launch, not a matched
A/B window; background release verification was also running. The r2 installed
module was left unchanged.

The upstream r3 port was additionally prepared from the complete clean patch series,
rebuilt and tested in an isolated prefix. Ordinary commands ran with `HB_AOE4` off
and on; the helper attached only to the opted-in AoE-named fixture. A separate test
build passed the r3 memory, no-fault and concurrent mutation fixtures (11,194 reads,
zero errors) and explicitly confirmed VM tracking remains off without `HB_AOE4=1`.
The test bridge is absent from the upstream patch series as well.
