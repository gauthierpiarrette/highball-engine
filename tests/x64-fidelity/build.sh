#!/bin/sh
# Builds the x86-64 fidelity tests: build.sh <llvm-mingw bin dir> <output dir>
# Same compiler everywhere (llvm-mingw 20260922, clang 23.1.2) so the Windows reference runs and the engine
# runs use the same code.
set -eu
BIN=${1:?llvm-mingw bin directory}; OUT=${2:?output directory}
HERE=$(cd "$(dirname "$0")" && pwd)
CC="$BIN/x86_64-w64-mingw32-clang -O2 -Wall -Wno-unused-function -Wno-unused-variable"
mkdir -p "$OUT"
$CC -o "$OUT/ctxtest.exe"   "$HERE/ctxtest.c"   "$HERE/ctx_asm.S"
$CC -o "$OUT/cpuidtest.exe" "$HERE/cpuidtest.c" "$HERE/cpu_asm.S"
$CC -o "$OUT/x87test.exe"   "$HERE/x87test.c"
$CC -o "$OUT/fibertest.exe" "$HERE/fibertest.c" "$HERE/fiber_asm.S"
$CC -o "$OUT/memtest.exe"   "$HERE/memtest.c"   "$HERE/mem_asm.S"
$CC -o "$OUT/tsctest.exe"   "$HERE/tsctest.c"
$CC -o "$OUT/syscalltest.exe" "$HERE/syscalltest.c" "$HERE/syscall_asm.S"
$CC -o "$OUT/kattest.exe"   "$HERE/kattest.c"
$CC -o "$OUT/drtest.exe"    "$HERE/drtest.c"
$CC -o "$OUT/drdbg.exe"     "$HERE/drdbg.c"
$CC -o "$OUT/ordertest.exe" "$HERE/ordertest.c"
$CC -o "$OUT/smcdata.exe"   "$HERE/smcdata.c"
$CC -o "$OUT/smcretire.exe" "$HERE/smcretire.c"
$CC -o "$OUT/syscallwrite.exe" "$HERE/syscallwrite.c"
$CC -o "$OUT/globalroot.exe" "$HERE/globalroot.c" -lpsapi
$CC -o "$OUT/topdown.exe" "$HERE/topdown.c"
$CC -o "$OUT/regiontest.exe" "$HERE/regiontest.c"
$CC -o "$OUT/cowtest.exe" "$HERE/cowtest.c"
$CC -shared -o "$OUT/imagequery-helper.dll" "$HERE/imagequery_dll.c" "$HERE/imagequery_asm.S"
$CC -o "$OUT/imagequery.exe" "$HERE/imagequery.c"
$CC -o "$OUT/imagemap.exe" "$HERE/imagemap.c"
# x87test also as a 32-bit program: the x87 matters most to 32-bit games, which run through WoW64
"$BIN/i686-w64-mingw32-clang" -O2 -Wall -Wno-unused-function -Wno-unused-variable -o "$OUT/x87test32.exe" "$HERE/x87test.c"
"$BIN/i686-w64-mingw32-clang" -O2 -Wall -Wno-unused-function -Wno-unused-variable -o "$OUT/drtest32.exe" "$HERE/drtest.c"
"$BIN/i686-w64-mingw32-clang" -O2 -Wall -Wno-unused-function -Wno-unused-variable -o "$OUT/drdbg32.exe" "$HERE/drdbg.c"
"$BIN/i686-w64-mingw32-clang" -O2 -Wall -Wno-unused-function -Wno-unused-variable -o "$OUT/ordertest32.exe" "$HERE/ordertest.c"
ls -l "$OUT"
