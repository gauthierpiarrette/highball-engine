# Age of Empires IV on Rosetta

Wine 11 patch 0032 adds a combined software-fault and x87 decoder for the game,
plus a cache for repeated fragments of its generated code. The Highball recipe
sets `HB_AOE4=1` for AoE IV (Steam app 1466860) and selects D3DMetal. The switch
is off when absent or set to anything other than `1`. Wine also requires Rosetta
on macOS 26 or newer, checks for `RelicCardinal.exe`, and verifies the executable's
x64 PE layout before re-executing through the helper. Unsupported layouts use
the usual Wine launch. The executable Wine maps must match the inspected hash.

The cache uses a SHA-256 profile for game builds 16.3.11308.0 and 17.0.12303.0.
For other builds, only a unique, aligned ring pointer agreed on by the
allocator and slot writer can be used. Auto-discovery also checks the live
16 MiB executable allocation before caching any 32-byte slots. When discovery
is inconclusive the decoder still runs. Cached fragments report exception
addresses in the original game code.

Launch with **Play** in Highball after applying the fix. If the graphics driver
warning appears, acknowledge it. For comparisons, `AOE4_CACHE=0` leaves the
decoder on, `AOE4_X87=0` selects the software-fault decoder alone, and
`AOE4_DISABLED=1` switches the patch off. `AOELAB_COUNTERS_DIR` optionally
enables cache counters; `cache-stats.py` summarizes them. See [validation](VALIDATION.md)
for prototype results and remaining checks.

## r3 memory-page cache (patch 0033)


r3 retains r2's relocated-instruction cache and adds a thread-local cache of validated
memory-page permissions. Most instruction lookups no longer acquire Wine's VM lock or
search its mapping tree. They still reread the live ring pointer and instruction bytes.
A VM generation changes around mapping/protection operations; a changed generation,
changed pointer, different page or failed copy falls back to the locked reader. Normal
resumes outside the ring preserve the useful page snapshot.

A private no-fault copy recovers only faults at its own load instruction, before Wine
consumes a Windows guard page. The optimization is limited to the AoE executable on
Darwin x86-64; the existing executable verification and ring discovery still gate use.
Steam and other executables do not enable VM generation tracking.

Three same-process Late Siege comparisons reduced P99 frame intervals from 75–83 ms
to 42 ms, with about 50 ms-or-longer frames falling from 40–41 to 4–7 per 20-second
window. Average FPS rose from 36.6–37.0 to 38.7–39.2. This does not eliminate every
stutter. See [r3 measurements and limitations](VALIDATION.md#r3-validated-page-cache).

Set `AOE4_DISABLE_VM_CACHE=1` and restart the game to use the previous locked
reader while retaining the other AoE fixes. Remove the variable entirely to restore r3.
The upstream path still requires `HB_AOE4=1`.

## Build and test

Dispatch the usual **build-engine** workflow. It applies the Wine 11 series
to the checksum-verified CrossOver source in [inputs.json](../inputs.json),
builds Wine and the signed arm64 helper from pinned source in the same run,
and places the helper and profiles under `engine/share/wine/aoe4` in the
standard Wine archive. Nothing is published without `publish_release=true`.
The workflow also packages corresponding source and checksums. The separate
stock 32-bit x87sidecar and `ROSETTA_X87_PATH` path remain available.

On a clean checkout, apply the pinned source and run the tests:

```sh
.github/scripts/prepare-wine.sh
aoe4/tests/run.sh "$PWD/src/sources/wine"
python3 aoe4/tests/test_tools.py
```

The relocator and executable cache tests require x86-64. On Apple Silicon,
run the test script under Rosetta with an x86-64-capable SDK and compiler.
After building the helper, run `x87sidecar --probe` on Apple Silicon to check
the installed Rosetta runtime. For a game update, validate the live pointer,
allocation size, slot layout and cache counters before using `profile.py` to
record a new measured profile. Profiles contain offsets and hashes only.

## Credits

The LGPL Wine thunks and cache derive from [Marc Ibrahim's Wine source as
mirrored by NerRobDog](https://github.com/NerRobDog/wine-aoe4/tree/711c6fc7559a565c417aab4d52d84b09767a9c1d).
The MIT helper is [NerRobDog's x87sidecar, based on athei and
Lifeisawful](https://github.com/NerRobDog/x87sidecar/tree/c407ddfabbbe6bc5f5d56a258a4866ac89b04d8e).
The decoder composition, GS fallback and FBLD changes are MIT-licensed
[MacGames patches](https://github.com/tarikbc/macgames/tree/1579841b15ce569807b92af95b478c752c26df3d/patches/x87sidecar).
The [licenses](Licenses/) accompany the Wine archive.
