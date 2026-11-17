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
  - `0012` the memory heap's budget is 90% of the Metal device's recommended working set, as its size already is. It
    was 90% of the memory macOS reported available, which with a game, Steam and a browser open was about 10 GB on a
    48 GB Mac, and games sized their texture streaming to it: Forza Horizon 6 left tyres and fine detail at low
    resolution on many cars, and they came back with this patch (M5 Max, 2026-11-17). The working set scales with the
    Mac, about 9.6 GB on a 16 GB one.

The driver needs macOS 26 for Metal 4. The archive holds the driver in `renderers/kosmickrisp` with its ICD file, and
the Khronos loader as `lib/libMoltenVK.dylib`, the name Wine's winevulkan opens on this line.
