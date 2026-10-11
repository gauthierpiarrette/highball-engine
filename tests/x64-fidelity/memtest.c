/* memtest: Windows virtual memory semantics as an x86-64 program sees them, at 4 KB granularity.
 *
 *   query      VirtualQuery of a 64 KB reservation with pages in every state and protection
 *   access     read/write/execute outcome on every protection, incl. PAGE_GUARD (one-shot) and decommit
 *   split      an 8-byte access across a page boundary: fault address, partial store
 *   protect    VirtualProtect transitions and the old protection it returns
 *   code       generated code: documented RW -> RX + FlushInstructionCache path, RWX without a flush,
 *              inline self-modification, WriteProcessMemory, another thread modifying running code
 *   watch      MEM_WRITE_WATCH / GetWriteWatch granularity
 *   stack      stack growth through the guard page down 1.5 MB
 *   layout     page size, allocation granularity, addresses
 * Build: x86_64-w64-mingw32-clang -O2 memtest.c mem_asm.S -o memtest.exe */
#include "fid.h"

extern char mprobe_landing[], inline_smc_start[], inline_smc_end[];
extern int64_t mprobe_read1(const void *), mprobe_read8(const void *), mprobe_write1(void *, uint64_t),
    mprobe_write8(void *, uint64_t), mprobe_exec(const void *);

static volatile int g_probing;
static volatile DWORD g_code;
static volatile ULONG_PTR g_info0, g_info1;
static volatile void *g_addr;

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    if (!g_probing) return EXCEPTION_CONTINUE_SEARCH;
    g_code = ep->ExceptionRecord->ExceptionCode;
    g_addr = ep->ExceptionRecord->ExceptionAddress;
    g_info0 = ep->ExceptionRecord->NumberParameters > 0 ? ep->ExceptionRecord->ExceptionInformation[0] : ~0ull;
    g_info1 = ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : ~0ull;
    ep->ContextRecord->Rip = (DWORD64)mprobe_landing;
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* outcome of one access: 0 ok, else the exception code; info in g_info0/1 */
static DWORD probe(int kind, void *p)
{
    int64_t r;
    g_code = 0;
    g_probing = 1;
    switch (kind)
    {
    case 'r': r = mprobe_read1(p); break;
    case 'w': r = mprobe_write1(p, 0x5a); break;
    default:  r = mprobe_exec(p); break;
    }
    g_probing = 0;
    (void)r;
    return g_code;
}

#define PG 0x1000

/* ---------------------------------------------------------------- query */

static void check_query(const char *id, void *base, void *addr, DWORD state, DWORD protect, SIZE_T region)
{
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T n = VirtualQuery(addr, &mbi, sizeof(mbi));
    int ok = n == sizeof(mbi) && mbi.BaseAddress == addr && mbi.AllocationBase == base && mbi.State == state &&
             (state == MEM_RESERVE || mbi.Protect == protect) && mbi.RegionSize == region && mbi.Type == MEM_PRIVATE &&
             mbi.AllocationProtect == PAGE_NOACCESS;
    fid_check(ok, id, "base %p alloc %p state %#lx protect %#lx region %#zx type %#lx allocprot %#lx; want base %p state %#lx protect %#lx region %#zx",
              mbi.BaseAddress, mbi.AllocationBase, mbi.State, mbi.Protect, mbi.RegionSize, mbi.Type, mbi.AllocationProtect,
              addr, state, protect, region);
}

