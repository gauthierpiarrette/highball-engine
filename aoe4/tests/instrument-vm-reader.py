#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Add a test-only syscall bridge to a DISPOSABLE copy of prepared r3 Wine."""
from pathlib import Path
import sys
if len(sys.argv) != 2:
    sys.exit("usage: instrument-vm-reader.py /disposable/patched/wine")
u = Path(sys.argv[1]) / "dlls/ntdll/unix"
v = (u / "virtual.c").read_text()
s = (u / "signal_x86_64.c").read_text()
if "aoe_test_fragment" in v or "HIGHBALL_AOE4_VM_READER_TEST" in s:
    sys.exit("Already instrumented")
marker = "#ifdef __APPLE__\n/* Each cached range"
assert marker in v
# This bridge cannot be compiled into the packaged module: release preparation
# applies only proposals/aoe4/patches/*.patch, never this script.
bridge = r'''#ifdef __APPLE__
SIZE_T aoe_test_fragment( const void *pointer, UINT_PTR pc, BYTE *bytes, UINT_PTR mode )
{
    UINT_PTR ring = 0;
    if (mode == 3) return aoe_vm_tracking ? 32 : 0;
    if (mode == 2) return aoe_nofault_copy( bytes, (const void *)pc, 32 ) ? 32 : 0;
    if (mode) return virtual_read_aoe_fragment( pointer, pc, &ring, bytes );
    return read_aoe_fragment_locked( pointer, pc, &ring, bytes );
}
#endif
'''
v = v.replace(marker, bridge + marker, 1)
marker = "NTSTATUS WINAPI NtSetContextThread( HANDLE handle, const CONTEXT *context )\n{"
assert marker in s
hook = r'''
#ifdef __APPLE__
    if (handle == GetCurrentThread() && context->ContextFlags == 0xa0e40000 &&
        getenv( "HIGHBALL_AOE4_VM_READER_TEST" ))
    {
        extern SIZE_T aoe_test_fragment( const void *, UINT_PTR, BYTE *, UINT_PTR );
        BYTE bytes[32] = {0};
        SIZE_T length = aoe_test_fragment( (const void *)context->Rdi, context->Rip, bytes, context->R8 );
        if (length && context->Rsi) memcpy( (void *)context->Rsi, bytes, length );
        return length;
    }
#endif
'''
s = s.replace(marker, marker + hook, 1)
(u / "virtual.c").write_text(v)
(u / "signal_x86_64.c").write_text(s)
print("TEST ONLY: instrumented VM reader; do not distribute this ntdll")
