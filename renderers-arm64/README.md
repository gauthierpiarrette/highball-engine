# Renderers for the arm64 line

vkd3d-proton (Direct3D 12) and DXVK (DXGI, Direct3D 11 and 10) on Vulkan, at the commits pinned in
`inputs-arm64.json`, with the patches below. `build-renderers-arm64.yml` cross-compiles them for x86-64 Windows with
llvm-mingw 20250910 (clang 21, the toolchain of the tested builds; clang 23 refuses dxil-spirv's dxbc-spirv), marks
every DLL as a Wine builtin (Wine 11.18 ignores DLLs in WINEDLLPATH that are not), and packages
`renderers/vkd3d/wine/aarch64-windows` and `renderers/dxvk/wine/aarch64-windows`, the overlays Highball's .vkd3d and
.dxvk modes put first in WINEDLLPATH. The DLLs run under FEX like the games that load them.

Each series is `git format-patch` output, applied with `git apply` to its pinned commit. Applied in order, vkd3d-proton's 0001 to 0006 and
DXVK's 0001 to 0003 rebuild, file for file, the trees the Microsoft Flight Simulator 2024 runs of 2026-10-11 and 12
were built from (checked on the M4). vkd3d-proton's 0007 came after those runs.

## At a glance

Kinds are explained in the [top-level README](../README.md#patches-at-a-glance). None of these has been sent to
vkd3d-proton or DXVK yet.

| Patch | What it fixes | Needed by | Kind | Pairs with |
|---|---|---|---|---|
| vkd3d-proton `0001` optional cooperative matrix | No Direct3D 12 device could be created on MoltenVK. | every D3D12 game | general | |
| vkd3d-proton `0002` warnings, not a failed device | Device creation failed for missing transform feedback, single texel alignment and robustness2. | every D3D12 game | workaround | 0004 |
| vkd3d-proton `0003` decompression region count | Two NV memory decompression shaders failed to translate (Metal refuses atomics on vector elements). | general | Apple GPU | |
| vkd3d-proton `0004` emulated null descriptors | GPU faults when shaders touch null descriptors, on devices without nullDescriptor. | general | workaround | 0002 |
| vkd3d-proton `0005` tessellation on MoltenVK | Tessellation pipelines failed to translate to Metal. | Microsoft Flight Simulator 2024 (terrain) | workaround | |
| vkd3d-proton `0006` SV_ShadingRate dropped | Pipelines writing a shading rate failed to compile on devices that cannot express it. | general | general | |
| vkd3d-proton `0007` force_native_fp16 | Games lost pipelines using 16-bit shader operations (16 per Microsoft Flight Simulator 2024 run). | Microsoft Flight Simulator 2024 (its recipe sets it) | workaround (opt-in: `VKD3D_CONFIG=force_native_fp16`) | |
| DXVK `0001` unshared fallback | A crash at the game's first boot video when a texture could not be shared. | Microsoft Flight Simulator 2024 | general | Wine 0036 made sharing work later |
| DXVK `0002` video memory budget | Games filled more memory than the GPU may wire, and the GPU stalled. | Microsoft Flight Simulator 2024 (its recipe sets it) | Apple GPU (opt-in: `dxgi.maxVideoMemoryBudget`) | |
| DXVK `0003` relaxed features | DXVK rejected Apple GPUs for missing features, geometry shaders included. | every DXVK game on this line (the engine's base environment sets it) | workaround (opt-in: `DXVK_RELAX_FEATURES=1`) | from Michael Liendo's work (highball-db#6) |

## Details

### vkd3d-proton (v3.0.1)

- `0001` vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR is optional (winevulkan returns NULL for it on MoltenVK).
- `0002` missing transform feedback queries, single texel alignment and robustness2 are warnings, not a failed
  device.
- `0003` the NV memory decompression shaders count regions in an array, not a uvec3 (Metal refuses an atomic on a
  vector element).
- `0004` null descriptors emulated with dummy resources on devices without nullDescriptor. Known limit: the dummy
  resources are shared and writable, so a write through one null UAV is visible through another.
- `0005` tessellation on MoltenVK (driver ID check): one Location per varying, no Component decorations, which
  SPIRV-Cross cannot translate in tessellation stages.
- `0006` SV_ShadingRate dropped on devices without primitive fragment shading rate.
- `0007` `VKD3D_CONFIG=force_native_fp16` reports native 16-bit shader operations on drivers that report neither
  float16 denorm preservation nor an independent denorm behaviour (MoltenVK and KosmicKrisp on Apple GPUs). Without
  it, games that use 16-bit operations without checking the cap lose those pipelines (16 compute pipelines per
  Microsoft Flight Simulator 2024 run). Float16 denormals may flush with it. Off unless the flag is set.
- `0008` with dxil-spirv's `0001` early fragment tests for pixel shaders where they cannot be told from late ones. Every
  Forza Horizon 6 run has them. Not in the archive Microsoft Flight Simulator 2024's engines use.
- `0009` reserved-resource fallback on devices without sparse binding, including buffers. Extends
  the existing committed fallback used for unsupported sparse image formats. It commits the whole allocation
  and ignores tile mappings; it does not implement residency or tile aliasing. The standalone reserved-buffer
  probe changes from 2 pass / 1 fail to 4 / 0, and FH6 gets past its FHE21 resource-creation error. On a driver with
  sparse binding (KosmicKrisp, which FH6 plays on) real tiles are used and this path is not taken.
- `0010` limit the companion texel view for raw/structured buffers backed by an SSBO. A full 2 GB raw view in FH6
  exceeded MoltenVK's texel limit and triggered a Metal assertion. The SSBO descriptor and byte bounds retain
  the full range; actual typed buffers are unchanged. `tests/vkd3d-reserved/large-raw-buffer.c` reproduces the
  assertion on the control and passes all 35 checks with the patch, including raw and structured GPU reads
  through the last word of 2 GB. Oversized actual typed views and type aliasing beyond the limit remain unsupported.

### DXVK (master of 2026-10-09, 1cad6cd)

- `0001` a texture that cannot be shared is created unshared instead of calling vkGetMemoryWin32HandleKHR, which was
  never loaded (Microsoft Flight Simulator 2024 crashed at its first boot video). Written before Wine's
  patches-arm64/0036 gave MoltenVK shared memory the same day. It stays as the fallback when a texture cannot be
  shared.
- `0002` `dxgi.maxVideoMemoryBudget` (MB) caps the budget QueryVideoMemoryInfo reports, which on unified memory was
  about 80% of RAM and let games fill more than the GPU may wire.
- `0003` geometry shaders are optional on every device (Apple GPUs have none), and `DXVK_RELAX_FEATURES=1` turns the
  other missing required features into warnings. The arm64 engine's base environment sets it. From Michael-Liendo's
  msfs2024-apple-silicon work (highball-db#6).
