# Reserved buffer creation probe

Build with the engine's LLVM-MinGW toolchain:

```sh
x86_64-w64-mingw32-clang -O2 -Wall -Wno-unused-function reserved.c -ldxguid -o reserved.exe
```

Run `reserved.exe` in a test bottle using the renderer being investigated. It
loads D3D12 dynamically, reports the advertised tiled-resource tier, creates a
16 MB reserved UAV buffer, and checks for a nonzero GPU virtual address.
Exit status is the failure count, using the shared fidelity test harness.

For the ARM64 MoltenVK test, use the engine's normal renderer environment,
including `VKD3D_FEATURE_LEVEL=12_1` and `VKD3D_SHADER_MODEL=6_6`. Record which
native `d3d12core.dll` is installed in the bottle: it can take precedence over
`WINEDLLPATH_PREPEND` when the DLL override is `n,b`.

On the M4 test with r6 Wine, stock FEX and MoltenVK 1.4.2, the same-source
renderer control fails reserved buffer creation with E_FAIL (2 pass / 1 fail).
Patch 0009's committed-resource fallback passes (4 / 0).

This is a creation regression probe. It does **not** validate tile mapping,
aliasing, residency, GPU reads/writes, or gameplay. The fallback allocates the
whole resource and does not implement sparse residency semantics.

## Large raw-buffer companion views

```sh
x86_64-w64-mingw32-clang -O2 -Wall -Wno-unused-function large-raw-buffer.c \
    -ld3d12 -ld3dcompiler -ldxguid -o large-raw-buffer.exe
```

This test uses a **committed** 2 GB buffer to isolate the texel-view limit from
sparse binding. It creates full-range raw and structured SRVs and a raw UAV,
uploads known words at seven offsets, then checks GPU reads through both SRVs.
Offsets bracket 256 MB and 1 GB, and include the last word of the allocation.
It then writes through the full-range raw UAV and runs eight contending atomic
increments per offset. It checks all returned old values, the final shader read,
and an independent GPU copy from each buffer location. Shader compilation uses
D3DCompiler; fence completion is required for each phase.

The write/atomic shader needs a compiler with `RWByteAddressBuffer.InterlockedAdd`
support. The tested Wine builtin compiler rejects this method before GPU work;
that is a compiler limitation, not a failed atomic check. The extended M4 test
used the existing native `d3dcompiler_47.dll` distributed with Steam, beside the
test executable, with `WINEDLLOVERRIDES=d3dcompiler_47=n,b` for that test only.

With the control renderer, creating the raw SRV reaches Metal's texture-width
assertion before GPU work starts. Patch 0010 limits only the companion texel
view when raw/structured access uses an SSBO; the test checks that SSBO reads
retain the full range. An aborted run may report exit zero through Wine, so
require the final `SUMMARY` line and all data checks to pass.

The original read-only fixture on M4 with patch 0010: **35 pass / 0 fail**,
including all fourteen raw/structured GPU reads. Same-source control aborts at
the first raw SRV creation. The extended fixture with the native compiler passes
**65 / 0** on M4 with the r3 driver in both normal/full FEX validation modes,
and **65 / 0** with the private geometry-shader driver in normal mode. These
results validate the committed-buffer path; they do not validate sparse mapping
or gameplay. Native Windows reference testing is still pending.

## Tile mapping and physical aliasing

```sh
x86_64-w64-mingw32-clang -O2 -Wall -Wextra -Werror sparse-tiles.c \
    -ldxguid -o sparse-tiles.exe
```

The fixture maps two reserved-buffer tiles to one 64 KiB heap page. GPU copies
verify that writing either virtual tile changes both aliases. It unmaps the
second tile, checks a zero read while the first retains its data, then remaps
and writes through the second alias. Every submission has a bounded fence wait.
Run with `host-visible` to exercise a custom WRITE_BACK/L0 heap on a compatible
UMA adapter. No shader compiler is required.

On the M4 with canonical ARM64 Wine/FEX, the committed-resource fallback reports
Tier 2 but fails both alias-propagation checks: **37 pass / 2 fail**. The private
KosmicKrisp sparse backend passes **39 / 0** with a device-local heap. Its original
CPU-visible allocation path hits a Metal sparse-page compatibility assertion;
the private correction that allocates compatible heaps for both memory types
passes **39 / 0** in each mode. The corrected backend also retains the large RAW
fixture's **65 / 0** result. These checks establish tile mapping through D3D12,
Wine/FEX and Vulkan; gameplay and a native Windows reference remain separate
verification requirements. The alternative backend is not part of this engine
release.
