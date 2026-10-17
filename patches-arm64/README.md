# Patch series, arm64 line

Applied in lexical order on top of upstream Wine (`inputs-arm64.json`), by `build-arm64.yml`. The
Intel line's series in `patches/` is for CrossOver's tree and does not apply here; what the arm64
line needs of it is carried over by number.

Measured on an M4 (macOS 27.0, 2026-10-04): with 0003, 0018 and 0019, Hangover's FEX DLLs and the
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

The rest makes x86-64 programs see what real x86-64 Windows shows them (2026-10-11, M1 Pro, macOS
27.0). Each finding comes from a check in `tests/x64-fidelity`, whose expectations hold on real
x86-64 Windows (the `x64-fidelity` workflow). 0022 to 0024 come from the FEX and arm64 spikes of
2026-10-09 and 2026-10-10. With the FEX build of `fex/patches/` (`fex_build: source`, the default
since 2026-10-11) they are its other half. With Hangover's FEX DLLs (`fex_build: hangover`) they
break nothing that works on main's engine, checked with the whole `tests/x64-fidelity` suite on an
M1 Pro (build-arm64 run 38126527000 against run 37245359313). The state they carry is simply absent
there, and the hooks that ask the emulator fall back when its exports are missing. 0022 to 0024
help even then: a program that unmasks a floating-point exception through the CRT gets the
exception instead of dying with an illegal instruction, and guard pages fault.

- `0022-ntdll-arm64-macos-fp-trap-sigill.patch`: Apple silicon implements the FPCR trap enables,
  and macOS reports a trapped floating-point exception as SIGILL with ILL_ILLTRP instead of
  SIGFPE. The SIGILL handler now tells it apart by the syndrome (EC 0x2c) and raises the matching
  STATUS_FLOAT_* code at the instruction, and clears the trap enables in the context the
  dispatcher runs with. The x64 half of unmasked SSE and x87 exceptions (FEX patch 0001).

- `0023-loader-arm64-macos-4k-pages.patch`: every Wine process re-execs itself with
  `posix_spawnattr_set_4k_page_size_np` (Apple's cross-architecture entitlement, which Highball's
  signed loader carries), so it runs with 4 KB pages: Windows programs assume 4 KB protection
  granularity (guard pages, VirtualProtect on part of a 16 KB page, write watches, self-modifying
  code detection). With 16 KB pages six memory checks fail. Falls back to host pages with a
  message when the attribute is refused; `HB_NO_4K_PAGES=1` turns it off.

- `0024-ntdll-arm64-macos-4k-pages-initial-process.patch`: the first process of a session never
  went through 0023's exec, only its children did. It now re-execs too while it still has 16 KB
  pages.

- `0025-ntdll-arm64-macos-id-registers.patch`: FEX decides which x86 extensions it can implement
  from the host's ID registers, which ntdll publishes in the registry (`CP 40xx`). macOS traps user
  reads of those registers, so `get_core_id_regs_arm64` was a stub and FEX saw a plain ARMv8.0
  CPU: no SSE4.2, AES-NI, PCLMULQDQ or SHA for x86 programs, and no LSE atomics for FEX itself,
  on Macs that have all of them. The registers are now built field by field from the
  `hw.optional.arm.FEAT_*` sysctls. On an M1 Pro: all advertised, and kattest's known-answer
  checks (CRC32C, AES-NI, PCLMULQDQ, SHA-1 and SHA-256 blocks, VAES, VPCLMULQDQ) all match.

- `0026-ntdll-aarch64-xstate-layout.patch`: `GetEnabledXStateFeatures` returned 0 on aarch64
  hosts although the emulator reports AVX. The XState layout (x87, SSE, AVX, standard form) now
  matches the emulator's CPUID leaf 0xD, so contexts can carry the YMM upper halves.

- `0027-ntdll-arm64ec-x64-state-through-context.patch`: the ARM64EC register mapping has no room
  for the x87 control, status and tag words, the EFLAGS bits NZCV cannot hold (PF, AF, DF, TF),
  MXCSR's DAZ or the YMM upper halves, so every exception handler, `GetThreadContext` and continue
  lost them: handlers saw a zero x87 control word, a continued thread came back with its x87 stack
  scrambled and DF from wherever the JIT left it. The conversions now carry them in registers
  ARM64EC code never uses (X13, X14, a marker in X28, V16-V31) and the dispatchers use an x64
  context with room for the AVX state. `RtlCaptureContext`, `setjmp` and `GetThreadContext` on the
  current thread take the x87 state from the emulator (`GetX87State`): before, a C++ exception
  unwound by `RtlUnwindEx` reset the x87 control word to 0x27f, which drops mingw-w64 programs
  from 64-bit to 53-bit precision. `SetThreadContext` merges the classes it is given into another
  thread's full state; on the current thread it applies them as they are (2026-10-11 fix: the merge
  read the state of the call itself and sent the thread back into it, so a `SetThreadContext` of
  the current thread with one class killed the program; ctxtest `self.set.*`).

