# MoltenVK for the arm64 line

MoltenVK v1.4.2 (Vulkan on Metal) with the patches below, and SPIRV-Cross (MoltenVK's SPIR-V to Metal Shading
Language translator) at the revision MoltenVK 1.4.2 pins, with Highball's patches. `build-moltenvk-arm64.yml` builds
them for arm64 and packages `lib/libMoltenVK.dylib`, the library the engine's winevulkan opens. The Intel line's
MoltenVK is built separately, for x86-64, by the app repository's `Scripts/build-moltenvk.sh`.

Each series is `git format-patch` output against its pinned commit. Applied in order, MoltenVK 0001 to 0004 rebuild,
file for file, the MoltenVK tree of the gin19g build that the Microsoft Flight Simulator 2024 runs of 2026-10-12 used
(checked on the M4 against that tree), and the SPIRV-Cross series is the commits that build carried. MoltenVK 0005 to
0011 came from the runs of 2026-10-12 and 13.

## MoltenVK (v1.4.2, db66022)

- `0001` a descriptor set is bound again after MoltenVK's own clears, copies and fills replaced its argument buffer at
  the same Metal index. Without it the next draw or dispatch read the helper's buffer as the set: GPU hangs and
  garbage in Microsoft Flight Simulator 2024 through vkd3d-proton.
- `0002` destroyed Metal objects are released 3 seconds late. The GPU may still run work submitted before the
  destroy; Windows and Linux keep such memory until the GPU is done, and D3D12 games rely on it. A time limit is
  crude: releasing when the submitted work completes would be exact.
- `0003` command buffer error messages name the VkDevice (DXVK's and vkd3d-proton's both label theirs "Queue 0-0").
- `0004` `HB_MVK_TEXTURE_2D_AS_ARRAY=1` creates shader-readable 2D views as one-layer 2D array textures and converts
  shaders with SPIRV-Cross's `texture_2D_as_2D_array` (SPIRV-Cross `0004`). Off unless set.
- `0005` placement heaps go into the device's residency set. Images placed in a heap had their own texture added,
  but the textures MoltenVK makes from the heap later (block-texel views of compressed images, 2D views of 3D images)
  did not, and shader validation reported them used while not resident in three compute pipelines of Microsoft
  Flight Simulator 2024.
- `0006` an imported MTLTexture stays the memory of its dedicated image. initExternalMemory(), written for export,
  replaced it with a new texture, so memory shared between two VkDevices as an MTLTexture never showed the other
  side's content. Needed by patches-arm64/0036.
- `0007` textures shared with another VkDevice are not compressed (allowGPUOptimizedContents NO). The two devices'
  queues are not ordered, and a read racing a write of a compressed texture showed whole tiles of magenta in the
  game's videos. Measured: magenta blocks in 2 of 25 boot screenshots before, 0 of 216 after (one per second).
- `0008` an image bound to imported MTLTexture memory retains the texture. It used the memory's texture without a
  reference of its own and released it on destroy, so a texture shared between two VkDevices was freed while the
  other side still used it (crashes in vkDestroyImage/vkDestroyBuffer and a hung game once videos were shared). Ships
  with 0006 and patches-arm64/0036.
- `0009` VK_EXT_image_sliced_view_of_3d for one-level 3D images. D3D12 UAVs can cover a range of a 3D texture's
  slices, and without the extension vkd3d-proton views the whole volume, so a shader writing "its" slice at z 0 wrote
  slice 0 (the game builds a 64x64x64 volume one slice per UAV). A sliced view is a 3D texture of the slice count
  placed in the image's heap at the first slice, which works because an uncompressed one-level 3D texture keeps its
  slices whole at a fixed stride (measured on Apple GPUs for 1 to 16 byte formats, 4x4 to 256x128). With mip levels
  Metal interleaves the levels, so those images keep the whole-image view. Such images go in a heap uncompressed.
- `0010` subresource offsets of compressed 2D and 2D array images on Apple GPUs follow the GPU's miptree layout, as
  Mesa's AGX driver describes it (src/asahi/layout/layout.c, MIT). Block-texel views alias a level and layer at these
  offsets. The previous sum of standalone level sizes misplaced every level from 2 on of textures whose size is not
  a power of two (6 of 30 levels and layers right in a 520x520 BC7 array), and the game compresses its terrain and
  imagery tiles on the GPU through such views. Measured: all 371 levels and layers tested at the computed offset.
- `0011` a render pass without attachments keeps width x height x layers within 2^34 by lowering its layer count.
  vkd3d-proton renders D3D12 UAV-only draws in such a pass at the device maximums (16384x16384, 2048 layers), and on
  Apple GPUs every command buffer holding one fails with "Internal Error (0000010d)" without running, taking its
  texture streaming work along (block noise in the cockpit displays, garbage ground textures, about 70 command
  buffers by Ready to fly). A Metal-only test fails the same way (an M1 Pro from 256-512 layers at 16384x16384, an M4
  at 2048) and works with one layer. With the patch a full test flight had no failed command buffer.
- `0012` every VkDevice keeps an implicit pipeline cache on disk for pipelines created without one (vkd3d-proton's
  global_pipeline_cache mode, enforced with a shader cache path, gives MoltenVK none, and MoltenVK has no driver
  cache of its own as Mesa has): read at creation, written every 60 s when it grew and at destruction, one file per
  device under highball/<program> in the user's cache folder, named with the pipelineCacheUUID. Libraries read from
  cache data compile their MSL on first use. The SPIR-V to MSL conversions of one start serve the next (Microsoft
  Flight Simulator 2024: about 11,000 at 100 to 140 ms each, the critical path of its 7 to 8 minute shader
  preparation). HB_MVK_PIPELINE_CACHE names another folder, 0 disables.
- `0013` an update-after-bind descriptor pool (a D3D12 bindless heap under vkd3d-proton) gets 8 MiB + 64 KiB of guard
  room after its GPU buffer: the shaders declare the descriptor arrays at the layout maximum (1,000,003 entries of 8
  bytes) while the pool holds what the app asked for, and a descriptor index past the pool's end reads an unmapped
  page on Metal, a GPU page fault the device recovers from (Microsoft Flight Simulator 2024 at the end of its loading
  screens, every start; a PC's mapped heap memory absorbs the same read). HB_MVK_POOL_GUARD=0 turns it off.
- `0014` a copy from a compressed image to an uncompressed one keeps every partial edge block (the extent divided by
  the block size rounds up): a 5x5 BC7 region reached a 1x1 destination instead of 2x2. Proven with a standalone
  probe on 2026-10-13 (5 of 8 cases wrong before, 8 of 8 right after); the game compresses its tiles on the GPU
  through such views, with borders that make their sizes odd.

## SPIRV-Cross (6c09849, MoltenVK 1.4.2's pin)

- `0001` alias casts keep the base variable's `__restrict` (Metal refused casts that dropped it, about 55 of the
  game's failing shaders), `raster_order_group` stays off arrays of buffer pointers, and an OpCopyObject of an access
  chain keeps its access chain flag.
- `0002` `__restrict` goes on the element pointer of buffer arrays passed by reference, so the parameter binds to the
  argument buffer member (hull shaders failed with "no matching function for call to 'patch_main'").
- `0003` a flattened input array element widened to the previous stage's vector size (a float3 read as a float4) is
  swizzled back when copied into the array.
- `0004` `Options::texture_2D_as_2D_array` declares non-multisampled 2D textures as 2D arrays and reads layer 0, the
  same shape as `texture_1D_as_2D`, for D3D12 shaders that read a Texture2DArray descriptor through a Texture2D
  declaration (the game's compute passes).
