# x86-64 fidelity tests

Standalone Windows x64 programs (four of them also built as 32-bit ones) that check what an x86-64 program can observe about the CPU and the
Windows exception, memory and file path models. They are the regression tests for the arm64 engine line (Wine ARM64EC +
FEX), and run unchanged on real x86-64 Windows, which is the reference: a check that fails on real Windows is
a wrong check, not a finding.

| program | area |
|---|---|
| `globalroot` | reopening files and mapped images through GLOBALROOT aliases; attributes, mixed-case and nested aliases, DOS namespace aliases, and invalid or missing paths |
| `topdown` | MEM_TOP_DOWN prefers a known available high address, high-address commit/read/write/release, and low address bounds and alignment |
| `imagequery` | page-aligned memory regions and protection for a writable executable section in a DLL loaded after startup, including execution and rewriting |
| `imagemap` | execute a companion DLL's section mapped directly with SEC_IMAGE, compare reported protections, and check execution after an explicit VirtualProtect |
| `cowtest` | executable copy-on-write mapping protection before writes, RX region boundaries, code rewrites, and isolation from shared views and the backing file |
| `regiontest` | VirtualQuery region size and protection across a generated-code allocation before and after execution, a real RX boundary, and a decommitted page |
| `ctxtest` | exceptions and CONTEXT: the record and context a vectored handler receives for int3, ud2, divide errors, access violations, DEP, privileged instructions and single step; continuing with a modified context (GPRs, EFlags incl. PF/AF/DF, MXCSR, x87 control word and stack, XMM, YMM); DF in handlers; nested and repeated exceptions; SEH unwinding of nonvolatile registers; RtlCaptureContext and RtlRestoreContext; Get/SetThreadContext on a suspended thread and, one class at a time, on the current thread; RF in fault contexts and reserved EFLAGS bits; a thread checking its own x87/MXCSR/DF/YMM state while another thread suspends and resumes it |
| `cpuidtest` | CPUID, XGETBV and XSAVE consistency: advertised extensions execute and have their XCR0 state, leaf 0xD sizes, XSAVE/XSAVEOPT/XSAVEC/XRSTOR/FXSAVE/FXRSTOR layout and round trips, XRSTOR init semantics, GetEnabledXStateFeatures and IsProcessorFeaturePresent against CPUID, AVX state under preemption and suspension |
| `x87test` | FXAM on every class, full and abridged tag words, TOP, FNSAVE/FRSTOR/FNSTENV/FLDENV, precision and rounding control, masked exception flags and stack faults, FPREM/FPREM1 quotient bits, FCOM/FUCOM/FCOMI, FIST range, MMX aliasing, MXCSR (round trip, sticky flags, DAZ, FTZ, RC) |
| `fibertest` | nonvolatile registers (rbx rbp rsi rdi r12-r15, xmm6-15), stack canaries, FLS and GetCurrentFiber across fiber switches, a fiber migrating between threads, exceptions between switches; MXCSR/x87 control word per fiber recorded |
| `memtest` | VirtualQuery/VirtualProtect/VirtualFree at 4 KB granularity, every protection, PAGE_GUARD, page-split accesses, generated and self-modifying code (documented flush path, RWX without a flush, inline, WriteProcessMemory, another thread), write watch, stack growth |
| `kattest` | known-answer tests for CRC32 (SSE4.2), AES-NI, PCLMULQDQ, SHA-NI (full SHA-1 and SHA-256 blocks), VAES and VPCLMULQDQ against portable C, for every one of them CPUID advertises |
| `tsctest` | fenced RDTSC monotonic, happens-before across threads and while migrating, RDTSCP, CPUID's TSC frequency against the measured rate |
| `syscalltest` | direct SYSCALL return address in RCX, flags in R11 and restored RFLAGS, across nine arithmetic/direction flag patterns, success, invalid handle and invalid service; service numbers read from this runtime's own stubs |
| `drtest` | the hardware debug registers DR0-DR3, DR6 and DR7 through Get/SetThreadContext, exception contexts and NtContinue: what reads back (64-bit and 32-bit programs differ), instruction breakpoints (a fault before the instruction, Dr6 B bits, RF on resume and after a fault, per thread, set by handle on a thread created suspended and on one already running), data breakpoints (write and read/write, every length, range edges, unaligned addresses, rep stosb and repe cmpsb traps with Rcx left, another thread's and the kernel's writes); also built as `drtest32.exe` |
| `drdbg` | the debug registers of a thread in another process, as a debugger or a trainer reaches them: set at a debug event (one raised by native code, OutputDebugString, and an x64 exception), read back, Dr6 at each breakpoint event, continuing with RF and DBG_CONTINUE; set on a thread suspended while it runs x64 code and on one blocked in a wait, read back before and after the thread ran (DR7 bit 10 only shows once it ran), a second breakpoint added to the ones read back, cleared; set on a process created suspended before its first instruction; also built as `drdbg32.exe` |
| `ordertest` | x86 memory ordering in every thread: message passing between a writer and a reader thread created for each pair (data then flag, flag then data), which TSO never reorders and Arm's weak ordering does unless the emulator orders that thread; arguments `[pairs] [rounds]`, defaults 4 and 2,000,000; also built as `ordertest32.exe` |
| `syscallwrite` | system calls (ReadProcessMemory, ReadFile, NtQuerySystemInformation for the SMBIOS firmware table) that write into a read-write-execute page whose code already ran, before the program writes there itself; the code still runs afterwards and a rewrite without a flush takes effect |

