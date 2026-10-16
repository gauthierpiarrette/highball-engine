# FEX for the arm64 line

`fexunixlib_darwin.cpp` is the Unix helper FEX's Windows DLLs load from the ntdll.so directory: the
same call table as FEX's Linux helper, with BSD calls, and TSO through `thread_set_x86_64_compat`.

## Patch series for a FEX 2610 build (`patches/`)

`build-arm64` builds `libarm64ecfex.dll` and `libwow64fex.dll` from FEX 2610 (`inputs-arm64.json`,
`fex-source`) with this series applied, for x86-64 programs that see what real x86-64 Windows shows
them. That is the default since 2026-10-11. Dispatched with `fex_build: hangover`, it uses Hangover
11.16's FEX 2608 DLLs instead, which have none of this. Each patch was measured with
`tests/x64-fidelity`, whose expectations hold on real x86-64 Windows, on an M1 Pro (2026-10-10 and
2026-10-11). The Wine half is `patches-arm64/0022` to `0029`.

- `0001` floating-point exceptions: unmasked SSE and x87 exceptions trap (FPCR trap enables armed
  from MXCSR in JIT code only), MXCSR status flags live in FPSR as on ARM64EC Windows, x87 status
  flags from SoftFloat, x87 #MF before the next waiting instruction.
- `0002` integer divide errors: DIV and IDIV by zero raise STATUS_INTEGER_DIVIDE_BY_ZERO and a
  quotient overflow STATUS_INTEGER_OVERFLOW at the instruction (.NET, Mono and Java rely on them).
- `0003` the x64 state an ARM64 context has no room for (x87 words, EFLAGS, DAZ, YMM upper halves)
  travels through ntdll; exception handlers run with DF and TF clear.
- `0004` FEX's own C++ code (compiler, linker) no longer leaves the inexact flag in the guest's
  MXCSR: before, the first call of every API set MXCSR.PE.
- `0005` a REP MOVS or STOS interrupted by a fault resumes where it stopped, not from the start,
  which corrupted overlapping copies (memmove) on commit-on-demand memory.
- `0006` `GetX87State` for ntdll's `RtlCaptureContext`, `setjmp` and `GetThreadContext`.
- `0007` MXCSR.DAZ kept in FPCR bit 19, where the ARM64EC context conversion reads it.
- `0008` FUCOM and FUCOMI raise invalid only for a signalling NaN.
- `0009` one MXCSR for x64 code and the native CRT: rounding and denormal control set by
  `_controlfp` show in MXCSR, masks are the emulator's (`GetX64Mxcsr`, `SetX64Mxcsr`).
- `0010` exact FPREM and FPREM1 with the quotient bits in C0, C3 and C1 and partial reduction in C2
  (2^100 mod 3 gave 0 before; real x86 confirms every case of the test).
- `0011` MXCSR_MASK 0xFFFF in FXSAVE and XSAVE.
- `0012` no FPCR write when nothing changes (writing it costs about 4 ns on Apple cores): calls
  from x64 code into native code cost what they cost with FEX 2610 unpatched.
- `0013` self-modifying code across threads: a block whose guest code changed before its page was
  protected is compiled again. A thread kept running code another thread had patched in about 1 of
  300 patches, for good.
- `0014` the same string-instruction resume as `0005` for 32-bit programs (the WoW64 module).
- `0015` MXCSR and the x87 status word through the 32-bit context. Without it `0001` hangs a 32-bit
  program that unmasks an SSE exception and masks it again in its handler (the trap re-armed from a
  copy the context never reached). 32-bit SSE exceptions arrive as STATUS_FLOAT_MULTIPLE_TRAPS, as
  32-bit Windows reports them.

- `0016` EFLAGS in contexts: only the architectural bits. The NZCV storage bytes leaked into bits
  24-31 once `0003` passed all of EFLAGS (an int3 context showed 0xa0000e97 where Windows shows
  0xe97).
- `0017` the x86 debug registers DR0-DR3, DR6 and DR7, per thread. Instruction breakpoints end a
  block before the instruction and fault there (#DB, RIP at the instruction); RF from the context
  the thread resumes with lets it run once, and fault contexts report RF set, as Windows does. Data
  breakpoints are checked on every guest memory access; a thread with one runs one instruction per
  block so the trap comes right after the access, and REP string instructions run one element per
  execution (RIP at the instruction, RCX the count left). A thread with breakpoints compiles into a
  code buffer of its own. No cost without breakpoints; about three times slower while a data
  breakpoint is set. The registers cross ntdll in contexts, like the rest of the x64 state: the
  ARM64EC module takes them from a context it is entered with and reports them in the contexts it
  builds, and exports `KeepX64DebugRegisters`, what x86-64 Windows keeps of registers a context sets
  (ntdll keeps a thread's registers while it is outside the emulator, `patches-arm64/0029`); the
  WoW64 module takes them from the 32-bit context. Registers read back as Windows reports them for a
  64-bit and for a 32-bit program, which differ (drtest), from this thread or from another process
  (drdbg).

- `0018` hardware TSO in every thread on macOS. TSOHandlerConfig turned Apple's TSO mode on once, in
  ProcessInit, and then told the JIT the hardware orders memory, so the JIT emits no ordering of its own.
  `thread_set_x86_64_compat` changes only the calling thread on macOS (Linux's prctl carries over to new
  threads), so every thread but the first ran x86-64 code with Arm's weak ordering, in every program on the
  arm64 line. ThreadInit (ARM64EC) and BTCpuThreadInit (WoW64) now turn it on for each thread before it runs
  guest code. Measured on the M4 (2026-10-12) with the message-passing test that became
  `tests/x64-fidelity/ordertest`, 6 pairs of 20,000,000 rounds: 2,938 to 2,841,851 reorderings per pair
  before, none after, 27 s instead of 21. `ordertest` at its defaults on the CI engine (M4, 2026-10-12):
  without 0018 all four pairs fail in 64-bit (356,151 to 1,227,594 reorderings) and in 32-bit through
  WoW64 (427,248 to 3,036,629); with it all pass in both. Found while Microsoft Flight Simulator 2024
  crashed in a game thread during loading.

- `0019` SMC quarantine. An SMC write fault on a page erased every block compiled from that page and
  the next execution recompiled them all; Microsoft Flight Simulator 2024 writes data that shares
  pages with hot code, and per-thread counters (M4, 2026-10-16) showed the instruction bytes unchanged
  at almost every re-protection (idle 1,012 unchanged against 3 changed, loading 5,136 against 0),
  90 to 98 percent of compiles being recompiles, and the flight's present-time spikes matching the
  JIT bursts. The fault side now sets the page's blocks aside in a fixed table (nothing allocates in
  the fault handler: two allocating diagnostic builds froze the game through Wine's FLS critical
  section) with a copy of the page and a mask of the decoded instruction bytes; the next compile on
  the page protects it and compares the masked bytes: unchanged puts the blocks back, changed frees
  them. Blocks spanning other pages are erased as before. One flight each on the M4: 27.4 fps parked
  with p99 57 ms against 25.7 and 118 ms, 15 spikes against 50; later flights inside the run-to-run
  variance (GPU hangs set the spikes). Six counters join FEX_PROFILESTATS' per-thread statistics.

32-bit programs were measured with the engine built by CI (it has the i386 half): x87test32 120/128,
the same eight x87 gaps as the 64-bit build; the string resume and the code patching probe pass.
