/* VirtualQuery must preserve guest region sizes after generated code executes.
 * Covers artificial SMC boundaries, genuine RX protection and decommit boundaries. */
#include "fid.h"

int main(void)
{
    const SIZE_T size = 32 * 4096;
    BYTE *p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    fid_name = "regiontest";
    fid_environment();
    if (!p)
    {
        fid_check(0, "setup.allocate", "error %lu", GetLastError());
        return fid_summary();
    }
    for (unsigned phase = 0; phase < 5; ++phase)
    {
        DWORD old;
        if (phase == 3 && !VirtualProtect(p + 13 * 4096, 4096, PAGE_EXECUTE_READ, &old))
        { fid_check(0, "setup.protect", "error %lu", GetLastError()); break; }
        if (phase == 4 && !VirtualFree(p + 17 * 4096, 4096, MEM_DECOMMIT))
        { fid_check(0, "setup.decommit", "error %lu", GetLastError()); break; }
        if (phase)
        {
            BYTE *code = p + (phase == 1 ? 12 : 24) * 4096;
            const BYTE body[] = {0xb8, 0x2a, 0, 0, 0, 0xc3};
            memcpy(code, body, sizeof(body));
            if (!FlushInstructionCache(GetCurrentProcess(), code, sizeof(body)))
            { fid_check(0, "setup.flush", "error %lu", GetLastError()); break; }
            if (((int (*)(void))code)() != 42)
            { fid_check(0, "setup.execute", "incorrect generated-code result"); break; }
        }
        for (unsigned page = 0; page < 32; ++page)
        {
            MEMORY_BASIC_INFORMATION mbi = {0};
            char id[64];
            BYTE *address = p + page * 4096;
            unsigned end = 32;
            DWORD protection = PAGE_EXECUTE_READWRITE, state = MEM_COMMIT;
            if (phase >= 3)
            {
                if (page < 13) end = 13;
                else if (page == 13) { end = 14; protection = PAGE_EXECUTE_READ; }
                else if (phase >= 4)
                {
                    if (page < 17) end = 17;
                    else if (page == 17) { end = 18; protection = 0; state = MEM_RESERVE; }
                }
            }
            int ok = VirtualQuery(address, &mbi, sizeof(mbi)) == sizeof(mbi) &&
                mbi.BaseAddress == address && mbi.AllocationBase == p &&
                mbi.RegionSize == (end - page) * 4096 && mbi.State == state &&
                mbi.Protect == protection && mbi.Type == MEM_PRIVATE &&
                mbi.AllocationProtect == PAGE_EXECUTE_READWRITE;
            sprintf(id, "region.phase%u.page%u", phase, page);
            fid_check(ok, id, "base %p alloc %p size %#llx state %#lx protect %#lx type %#lx; want size %#x state %#lx protect %#lx",
                      mbi.BaseAddress, mbi.AllocationBase, (unsigned long long)mbi.RegionSize,
                      mbi.State, mbi.Protect, mbi.Type, (end - page) * 4096, state, protection);
        }
    }
    VirtualFree(p, 0, MEM_RELEASE);
    return fid_summary();
}