static void group_query(void)
{
    uint8_t *b = VirtualAlloc(NULL, 16 * PG, MEM_RESERVE, PAGE_NOACCESS);
    DWORD old;
    VirtualAlloc(b + 1 * PG, 2 * PG, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(b + 4 * PG, PG, MEM_COMMIT, PAGE_READONLY);
    VirtualAlloc(b + 5 * PG, PG, MEM_COMMIT, PAGE_EXECUTE_READ);
    VirtualAlloc(b + 7 * PG, PG, MEM_COMMIT, PAGE_NOACCESS);
    VirtualAlloc(b + 8 * PG, PG, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    check_query("query.p0.reserve", b, b, MEM_RESERVE, 0, PG);
    check_query("query.p1.rw2", b, b + PG, MEM_COMMIT, PAGE_READWRITE, 2 * PG);
    check_query("query.p2.mid", b, b + 2 * PG, MEM_COMMIT, PAGE_READWRITE, PG);
    check_query("query.p3.reserve", b, b + 3 * PG, MEM_RESERVE, 0, PG);
    check_query("query.p4.r", b, b + 4 * PG, MEM_COMMIT, PAGE_READONLY, PG);
    check_query("query.p5.rx", b, b + 5 * PG, MEM_COMMIT, PAGE_EXECUTE_READ, PG);
    check_query("query.p7.noaccess", b, b + 7 * PG, MEM_COMMIT, PAGE_NOACCESS, PG);
    check_query("query.p8.guard", b, b + 8 * PG, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD, PG);
    check_query("query.p9.tail", b, b + 9 * PG, MEM_RESERVE, 0, 7 * PG);
    /* decommit the first of the two RW pages */
    VirtualFree(b + PG, PG, MEM_DECOMMIT);
    check_query("query.decommit", b, b + PG, MEM_RESERVE, 0, PG);
    check_query("query.decommit.next", b, b + 2 * PG, MEM_COMMIT, PAGE_READWRITE, PG);
    /* VirtualProtect over two pages returns the first page's protection */
    fid_check(VirtualProtect(b + 4 * PG, 2 * PG, PAGE_READWRITE, &old) && old == PAGE_READONLY, "protect.old_first",
              "old protection %#lx want PAGE_READONLY", old);
    check_query("protect.merged", b, b + 4 * PG, MEM_COMMIT, PAGE_READWRITE, 2 * PG);
    fid_check(!VirtualProtect(b + 3 * PG, PG, PAGE_READWRITE, &old) && GetLastError() == ERROR_INVALID_ADDRESS, "protect.reserved",
              "VirtualProtect on a reserved page: error %lu", GetLastError());
    VirtualFree(b, 0, MEM_RELEASE);
}

/* ---------------------------------------------------------------- access */

static void expect(const char *id, DWORD got, DWORD want, ULONG_PTR want0, const void *want1)
{
    if (!want) fid_check(!got, id, "raised %#lx", got);
    else fid_check(got == want && g_info0 == want0 && g_info1 == (ULONG_PTR)want1, id, "code %#lx [0] %#llx [1] %#llx, want %#lx %#llx %p",
                   got, (unsigned long long)g_info0, (unsigned long long)g_info1, want, (unsigned long long)want0, want1);
}

static void group_access(void)
{
    static const struct { const char *name; DWORD prot; DWORD r, w, x; } cases[] =
    {
        { "noaccess", PAGE_NOACCESS,          EXCEPTION_ACCESS_VIOLATION, EXCEPTION_ACCESS_VIOLATION, EXCEPTION_ACCESS_VIOLATION },
        { "readonly", PAGE_READONLY,          0, EXCEPTION_ACCESS_VIOLATION, EXCEPTION_ACCESS_VIOLATION },
        { "readwrite", PAGE_READWRITE,        0, 0, EXCEPTION_ACCESS_VIOLATION },
        { "execute_read", PAGE_EXECUTE_READ,  0, EXCEPTION_ACCESS_VIOLATION, 0 },
        { "execute_readwrite", PAGE_EXECUTE_READWRITE, 0, 0, 0 },
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint8_t *p = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        DWORD old;
        char id[64];
        p[0] = 0xc3;   /* ret, for the execute probe */
        VirtualProtect(p, PG, cases[i].prot, &old);
        sprintf(id, "access.%s.read", cases[i].name);
        expect(id, probe('r', p + 0x10), cases[i].r, 0, p + 0x10);
        sprintf(id, "access.%s.write", cases[i].name);
        expect(id, probe('w', p + 0x20), cases[i].w, 1, p + 0x20);
        sprintf(id, "access.%s.execute", cases[i].name);
        expect(id, probe('x', p), cases[i].x, 8, p);
        VirtualFree(p, 0, MEM_RELEASE);
    }
    /* guard page: one-shot STATUS_GUARD_PAGE_VIOLATION, then the protection without PAGE_GUARD */
    {
        uint8_t *p = VirtualAlloc(NULL, 2 * PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        MEMORY_BASIC_INFORMATION mbi;
        DWORD old;
        VirtualProtect(p + PG, PG, PAGE_READWRITE | PAGE_GUARD, &old);
        expect("access.guard.first", probe('r', p + PG + 8), STATUS_GUARD_PAGE_VIOLATION, 0, p + PG + 8);
        expect("access.guard.second", probe('r', p + PG + 8), 0, 0, 0);
        VirtualQuery(p + PG, &mbi, sizeof(mbi));
        fid_check(mbi.Protect == PAGE_READWRITE, "access.guard.cleared", "protect %#lx after the guard fault", mbi.Protect);
        VirtualProtect(p + PG, PG, PAGE_READWRITE | PAGE_GUARD, &old);
        expect("access.guard.write", probe('w', p + PG + 0x30), STATUS_GUARD_PAGE_VIOLATION, 1, p + PG + 0x30);
        /* the neighbouring page sharing a 16 KB host page must not trip the guard */
        VirtualProtect(p + PG, PG, PAGE_READWRITE | PAGE_GUARD, &old);
        expect("access.guard.neighbour", probe('r', p + 0x10), 0, 0, 0);
        VirtualQuery(p + PG, &mbi, sizeof(mbi));
        fid_check(mbi.Protect == (PAGE_READWRITE | PAGE_GUARD), "access.guard.neighbour_kept", "protect %#lx", mbi.Protect);
        VirtualFree(p, 0, MEM_RELEASE);
    }
    /* reserved and decommitted pages */
    {
        uint8_t *p = VirtualAlloc(NULL, 4 * PG, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(p, 4 * PG, MEM_COMMIT, PAGE_READWRITE);
        VirtualFree(p + 2 * PG, PG, MEM_DECOMMIT);
        expect("access.decommitted.read", probe('r', p + 2 * PG + 4), EXCEPTION_ACCESS_VIOLATION, 0, p + 2 * PG + 4);
        expect("access.decommitted.prev", probe('r', p + 2 * PG - 1), 0, 0, 0);
        expect("access.decommitted.next", probe('w', p + 3 * PG), 0, 0, 0);
        VirtualFree(p, 0, MEM_RELEASE);
    }
}

/* ---------------------------------------------------------------- split accesses */

static void group_split(void)
{
    uint8_t *p = VirtualAlloc(NULL, 2 * PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old, code;
    VirtualProtect(p + PG, PG, PAGE_NOACCESS, &old);
    g_code = 0; g_probing = 1; mprobe_read8(p + PG - 4); g_probing = 0; code = g_code;
    fid_check(code == EXCEPTION_ACCESS_VIOLATION && g_info0 == 0 && g_info1 == (ULONG_PTR)(p + PG), "split.read",
              "code %#lx [0] %llu [1] %#llx want AV read at %p", code, (unsigned long long)g_info0, (unsigned long long)g_info1, p + PG);
    memset(p + PG - 4, 0x11, 4);
    g_code = 0; g_probing = 1; mprobe_write8(p + PG - 4, 0x2222222222222222ull); g_probing = 0; code = g_code;
    fid_check(code == EXCEPTION_ACCESS_VIOLATION && g_info0 == 1 && g_info1 == (ULONG_PTR)(p + PG), "split.write",
              "code %#lx [0] %llu [1] %#llx want AV write at %p", code, (unsigned long long)g_info0, (unsigned long long)g_info1, p + PG);
    fid_info("split.write.partial", "bytes before the boundary after the faulting store: %02x %02x %02x %02x",
             p[PG - 4], p[PG - 3], p[PG - 2], p[PG - 1]);
    VirtualFree(p, 0, MEM_RELEASE);
}

/* ---------------------------------------------------------------- generated code */

typedef uint32_t (*fn0)(void);
typedef uint32_t (*fn1)(uint32_t);

static void emit_mov_ret(uint8_t *c, uint32_t v) { c[0] = 0xb8; memcpy(c + 1, &v, 4); c[5] = 0xc3; }

static volatile LONG g_xmod_stop;
static uint8_t *g_xmod_code;
static DWORD WINAPI xmod_writer(void *arg)
{
    Sleep(50);
    emit_mov_ret(g_xmod_code, 0x22222222);
    return 0;
}

static void group_code(unsigned n)
{
    unsigned i, stale;
    DWORD old;
    /* documented: RW -> write -> RX + FlushInstructionCache -> call; repeat */
    {
        uint8_t *c = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        for (i = 0, stale = 0; i < n; i++)
        {
            VirtualProtect(c, PG, PAGE_READWRITE, &old);
            emit_mov_ret(c, 0x1000 + i);
            VirtualProtect(c, PG, PAGE_EXECUTE_READ, &old);
            FlushInstructionCache(GetCurrentProcess(), c, 6);
            if (((fn0)c)() != 0x1000 + i) stale++;
        }
        fid_check(!stale, "code.protect_flush", "%u of %u calls ran the previous code", stale, n);
        VirtualFree(c, 0, MEM_RELEASE);
    }
    /* RWX, rewritten between calls without FlushInstructionCache (x86 keeps caches coherent) */
    {
        uint8_t *c = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        for (i = 0, stale = 0; i < n; i++)
        {
            emit_mov_ret(c, 0x2000 + i);
            if (((fn0)c)() != 0x2000 + i) stale++;
        }
        fid_check(!stale, "code.rwx_noflush", "%u of %u calls ran the previous code", stale, n);
        /* only the immediate changes (the opcode bytes stay), data written next to the code */
        for (i = 0, stale = 0; i < n; i++)
        {
            uint32_t v = 0x3000 + i;
            memcpy(c + 1, &v, 4);
            c[0x800 + (i & 0xff)] = (uint8_t)i;
            if (((fn0)c)() != v) stale++;
        }
        fid_check(!stale, "code.rwx_immediate", "%u of %u calls ran the previous immediate", stale, n);
        VirtualFree(c, 0, MEM_RELEASE);
    }
    /* inline: the code rewrites the instruction it executes next */
    {
        uint8_t *c = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        memcpy(c, inline_smc_start, inline_smc_end - inline_smc_start);
        for (i = 0, stale = 0; i < n; i++)
            if (((fn1)c)(0x4000 + i) != 0x4000 + i) stale++;
        fid_check(!stale, "code.inline", "%u of %u inline modifications not seen", stale, n);
        VirtualFree(c, 0, MEM_RELEASE);
    }
    /* WriteProcessMemory into our own RX code */
    {
        uint8_t *c = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        emit_mov_ret(c, 1);
        VirtualProtect(c, PG, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), c, 6);
        for (i = 0, stale = 0; i < n / 10; i++)
        {
            uint32_t v = 0x5000 + i;
            SIZE_T done = 0;
            ((fn0)c)();
            if (!WriteProcessMemory(GetCurrentProcess(), c + 1, &v, 4, &done) || done != 4) { stale++; continue; }
            if (((fn0)c)() != v) stale++;
        }
        fid_check(!stale, "code.writeprocessmemory", "%u of %u calls after WriteProcessMemory ran the previous code", stale, n / 10);
        VirtualFree(c, 0, MEM_RELEASE);
    }
    /* another thread rewrites code this thread keeps calling */
    {
        HANDLE t;
        DWORD start = GetTickCount();
        uint32_t r = 0;
        g_xmod_code = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        emit_mov_ret(g_xmod_code, 0x11111111);
        t = CreateThread(NULL, 0, xmod_writer, NULL, 0, NULL);
        while (GetTickCount() - start < 3000 && (r = ((fn0)g_xmod_code)()) != 0x22222222) ;
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
        fid_check(r == 0x22222222, "code.cross_thread", "still running the old code 3 s after another thread rewrote it (%#x)", r);
        VirtualFree(g_xmod_code, 0, MEM_RELEASE);
    }
}

/* ---------------------------------------------------------------- write watch */

static void group_watch(void)
{
    typedef UINT (WINAPI *GetWriteWatch_t)(DWORD, void *, SIZE_T, void **, ULONG_PTR *, ULONG *);
    typedef UINT (WINAPI *ResetWriteWatch_t)(void *, SIZE_T);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    GetWriteWatch_t pGet = (void *)GetProcAddress(k32, "GetWriteWatch");
    ResetWriteWatch_t pReset = (void *)GetProcAddress(k32, "ResetWriteWatch");
    uint8_t *p = VirtualAlloc(NULL, 16 * PG, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    void *addrs[16];
    ULONG_PTR count = 16;
    ULONG gran = 0;
    if (!p || !pGet) { fid_check(0, "watch.alloc", "MEM_WRITE_WATCH allocation failed (%lu)", GetLastError()); return; }
    pReset(p, 16 * PG);
    p[0] = 1; p[3 * PG + 5] = 2; p[9 * PG + 100] = 3; p[9 * PG + 200] = 4;
    fid_check(!pGet(0, p, 16 * PG, addrs, &count, &gran) && count == 3 && gran == PG && addrs[0] == p && addrs[1] == p + 3 * PG &&
              addrs[2] == p + 9 * PG, "watch.pages", "count %llu granularity %lu first %p %p %p", (unsigned long long)count, gran,
              count > 0 ? addrs[0] : NULL, count > 1 ? addrs[1] : NULL, count > 2 ? addrs[2] : NULL);
    VirtualFree(p, 0, MEM_RELEASE);
}

/* ---------------------------------------------------------------- stack */

static __attribute__((noinline)) int deep(volatile char *top)
{
    volatile char big[1536 * 1024];
    unsigned i;
    for (i = 0; i < sizeof(big); i += 4096) big[i] = (char)i;
    return big[sizeof(big) - 4096] + (top != NULL);
}

static DWORD WINAPI stack_thread(void *arg)
{
    volatile char top = 0;
    return deep(&top) >= 0;
}

static void group_stack(void)
{
    HANDLE t = CreateThread(NULL, 2 * 1024 * 1024, stack_thread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    DWORD code = 0;
    WaitForSingleObject(t, 10000);
    GetExitCodeThread(t, &code);
    fid_check(code == 1, "stack.grow", "thread touching 1.5 MB of a 2 MB stack exited with %#lx", code);
    CloseHandle(t);
}

static void group_layout(void)
{
    SYSTEM_INFO si;
    uint8_t *a = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t *b = VirtualAlloc(NULL, PG, MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE);
    GetSystemInfo(&si);
    fid_check(si.dwPageSize == PG && si.dwAllocationGranularity == 0x10000, "layout.sysinfo", "page %lu granularity %lu",
              si.dwPageSize, si.dwAllocationGranularity);
    fid_check(!((uintptr_t)a & 0xffff) && !((uintptr_t)b & 0xffff), "layout.granularity", "allocations at %p %p", a, b);
    fid_info("layout.addresses", "min %p max %p, alloc %p, top-down %p", si.lpMinimumApplicationAddress, si.lpMaximumApplicationAddress, a, b);
    VirtualFree(a, 0, MEM_RELEASE);
    VirtualFree(b, 0, MEM_RELEASE);
}

int main(int argc, char **argv)
{
    unsigned n = argc > 1 ? (unsigned)atoi(argv[1]) : 2500;
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "memtest";
    fid_environment();
    AddVectoredExceptionHandler(1, veh);
    group_layout();
    group_query();
    group_access();
    group_split();
    group_code(n);
    group_watch();
    group_stack();
    return fid_summary();
}
