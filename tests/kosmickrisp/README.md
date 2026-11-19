# KosmicKrisp probes

Native macOS Vulkan programs that run against a KosmicKrisp build directly, without Wine. Build with the Khronos loader
and headers from Homebrew (`vulkan-loader`, `vulkan-headers`) and point the loader at the driver under test:

```sh
clang -O2 -I/opt/homebrew/include empty-renderings.c -L/opt/homebrew/lib -lvulkan -o empty-renderings
glslangValidator -V storage-robustness.comp -o storage-robustness.spv
clang -O2 -I/opt/homebrew/include storage-robustness.c -L/opt/homebrew/lib -lvulkan -o storage-robustness
VK_DRIVER_FILES=<build>/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.aarch64.json ./empty-renderings
```

## Empty renderings (patch 0010)

`empty-renderings.c` records renderings with no draw and checks a pixel of the attachment after each:
a CLEAR rendering clears, an empty LOAD/STORE rendering keeps the contents, a SUSPENDING + RESUMING pair keeps them,
empty / CLEAR / empty in one command buffer ends with the CLEAR colour, an empty 4x MSAA rendering with a resolve
attachment still resolves, an empty rendering that loads colour and clears a D32_SFLOAT_S8_UINT depth/stencil
attachment clears both aspects and keeps the colour, one that only loads colour, depth and stencil keeps all three, and
an occlusion query begun and ended inside an empty rendering is available with 0 samples. Exit status is the failure
count.

On an M5 Max, macOS 27.2 (2026-11-15) all six pass with patch 0010, with `HB_KK_SKIP_EMPTY_PASSES=1` and without it, on the
highball series (Mesa `ce576c29`, steamac, highball 0001 to 0010). The depth/stencil and occlusion query cases
were added on 2026-11-19 and pass both ways on the same series (13 checks). It does not check that a skipped rendering saves
GPU time; that was measured in Forza Horizon 6 (see the KosmicKrisp README).

## Storage buffer robustness (patch 0009)

`storage-robustness.c` enables robustBufferAccess2 and times 20 dispatches of 4M threads, each doing 64 scattered
storage-buffer reads and 64 uniform-buffer reads, with timestamps. Run it three ways:

```sh
MESA_SHADER_CACHE_DISABLE=true ./storage-robustness
MESA_SHADER_CACHE_DISABLE=true HB_KK_NO_UBO_ROBUST=1 ./storage-robustness
MESA_SHADER_CACHE_DISABLE=true HB_KK_NO_UBO_ROBUST=1 HB_KK_NO_SSBO_ROBUST=1 ./storage-robustness
```

M5 Max, macOS 27.2, same series: 12.25 ms, 10.44 ms, 8.73 ms. It is a throughput probe of a read-heavy shader, not a
measure of what a game gains, and it does not test what an out-of-bounds read returns.
