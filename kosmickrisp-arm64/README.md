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
  - `0013` a temporal upscale on MetalFX's temporal scaler: a vkCmdBlitImage2 whose VkBlitImageInfo2 carries a
    VkHbTemporalUpscaleInfo (`src/kosmickrisp/vulkan/kk_hb_upscale.h`; driverInfo lists "hb-upscale") upscales its
    source, the color image, from the source region (the rendered content, from the origin) into its destination,
    the whole output image, with the depth, motion, optional reactive mask and exposure images, jitter, motion vector
    scale, pre-exposure and flags of that structure. A blit the scaler cannot take (another region, a missing image, a
    content scale outside what MetalFX supports, motion vectors at the output size without the macOS 27 SDK and
    runtime) is performed as an ordinary blit, so the output is always written. One scaler per context (a nonzero
    id the caller picks), up to 8, sized from the color and output images with dynamic input content enabled.
    Content outside the supported range takes the ordinary blit and logs once per device. The scaler stays cached
    through that fallback and starts fresh when temporal scaling resumes. A queue submission expires cache entries
    idle for 30 seconds, and a full cache evicts the least recently used entry. Command buffers retain the scalers
    they encode until reset or free. A producer barrier immediately after MetalFX orders later work on the same Metal
    queue, including work in other command buffers. A command buffer replayed by the queue drops the structure
    (Mesa's command queue copies unknown pNext structures by reference), so a replay is an ordinary blit.
    Credit to Avery Felts and MetalSharp for the idea of adding MetalFX temporal upscaling to the driver.
    The implementation uses Apple's MetalFX headers. Used by vkd3d-proton `0014`
    for FSR 3.1. Forza Horizon 6 at FSR Quality on an M5 Max, macOS 27.2, on the base of this series with `0011`,
    `0012` and vkd3d-proton `0013` and `0014` built as in CI (2026-11-19): "kk: MetalFX temporal 1728x1117, content
    1152x744 -> 1728x1117" and, at Retina, 2304x1492 to 3456x2234; full frame, the game's sharpening, HDR, no
    visible artifacts. No Metal validation run or image comparison yet. Builds with the macOS 26.5 and 27.2 SDKs.

The driver needs macOS 26 for Metal 4. The archive holds the driver in `renderers/kosmickrisp` with its ICD file, and
the Khronos loader as `lib/libMoltenVK.dylib`, the name Wine's winevulkan opens on this line.
