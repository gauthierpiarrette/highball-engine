# Patch series, arm64 line

Applied in lexical order on top of upstream Wine (`inputs-arm64.json`), by `build-arm64.yml`. The
Intel line's series in `patches/` is for CrossOver's tree and does not apply here; what the arm64
line needs of it is carried over by number.

Measured on an M4 (macOS 27.0, 2026-10-04): with these three patches, Hangover's FEX DLLs and the
Darwin helper in `fex/`, the native arm64 Wine boots a prefix in 2.4 s with both halves populated,
and 32-bit and 64-bit x86 programs run within 20 percent of Rosetta on a compute loop. Graphics are
not covered yet: DXMT needs the Mac driver's Metal glue, which upstream Wine does not carry (the
CrossOver tree does, as `macdrv_functions`), so the arm64 line has no renderer overlay until
CrossOver 27's sources are published or the glue is ported (about 500 lines).

- `0003-ntdll-winedllpath-prepend.patch`: WINEDLLPATH_PREPEND, the same file as the Intel line's
  0003. Highball's renderer overlays and shims all go through it.

- `0018-loader-arm64-macos-pagezero-reserve.patch`: arm64 macOS has no preloader and a `__PAGEZERO`
  of 4 GB or more. With Apple's cross-architecture entitlement the kernel lets the process map over
  it, so the loader hands Wine that range as its reserved low memory, where 32-bit and 64-bit
  Windows programs expect their address space. Without it `map_fixed_area` fails for every image
  below 4 GB. Only effective in a loader linked with `-pagezero_size 0x170000000 -image_base
  0x170000000 -x86_64_layout_emulation` (Xcode 26.4 or later) and signed with the entitlement, which
  is Highball's `WineLoader.app`; the unsigned loader this build produces is for development.

- `0019-ntdll-arm64-macos-image-pages.patch`: macOS on arm64 refuses writable-and-executable
  memory outside `MAP_JIT`, refuses to execute a file-backed mapping that is not signed, and has
  16 KB pages where x86 images are laid out in 4 KB ones, so code and data share a host page.
  `map_file_into_view` reads writable image sections into anonymous memory instead of mapping the
  file, requests for RWX memory are granted as RW and the page is flipped to RX on the first
  execute fault and back on the first write fault (`virtual_handle_fault`), tracked in a small
  range table that `delete_view` clears. `MAP_JIT` was tried first and does not fit: a thread's JIT
  write-protect state cannot be flipped from inside a signal handler, the flip is lost on return.
  Upstream: Brendan Shanks (CodeWeavers) announced macOS ARM64 merge requests on wine-devel on
  2026-08-07; none had landed by 2026-10-04. These two patches are Highball's own and go the day his
  land.
