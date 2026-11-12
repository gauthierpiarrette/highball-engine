# Patch series, arm64 line

Applied in lexical order on top of upstream Wine 11.18 (`inputs-arm64.json`) by `build-arm64.yml`. FEX runs the
x86-64 and x86 code ([`fex/`](../fex/)), vkd3d-proton and DXVK draw Direct3D 12, 11 and 10 through MoltenVK
([`renderers-arm64/`](../renderers-arm64/), [`moltenvk-arm64/`](../moltenvk-arm64/)). DXMT is not on this line: it
needs the Mac driver's Metal glue that only CrossOver's tree has (`macdrv_functions`).

The numbers do not match the Intel line's. This series started on 2026-10-05, when `patches/` ended at 0016, with
0003 (the same file as the Intel line's 0003) and its own 0018 and 0019, and later patches took the next free number
on either line. Since then the Intel line has used 0017 to 0025 for other patches. Two arm64 patches are ports of
Intel ones under new numbers (0034 is the Intel line's 0019, 0035 its 0006), and two fix the same thing as an Intel
patch in a different way (0031 and the Intel 0007, 0039 and the Intel 0018).

## At a glance

Kinds are explained in the [top-level README](../README.md#patches-at-a-glance). None of these has been sent to WineHQ.

| Patch | What it fixes | Needed by | Kind | Upstream, and what it pairs with |
|---|---|---|---|---|
| `0003` ntdll-winedllpath-prepend | Lets Highball's renderer folders go in front of Wine's own DLLs. | Highball's renderer overlays | Highball | not upstreamable as is (MacPorts GPTK patch 1005) |
| `0018` loader-arm64-macos-pagezero-reserve | Windows programs could not use memory below 4 GB, so images placed there failed to load. | every program | arm64 port | ours until Brendan Shanks' macOS arm64 merge requests land |
| `0019` ntdll-arm64-macos-image-pages | macOS refused to run Windows code from writable memory or unsigned file mappings. | every program | arm64 port | as 0018 |
| `0022` ntdll-arm64-macos-fp-trap-sigill | A program that unmasked floating-point exceptions died with an illegal instruction instead. | tests/x64-fidelity | x86-64 fidelity | the Wine half of FEX 0001 |
| `0023` loader-arm64-macos-4k-pages | Guard pages and fine-grained memory protection broke on Apple silicon's 16 KB pages. | tests/x64-fidelity (memtest) | arm64 port (off-switch: `HB_NO_4K_PAGES`) | needs Highball's signed loader |
| `0024` ntdll-arm64-macos-4k-pages-initial-process | The first program of a session kept 16 KB pages. | as 0023 | arm64 port (off-switch: `HB_NO_4K_PAGES`) | follows 0023 |
| `0025` ntdll-arm64-macos-id-registers | x86 programs were denied SSE4.2, AES-NI, PCLMULQDQ and SHA that the Mac's CPU supports. | tests/x64-fidelity (kattest) | arm64 port | |
| `0026` ntdll-aarch64-xstate-layout | x86-64 programs were told AVX state was unsupported. | tests/x64-fidelity | x86-64 fidelity | |
| `0027` ntdll-arm64ec-x64-state-through-context | Exceptions lost floating-point, flag and AVX state, and mingw-w64 programs lost precision. | tests/x64-fidelity (ctxtest) | x86-64 fidelity | pairs with FEX 0003 and 0006 |
| `0028` ntdll-arm64ec-x64-information-through-emulator | The C runtime's `_controlfp` and the program's x86 code did not see each other's settings. | tests/x64-fidelity | x86-64 fidelity | pairs with FEX 0009 |
| `0029` ntdll-arm64ec-x64-debug-registers | Hardware breakpoints set by programs or debuggers never fired. | tests/x64-fidelity (drtest, drdbg) | x86-64 fidelity | pairs with FEX 0017 |
| `0030` unixlib-host-libraries-next-to-wine | Programs started by other Windows programs got no Vulkan, fonts, TLS or SDL. | Steam client | macOS | |
| `0031` winemac-child-window-metal-swapchain | Steam's pages stayed black inside its window. | Steam client | macOS | the Intel line's 0007 fixes the same on CrossOver's tree |
| `0032` winevulkan-layered-api-properties | Direct3D 12 device setup crashed through vkd3d-proton on MoltenVK. | vkd3d-proton games | general | the proper fix belongs in make_vulkan |
| `0033` ntdll-macos-skip-host-mappings-in-free-area-search | Creating a thread in a large x86-64 program took over two seconds. | Microsoft Flight Simulator 2024 | arm64 port | |
| `0034` advapi32-credenumerate-empty-keychain-is-not-an-error | Xbox games asked for a sign-in at every launch. | Microsoft Flight Simulator 2024 | macOS | the Intel line's 0019, upstream has the same code |
| `0035` kernelbase-per-exe-command-line-append | Lets the app give Steam's browser the switches it needs under FEX. | Steam client | Highball | the Intel line's 0006 |
| `0036` win32u-vulkan-share-memory-through-metal-objects | Videos stayed black in Direct3D 12 games because D3D11 and D3D12 could not share textures. | Microsoft Flight Simulator 2024 | macOS | needs MoltenVK 0006 and 0008 |
| `0037` ffmpeg-arm64ec-yuv2rgb-narrow-blue-sums-first | Video converted to BGRA had stripes every 16 pixels and wrong colours. | videos through Media Foundation | general | Wine master (11.19) still had the bug on 2026-10-12 |
| `0038` winemac-per-program-metal-shader-cache-folder | Every program shared the loader's Metal shader cache, so games compiled their shaders again. | Microsoft Flight Simulator 2024 | macOS (off-switch: `HB_METAL_SHADER_CACHE=0`) | |
| `0039` ntdll-macos-vm-counters-from-the-physical-footprint | Programs were told they used hundreds of gigabytes, so a game cut its textures and detail. | Microsoft Flight Simulator 2024 | macOS | the Intel line's 0018 does the same |
| `0040` mfmediaengine-events-from-a-work-queue-thread-never-under-the-engine-lock | A program playing video through Wine's media engine could freeze for good. | Microsoft Flight Simulator 2024 | general | upstream master has the same code, reproducer in `tests/mfmediaengine` |
| `0041` ntdll-arm64ec-the-x64-state-on-an-unhandled-exception | A crash log named the instruction but not the x64 registers. | Microsoft Flight Simulator 2024 | diagnostics | |
| `0042` ntdll-arm64ec-rtliseccode-above-the-user-address-space | A thread could fault in a loop until its stack ran out. | Microsoft Flight Simulator 2024 | general | |
| `0043` ntdll-arm64ec-a-program-sees-its-own-page-protection | Programs saw the emulator's page protection, so restoring it made their own pages fault. | Microsoft Flight Simulator 2024, copy protections | x86-64 fidelity | pairs with FEX 0022 |

## Details

Measured on an M4 (macOS 27.0, 2026-10-04): with 0003, 0018 and 0019, Hangover's FEX DLLs and the
Darwin helper in `fex/`, the native arm64 Wine boots a prefix in 2.4 s with both halves populated,
and 32-bit and 64-bit x86 programs run within 20 percent of Rosetta on a compute loop.

- `0003-ntdll-winedllpath-prepend.patch`: WINEDLLPATH_PREPEND, the same file as the Intel line's
  0003. Highball's renderer overlays and shims all go through it.

- `0018-loader-arm64-macos-pagezero-reserve.patch`: arm64 macOS has no preloader and a `__PAGEZERO`
  of 4 GB or more. With Apple's cross-architecture entitlement the kernel lets the process map over
  it, so the loader hands Wine that range as its reserved low memory, where 32-bit and 64-bit
  Windows programs expect their address space. Without it `map_fixed_area` fails for every image
  below 4 GB. Only effective in a loader linked with `-pagezero_size 0x170000000 -image_base
  0x170000000 -x86_64_layout_emulation` (Xcode 26.4 or later) and signed with the entitlement, which
  is Highball's `WineLoader.app`; the unsigned loader this build produces is for development.
  Upstream: as 0019.

- `0019-ntdll-arm64-macos-image-pages.patch`: macOS on arm64 refuses writable-and-executable
  memory outside `MAP_JIT`, refuses to execute a file-backed mapping that is not signed, and has
  16 KB pages where x86 images are laid out in 4 KB ones, so code and data share a host page.
  `map_file_into_view` reads writable image sections into anonymous memory instead of mapping the
  file, requests for RWX memory are granted as RW and the page is flipped to RX on the first
  execute fault and back on the first write fault (`virtual_handle_fault`), tracked in a small
  range table that `delete_view` clears. `MAP_JIT` was tried first and does not fit: a thread's JIT
  write-protect state cannot be flipped from inside a signal handler, the flip is lost on return.
  Upstream: Brendan Shanks (CodeWeavers) announced macOS ARM64 merge requests on wine-devel on
  2026-08-07; none had landed by 2026-10-04. 0018 and 0019 are Highball's own and go the day his
  land.

The next eight make x86-64 programs see what real x86-64 Windows shows them (2026-10-11, M1 Pro, macOS
27.0). Each finding comes from a check in `tests/x64-fidelity`, whose expectations hold on real
x86-64 Windows (the `x64-fidelity` workflow). 0022 to 0024 come from the FEX and arm64 spikes of
2026-10-09 and 2026-10-10. With the FEX build of `fex/patches/` (`fex_build: source`, the default
since 2026-10-11) they are its other half. With Hangover's FEX DLLs (`fex_build: hangover`) they
broke nothing that worked on main's engine, checked with the whole `tests/x64-fidelity` suite on an
M1 Pro (build-arm64 run 38126527000 against run 37245359313, before 0029 and the 0027 fix). The state they carry is simply absent
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
  message when the attribute is refused. `HB_NO_4K_PAGES` set to any value turns it off. The file
  is named after the loader but patches `dlls/ntdll/unix/loader.c`.

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
  the current thread with one class killed the program; ctxtest `self.set.*`). The patch header
  predates that fix and still describes the merge for every thread.

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
  find. `dlopen_host_lib` in `include/wine/unixlib.h` now tries the copy two directories above the calling Unix
  module first (`lib/wine/<arch>-unix/x.so` -> `lib/<name>`), where the build puts those libraries with
  `@loader_path` install names, and the leaf name second (since 2026-10-16: the leaf name found GStreamer's own
  libMoltenVK first, and DXVK then refused the device). Used by win32u (Vulkan, FreeType), dwrite (FreeType), crypt32
  and secur32 (GnuTLS) and winebus (SDL). On an M4 with no DYLD variable at all: 848 font families with TrueType
  text, MoltenVK loaded and a D3D12 device through vkd3d-proton with all 14 smprobe shader tests correct. The spike's
  version had the same helper in three files and covered only MoltenVK and FreeType.

- `0031-winemac-child-window-metal-swapchain.patch`: upstream refuses a Metal swapchain on a child window whose
  toplevel belongs to another process. Chromium's GPU process presents into exactly such a child under the
  browser's window, so Steam's pages stayed black. The foreign-toplevel path (an offscreen CAMetalLayer exported
  through a CAContext and hosted by the owner in a CALayerHost) now also serves a child. Verified in the spike:
  Steam's sign-in page and library render. Not handled yet: a child that moves to another toplevel, DPI changes.
  The Intel line's 0007 solves the same problem on CrossOver's tree.

- `0032-winevulkan-layered-api-properties.patch`: the generated converter for
  `VkPhysicalDeviceLayeredApiPropertiesKHR` left the `VkPhysicalDeviceProperties2` inside a layered Vulkan entry
  unconverted. MoltenVK reports one layered API where Linux drivers report none, and vkd3d-proton's device setup
  faulted in `vkGetPhysicalDeviceProperties2`. The proper fix belongs in make_vulkan. Verified: a D3D12 device through
  vkd3d-proton on MoltenVK.

The rest came from Microsoft Flight Simulator 2024 runs on the M4 between 2026-10-11 and 2026-10-18.

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
  and imported with VkImportMemoryMetalHandleInfoEXT. Same process only. Needs MoltenVK patches 0006 and 0008 (without
  0008 the shared textures were freed while still in use), and 0007 keeps them free of compression artefacts.

- `0037-ffmpeg-arm64ec-yuv2rgb-narrow-blue-sums-first.patch`: Wine's ARM64EC port of swscale's yuv2rgb NEON code wrote
  some outputs (R2 for bgra) into v10/v11 before reading the blue sums kept there, so video converted to BGRA had
  vertical stripes and wrong colours. The blue sums are narrowed first. Upstream master (11.19) had the same code,
  bug included, on 2026-10-12.

- `0038-winemac-per-program-metal-shader-cache-folder.patch`: the Mac driver gives each program its own Metal shader
  cache folder (highball/<program>/com.apple.metal under the user's cache folder, as DXMT does per game) in
  macdrv_init, before the Cocoa side creates the first Metal device, after which Metal keeps its default folder, the
  loader's, shared by every program the engine runs. HB_METAL_SHADER_CACHE names another folder, 0 keeps the default.

- `0039-ntdll-macos-vm-counters-from-the-physical-footprint.patch`: a process's memory counters on macOS come from
  the task's physical footprint. The Mach virtual size, which the emulator's reservations push past 450 GB, was
  reported as PagefileUsage (PrivateUsage) and VirtualSize; Microsoft Flight Simulator 2024 budgeted on it, kept 102
  MB of textures and cut its terrain level of detail to a quarter (its own trace: memoryKiB 639,388,984,
  terrainLodMilli 251), the coarse Free Flight map and menu globe. A 17 MB probe reports 16 MB of page file usage now
  instead of 463,651 MB (M4, 2026-10-16). The Intel line's 0018 makes the same change in CrossOver's tree.

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

- `0042-ntdll-arm64ec-rtliseccode-above-the-user-address-space.patch`: RtlIsEcCode answers FALSE for an address
  above the user address space instead of reading past the ARM64EC code map. When the unwinder met a garbage
  return address, the fault was dispatched through the same unwind and faulted again, about 280 times, until the
  thread's stack ran out: Microsoft Flight Simulator 2024's main thread hung this way 45 s into every flight with
  FEX's store replay and retirement both on (2026-10-18), and once before (boot73, 2026-10-16). Windows answers
  FALSE there.

- `0043-ntdll-arm64ec-a-program-sees-its-own-page-protection.patch`: NtProtectVirtualMemory's old protection and
  NtQueryVirtualMemory's (MemoryBasicInformation) go through the emulator's `GuestProtection` (FEX `0022`), so a
  page FEX made read-execute to see writes into translated code reads back with the program's own protection, as on
  Windows. NtQueryVirtualMemory is wrapped like the other memory calls for this; the emulator's own calls skip it.
- `0044-ntdll-globalroot-dos-file-paths.patch`: resolve GLOBALROOT aliases that lead back into a DOS namespace
  before Unix file translation. FH6 reopens a mapped file this way; the failed open caused its later startup
  exception to go unhandled. Nested and mixed-case aliases work; native object paths still use server lookup.
  `tests/x64-fidelity/globalroot.c`: initial stock control 3/12, patched 12/12; final test adds native Null and
  passes 13/13 on the M4. Native Windows reference testing remains pending.
- `0045-ntdll-arm64-macos-top-down-high-addresses.patch`: Windows MEM_TOP_DOWN allocations on ARM64 macOS search
  high free addresses before the small reserved low region. FEX's large reservations previously consumed the
  space above the PE image, preventing FH6 from placing generated code nearby. Explicit addresses, caller bounds,
  alignment and fallback are preserved. Wine's own stack mappings retain their old policy: moving those high
  caused posix_spawn EFAULT. `tests/x64-fidelity/topdown.c`: stock 7/8, patched 8/8. The full fidelity suite has no
  new failures versus stock r6 FEX; x87 failures remain, and FEX patch 0024 addresses WriteProcessMemory. Native Windows reference
  testing remains pending. These two patches reach FH6 graphics initialization; gameplay is not yet verified.
- `0046-ntdll-arm64ec-track-executable-data-mappings.patch`: successful non-image section mappings report their
  protection through the emulator's existing callbacks. Previously a new FILE_MAP_EXECUTE view reported RX but
  faulted on its first call until a redundant VirtualProtect registered it. Loader-only image notifications retain
  their existing behavior. `memtest` adds a mapped alias / FlushInstructionCache check: control 50/1, patched 51/0
  in normal and full validation modes; context 546/0. Native Windows reference and FH6 gameplay remain pending.
- `0047-ntdll-arm64ec-preserve-guest-memory-region-sizes.patch`: use FEX 0028's optional protection-range hook
  to report coherent guest regions across SMC write traps. Clamp at genuine guest protection boundaries, then
  coalesce adjacent matching regions within one allocation. The old scalar hook remains the fallback for older
  emulators; other information classes and cross-process queries retain their existing behavior. `regiontest`
  covers generated code, a true RX boundary and decommit: M4 control 78/82, paired correction 160/0 in normal/full
  modes; x64 Wine control 160/0. Memory 51/0 and context 546/0 remain passing. Native Windows references and FH6
  gameplay verification remain pending.

`0048` preserves PAGE_GUARD faults in the macOS ARM64 W^X fast paths. An executable writable guard page
otherwise accepts a write before the normal guard handler runs, retaining PAGE_GUARD and delivering no exception.
The expanded `memtest` changes from 51/2 to 53/0 in normal/full modes on the M4, using clean FEX through 0030.
The independent write fixture also matches x64 Wine afterward. This fixes the Unix memory layer only; executable
instruction decoding through guard pages has a separate FEX limitation, and native Windows references remain pending.
FH6 still exits during startup; this is not a game compatibility claim.

`0049` reports the actual committed region protections for directly mapped images and data sections through
the existing ARM64EC protection callbacks. A SEC_IMAGE map outside LoadLibrary previously skipped the loader's
image notification, so an executable PE section faulted until a redundant VirtualProtect registered it with FEX.
The requested mapping protection does not describe an image's individual section protections. Loader-managed
images retain their existing callback. `imagemap` changes from 3/1 to 4/0 on clean FEX0030 in normal/full modes,
with x64 Wine also 4/0. Memory53/0, region160/0, COW22/0, image-query6/0 and context546/0 retain their passing
results in both modes. Native Windows references and FH6 gameplay remain unverified.

`0050` gives direct x64 syscalls a separate aligned frame, preserving the guest's live stack values and
instruction-defined RCX/R11/flags. It copies only the stack arguments declared in ALL_SYSCALLS, rather than
using guest stack slots for bookkeeping. Entry flags are saved separately because INT 2E does not put them
in R11. The final `syscalltest` changes from 173/72 to 245/0; its Wine/FEX-only INT 2E mode changes from
164/54 to 218/0. Corrected results pass normal/full validation with either RSP alignment, and x64 Wine gives
245/0 for SYSCALL. Context546/0, memory53/0 and fibers86/0 retain their passing results. A synthetic unwind
recovers the guest continuation, RBP and RSP. Native Windows reference and FH6 gameplay remain unverified.

`0051` is a compatibility candidate for kernel32's imported ARM64EC exports. Their x64 entry points now use
the same eight-byte no-op prologue as x64 Wine, then jump through the same IAT destination. The list of 791
forwarders is selected from Wine 11.18's ARM64EC spec and must be regenerated if those imports change.
Syscall245/0, context546/0, memory53/0 and fibers86/0 pass in normal/full modes. A prototype reaches FH6's
title, completes Xbox sign-in and reaches the main menu and shader optimization on M4. One prototype
repeat stalls during saved-account loading; the reproducible patch build subsequently loads the cached
account, cloud save and main menu. Driving, a complete repeat and the same-build control remain pending. This is not a verified ARM64 compatibility claim or a published engine.

`0052` makes two opt-in switches for programs that rewrite their own code all the time. `HB_FEX_MAPJIT=1` maps the
emulator's code buffers MAP_JIT and lets the emulator flip its own thread between writing and executing
(ThreadHbJitWrite, class 0x7fe of NtSetInformationThread), with no W^X page flips. `HB_DIRECT_CONTINUE=1` continues
emulated code straight from the syscall instead of through a signal. Both are off unless set.

### Forza Horizon 6

`0044` to `0052` with FEX `0024` to `0033`, vkd3d-proton `0008` to `0010` and KosmicKrisp (`kosmickrisp-arm64/`) play Forza
Horizon 6 on a base M4 with 32 GB, macOS 27.0, with the switches in highball-db's recipe: intro, Xbox sign-in, menus, free
roam and normal exit, many sessions between 2026-10-29 and 2026-10-31. The stall of about 100 ms every 1.1 s from its copy
protection is gone, and at 1080p, Very Low, FSR Quality and a cap of 30 the panel showed 8995 of 8997 frames at an even
2 vsyncs. These patches apply to every game on an engine built with them, so the line that ships them is separate
from the one Microsoft Flight Simulator 2024 pins until a flight has run on them.

### System call writes into the emulator's write trap (0058)

FEX makes a read-write-execute page that holds translated code read-execute, so that a write into it faults into
its tracking of self-modifying code. A system call that wrote into such a page for the program faulted on the Unix
side and returned STATUS_ACCESS_VIOLATION, where the Windows kernel writes: a buffer next to code that ran could not
take a read, a copy or a query. Football Manager 27's copy protection asks for the SMBIOS table that way and stopped
the game at "Loading game database". `0058` marks the pages the emulator traps (ntdll's ARM64EC side asks
`GuestProtection` from `0043`), lets a write from inside a system call through to a marked page, and gives the page
its trap back when the call returns. tests/x64-fidelity `syscallwrite`: 6 of 11 checks passed before, all 11 under
Rosetta 2 (M5 Pro, macOS 27.2).
