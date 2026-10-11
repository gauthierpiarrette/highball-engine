/* fibertest: architectural state across Windows fiber switches.
 *
 * Every SwitchToFiber is made by switch_checked() (fiber_asm.S) with the fiber's own pattern in the
 * registers the Windows x64 ABI makes nonvolatile (rbx rbp rsi rdi r12-r15, xmm6-15); when the fiber
 * resumes, those registers must hold the same values. Also per fiber: a stack canary, FlsGetValue,
 * GetCurrentFiber/GetFiberData. Groups:
 *   ring       4 fibers + the thread fiber switching in a ring, many rounds, unrelated work in between
 *   migrate    one fiber resumed alternately by two threads (it continues on another host thread)
 *   exceptions the ring with an int3 (vectored handler, continue) between switches
 *   control    MXCSR rounding and x87 control word set per fiber: recorded (the ABI makes them
 *              nonvolatile; whether SwitchToFiber switches them is what Windows shows)
 * Build: x86_64-w64-mingw32-clang -O2 fibertest.c fiber_asm.S -o fibertest.exe */
#include "fid.h"

extern uint32_t switch_checked(void *target, const uint64_t *gpr, const uint8_t (*xmm)[16]);
extern void raise_int3(void);
void *g_pSwitchToFiber;

#define NFIB 4

typedef struct fib
{
    int id;
    void *fiber;
    uint64_t gpr[8];
    uint8_t xmm[10][16];
    uint32_t mxcsr;
    uint16_t fcw;
    volatile unsigned bad_regs, bad_canary, bad_fls, bad_self, bad_control, rounds;
    uint32_t first_mask;
} fib;

static fib g_fibs[NFIB + 1];         /* [NFIB] is the thread's own fiber */
static DWORD g_fls;
static unsigned g_rounds = 2000;
static volatile int g_with_exceptions, g_control;
static volatile LONG g_int3_seen;

static void make(fib *f, int id)
{
    unsigned i, j;
    f->id = id;
    for (i = 0; i < 8; i++) f->gpr[i] = 0x5a00000000000000ull | ((uint64_t)(id + 1) << 44) | ((uint64_t)(i + 1) << 32) | (id * 0x111 + i);
    for (i = 0; i < 10; i++) for (j = 0; j < 16; j++) f->xmm[i][j] = (uint8_t)(id * 37 + i * 16 + j + 1);
    f->mxcsr = 0x1f80 | ((id & 3) << 13);
    f->fcw = 0x027f | ((id & 3) << 10);
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&g_int3_seen);
    ep->ContextRecord->Rip = (DWORD64)ep->ExceptionRecord->ExceptionAddress + 1;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void set_control(const fib *f)
{
    __asm__ volatile("ldmxcsr %0\n\tfldcw %1" :: "m"(f->mxcsr), "m"(f->fcw));
}

static void check_after_resume(fib *me, volatile uint64_t *canary, uint64_t canary_value, uint32_t mask)
{
    uint32_t mxcsr;
    uint16_t fcw;
    if (mask) { if (!me->bad_regs++) me->first_mask = mask; }
    if (canary[0] != canary_value || canary[7] != ~canary_value) me->bad_canary++;
    if ((uintptr_t)FlsGetValue(g_fls) != (uintptr_t)me->id + 1) me->bad_fls++;
    if (GetCurrentFiber() != me->fiber || GetFiberData() != me) me->bad_self++;
    if (g_control)
    {
        __asm__ volatile("stmxcsr %0\n\tfnstcw %1" : "=m"(mxcsr), "=m"(fcw));
        if ((mxcsr & 0xffc0) != (me->mxcsr & 0xffc0) || fcw != me->fcw) me->bad_control++;
    }
}

