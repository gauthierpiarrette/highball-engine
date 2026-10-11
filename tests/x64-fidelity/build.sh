#!/bin/sh
# Builds the six x86-64 fidelity tests: build.sh <llvm-mingw bin dir> <output dir>
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
ls -l "$OUT"
