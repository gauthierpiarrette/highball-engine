# FEX 2610 patches, at a glance

Applied in order to FEX 2610 by `build-arm64.yml` (`fex_build: source`, the default) for the arm64 line's
`libarm64ecfex.dll` and `libwow64fex.dll`. Kinds are explained in the
[top-level README](../../README.md#patches-at-a-glance). The details, measurements and what each one needs from
Wine are in [`../README.md`](../README.md). None has been sent to FEX upstream yet.

| Patch | What it fixes | Needed by | Kind | Pairs with |
|---|---|---|---|---|
| `0001` floating-point exceptions | Unmasked SSE and x87 floating-point exceptions never reached the program. | general | x86-64 fidelity (off-switch: `FEX_HOSTFEATURES=disablefloatexceptions`) | Wine 0022 |
| `0002` divide errors | Dividing by zero returned a value instead of raising an exception (64-bit programs). | .NET, Mono and Java programs | x86-64 fidelity | |
| `0003` x64 state through ntdll | Exceptions and continues lost the x87 words, DF and the rest of the x64 state. | general | x86-64 fidelity | Wine 0027 |
| `0004` MXCSR inexact flag | The first call into any API set MXCSR's inexact flag. | general | x86-64 fidelity | 0001 |
| `0005` REP MOVS/STOS resume | A fault inside REP MOVS or STOS restarted the copy and corrupted overlapping copies. | general | x86-64 fidelity | |
| `0006` GetX87State export | C++ exceptions and longjmp reset x87 precision (mingw-w64 programs dropped to 53 bits). | mingw-w64 programs | x86-64 fidelity | Wine 0027, 0028 |
| `0007` DAZ in FPCR | Contexts reported MXCSR.DAZ set after the program had cleared it. | general | x86-64 fidelity | |
| `0008` FUCOM and FUCOMI | x87 compares of a quiet NaN raised the invalid exception. | general | x86-64 fidelity | |
| `0009` one MXCSR with the CRT | Settings made with `_controlfp` did not reach x64 code, and unmasked exceptions never trapped. | programs using `_controlfp` | x86-64 fidelity | 0001, Wine 0028 |
| `0010` exact FPREM and FPREM1 | x87 remainders were wrong for large quotients, without quotient or partial-reduction bits. | general | x86-64 fidelity | |
| `0011` MXCSR_MASK | FXSAVE and XSAVE said the MXCSR status flags cannot be written. | general | x86-64 fidelity | |
| `0012` no needless FPCR writes | 0001 made every call from x64 into native code about 5 ns slower. | general | performance | 0001 |
| `0013` self-modifying code race | A thread could keep running old code another thread had just patched. | programs patching live code | x86-64 fidelity | |
| `0014` 32-bit string resume | As 0005, for 32-bit programs. | general | x86-64 fidelity | 0005 |
| `0015` 32-bit MXCSR and x87 status | A 32-bit program masking an SSE exception in its handler trapped again forever. | 32-bit programs | x86-64 fidelity | 0001 |
| `0016` EFLAGS in contexts | Exception contexts showed stray EFLAGS bits. | general | x86-64 fidelity | 0003 |
| `0017` debug registers | Hardware breakpoints set through the debug registers never fired. | debuggers, crash reporters | x86-64 fidelity | 0003, Wine 0029 |
| `0018` TSO in every thread | On macOS every thread but the first ran with Arm's weaker memory ordering. | every multithreaded program | macOS | |
| `0019` store replay | Writing data next to code on the same page threw away that page's translations. | Microsoft Flight Simulator 2024 | performance (opt-in: `HB_FEX_SMC_REPLAY=1`) | the Darwin helper's `AliasWrite` |
| `0020` keep and reinstate translations | Code a copy protection decrypts, runs and encrypts again was translated again every round. | Microsoft Flight Simulator 2024 (its recipe sets it) | performance (opt-in: `HB_FEX_SMC_RETIRE=1`) | 0019 |
| `0021` 0019 and 0020 opt-in | With 0019 and 0020 both on, the game hung 45 s into every flight, so both became opt-in. | Microsoft Flight Simulator 2024 | performance (adds diagnosis switches) | 0019, 0020, Wine 0042 |
| `0022` GuestProtection export | A program saw FEX's write trap as its page protection and locked its own pages. | copy protections, Microsoft Flight Simulator 2024 | x86-64 fidelity | Wine 0043 |
| `0023` code rewriting and lock work | The game stuttered while recompiling code its copy protection rewrites. | Microsoft Flight Simulator 2024 (its recipe sets four switches) | performance (opt-in, seven switches) | 0019 to 0021, the Darwin helper's `ZeroPages` |

`0034` adds the `HbJitWrite` toggle around unaligned atomic backpatching. With `HB_FEX_MAPJIT=1`, that path wrote to the MAP_JIT
code buffer without it and crashed Forza Horizon 6 at `HandleUnalignedAccess`, seen when a controller came into use. With it, about 30 minutes of play with a controller had no crash.

0019 and 0020 were on by default when they were added. 0023 does not build FEX's own Linux helper as it stands (see
its paragraph). With no switch set, 0023 behaves as 0001 to 0022.