- `0028-ntdll-arm64ec-x64-information-through-emulator.patch`: the CRT's `_controlfp` is native
  code and reads and sets MXCSR through `__os_arm64x_get_x64_information` and
  `__os_arm64x_set_x64_information`. The exception masks now come from and go to the emulator
  (`GetX64Mxcsr`, `SetX64Mxcsr`), which arms the trap enables for x64 code only, and native code
  keeps running with them off; the x87 control word comes from `GetX87State` instead of a fixed
  0x27f. With FEX patch 0009, `_controlfp` and `LDMXCSR` see each other's rounding mode, masks and
  denormal control, and an exception unmasked with `_controlfp` traps, as on x86-64 Windows.

- `0029-ntdll-arm64ec-x64-debug-registers.patch`: the x64 debug registers DR0-DR3, DR6 and DR7.
  The ARM64EC conversions dropped them and the arm64 Unix side answered them with a FIXME, so
  hardware breakpoints set with `SetThreadContext`, by a debugger, or in an exception handler's
  context never existed. The emulator implements them (FEX patch 0017), and they reach it the way
  the rest of the x64 state does, in contexts: the ARM64 breakpoint registers carry them, tagged.
  The Unix side keeps a thread's registers while it runs native code or waits, so that the contexts
  it reports carry them, and hands a change (from the thread itself, another thread or process, a
  debugger at a debug event) to the emulator with the next context the thread enters it with; a
  thread that goes back to x64 code through native code gets there through the suspend doorbell.
  Another thread's go through the server like its other registers: on hosts other than x86 the
  server now keeps the debug registers a thread reports for itself (it read them from the processor
  before, which only works on x86). They read back as x86-64 Windows gives them: as they were set
  until the thread runs with them (what a set keeps is the emulator's to say,
  `KeepX64DebugRegisters`), then with DR7 bit 10. With Hangover's FEX DLLs they are ignored and read
  0, as on Windows on Arm. On an M4, drtest and drtest32 71 of 71 and drdbg and drdbg32 25 of 25, as
  on x86-64 Windows Server 2022 and 2025. A trap can report registers another process changed while
  it was being dispatched: Windows Server 2025 showed it once for a 64-bit program, and a 32-bit
  program on the engine shows it more readily (Wine's wow64 reads the 32-bit context back then).

The next three come from the MSFS 2024 spike on an M4 (2026-10-09 and 2026-10-10), where they made Steam and
D3D12 through vkd3d-proton run on this line. They are what Vulkan needs here, and the build now configures Wine
with it (MoltenVK and the Vulkan headers from Homebrew).

- `0030-unixlib-host-libraries-next-to-wine.patch`: Wine opens MoltenVK, FreeType, GnuTLS and SDL by their leaf
  names, which a process started without its parent's DYLD variables (the children of a Windows program) does not
  find. `dlopen_host_lib` in `include/wine/unixlib.h` also looks two directories above the calling Unix module
  (`lib/wine/<arch>-unix/x.so` -> `lib/<name>`), where the build puts those libraries with `@loader_path` install
  names. Used by win32u (Vulkan, FreeType), dwrite (FreeType), crypt32 and secur32 (GnuTLS) and winebus (SDL). On an
  M4 with no DYLD variable at all: 848 font families with TrueType text, MoltenVK loaded and a D3D12 device through
  vkd3d-proton with all 14 smprobe shader tests correct. The spike's version had the same helper in three files and
  covered only MoltenVK and FreeType.

- `0031-winemac-child-window-metal-swapchain.patch`: upstream refuses a Metal swapchain on a child window whose
  toplevel belongs to another process. Chromium's GPU process presents into exactly such a child under the
  browser's window, so Steam's pages stayed black. The foreign-toplevel path (an offscreen CAMetalLayer exported
  through a CAContext and hosted by the owner in a CALayerHost) now also serves a child. Verified in the spike:
  Steam's sign-in page and library render. Not handled yet: a child that moves to another toplevel, DPI changes.

- `0032-winevulkan-layered-api-properties.patch`: the generated converter for
  `VkPhysicalDeviceLayeredApiPropertiesKHR` left the `VkPhysicalDeviceProperties2` inside a layered Vulkan entry
  unconverted. MoltenVK reports one layered API where Linux drivers report none, and vkd3d-proton's device setup
  faulted in `vkGetPhysicalDeviceProperties2`. The proper fix belongs in make_vulkan. Verified: a D3D12 device through
  vkd3d-proton on MoltenVK.

- `0033-ntdll-macos-skip-host-mappings-in-free-area-search.patch`: `map_free_area` tries a fixed mapping at every
  64 KB step of a gap between Wine's views until one succeeds. On arm64 macOS those gaps hold gigabytes of host
  mappings Wine has no view for: the loader image at 0x170000000 right above the reserved low memory of 0018, the
  shared system libraries after it, and 385 GB the kernel keeps for the GPU at 0xfc0000000. Every ARM64EC thread
  allocates its thread data and its emulator stack above 4 GB, so in a program that has used the reserved space up to
  0x170000000 each new thread cost two searches of about 130 000 failing `mach_vm_map` calls. After a failed attempt
  `try_map_free_area` now asks the kernel which mapping is in the way (`mach_vm_region`) and continues past it, which
  finds the same address. On an M4: 2440 ms per thread with 4 to 8 GB filled as MSFS 2024 fills it, 0.535 ms with the
  patch, 0.37 ms in a fresh process either way. MSFS 2024 reached its first boot video 38 s after launch instead of
  13 minutes.
- `0034-advapi32-credenumerate-empty-keychain-is-not-an-error.patch`: the Intel line's 0019. On macOS
  CredEnumerateW() also asks the Keychain through mountmgr, which answers STATUS_NOT_FOUND when nothing there
  matches; the call then failed with ERROR_NOT_FOUND and threw away the registry's matches. Xbox's XAL looks up
  its device key and tokens that way, so every XAL game (Microsoft Flight Simulator 2024 on this line, Forza
  Horizon 6 on the Intel line) asked for a sign-in at every launch. An empty answer from the host now counts
  like an unsupported host.
- `0035-kernelbase-per-exe-command-line-append.patch`: the Intel line's 0006. A new process gets
  `HKCU\Software\Wine\AppDefaults\<exe>\CommandLineAppend` appended to its command line. The engine's manifest
  uses it for Steam's browser, which under FEX cannot start its GPU child inside Chromium's sandbox: Highball
  writes `--disable-gpu --disable-gpu-compositing --no-sandbox --disable-gpu-sandbox` for steamwebhelper.exe,
  so the browser gets them however Steam was started.
- `0036-win32u-vulkan-share-memory-through-metal-objects.patch`: VK_KHR_external_memory_win32 on macOS. win32u only
  had it on top of fd memory, which MoltenVK lacks, so DXVK could not create shared textures and vkd3d-proton could
  not open them: no video in D3D12 games that decode it through Media Foundation in D3D11 (Microsoft Flight Simulator
  2024's boot, loading and start screens). The allocation is exported as a Metal object (MTLTexture, MTLBuffer or
  MTLHeap), kept in a process-local table under the inode of a pipe that stands for the fd inside the D3DKMT resource,
  and imported with VkImportMemoryMetalHandleInfoEXT. Same process only. Needs MoltenVK patch 0006.
- `0037-ffmpeg-arm64ec-yuv2rgb-narrow-blue-sums-first.patch`: Wine's ARM64EC port of swscale's yuv2rgb NEON code wrote
  some outputs (R2 for bgra) into v10/v11 before reading the blue sums kept there, so video converted to BGRA had
  vertical stripes and wrong colours. The blue sums are narrowed first. Same code in upstream master.
- `0038-winemac-per-program-metal-shader-cache-folder.patch`: the Mac driver gives each program its own Metal shader
  cache folder (highball/<program>/com.apple.metal under the user's cache folder, as DXMT does per game) in
  macdrv_init, before the Cocoa side creates the first Metal device, after which Metal keeps its default folder, the
  loader's, shared by every program the engine runs. HB_METAL_SHADER_CACHE names another folder, 0 keeps the default.
- `0039` ntdll: a process's memory counters on macOS from the task's physical footprint. The Mach virtual size,
  which the emulator's reservations push past 450 GB, was reported as PagefileUsage (PrivateUsage) and
  VirtualSize; Microsoft Flight Simulator 2024 budgeted on it, kept 102 MB of textures and cut its terrain
  level of detail to a quarter (its own trace: memoryKiB 639,388,984, terrainLodMilli 251), the coarse Free
  Flight map and menu globe. A 17 MB probe reports 16 MB of page file usage now instead of 463,651 MB (M4,
  2026-10-16).
- `0040-mfmediaengine-events-from-a-work-queue-thread-never-under-the-engine-lock.patch`: Wine's media engine called
  the application's IMFMediaEngineNotify::EventNotify with its own lock held (on the caller's thread in Play, Pause,
  SetSource and the setters, on the MF work-queue thread for the session events), so an application whose callback
  takes a lock of its own, while another of its threads holds that lock and calls any engine method, deadlocked:
  Microsoft Flight Simulator 2024's main and intro-video threads on the M4 (2026-10-16), a black screen for good.
  Events now queue in order and are delivered by one dispatcher work item on the multithreaded queue with no engine
  lock held; Shutdown drains them. `tests/mfmediaengine/mfdeadlock.c` reproduces the deadlock (DEADLOCK on the engine
  as shipped, OK with the patch). The code is platform-independent and belongs in `patches/` for the Intel line's next
  Wine 11 revision too.
- `0041-ntdll-arm64ec-the-x64-state-on-an-unhandled-exception.patch`: when an unhandled exception from emulated x64
  code is about to end the process (the second-chance NtRaiseException, the last point with an x64 context), the log
  gets the exception's code, address and parameters, rip, rsp, rbp, eflags and the general registers, with the TEB
  stack bounds and a note when rsp is outside them. Wine's own line names only the instruction; winedbg --auto is too
  slow on a 25 GB process; +seh costs MSFS 2024 two to three times its loading time. First chances print nothing.
