/* tsctest: time-stamp counter, judged only on what the architecture defines.
 *
 * Not checked: that consecutive reads differ, or any particular rate. Checked:
 *  - fenced RDTSC (LFENCE; RDTSC) never goes backwards on one thread, across many samples
 *  - happens-before across threads: a value read before a release store is never larger than a value read
 *    after the matching acquire on another thread (Windows treats an invariant TSC as synchronized)
 *  - the same while the reading thread migrates between processors (affinity changes)
 *  - RDTSCP (if advertised) is ordered the same way, and its ECX (IA32_TSC_AUX) is stable on one processor
 *  - consistency of what CPUID says about the TSC (leaf 0x15/0x16 frequency vs the measured rate, invariant
 *    TSC bit vs a constant rate) — a mismatch is reported only when CPUID makes a claim
 * Build: x86_64-w64-mingw32-clang -O2 tsctest.c -o tsctest.exe */
#include "fid.h"

static inline uint64_t rdtsc_fenced(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence\n\trdtsc\n\tlfence" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp_aux(uint32_t *aux)
{
    uint32_t lo, hi, c;
    __asm__ volatile("rdtscp\n\tlfence" : "=a"(lo), "=d"(hi), "=c"(c) :: "memory");
    *aux = c;
    return ((uint64_t)hi << 32) | lo;
}

static void group_monotonic(unsigned n)
{
    uint64_t prev = rdtsc_fenced(), back = 0, equal = 0, maxback = 0;
    unsigned i;
    for (i = 0; i < n; i++)
    {
        uint64_t t = rdtsc_fenced();
        if (t < prev) { back++; if (prev - t > maxback) maxback = prev - t; }
        else if (t == prev) equal++;
        prev = t;
    }
    fid_check(!back, "tsc.monotonic", "%llu of %u fenced reads went backwards (largest step back %llu)",
              (unsigned long long)back, n, (unsigned long long)maxback);
    fid_info("tsc.equal_reads", "%llu of %u consecutive fenced reads equal", (unsigned long long)equal, n);
}

/* happens-before: thread A stores (round, tsc) with release semantics; thread B, after observing the round,
 * reads the TSC: it must not be smaller than A's value */
static volatile LONG64 g_round;
static volatile uint64_t g_stamp;
static volatile LONG g_stop;
static volatile uint64_t g_violations, g_checked, g_worst;
static DWORD_PTR g_affinity_b;

static DWORD WINAPI hb_writer(void *arg)
{
    LONG64 r;
    for (r = 1; !g_stop; r++)
    {
        g_stamp = rdtsc_fenced();
        InterlockedExchange64(&g_round, r);      /* full barrier: release of g_stamp */
        while (g_round == r && !g_stop) YieldProcessor();
    }
    return 0;
}

static DWORD WINAPI hb_reader(void *arg)
{
    LONG64 seen = 0;
    int migrate = (int)(uintptr_t)arg;
    DWORD_PTR procmask = 0, sysmask = 0;
    unsigned cpu = 0, ncpu = 0;
    if (migrate)
    {
        GetProcessAffinityMask(GetCurrentProcess(), &procmask, &sysmask);
        for (cpu = 0; cpu < 64; cpu++) if (procmask & ((DWORD_PTR)1 << cpu)) ncpu++;
        cpu = 0;
    }
    while (!g_stop)
    {
        LONG64 r = InterlockedCompareExchange64(&g_round, 0, 0);   /* acquire */
        uint64_t mine, theirs;
        if (r <= seen) { YieldProcessor(); continue; }    /* nothing new (negative = our own hand-back) */
        theirs = g_stamp;
        mine = rdtsc_fenced();
        if (mine < theirs)
        {
            g_violations++;
            if (theirs - mine > g_worst) g_worst = theirs - mine;
        }
        g_checked++;
        seen = r;
        InterlockedExchange64(&g_round, -r);     /* let the writer go on */
        if (migrate && ncpu > 1 && (g_checked % 64) == 0)
        {
            do cpu = (cpu + 1) % 64; while (!(procmask & ((DWORD_PTR)1 << cpu)));
            SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << cpu);
        }
    }
    return 0;
}

static void group_order(const char *id, int migrate, DWORD ms)
{
    HANDLE t[2];
    g_round = 0; g_stamp = 0; g_stop = 0; g_violations = 0; g_checked = 0; g_worst = 0;
    t[0] = CreateThread(NULL, 0, hb_writer, NULL, 0, NULL);
    t[1] = CreateThread(NULL, 0, hb_reader, (void *)(uintptr_t)migrate, 0, NULL);
    Sleep(ms);
    g_stop = 1;
    WaitForMultipleObjects(2, t, TRUE, 10000);
    CloseHandle(t[0]); CloseHandle(t[1]);
    fid_check(!g_violations && g_checked > 1000, id, "%llu of %llu handoffs read a smaller TSC after the acquire (worst %llu)",
              (unsigned long long)g_violations, (unsigned long long)g_checked, (unsigned long long)g_worst);
    fid_info(id, "%llu handoffs", (unsigned long long)g_checked);
}

