# Age of Empires IV on Rosetta

Patch 0032 lets the Wine 11 engine run AoE IV's generated-code decoder on
Apple Silicon. The game recipe sets `HB_AOE4=1` and selects D3DMetal. Wine also
requires Rosetta on macOS 26+, `RelicCardinal.exe`, a supported x64 PE layout,
and a matching hash for the executable actually mapped. Other processes use
normal Wine behavior. The existing 32-bit helper remains separate.

The arm64 helper combines the software-fault and x87 decoders. Wine caches
relocated instruction fragments, retaining the original PC for exceptions.
It finds the code ring using a measured SHA-256 profile or unique allocator/
writer references, and validates the live 16 MiB allocation before using it.
Unsupported fragments use the decoder; inconclusive discovery leaves caching off.

The cache includes per-thread validated-page snapshots. VM mapping/protection
changes invalidate them. Lookups still reread the live ring pointer and bytes;
a private no-fault copy preserves guard pages. Invalid snapshots use the locked
reader automatically. This is part of the patch, without a separate disable option.

## Build and check

The existing Intel `build-engine` workflow applies the patch, runs its tests,
and builds the pinned arm64 helper. The engine archive includes the helper,
profiles and license notices under `share/wine/aoe4`; the release also includes
corresponding source. Source preparation and publication otherwise use the
existing workflow.

After applying the normal Wine patch series:

```sh
aoe4/tests/run.sh /path/to/patched/wine
```

Executable relocation/cache tests need x86-64 (use Rosetta on Apple Silicon).
[Windows VM tests](tests/VM-READER.md) exercise real guard pages, released and
relocated mappings, and concurrent protection changes in an isolated prefix.
The test bridge is never part of the engine patch.

For a game check, apply the recipe and use **Play** in Highball. Acknowledge the
compatibility-driver warning if shown. `AOELAB_COUNTERS_DIR` optionally records
cache activity for `cache-stats.py`. Unsupported layouts retain the normal launch.

## Validation

Local tests cover executable/profile rejection, ring discovery, relocation,
cache correctness, VM invalidation and guard preservation. The Unix module was
rebuilt and booted an isolated prefix with the game opt-in off and on; only the
opted-in AoE fixture attached the helper. Full upstream Actions-artifact gameplay
and the clean recipe-to-Play UI flow remain to be checked.

The [unpatched comparison](https://github.com/himbeles/highball-engine-aoe4/blob/1a300f70f71f89ca58aaea604bd66284ffa4c474/proposals/aoe4/VALIDATION.md#unpatched-and-patched-r32)
used the same Nagari save, settings, D3DMetal and M2 Max: unpatched Wine presented
37.8 FPS versus 58.7 with the decoder/generated-code cache; the longest interval
fell from 133.3 to 33.3 ms. This measures that path, not a matched unpatched run
of the final combined PR head. Smaller stutters remain in Late Siege.

## Credits

Wine thunks/cache: [Marc Ibrahim's LGPL source, mirrored by NerRobDog](https://github.com/NerRobDog/wine-aoe4/tree/711c6fc7559a565c417aab4d52d84b09767a9c1d).
MIT helper: [NerRobDog/x87sidecar, based on athei and Lifeisawful](https://github.com/NerRobDog/x87sidecar/tree/c407ddfabbbe6bc5f5d56a258a4866ac89b04d8e).
MIT decoder composition, GS fallback and FBLD fixes: [MacGames](https://github.com/tarikbc/macgames/tree/1579841b15ce569807b92af95b478c752c26df3d/patches/x87sidecar).
Source pins and patch hashes are in `inputs.json`; [licenses](Licenses/) accompany the engine.
