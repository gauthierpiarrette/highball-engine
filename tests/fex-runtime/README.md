# FEX runtime checks

`dispatch-protection.c` runs as an x64 program inside the ARM64EC Wine/FEX runtime. It checks that the three
CHPE dispatch slots patched during FEX initialization retain their PE section's write permission. It reads
metadata and calls VirtualQuery; it does not write the runtime's memory. This is an internal runtime test,
separate from the native-Windows fidelity suite, because native x64 Windows does not load FEX.

```sh
x86_64-w64-mingw32-clang -O2 -Wall -Wextra dispatch-protection.c -o dispatch-protection.exe
WINEPREFIX=/path/to/test-prefix /path/to/arm64-engine/bin/wine dispatch-protection.exe
```

M4/macOS 27.0, Wine 11.18 r6 + Wine 0044–0046 and FEX 0024–0026: **0 pass / 3 fail**,
all three slots remain PAGE_WRITECOPY although the PE section is read-only. With FEX 0027: **3/0**,
all three report PAGE_READONLY. The compiler reports no warnings. Memory 51/0, context 546/0, fibers 86/0
and invalid-instruction context 108/0 remain unchanged. This probe does not establish game compatibility.

`wow64-smc.c` is a 32-bit companion for the WoW64 component. It recovers from executing a no-access page,
then repeatedly rewrites RX code with WriteProcessMemory and checks the executed results.

```sh
i686-w64-mingw32-clang -O2 -Wall -Wextra wow64-smc.c -o wow64-smc.exe
FEX_SMCCHECKS=full FEX_DISKCACHE=0 WINEPREFIX=/path/to/test-prefix /path/to/arm64-engine/bin/wine wow64-smc.exe
```

On the same M4, stock r6 gives **2/0 in normal mode** but times out after 25 seconds in full validation.
The rebuilt WoW64 component with the shared fixes through 0028 gives **2/0 in both modes**. The existing
32-bit suites retain x87 121/7, debug registers 71/0, debugger 25/0 and memory ordering 4/0. The timeout
is a failed run even though terminating its prefix made Wine return exit 0. No native Windows reference
or game compatibility is established by this probe.

`callchecker-bounds.c` checks the actual ARM64EC `CheckCall` routine. It reads the unstripped FEX module's
COFF symbols, then calls a small native helper that temporarily supplies a controlled table in this test
process and restores the runtime variables before returning to x64. The backing array includes sentinel
entries beyond the logical table, so incorrect lookups produce a measurable wrong address without an
out-of-allocation read. Run it only as its own process in a disposable test prefix. A stripped module is
reported as an unavailable test, not a pass. The program does not inspect or alter another process.

```sh
arm64ec-w64-mingw32-clang -O2 -Wall -Wextra -shared callchecker-native.c callchecker-native.S -o callchecker-native.dll
x86_64-w64-mingw32-clang -O2 -Wall -Wextra callchecker-bounds.c -o callchecker-bounds.exe
FEX_DISKCACHE=0 WINEPREFIX=/path/to/test-prefix /path/to/arm64-engine/bin/wine callchecker-bounds.exe 'Z:\absolute\path\callchecker-native.dll'
```

M4/macOS 27.0, Wine 11.18 r6 plus the memory fixes through Wine0049/FEX0030: **2/2**. The endpoint lookup
and a lookup with nonzero bytes adjacent to the 32-bit size both return the sentinel's redirection.
With FEX0031: **4/0 in normal and full validation**, including a valid lookup and restoration of the
original runtime variables. Both compilers report no warnings. Context 546/0, memory 53/0 and fibers
86/0 remain passing in both modes. This ARM64EC-only check does not establish FH6 gameplay support.

`native-stack-args.c`, `native-stack-call.S` and `native-stack-target.c` check six integer arguments and
the result of an x64 call into a native helper. One caller has the normal stack alignment; the other
deliberately leaves RSP eight-byte aligned before CALL. The helper only records arguments, uses no aligned
vector stack accesses and does not throw. The wrappers restore RSP and do not change nonvolatile registers.

```sh
arm64ec-w64-mingw32-clang -O2 -Wall -Wextra -shared native-stack-target.c -o native-stack-target.dll
x86_64-w64-mingw32-clang -O2 -Wall -Wextra native-stack-args.c native-stack-call.S -o native-stack-args.exe
FEX_DISKCACHE=0 WINEPREFIX=/path/to/test-prefix /path/to/arm64-engine/bin/wine native-stack-args.exe 'Z:\absolute\path\native-stack-target.dll'
```

M4/macOS 27.0, Wine 11.18 through Wine0049/FEX0031: **11/3 in normal/full validation**. Aligned calls pass;
misaligned arguments five/six and the resulting return value fail. With FEX0032: **14/0 in both modes**.
Building the helper as AMD64 and running the same executable on x64 Wine gives **14/0**. Both compilers
report no warnings. Context 546/0, memory 53/0, fibers 86/0, invalid-instruction context 108/0, expanded
legacy context 1068/0 and call-checker 4/0 remain passing with the production PE ntdll restored. Native
Windows references remain pending. These results do not establish FH6 gameplay support.