static void group_rdtscp(int advertised)
{
    uint32_t aux, aux2, cpu, mismatch = 0;
    uint64_t prev, t;
    unsigned i, back = 0;
    if (!advertised) { fid_info("rdtscp", "not advertised"); return; }
    SetThreadAffinityMask(GetCurrentThread(), 1);
    Sleep(1);
    prev = rdtscp_aux(&aux);
    for (i = 0; i < 100000; i++)
    {
        t = rdtscp_aux(&aux2);
        if (t < prev) back++;
        if (aux2 != aux) mismatch++;
        prev = t;
    }
    cpu = GetCurrentProcessorNumber();
    fid_check(!back, "rdtscp.monotonic", "%u reads went backwards", back);
    fid_check(!mismatch, "rdtscp.aux_stable", "TSC_AUX changed %u times while pinned to one processor", mismatch);
    fid_info("rdtscp.aux", "TSC_AUX %#x, GetCurrentProcessorNumber %u", aux, cpu);
    {
        DWORD_PTR procmask, sysmask;
        GetProcessAffinityMask(GetCurrentProcess(), &procmask, &sysmask);
        SetThreadAffinityMask(GetCurrentThread(), procmask);
    }
}

static void group_rate(void)
{
    int r[4];
    LARGE_INTEGER f, q0, q1;
    uint64_t t0, t1;
    double hz, claimed = 0;
    unsigned max_basic, max_ext, invariant = 0;
    __cpuid(r, 0); max_basic = r[0];
    __cpuid(r, 0x80000000); max_ext = r[0];
    if (max_ext >= 0x80000007) { __cpuid(r, 0x80000007); invariant = (r[3] >> 8) & 1; }
    if (max_basic >= 0x15)
    {
        __cpuid(r, 0x15);
        fid_info("cpuid.15", "denominator %u numerator %u crystal %u Hz", r[0], r[1], r[2]);
        if (r[0] && r[1] && r[2]) claimed = (double)r[2] * r[1] / r[0];
    }
    if (max_basic >= 0x16) { __cpuid(r, 0x16); fid_info("cpuid.16", "base %u MHz max %u MHz bus %u MHz", r[0], r[1], r[2]); }
    fid_info("cpuid.invariant_tsc", "%u", invariant);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0); t0 = rdtsc_fenced();
    Sleep(500);
    QueryPerformanceCounter(&q1); t1 = rdtsc_fenced();
    hz = (double)(t1 - t0) * f.QuadPart / (double)(q1.QuadPart - q0.QuadPart);
    fid_info("tsc.rate", "%.3f MHz measured against QueryPerformanceCounter (QPC %lld Hz)", hz / 1e6, f.QuadPart);
    if (claimed)
        fid_check(hz > claimed * 0.98 && hz < claimed * 1.02, "tsc.rate_vs_cpuid", "measured %.3f MHz, CPUID leaf 0x15 says %.3f MHz",
                  hz / 1e6, claimed / 1e6);
    if (invariant)
    {
        double hz2;
        QueryPerformanceCounter(&q0); t0 = rdtsc_fenced();
        Sleep(300);
        QueryPerformanceCounter(&q1); t1 = rdtsc_fenced();
        hz2 = (double)(t1 - t0) * f.QuadPart / (double)(q1.QuadPart - q0.QuadPart);
        fid_check(hz2 > hz * 0.98 && hz2 < hz * 1.02, "tsc.invariant_rate", "rate %.3f then %.3f MHz with invariant TSC advertised",
                  hz / 1e6, hz2 / 1e6);
    }
}

int main(int argc, char **argv)
{
    int r[4];
    unsigned max_ext;
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "tsctest";
    fid_environment();
    __cpuid(r, 1);
    fid_check((r[3] >> 4) & 1, "cpuid.tsc", "CPUID.1:EDX.TSC clear");
    __cpuid(r, 0x80000000); max_ext = r[0];
    if (max_ext >= 0x80000001) __cpuid(r, 0x80000001); else r[3] = 0;
    group_monotonic(argc > 1 ? (unsigned)atoi(argv[1]) : 5000000);
    group_order("tsc.happens_before", 0, 1500);
    group_order("tsc.happens_before_migrating", 1, 1500);
    group_rdtscp((r[3] >> 27) & 1);
    group_rate();
    return fid_summary();
}
