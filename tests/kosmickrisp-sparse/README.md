# Sparse texture tests for KosmicKrisp

Two native Vulkan programs that exercise sparse residency textures the way a D3D12 game that streams
textures does. They need the Vulkan headers and loader, and `VK_DRIVER_FILES` pointing at an ICD file for the
driver build under test.

```sh
cc -O2 sparse.c -o sparse -lvulkan
cc -O2 sparse_sample.c -o sparse_sample -lvulkan
```

`samp_spv.h` is `samp.comp` compiled (`glslangValidator -V samp.comp -o samp.spv; xxd -i samp.spv > samp_spv.h`).

- `sparse [width [height]]` (default 2048) binds a mip chain, mip tail first and then each level tile by tile,
  in both orders, and reads every level back by copy after each step. Then it unbinds levels finest first, and
  last the mip tail, which it binds again and refills. How many unbound levels read zero is reported, not judged.
  Formats: BC1 (RGB, RGBA, sRGB), BC3, BC4, BC5, BC6H, BC7 (and sRGB), RGBA8, R8, RGBA16F.
- `sparse_sample [size [sampler LOD bias]]` samples in a compute shader (`textureLod`, `texelFetch`, level
  count) through views that start at mips 0 to 6, fully bound and in streaming order, for RGBA8, BC1, BC1 sRGB
  and BC4.

Both print one line per case and end with a pass or failure count.

## What they found (base M5, macOS 27.3 beta, 2026-11-12)

Without highball patch `0008`, `sparse 4`, `sparse 4 8` and `sparse 4 16` fail for every block-compressed format
(72 failures each) and `sparse_sample 4` reports 159 wrong samples: when the base level is one block wide, levels
0 to 2 behave as if they share storage, so the level written last is right and the others read wrong data or
zero. 8x8, 16x4, 8x4 and uncompressed 4x4 pass. With `0008` every size from 4 to 4096 passes, and
`HB_KK_SPARSE_SMALL_BC=1` brings the failures back. Not run on other chips or macOS versions.

Unbinding: with `0008` the one-block-wide textures read zero at every level once their tail is unbound (3 of 3).
Real placement sparse textures do so only in part, with or without the patch: an 8x8 BC texture 2 of 4 tail
levels, a 256 or larger BC texture 6 of 8 or 7 of 9, RGBA8, R8 and RGBA16F none. That is not looked into.
