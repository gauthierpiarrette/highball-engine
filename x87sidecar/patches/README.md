# x87sidecar patch series

Wine 11 pins [athei/x87sidecar at `052944f`](https://github.com/athei/x87sidecar/commit/052944fc613e187f5e238bfa362b411787045d6b) in [inputs.json](../../inputs.json). That commit includes upstream [#54](https://github.com/athei/x87sidecar/pull/54) and [#57](https://github.com/athei/x87sidecar/pull/57). The workflow builds one arm64 executable, installed as `x87sidecar/x87sidecar` inside the engine.

| Patch | What it changes | Scope | Kind / source |
|---|---|---|---|
| [0003](0003-aoe4-decoder.patch) | Adds the bounded decoder, FBLD correction and regression fixtures | Verified AoE IV launches with `HB_AOE4=1` | [GameToMac decoder](https://github.com/NerRobDog/x87sidecar/commit/4b6bad8), [MacGames patches](https://github.com/tarikbc/macgames/tree/1579841b15ce569807b92af95b478c752c26df3d/patches/x87sidecar) |

Wine patch 0012 handles ordinary x87 launches. Patch 0032 checks the AoE IV image and supplies the internal launch marker and decoder addresses to the same helper. Ordinary launches use upstream x87 behavior at the pinned commit.
