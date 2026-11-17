# x87sidecar patch series

The Wine 11 engine pins [athei/x87sidecar **v1.9.1**](https://github.com/athei/x87sidecar/releases/tag/v1.9.1) at `03581e1b5e8465c9e78cea7c20652736a0e02f0a` in [inputs.json](../../inputs.json). The helper workflow applies the numbered patches in lexical order and builds one arm64 executable, installed as `x87sidecar/x87sidecar` inside the engine. Remove a backport when the pinned release contains it.

| Patch | What it changes | Scope | Kind / source |
|---|---|---|---|
| 0001 | Splits x87 runs on self-modifying pages and reuses target buffers | Shared x87 | Upstream backport [#54](https://github.com/athei/x87sidecar/pull/54) |
| 0002 | Preserves exact 64-bit integer loads in optimized x87 runs | Shared x87 | Upstream backport [#57](https://github.com/athei/x87sidecar/pull/57) |
| 0003 | Adds the opt-in AoE IV decoder, the shared x87 FBLD correction and their regression fixtures | AoE IV and shared x87 correctness | Marc Ibrahim’s [GameToMac decoder](https://github.com/NerRobDog/x87sidecar/commit/4b6bad8), [MacGames patches](https://github.com/tarikbc/macgames/tree/1579841b15ce569807b92af95b478c752c26df3d/patches/x87sidecar), [AoE fork](https://github.com/himbeles/x87sidecar-aoe4/commit/f028874) |

Wine patch 0012 handles ordinary x87 launches. For AoE IV, patch 0032 verifies the game image and supplies the decoder addresses when the recipe enables `HB_AOE4=1`. The general x87 backports and FBLD correction apply in ordinary x87 mode too.