static void CALLBACK fiber_proc(void *arg)
{
    fib *me = arg;
    volatile uint64_t canary[8];
    uint64_t cv = 0xc0ffee0000000000ull | me->id;
    volatile double work = 1.0;
    unsigned r, k;

    for (k = 0; k < 8; k++) canary[k] = k == 7 ? ~cv : cv;
    FlsSetValue(g_fls, (void *)(uintptr_t)(me->id + 1));
    for (r = 0; ; r++)
    {
        fib *next = &g_fibs[me->id + 1 <= NFIB ? me->id + 1 : 0];
        uint32_t mask;
        for (k = 0; k < 50; k++) work = work * 1.0000001 + k;   /* unrelated work between switches */
        if (g_with_exceptions && (r % 7) == 3) raise_int3();
        if (g_control) set_control(me);
        mask = switch_checked(next->fiber, me->gpr, (const uint8_t (*)[16])me->xmm);
        check_after_resume(me, canary, cv, mask);
        me->rounds++;
    }
}

static void run_ring(const char *group)
{
    fib *self = &g_fibs[NFIB];
    volatile uint64_t canary[8];
    uint64_t cv = 0xfeedface00000000ull;
    unsigned i, r, k;
    char id[64];

    for (k = 0; k < 8; k++) canary[k] = k == 7 ? ~cv : cv;
    for (i = 0; i <= NFIB; i++)
    {
        fib *f = &g_fibs[i];
        f->bad_regs = f->bad_canary = f->bad_fls = f->bad_self = f->bad_control = f->rounds = 0;
        f->first_mask = 0;
    }
    FlsSetValue(g_fls, (void *)(uintptr_t)(self->id + 1));
    for (r = 0; r < g_rounds; r++)
    {
        uint32_t mask;
        if (g_control) set_control(self);
        mask = switch_checked(g_fibs[0].fiber, self->gpr, (const uint8_t (*)[16])self->xmm);
        check_after_resume(self, canary, cv, mask);
        self->rounds++;
    }
    __asm__ volatile("ldmxcsr %0\n\tfldcw %1" :: "m"((uint32_t){ 0x1f80 }), "m"((uint16_t){ 0x27f }));
    for (i = 0; i <= NFIB; i++)
    {
        fib *f = &g_fibs[i];
        sprintf(id, "%s.fiber%d.regs", group, i);
        fid_check(!f->bad_regs, id, "%u of %u resumptions with wrong nonvolatile registers (first mask %#x)", f->bad_regs, f->rounds,
                  f->first_mask);
        sprintf(id, "%s.fiber%d.canary", group, i);
        fid_check(!f->bad_canary, id, "%u stack canary mismatches", f->bad_canary);
        sprintf(id, "%s.fiber%d.fls", group, i);
        fid_check(!f->bad_fls, id, "%u FLS mismatches", f->bad_fls);
        sprintf(id, "%s.fiber%d.self", group, i);
        fid_check(!f->bad_self, id, "%u GetCurrentFiber/GetFiberData mismatches", f->bad_self);
        if (g_control)
        {
            sprintf(id, "%s.fiber%d.control", group, i);
            fid_info(id, "%u of %u resumptions saw another fiber's MXCSR/FCW", f->bad_control, f->rounds);
        }
        /* a fiber's first entry is not a resumption: the first ring gives the created fibers g_rounds - 1 */
        sprintf(id, "%s.fiber%d.rounds", group, i);
        fid_check(f->rounds >= g_rounds - 1, id, "only %u rounds", f->rounds);
    }
}

/* ---------------------------------------------------------------- migration between threads */

static fib g_mig, g_thr[2];
static HANDLE g_turn[2];
static volatile int g_owner;           /* which thread fiber the migrating fiber returns to */
static volatile unsigned g_mig_rounds;
static volatile DWORD g_mig_tids[2];

static void CALLBACK mig_proc(void *arg)
{
    fib *me = arg;
    volatile uint64_t canary[8];
    uint64_t cv = 0xabad1dea00000000ull;
    unsigned k;
    for (k = 0; k < 8; k++) canary[k] = k == 7 ? ~cv : cv;
    FlsSetValue(g_fls, (void *)(uintptr_t)(me->id + 1));
    for (;;)
    {
        uint32_t mask;
        DWORD tid = GetCurrentThreadId();
        if (tid != g_mig_tids[g_owner]) me->bad_self++;
        if (g_with_exceptions && (g_mig_rounds % 5) == 2) raise_int3();
        mask = switch_checked(g_thr[g_owner].fiber, me->gpr, (const uint8_t (*)[16])me->xmm);
        check_after_resume(me, canary, cv, mask);
        me->rounds++;
    }
}

