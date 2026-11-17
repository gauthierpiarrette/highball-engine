# Darwin VM reader regression test

This fixture exercises the production r3 reader with real Windows memory APIs.
It needs an x86-64 Wine build on an Apple Silicon Mac and an isolated test prefix.
Do not run Steam in that prefix.

1. Prepare Wine with `.github/scripts/prepare-wine.sh`. Copy the prepared tree and out-of-tree
   build to a disposable directory. Keep the source/build relative layout.
2. Run `python3 tests/instrument-vm-reader.py /disposable/sources/wine` from this
   proposal directory. Rebuild and ad-hoc sign `dlls/ntdll/ntdll.so` with the same
   configure options as the release. Install it only into the isolated test engine.
3. Build `x86_64-w64-mingw32-gcc -O2 tests/test_vm_reader.c -o RelicCardinal.exe`.
   The executable name deliberately exercises the production process-name gate.
4. Set `HB_AOE4=1` and `HIGHBALL_AOE4_VM_READER_TEST=1` in the isolated test bottle and run the
   fixture with arguments `r3`, `r2`, `nofault`, `stress`, and `bench` separately.
   Each must exit zero. The `r3` case explicitly checks that tracking was enabled.
   In Highball, use `highball run TestBottle /path/to/RelicCardinal.exe -- r3`.
5. Discard the instrumented engine. Package only a separately built, uninstrumented
   module. Confirm its symbols/strings contain no `aoe_test_fragment` or
   `HIGHBALL_AOE4_VM_READER_TEST`.

The test-only bridge intercepts a reserved context flag in `NtSetContextThread`.
It is not part of the release patch series. It exposes both the old locked reader
and the new reader, plus the exact no-fault copy used by the fast path. The fixture
checks guard preservation, stale/freed/no-access mappings, rewritten bytes, pointer
changes, concurrent protection changes, and same-address remapping/decommit.
`bench` alternates one ring read and one ordinary resume per iteration; reported
nanoseconds are **per pair**, not per individual read. Timing is diagnostic, not
a pass/fail assertion.