Output: one line per check, `PASS <id>`, `FAIL <id>: <detail>` or `INFO <id>: <value>`, then
`SUMMARY <test> pass=N fail=M`; the exit code is the number of failures.

`memtest` also executes a newly mapped RX view and rewrites its code through a separate RW view, flushing the
instruction cache before each call. This covers ARM64EC mapping notifications as well as explicit invalidation.
For FEX validation regressions, repeat it with `FEX_SMCCHECKS=full FEX_DISKCACHE=0`; normal mode alone does not
exercise empty-entry CRC validation. Full mode is a slow diagnostic and is not a game compatibility setting.

Build with llvm-mingw 20260922: `./build.sh <llvm-mingw>/bin out`. x87test is also built as a 32-bit
program (`x87test32.exe`), which Windows x64 runs through WoW64, as the engine does with FEX's WoW64 DLL. The workflow `x64-fidelity.yml` builds
them once and runs the same files on Windows Server 2022 and 2025 (x86-64) and on Windows 11 ARM64 (Microsoft's
x64 emulation, for comparison).

`cowtest` reproduces a FEX protection-tracking hang on an executable FILE_MAP_COPY view. It uses a temporary
file and generated code only. On the M4 candidate runtime it passes 22/0 in normal/full modes; the x64 Wine
control also gives 22/0. The new native Windows reference has not been run, so those comparisons remain pending.

`imagequery` loads the companion `imagequery-helper.dll` built by the same script. Its six-byte executable section
must occupy a full Windows memory page. The test gives 4/2 before FEX 0030 and 6/0 afterward in normal/full modes;
the x64 Wine control gives 6/0. The workflow includes the helper DLL in the artifact. Native Windows reference pending.

`imagemap` uses the same helper but maps it with CreateFileMapping / MapViewOfFile and SEC_IMAGE, without
LoadLibrary or DLL initialization. It checks that an executable section can be called immediately. Wine 0049
changes the M4 result from 3/1 to 4/0 in normal/full modes; the x64 Wine control gives 4/0. The executable access
fault is caught so the failing baseline completes normally with an honest nonzero result. Native Windows reference pending.

`memtest` also verifies that a write to PAGE_EXECUTE_READWRITE | PAGE_GUARD delivers the one-shot guard exception
before the underlying W^X handling. On ARM64 macOS the Wine 0048 fix changes the expanded suite from 51/2 to 53/0
in normal/full modes, without depending on the private FEX guard-execution prototype. Native Windows reference pending.

`syscalltest` reads the runtime's own service numbers and checks RCX, R11, RFLAGS and five live stack values
after direct SYSCALL, including an invalid service. Wine 0050 changes 173/72 to 245/0, matching x64 Wine.
The optional `int2e` argument is a separate Wine/FEX runtime check (164/54 before, 218/0 afterward): it leaves
R11=0x100 to ensure the shared helper does not interpret that register as saved flags. The `rsp8` argument
tests the other guest stack alignment; it can be combined with `int2e`. Both corrected entry modes pass
normal/full validation with either alignment. The workflow runs the default SYSCALL mode. Native Windows
reference remains pending; these results do not establish game compatibility.
