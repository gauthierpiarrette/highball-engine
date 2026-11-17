# Windows VM reader regression tests

Use a disposable copy of the patched Wine source/build and an isolated prefix.
Never put the instrumented ntdll in a gameplay or release engine.

1. Run `python3 aoe4/tests/instrument-vm-reader.py /disposable/patched/wine`.
2. Rebuild/sign `dlls/ntdll/ntdll.so` and copy it into the isolated engine.
3. Compile `x86_64-w64-mingw32-gcc -O2 aoe4/tests/test_vm_reader.c -o RelicCardinal.exe`.
4. Set `HIGHBALL_AOE4_VM_READER_TEST=1`. Run with `HB_AOE4=1` and arguments
   `read`, `nofault`, and `stress`. All must exit zero. Without `HB_AOE4`, run
   `scope-off` to confirm VM tracking stays disabled.
5. Discard the instrumented engine. A production module must contain neither
   `aoe_test_fragment` nor `HIGHBALL_AOE4_VM_READER_TEST`.

The fixture checks live/rewritten bytes, moved/freed/no-access mappings, guarded
ring and pointer pages, no-fault copies, concurrent protection changes, and
same-address release/reallocate/decommit cycles. Its reserved context-flag
bridge is a test-only addition, outside the release patch series.
