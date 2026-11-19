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
  - `0011` HDR swapchains reach the display as HDR. The Metal layer asked for extended dynamic range only for float
    formats, so an HDR10 (PQ) swapchain in a 10-bit integer format was shown as SDR: washed out, grey. EDR is now on
    for HDR10 and HLG too, and a PQ layer gets HDR10 metadata when the program sets none (peak `HB_HDR_MAX_NITS`, 1000
    by default; HLG gets Apple's HLG metadata), so macOS tone-maps highlights into the panel's headroom instead of
    clipping them. An extended-linear sRGB (scRGB) layer gets that default metadata only with
    `HB_HDR_SCRGB_METADATA=1`, asked for per game: DXVK's dxgi puts every RGBA16F swap chain in scRGB once the
    presenter accepts it, so without the switch an SDR game with an FP16 swap chain would get 1000 nit metadata and
    tone mapping too; without it an scRGB layer keeps what it had before this patch (EDR for float formats, no
    metadata) unless the program sets its own. The optical output scale is 100 on both paths, the default metadata and
    the metadata a program sets (which was 1): Apple maps 1.0 to 100 nits, and the scale only matters for float
    buffers, so this changes scRGB only. Forza Horizon 6 (PQ) on an M5 Max, macOS 27.2: correct HDR, highlights no
    longer clipped (2026-11-17); EA SPORTS FC 27 (scRGB) with `HB_HDR_SCRGB_METADATA=1` and the scale of 100: correct
    HDR and brightness (2026-11-19). An SDR game with an FP16 swap chain was not tried; without the switch it gets no
    metadata from this patch. Needs vkd3d-proton to enable VK_EXT_swapchain_colorspace, or the surface offers sRGB
    only.

  - `0009` `HB_KK_NO_SSBO_ROBUST=1`, asked for per game, drops the bounds check on storage buffer access as `0007` does
    for constant buffers. When either switch is set, both go into the pipeline cache UUID and the disk cache key, which
    held only the driver build, so toggling `HB_KK_NO_UBO_ROBUST` reused shaders translated with the other setting; with
    both off (the default) the key is unchanged and no existing cache is rebuilt. A native Vulkan
    compute test with robustBufferAccess2 (scattered storage and uniform reads, M5 Max): 12.25 ms, 10.44 with
    `HB_KK_NO_UBO_ROBUST=1`, 8.73 with both. Forza Horizon 6 with both: higher frame rate, no visible artifacts. A read past
    a bound storage buffer can access memory beyond its range. With the storage switch on, a null storage buffer
    descriptor also loses its zero-read and discarded-write behavior. The descriptor has size zero and uses GPU
    address zero unless `HB_KK_NO_UBO_ROBUST=1` supplies the shared zero buffer from `0007`, which a storage write
    can modify. These are consequences of the descriptor layout and shader lowering. Two consequences of the key: a
    recipe that already sets `HB_KK_NO_UBO_ROBUST` gets a new key once and rebuilds its cache; and a cache written under
    `0007` alone with that switch on stays under the default key, so if such a recipe later drops the switch, shaders
    translated without robustness are reused until that cache is cleared.
  - `0010` `HB_KK_SKIP_EMPTY_PASSES=1` (off by default, read once): a rendering with no draw, no clear and no resolve
    no longer starts a render encoder only to load and store every tile. vkd3d-proton's suspend/resume fixups record such
    renderings when it is told the GPU is a tiler (renderers-arm64 vkd3d-proton `0011`); in Forza Horizon 6 they were 46%
    of all render encoders, 1.57 of 4.7 million in a session were skipped, 5 to 10 fps on an M5 Max. Without that switch
    vkd3d-proton records few of them, so the gain against an engine without `0011` is not measured. The 5 to 10 fps
    are against `HB_VKD3D_TILER=1` without the skip; what both switches together gain over neither is not measured yet.
    `tests/kosmickrisp/empty-renderings.c` checks colour, depth and stencil clears and loads, suspend/resume, an MSAA
    resolve and an occlusion query inside an empty rendering, with the switch and without it.
  - `0012` `HB_KK_HEAP_BUDGET=1` (off by default, asked for per game, read once) makes the memory heap's budget 90% of
    the Metal device's recommended working set; the heap's size is 100% of it. By default the budget stays Mesa's: the
    smaller of the heap size and 90% of the memory macOS reports available, plus what the program already uses. With a
    game, Steam and a browser open that was about 10 GB on a 48 GB Mac, and Forza Horizon 6 sized its texture
    streaming to it, leaving tyres and fine detail at low resolution on many cars; they came back with the switch (M5
    Max, 2026-11-17). The switch ignores memory pressure: on a 16 GB Mac a game would be told it has about 9.6 GB of
    GPU memory out of shared memory whatever else is running, the stall pattern DXVK's `0002` caps, so it is for
    recipes of games that need it on Macs with memory to spare, and has not been tried on a 16 GB Mac.
  - `0013` MetalFX's temporal scaler, run when a VkHbMetalFXScaleInfo (`src/kosmickrisp/vulkan/kk_hb_metalfx.h`, the
    same interface as vkd3d-proton's `vkd3d_hb_metalfx.h`) is in the pNext chain of vkCmdPipelineBarrier2, in command
    order; driverInfo lists "hb-metalfx". One scaler per id and size is kept; it reads the rendered region of the color,
    depth and motion images and writes the output image, with the encoders around it ending in full barriers. Used by
    vkd3d-proton `0014` for FSR 3.1; on the base of this series Forza Horizon 6 logged "MetalFX temporal 1280x800 ->
    1920x1200 ... ok" (M5 Max, 2026-11-17).
  - `0013` a temporal upscale on MetalFX's temporal scaler: a vkCmdBlitImage2 whose VkBlitImageInfo2 carries a
    VkHbTemporalUpscaleInfo (`src/kosmickrisp/vulkan/kk_hb_upscale.h`; driverInfo lists "hb-upscale") upscales its
    source, the color image, from the source region (the rendered content, from the origin) into its destination,
    the whole output image, with the depth, motion, optional reactive mask and exposure images, jitter, motion vector
    scale, pre-exposure and flags of that structure. A blit the scaler cannot take (another region, a missing image, a
    content scale outside what MetalFX supports, motion vectors at the output size without the macOS 27 SDK and
    runtime) is performed as an ordinary blit, so the output is always written. One scaler per context (a nonzero
    id the caller picks), up to 8, least recently used replaced; it is sized from the color and output images, with
    input content properties on, so a change of render size within the color image keeps the scaler and its
    history, and a new scaler starts with a reset. Every command buffer that encodes a scaler holds a reference to it
    until it is reset or freed, which Vulkan allows only after it completed, so a replaced scaler lives as long as
    the GPU work that uses it. The encoder after the scale waits for all earlier work of the queue (KosmicKrisp's
    encoders already end in a barrier before all later stages). A command buffer replayed by the queue drops the
    structure (Mesa's command queue copies unknown pNext structures by reference), so a replay is an ordinary blit.
    Written for Highball from Apple's MetalFX headers; it shares no code with MetalSharp. Used by vkd3d-proton `0014`
    for FSR 3.1. Forza Horizon 6 at FSR Quality on an M5 Max, macOS 27.2, on the base of this series with `0011`,
    `0012` and vkd3d-proton `0013` and `0014` built as in CI (2026-11-19): "kk: MetalFX temporal 1728x1117, content
    1152x744 -> 1728x1117" and, at Retina, 2304x1492 to 3456x2234; full frame, the game's sharpening, HDR, no
    visible artifacts. No Metal validation run or image comparison yet. Builds with the macOS 26.5 and 27.2 SDKs.

The driver needs macOS 26 for Metal 4. The archive holds the driver in `renderers/kosmickrisp` with its ICD file, and
the Khronos loader as `lib/libMoltenVK.dylib`, the name Wine's winevulkan opens on this line.