static DWORD WINAPI mig_thread(void *arg)
{
    int idx = (int)(uintptr_t)arg;
    fib *self = &g_thr[idx];
    unsigned r;
    g_mig_tids[idx] = GetCurrentThreadId();
    self->fiber = ConvertThreadToFiber(self);
    FlsSetValue(g_fls, (void *)(uintptr_t)(self->id + 1));
    for (r = 0; r < g_rounds; r++)
    {
        WaitForSingleObject(g_turn[idx], INFINITE);
        g_owner = idx;
        switch_checked(g_mig.fiber, self->gpr, (const uint8_t (*)[16])self->xmm);
        g_mig_rounds++;
        SetEvent(g_turn[!idx]);
    }
    ConvertFiberToThread();
    return 0;
}

static void group_migrate(const char *group)
{
    HANDLE t[2];
    char id[64];
    make(&g_mig, 20);
    make(&g_thr[0], 21);
    make(&g_thr[1], 22);
    g_mig.bad_regs = g_mig.bad_canary = g_mig.bad_fls = g_mig.bad_self = g_mig.rounds = 0;
    g_mig.first_mask = 0;
    g_mig_rounds = 0;
    g_mig.fiber = CreateFiber(0x10000, mig_proc, &g_mig);
    g_turn[0] = CreateEventA(NULL, FALSE, TRUE, NULL);
    g_turn[1] = CreateEventA(NULL, FALSE, FALSE, NULL);
    t[0] = CreateThread(NULL, 0, mig_thread, (void *)0, 0, NULL);
    t[1] = CreateThread(NULL, 0, mig_thread, (void *)1, 0, NULL);
    WaitForMultipleObjects(2, t, TRUE, 120000);
    sprintf(id, "%s.regs", group);
    fid_check(!g_mig.bad_regs, id, "%u of %u resumptions on another thread with wrong nonvolatile registers (first mask %#x)",
              g_mig.bad_regs, g_mig.rounds, g_mig.first_mask);
    sprintf(id, "%s.canary", group);
    fid_check(!g_mig.bad_canary, id, "%u canary mismatches", g_mig.bad_canary);
    sprintf(id, "%s.fls", group);
    fid_check(!g_mig.bad_fls, id, "%u FLS mismatches", g_mig.bad_fls);
    sprintf(id, "%s.self", group);
    fid_check(!g_mig.bad_self, id, "%u wrong-thread or GetCurrentFiber mismatches", g_mig.bad_self);
    sprintf(id, "%s.rounds", group);
    fid_check(g_mig_rounds == 2 * g_rounds, id, "%u of %u migrations", g_mig_rounds, 2 * g_rounds);
    CloseHandle(t[0]); CloseHandle(t[1]);
    DeleteFiber(g_mig.fiber);
}

int main(int argc, char **argv)
{
    unsigned i;
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "fibertest";
    fid_environment();
    if (argc > 1) g_rounds = (unsigned)atoi(argv[1]);
    g_pSwitchToFiber = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SwitchToFiber");
    AddVectoredExceptionHandler(1, veh);
    g_fls = FlsAlloc(NULL);

    for (i = 0; i <= NFIB; i++) make(&g_fibs[i], i);
    g_fibs[NFIB].fiber = ConvertThreadToFiberEx(&g_fibs[NFIB], FIBER_FLAG_FLOAT_SWITCH);
    for (i = 0; i < NFIB; i++)
        g_fibs[i].fiber = CreateFiberEx(0x10000, 0x10000, i & 1 ? FIBER_FLAG_FLOAT_SWITCH : 0, fiber_proc, &g_fibs[i]);

    run_ring("ring");
    g_with_exceptions = 1;
    run_ring("exceptions");
    fid_check(g_int3_seen > 0, "exceptions.int3", "no int3 reached the handler");
    g_with_exceptions = 0;
    g_control = 1;
    run_ring("control");
    g_control = 0;

    ConvertFiberToThread();
    group_migrate("migrate");
    g_with_exceptions = 1;
    group_migrate("migrate_exc");
    return fid_summary();
}
