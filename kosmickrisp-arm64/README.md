# KosmicKrisp for the arm64 engine line

KosmicKrisp is Mesa's Vulkan driver on Metal 4. It runs Forza Horizon 6, whose D3D12 needs sparse resources that
MoltenVK does not have. `build-kosmickrisp-arm64` builds it from Mesa at the commit pinned in `inputs-arm64.json`
with two sets of patches, applied in this order:

- `steamac/`: the 49 patches of [steamac](https://github.com/fxgl/steamac)'s `host/kosmickrisp/patches` at aa0588d, with
  its `LICENSE` and `NOTICE`. Each patch keeps its author (steamac, Willie Abrams, Junmin Lee, Brian Misiak, Matthew
  Periut and others). Thank you all. Their Asahi hunks are left out, this build has no Asahi driver.
- `highball/`, written with an LLM's help (Generated-by: LLM) and used only here. They are not submitted to Mesa, whose
  policy asks people not to send LLM-generated work to its GitLab:
  - `0001` CPU-visible memory types can back sparse resources.
  - `0002` generated MSL is compacted and identical sources are shared, 4 GB of allocations where the game's world load
    held 19.5 GB.
  - `0003` compressed images do not ask for shader write, which corrupted sparse BC textures.
  - `0004` a shader-writable sparse image takes the queue separator before its mappings, or Metal crashes.
  - `0005` `HB_PRESENT_PACE_FPS` presents through Metal's minimum duration, so a capped game reaches the panel evenly.
  - `0006` a disk shader cache: the driver saved none, so every start compiled every shader again. A loaded shader keeps
    its program choice, which keeps its compile off the render thread.
  - `0007` frame time. An encoder's end barrier asks for Device visibility instead of ResourceAlias, unless a render
    encoder ran an occlusion query (`HB_KK_ALIAS_BARRIERS=1` brings the old flush back): GPU time 26.7 to 24.4 ms in
    Forza Horizon 6's benchmark on a base M4. `HB_KK_NO_UBO_ROBUST=1`, asked for per game, drops the bounds check on
    constant buffer reads, 24.4 to 21 ms there, and null constant buffer descriptors then read a buffer of zeros. A read
    past a bound constant buffer returns the memory after it instead of zero. Also 256 command buffer records, so
    `MESA_KK_DEBUG=cbs` logs all of a frame's.
  - `0012` `HB_KK_HEAP_BUDGET=1` (off by default, asked for per game, read once) makes the memory heap's budget 90% of
    the Metal device's recommended working set; the heap's size is 100% of it. By default the budget stays Mesa's: the
    smaller of the heap size and 90% of the memory macOS reports available, plus what the program already uses. With a
    game, Steam and a browser open that was about 10 GB on a 48 GB Mac, and Forza Horizon 6 sized its texture
    streaming to it, leaving tyres and fine detail at low resolution on many cars; they came back with the switch (M5
    Max, 2026-11-17). The switch ignores memory pressure: on a 16 GB Mac a game would be told it has about 9.6 GB of
    GPU memory out of shared memory whatever else is running, the stall pattern DXVK's `0002` caps, so it is for
    recipes of games that need it on Macs with memory to spare, and has not been tried on a 16 GB Mac.

The driver needs macOS 26 for Metal 4. The archive holds the driver in `renderers/kosmickrisp` with its ICD file, and
the Khronos loader as `lib/libMoltenVK.dylib`, the name Wine's winevulkan opens on this line.
