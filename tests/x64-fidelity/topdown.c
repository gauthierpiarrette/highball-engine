/* MEM_TOP_DOWN must prefer a known available high address over low reserves.
 * VirtualAlloc2 establishes the high candidate without assuming an OS layout. */
#include "fid.h"

typedef void *(WINAPI *alloc2_fn)(HANDLE, void *, SIZE_T, ULONG, ULONG,
                                MEM_EXTENDED_PARAMETER *, ULONG);

int main(void)
{
    alloc2_fn alloc2 = (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "VirtualAlloc2");
    MEM_ADDRESS_REQUIREMENTS bounds = {0};
    MEM_EXTENDED_PARAMETER parameter = {0};
    const SIZE_T size = 512 * 1024 * 1024;
    void *candidate, *allocation;
    uintptr_t high;
    fid_name = "topdown";
    fid_environment();
    fid_check(alloc2 != NULL, "api.virtualalloc2", "VirtualAlloc2 unavailable");
    if (!alloc2) return fid_summary();
    parameter.Type = MemExtendedParameterAddressRequirements;
    parameter.Pointer = &bounds;
#ifdef _WIN64
    bounds.LowestStartingAddress = (void *)(uintptr_t)0x200000000ull;
    candidate = alloc2(GetCurrentProcess(), NULL, size, MEM_RESERVE | MEM_TOP_DOWN,
                       PAGE_READWRITE, &parameter, 1);
    fid_check(candidate != NULL, "high.reserve", "error=%lu", GetLastError());
    if (candidate)
    {
        high = (uintptr_t)candidate;
        VirtualFree(candidate, 0, MEM_RELEASE);
        allocation = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE);
        fid_info("high.addresses", "known available=%p selected=%p", (void *)high, allocation);
        fid_check(allocation && (uintptr_t)allocation >= high, "high.preferred",
                  "selected=%p below known available=%p", allocation, (void *)high);
        if (allocation)
        {
            void *page = VirtualAlloc(allocation, 4096, MEM_COMMIT, PAGE_READWRITE);
            fid_check(page == allocation, "high.commit", "error=%lu", GetLastError());
            if (page)
            {
                *(volatile ULONG *)page = 0x1234abcd;
                fid_check(*(volatile ULONG *)page == 0x1234abcd, "high.read-write", "round trip");
            }
            fid_check(VirtualFree(allocation, 0, MEM_RELEASE), "high.release", "error=%lu", GetLastError());
        }
    }
#endif
    bounds.LowestStartingAddress = NULL;
    bounds.HighestEndingAddress = (void *)(uintptr_t)0x7ffeffff;
    bounds.Alignment = 0x10000;
    allocation = alloc2(GetCurrentProcess(), NULL, 0x10000, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN,
                        PAGE_READWRITE, &parameter, 1);
    fid_check(allocation && (uintptr_t)allocation + 0xffff <= (uintptr_t)bounds.HighestEndingAddress,
              "bounded.low", "address=%p error=%lu", allocation, GetLastError());
    if (allocation)
    {
        fid_check(!((uintptr_t)allocation & 0xffff), "bounded.alignment", "address=%p", allocation);
        VirtualFree(allocation, 0, MEM_RELEASE);
    }
    return fid_summary();
}
