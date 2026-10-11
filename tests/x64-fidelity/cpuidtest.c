/* cpuidtest: CPUID / XGETBV / XSAVE consistency of the x86-64 processor a Windows program sees.
 *
 * Not "does it look like a particular Intel CPU", but internal consistency:
 *  - every advertised instruction set extension executes, and has the OS state it needs (XCR0)
 *  - XCR0 is a subset of what CPUID leaf 0xD reports, and leaf 0xD's sizes and offsets match XCR0
 *  - XSAVE / XSAVEOPT / XSAVEC / XRSTOR / FXSAVE / FXRSTOR follow the SDM layout and round-trip every
 *    architecturally defined field; XRSTOR's init semantics for components with XSTATE_BV clear
 *  - the Windows view (GetEnabledXStateFeatures, IsProcessorFeaturePresent, SYSTEM_CPU_INFORMATION)
 *    agrees with CPUID and XGETBV
 *  - AVX state survives preemption and SuspendThread/ResumeThread
 * Build: x86_64-w64-mingw32-clang -O2 cpuidtest.c cpu_asm.S -o cpuidtest.exe */
#include "fid.h"
#include <stddef.h>
#include <winternl.h>

typedef struct DECLSPEC_ALIGN(64) fpstate
{
    uint16_t fcw;          /* 0x00 */
    uint16_t pad0;
    uint32_t mxcsr;        /* 0x04 */
    uint32_t avx;          /* 0x08 */
    uint32_t pad1;
    uint64_t rfbm;         /* 0x10 */
    uint64_t pad2;
    uint8_t  x87[3][16];   /* 0x20 */
    uint8_t  pad3[16];
    uint8_t  ymm[16][32];  /* 0x60 */
    uint8_t  fx[512];      /* 0x260 */
} fpstate;
_Static_assert(offsetof(fpstate, rfbm) == 0x10, "layout");
_Static_assert(offsetof(fpstate, x87) == 0x20, "layout");
_Static_assert(offsetof(fpstate, ymm) == 0x60, "layout");
_Static_assert(offsetof(fpstate, fx) == 0x260, "layout");

typedef int (*probe_fn)(void *);
extern int probe_sse3(void *), probe_ssse3(void *), probe_sse41(void *), probe_sse42(void *), probe_popcnt(void *),
    probe_movbe(void *), probe_cx16(void *), probe_aes(void *), probe_pclmul(void *), probe_avx(void *),
    probe_avx2(void *), probe_fma(void *), probe_f16c(void *), probe_bmi1(void *), probe_bmi2(void *),
    probe_lzcnt(void *), probe_adx(void *), probe_rdrand(void *), probe_rdseed(void *), probe_sha(void *),
    probe_rdtscp(void *), probe_rdpid(void *), probe_xgetbv1(void *), probe_xsaveopt(void *), probe_xsavec(void *),
    probe_avx512(void *), probe_vaes(void *), probe_vpclmul(void *), probe_clflushopt(void *), probe_clwb(void *),
    probe_avxvnni(void *), probe_gfni(void *);
extern char probe_ud_landing[];
extern uint64_t xgetbv0(void);
extern void fp_roundtrip(const fpstate *in, void *area, fpstate *out, int mode);
extern int avx_preempt_check(const uint8_t ymm[16][32], uint64_t iterations);

static volatile int g_probing;
static volatile DWORD g_probe_code;

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    if (!g_probing) return EXCEPTION_CONTINUE_SEARCH;
    g_probe_code = ep->ExceptionRecord->ExceptionCode;
    ep->ContextRecord->Rip = (DWORD64)probe_ud_landing;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int run_probe(probe_fn fn, void *buf)
{
    int r;
    g_probe_code = 0;
    g_probing = 1;
    r = fn(buf);
    g_probing = 0;
    return r;
}

typedef struct { unsigned eax, ebx, ecx, edx; } cpuid_t;
static cpuid_t cpuidex(unsigned leaf, unsigned sub)
{
    int r[4];
    cpuid_t c;
    __cpuidex(r, leaf, sub);
    c.eax = r[0]; c.ebx = r[1]; c.ecx = r[2]; c.edx = r[3];
    return c;
}

static void dump_leaf(unsigned leaf, unsigned sub)
{
    cpuid_t c = cpuidex(leaf, sub);
    char id[64];
    sprintf(id, "cpuid.%x.%x", leaf, sub);
    fid_info(id, "%08x %08x %08x %08x", c.eax, c.ebx, c.ecx, c.edx);
}

enum { EAX, EBX, ECX, EDX };
struct feature
{
    const char *name;
    unsigned leaf, sub, reg, bit;
    probe_fn probe;
    uint64_t xcr0_needed;   /* XCR0 bits the instructions need */
    const char *needs;      /* another feature this one implies */
};

static const struct feature features[] =
{
    { "sse3",      1, 0, ECX, 0,  probe_sse3 },
    { "pclmulqdq", 1, 0, ECX, 1,  probe_pclmul },
    { "ssse3",     1, 0, ECX, 9,  probe_ssse3 },
    { "fma",       1, 0, ECX, 12, probe_fma, 6, "avx" },
    { "cx16",      1, 0, ECX, 13, probe_cx16 },
    { "sse4.1",    1, 0, ECX, 19, probe_sse41 },
    { "sse4.2",    1, 0, ECX, 20, probe_sse42 },
    { "movbe",     1, 0, ECX, 22, probe_movbe },
    { "popcnt",    1, 0, ECX, 23, probe_popcnt },
    { "aes",       1, 0, ECX, 25, probe_aes },
    { "avx",       1, 0, ECX, 28, probe_avx, 6, "osxsave" },
    { "f16c",      1, 0, ECX, 29, probe_f16c, 6, "avx" },
    { "rdrand",    1, 0, ECX, 30, probe_rdrand },
    { "bmi1",      7, 0, EBX, 3,  probe_bmi1 },
    { "avx2",      7, 0, EBX, 5,  probe_avx2, 6, "avx" },
    { "bmi2",      7, 0, EBX, 8,  probe_bmi2 },
    { "avx512f",   7, 0, EBX, 16, probe_avx512, 0xe6, "avx2" },
    { "rdseed",    7, 0, EBX, 18, probe_rdseed },
    { "adx",       7, 0, EBX, 19, probe_adx },
    { "clflushopt",7, 0, EBX, 23, probe_clflushopt },
    { "clwb",      7, 0, EBX, 24, probe_clwb },
    { "sha",       7, 0, EBX, 29, probe_sha },
    { "gfni",      7, 0, ECX, 8,  probe_gfni },
    { "vaes",      7, 0, ECX, 9,  probe_vaes, 6, "avx" },
    { "vpclmulqdq",7, 0, ECX, 10, probe_vpclmul, 6, "avx" },
    { "rdpid",     7, 0, ECX, 22, probe_rdpid },
    { "avxvnni",   7, 1, EAX, 4,  probe_avxvnni, 6, "avx" },
    { "lzcnt",     0x80000001, 0, ECX, 5, probe_lzcnt },
    { "rdtscp",    0x80000001, 0, EDX, 27, probe_rdtscp },
    { "xsaveopt",  0xd, 1, EAX, 0, probe_xsaveopt, 0, "osxsave" },
    { "xsavec",    0xd, 1, EAX, 1, probe_xsavec, 0, "osxsave" },
    { "xgetbv1",   0xd, 1, EAX, 2, probe_xgetbv1, 0, "osxsave" },
};

static int has_feature(const char *name, unsigned max_basic, unsigned max_ext);

static int bit_of(unsigned leaf, unsigned sub, unsigned reg, unsigned bit, unsigned max_basic, unsigned max_ext)
{
    cpuid_t c;
    unsigned v;
    if (leaf < 0x80000000 && leaf > max_basic) return 0;
    if (leaf >= 0x80000000 && leaf > max_ext) return 0;
    c = cpuidex(leaf, sub);
    v = reg == EAX ? c.eax : reg == EBX ? c.ebx : reg == ECX ? c.ecx : c.edx;
    return (v >> bit) & 1;
}

static int has_feature(const char *name, unsigned max_basic, unsigned max_ext)
{
    unsigned i;
    if (!strcmp(name, "osxsave")) return bit_of(1, 0, ECX, 27, max_basic, max_ext);
    for (i = 0; i < sizeof(features) / sizeof(features[0]); i++)
        if (!strcmp(features[i].name, name))
            return bit_of(features[i].leaf, features[i].sub, features[i].reg, features[i].bit, max_basic, max_ext);
    return 0;
}

/* ---------------------------------------------------------------- XSAVE */

static void set80(uint8_t *d, uint64_t mant, uint16_t sexp)
{
    memset(d, 0, 16);
    memcpy(d, &mant, 8);
    memcpy(d + 8, &sexp, 2);
}

static void make_fp(fpstate *s, unsigned seed, int avx, uint64_t rfbm)
{
    unsigned i, j;
    memset(s, 0, sizeof(*s));
    s->fcw = 0x0b7f;
    s->mxcsr = 0x3f80;
    s->avx = avx;
    s->rfbm = rfbm;
    set80(s->x87[0], 0xc000000000000000ull, 0x3fff);        /* 1.5 */
    set80(s->x87[1], 0x9000000000000000ull, 0xc000);        /* -2.25 */
    set80(s->x87[2], 0x8000000000000000ull | seed, 0x4005);
    for (i = 0; i < 16; i++)
        for (j = 0; j < 32; j++) s->ymm[i][j] = (uint8_t)(0x10 * i + j + 3 * seed + 1);
}

static int check_legacy(const char *id, const uint8_t *img, const fpstate *want, int check_mask)
{
    char name[96], why[200] = "";
    unsigned i;
    int ok = 1;
    uint16_t fcw = *(uint16_t *)img, fsw = *(uint16_t *)(img + 2);
    uint32_t mxcsr = *(uint32_t *)(img + 24), mask = *(uint32_t *)(img + 28);

    if (fcw != want->fcw) { ok = 0; sprintf(why, "fcw %#x want %#x", fcw, want->fcw); }
    else if (((fsw >> 11) & 7) != 5) { ok = 0; sprintf(why, "fsw %#x: TOP %u want 5", fsw, (fsw >> 11) & 7); }
    else if (img[4] != 0xe0) { ok = 0; sprintf(why, "abridged ftw %#x want 0xe0", img[4]); }
    else if (mxcsr != want->mxcsr) { ok = 0; sprintf(why, "mxcsr %#x want %#x", mxcsr, want->mxcsr); }
    else if (check_mask && !mask) { ok = 0; sprintf(why, "MXCSR_MASK 0"); }
    for (i = 0; ok && i < 3; i++)
        if (memcmp(img + 32 + 16 * i, want->x87[2 - i], 10))
        {
            char g[32], w[32];
            fid_hex(g, img + 32 + 16 * i, 10); fid_hex(w, want->x87[2 - i], 10);
            ok = 0; sprintf(why, "st%u %s want %s", i, g, w);
        }
    for (i = 0; ok && i < 16; i++)
        if (memcmp(img + 160 + 16 * i, want->ymm[i], 16))
        {
            char g[40], w[40];
            fid_hex(g, img + 160 + 16 * i, 16); fid_hex(w, want->ymm[i], 16);
            ok = 0; sprintf(why, "xmm%u %s want %s", i, g, w);
        }
    sprintf(name, "%s.legacy", id);
    fid_check(ok, name, "%s", why);
    return ok;
}

static void check_ymmh(const char *id, const uint8_t *hi, unsigned stride, const fpstate *want, int want_zero)
{
    char why[200] = "";
    unsigned i;
    int ok = 1;
    for (i = 0; ok && i < 16; i++)
    {
        uint8_t zero[16] = { 0 };
        const uint8_t *w = want_zero ? zero : want->ymm[i] + 16;
        if (memcmp(hi + stride * i, w, 16))
        {
            char g[40], x[40];
            fid_hex(g, hi + stride * i, 16); fid_hex(x, w, 16);
            ok = 0; sprintf(why, "ymmh%u %s want %s", i, g, x);
        }
    }
    fid_check(ok, id, "%s", why);
}

static void group_xsave(int avx, uint64_t xcr0, unsigned max_basic, unsigned max_ext)
{
    static DECLSPEC_ALIGN(64) uint8_t area[8192];
    static fpstate in, out;
    uint64_t rfbm = xcr0 & (avx ? 7 : 3);
    int has_xsaveopt = has_feature("xsaveopt", max_basic, max_ext);
    int has_xsavec = has_feature("xsavec", max_basic, max_ext);
    uint64_t bv;
    unsigned i;
    int ok;

    /* standard-form XSAVE then XRSTOR */
    make_fp(&in, 1, avx, rfbm);
    memset(area, 0, sizeof(area));
    fp_roundtrip(&in, area, &out, 0);
    check_legacy("xsave.area", area, &in, 1);
    bv = *(uint64_t *)(area + 512);
    fid_check((bv & ~rfbm) == 0 && (bv & rfbm) == rfbm, "xsave.area.xstate_bv", "XSTATE_BV %#llx, RFBM %#llx (all components non-init)",
              (unsigned long long)bv, (unsigned long long)rfbm);
    fid_check(*(uint64_t *)(area + 520) == 0, "xsave.area.xcomp_bv", "XCOMP_BV %#llx in the standard form",
              (unsigned long long)*(uint64_t *)(area + 520));
    fid_info("xsave.area.mxcsr_mask", "%#x", *(uint32_t *)(area + 28));
    if (avx) check_ymmh("xsave.area.ymmh", area + 576, 16, &in, 0);
    check_legacy("xsave.restored", out.fx, &in, 1);
    if (avx) check_ymmh("xsave.restored.ymmh", out.ymm[0] + 16, 32, &in, 0);

    /* XRSTOR init semantics: a component whose XSTATE_BV bit is clear is set to its initial state */
    if (avx)
    {
        *(uint64_t *)(area + 512) = rfbm & ~4ull;
        fp_roundtrip(&in, area, &out, 4);
        check_ymmh("xrstor.init.avx", out.ymm[0] + 16, 32, &in, 1);
        ok = 1;
        for (i = 0; i < 16; i++) if (memcmp(out.ymm[i], in.ymm[i], 16)) ok = 0;
        fid_check(ok, "xrstor.init.avx.xmm", "xmm changed when only the AVX component was initialised");
    }
    *(uint64_t *)(area + 512) = rfbm & ~1ull;
    fp_roundtrip(&in, area, &out, 4);
    {
        uint16_t fcw = *(uint16_t *)out.fx, fsw = *(uint16_t *)(out.fx + 2);
        uint8_t zero[10] = { 0 };
        ok = fcw == 0x37f && fsw == 0 && out.fx[4] == 0;
        for (i = 0; ok && i < 8; i++) if (memcmp(out.fx + 32 + 16 * i, zero, 10)) ok = 0;
        fid_check(ok, "xrstor.init.x87", "after XRSTOR with XSTATE_BV[0]=0: fcw %#x fsw %#x ftw %#x (want 0x37f 0 0, registers 0)",
                  fcw, fsw, out.fx[4]);
    }
    *(uint64_t *)(area + 512) = rfbm & ~2ull;
    fp_roundtrip(&in, area, &out, 4);
    {
        uint8_t zero[16] = { 0 };
        uint32_t mxcsr = *(uint32_t *)(out.fx + 24);
        ok = 1;
        for (i = 0; i < 16; i++) if (memcmp(out.fx + 160 + 16 * i, zero, 16)) ok = 0;
        fid_check(ok, "xrstor.init.sse", "XMM registers not zero after XRSTOR with XSTATE_BV[1]=0");
        fid_check(mxcsr == in.mxcsr, "xrstor.init.sse.mxcsr", "MXCSR %#x: XRSTOR loads MXCSR from the area when RFBM[1]=1 (want %#x)",
                  mxcsr, in.mxcsr);
    }

    /* XSAVE with RFBM = SSE only: x87 part of the legacy region untouched, XSTATE_BV = RFBM & XINUSE */
    make_fp(&in, 2, avx, 2);
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);      /* XSAVE writes only XSTATE_BV; XRSTOR #GPs on a bad XCOMP_BV/reserved header */
    fp_roundtrip(&in, area, &out, 0);
    {
        int x87_untouched = area[0] == 0xcc && area[1] == 0xcc && area[2] == 0xcc && area[32] == 0xcc;
        bv = *(uint64_t *)(area + 512);
        fid_check(x87_untouched, "xsave.rfbm_sse.x87", "x87 fields written though RFBM[0]=0 (fcw bytes %02x%02x)", area[1], area[0]);
        fid_check(*(uint32_t *)(area + 24) == in.mxcsr, "xsave.rfbm_sse.mxcsr", "MXCSR %#x want %#x", *(uint32_t *)(area + 24), in.mxcsr);
        fid_check(!memcmp(area + 160, in.ymm[0], 16), "xsave.rfbm_sse.xmm", "xmm0 not saved");
        fid_check((bv & ~2ull) == 0 && (bv & 2), "xsave.rfbm_sse.xstate_bv", "XSTATE_BV %#llx want 0x2", (unsigned long long)bv);
        if (avx) fid_check(area[576] == 0xcc, "xsave.rfbm_sse.ymmh", "AVX area written though RFBM[2]=0");
    }

    /* FXSAVE / FXRSTOR: x87 + SSE, upper YMM untouched by FXRSTOR */
    make_fp(&in, 3, avx, rfbm);
    memset(area, 0, sizeof(area));
    fp_roundtrip(&in, area, &out, 1);
    check_legacy("fxsave.area", area, &in, 1);
    check_legacy("fxsave.restored", out.fx, &in, 1);
    if (avx)
    {
        ok = 1;
        for (i = 0; i < 16; i++) { unsigned j; for (j = 16; j < 32; j++) if (out.ymm[i][j] != 0xff) ok = 0; }
        fid_check(ok, "fxrstor.ymmh", "FXRSTOR changed the upper YMM halves (DESTROY left them all ones)");
    }

    if (has_xsaveopt)
    {
        make_fp(&in, 4, avx, rfbm);
        memset(area, 0, sizeof(area));
        fp_roundtrip(&in, area, &out, 2);
        check_legacy("xsaveopt.area", area, &in, 1);
        if (avx) check_ymmh("xsaveopt.area.ymmh", area + 576, 16, &in, 0);
        check_legacy("xsaveopt.restored", out.fx, &in, 1);
    }
    if (has_xsavec)
    {
        uint64_t xcomp;
        make_fp(&in, 5, avx, rfbm);
        memset(area, 0, sizeof(area));
        fp_roundtrip(&in, area, &out, 3);
        xcomp = *(uint64_t *)(area + 520);
        bv = *(uint64_t *)(area + 512);
        fid_check(xcomp == ((1ull << 63) | (rfbm & xcr0)), "xsavec.xcomp_bv", "XCOMP_BV %#llx want %#llx",
                  (unsigned long long)xcomp, (unsigned long long)((1ull << 63) | (rfbm & xcr0)));
        fid_check((bv & ~rfbm) == 0 && (bv & rfbm) == rfbm, "xsavec.xstate_bv", "XSTATE_BV %#llx", (unsigned long long)bv);
        check_legacy("xsavec.area", area, &in, 1);
        if (avx) check_ymmh("xsavec.area.ymmh", area + 576, 16, &in, 0);  /* first extended component at 576 */
        check_legacy("xsavec.restored", out.fx, &in, 1);
        if (avx) check_ymmh("xsavec.restored.ymmh", out.ymm[0] + 16, 32, &in, 0);
    }
}

/* ---------------------------------------------------------------- AVX state under preemption and suspension */

static volatile LONG g_spin_stop;
static DWORD WINAPI busy(void *arg)
{
    volatile uint64_t x = 1;
    while (!g_spin_stop) x = x * 6364136223846793005ull + 1;
    return (DWORD)x;
}

static uint8_t g_ymm_pattern[16][32];
static volatile LONG g_avx_result = -2;
static DWORD WINAPI avx_worker(void *arg)
{
    g_avx_result = avx_preempt_check((const uint8_t (*)[32])g_ymm_pattern, (uint64_t)(uintptr_t)arg);
    return 0;
}

static void group_avx_state(void)
{
    SYSTEM_INFO si;
    HANDLE threads[64], h;
    unsigned i, j, n, suspends = 0;
    GetSystemInfo(&si);
    n = si.dwNumberOfProcessors < 63 ? si.dwNumberOfProcessors : 63;
    for (i = 0; i < 16; i++) for (j = 0; j < 32; j++) g_ymm_pattern[i][j] = (uint8_t)(i * 31 + j * 7 + 5);
    g_spin_stop = 0;
    for (i = 0; i < n; i++) threads[i] = CreateThread(NULL, 0, busy, NULL, 0, NULL);
    g_avx_result = -2;
    h = CreateThread(NULL, 0, avx_worker, (void *)(uintptr_t)400000000ull, 0, NULL);
    /* suspend and resume the AVX thread while it spins with live YMM state */
    while (WaitForSingleObject(h, 2) == WAIT_TIMEOUT && suspends < 2000)
    {
        CONTEXT c;
        if (SuspendThread(h) == (DWORD)-1) break;
        c.ContextFlags = CONTEXT_CONTROL;
        GetThreadContext(h, &c);
        ResumeThread(h);
        suspends++;
    }
    WaitForSingleObject(h, INFINITE);
    g_spin_stop = 1;
    WaitForMultipleObjects(n, threads, TRUE, INFINITE);
    for (i = 0; i < n; i++) CloseHandle(threads[i]);
    CloseHandle(h);
    fid_check(g_avx_result == 0, "avx.state.preempt_suspend", "ymm%ld differs after spinning with %u busy threads and %u suspensions",
              (long)g_avx_result - 1, n, suspends);
    fid_info("avx.state.preempt_suspend", "%u suspensions", suspends);
}

/* ---------------------------------------------------------------- Windows view */

static void group_windows(int avx_usable, uint64_t xcr0, unsigned max_basic, unsigned max_ext)
{
    typedef DWORD64 (WINAPI *GetEnabledXStateFeatures_t)(void);
    GetEnabledXStateFeatures_t pGetEnabledXStateFeatures =
        (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetEnabledXStateFeatures");
    DWORD64 enabled = pGetEnabledXStateFeatures ? pGetEnabledXStateFeatures() : 0;
    cpuid_t c1 = cpuidex(1, 0);
    SYSTEM_INFO si, nsi;
    struct { USHORT arch, level, revision, maximum; ULONG features; } cpu = { 0 };
    unsigned family, model, stepping;
    static const struct { unsigned pf; const char *name; int classic; } pfs[] =
    {
        { 10, "sse2", 1 }, { 13, "sse3", 1 }, { 14, "cx16", 1 }, { 17, "osxsave", 1 }, { 28, "rdrand", 0 },
        { 32, "rdtscp", 0 }, { 36, "ssse3", 0 }, { 37, "sse4.1", 0 }, { 38, "sse4.2", 0 }, { 39, "avx", 0 },
        { 40, "avx2", 0 }, { 41, "avx512f", 0 },
    };
    unsigned i;

    fid_info("win.xstate", "GetEnabledXStateFeatures %#llx, XCR0 %#llx", (unsigned long long)enabled, (unsigned long long)xcr0);
    fid_check((enabled & 3) == 3, "win.xstate.legacy", "GetEnabledXStateFeatures %#llx lacks x87|SSE (3)", (unsigned long long)enabled);
    fid_check(!!(enabled & 4) == !!(xcr0 & 4), "win.xstate.avx", "GetEnabledXStateFeatures %#llx, XCR0 %#llx disagree on AVX",
              (unsigned long long)enabled, (unsigned long long)xcr0);
    fid_check((enabled & ~xcr0 & ~0x7f00ull) == 0, "win.xstate.subset", "Windows enables %#llx beyond XCR0 %#llx",
              (unsigned long long)enabled, (unsigned long long)xcr0);

    for (i = 0; i < sizeof(pfs) / sizeof(pfs[0]); i++)
    {
        int win = IsProcessorFeaturePresent(pfs[i].pf) != 0;
        int cpu_has;
        char id[64];
        if (!strcmp(pfs[i].name, "sse2")) cpu_has = (c1.edx >> 26) & 1;
        else if (!strcmp(pfs[i].name, "avx")) cpu_has = avx_usable;
        else if (!strcmp(pfs[i].name, "avx2")) cpu_has = avx_usable && has_feature("avx2", max_basic, max_ext);
        else if (!strcmp(pfs[i].name, "avx512f")) cpu_has = has_feature("avx512f", max_basic, max_ext) && (xcr0 & 0xe6) == 0xe6;
        else cpu_has = has_feature(pfs[i].name, max_basic, max_ext);
        sprintf(id, "win.pf.%s", pfs[i].name);
        if (pfs[i].classic) fid_check(win == cpu_has, id, "IsProcessorFeaturePresent(%u)=%d, CPUID says %d", pfs[i].pf, win, cpu_has);
        else fid_info(id, "IsProcessorFeaturePresent(%u)=%d, CPUID says %d%s", pfs[i].pf, win, cpu_has, win == cpu_has ? "" : " (differs)");
    }

    GetSystemInfo(&si);
    GetNativeSystemInfo(&nsi);
    fid_info("win.sysinfo", "arch %u level %u revision %#x cpus %lu, native arch %u", si.wProcessorArchitecture,
             si.wProcessorLevel, si.wProcessorRevision, si.dwNumberOfProcessors, nsi.wProcessorArchitecture);
    fid_check(si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64, "win.sysinfo.arch", "wProcessorArchitecture %u",
              si.wProcessorArchitecture);
    family = (c1.eax >> 8) & 0xf;
    model = (c1.eax >> 4) & 0xf;
    stepping = c1.eax & 0xf;
    if (family == 0xf) family += (c1.eax >> 20) & 0xff;
    if (family == 0x6 || family >= 0xf) model |= ((c1.eax >> 16) & 0xf) << 4;
    fid_check(si.wProcessorLevel == family && si.wProcessorRevision == ((model << 8) | stepping), "win.sysinfo.cpuid",
              "level %u revision %#x, CPUID family %u model %#x stepping %u", si.wProcessorLevel, si.wProcessorRevision,
              family, model, stepping);
    {
        typedef NTSTATUS (WINAPI *NtQSI_t)(ULONG, void *, ULONG, ULONG *);
        NtQSI_t p = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
        if (p && !p(1 /* SystemProcessorInformation */, &cpu, sizeof(cpu), NULL))
            fid_info("win.cpuinfo", "arch %u level %u revision %#x feature bits %#lx", cpu.arch, cpu.level, cpu.revision, cpu.features);
    }
}

int main(int argc, char **argv)
{
    static DECLSPEC_ALIGN(64) uint8_t buf[8192];
    cpuid_t c0 = cpuidex(0, 0), c1, e0;
    unsigned max_basic = c0.eax, max_ext, i, sub;
    uint64_t xcr0 = 0, supported = 0;
    int osxsave, avx_usable, probe_all = argc > 1 && !strcmp(argv[1], "probe-all");
    char vendor[13];

    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "cpuidtest";
    fid_environment();
    AddVectoredExceptionHandler(1, veh);

    memcpy(vendor, &c0.ebx, 4); memcpy(vendor + 4, &c0.edx, 4); memcpy(vendor + 8, &c0.ecx, 4); vendor[12] = 0;
    fid_info("cpuid.vendor", "%s, max basic leaf %#x", vendor, max_basic);
    e0 = cpuidex(0x80000000, 0);
    max_ext = e0.eax;
    for (i = 0; i <= (max_basic < 0x20 ? max_basic : 0x20); i++)
    {
        if (i == 4 || i == 7 || i == 0xb || i == 0xd || i == 0xf || i == 0x10 || i == 0x12 || i == 0x14 || i == 0x17 || i == 0x18 || i == 0x1f)
        {
            unsigned maxsub = i == 7 ? cpuidex(7, 0).eax : i == 0xd ? 63 : 4;
            for (sub = 0; sub <= maxsub && sub < 64; sub++)
            {
                cpuid_t c = cpuidex(i, sub);
                if (sub > 1 && !c.eax && !c.ebx && !c.ecx && !c.edx) continue;
                dump_leaf(i, sub);
            }
        }
        else dump_leaf(i, 0);
    }
    for (i = 0x80000000; i <= (max_ext < 0x80000020 ? max_ext : 0x80000020); i++) dump_leaf(i, 0);

    c1 = cpuidex(1, 0);
    osxsave = (c1.ecx >> 27) & 1;
    fid_check(!osxsave || ((c1.ecx >> 26) & 1), "cpuid.osxsave_implies_xsave", "OSXSAVE without XSAVE");
    if (osxsave)
    {
        cpuid_t d0 = cpuidex(0xd, 0), d1 = cpuidex(0xd, 1);
        unsigned need = 576, all = 576;
        xcr0 = xgetbv0();
        supported = d0.eax | ((uint64_t)d0.edx << 32);
        fid_info("xcr0", "%#llx, leaf 0xd supported %#llx, size for XCR0 %u, max size %u, leaf 0xd.1 eax %#x",
                 (unsigned long long)xcr0, (unsigned long long)supported, d0.ebx, d0.ecx, d1.eax);
        fid_check((xcr0 & 3) == 3, "xcr0.legacy", "XCR0 %#llx lacks x87|SSE", (unsigned long long)xcr0);
        fid_check((xcr0 & ~supported) == 0, "xcr0.subset", "XCR0 %#llx enables components leaf 0xd does not list (%#llx)",
                  (unsigned long long)xcr0, (unsigned long long)supported);
        for (i = 2; i < 63; i++)
        {
            cpuid_t di;
            if (!((supported >> i) & 1)) continue;
            di = cpuidex(0xd, i);
            if (i == 2) fid_check(di.eax == 256 && di.ebx == 576, "cpuid.d.2", "AVX component size %u offset %u (want 256 576)", di.eax, di.ebx);
            if (!(di.ecx & 1))   /* user components have an offset in the standard form */
            {
                if ((xcr0 >> i) & 1 && di.ebx + di.eax > need) need = di.ebx + di.eax;
                if (di.ebx + di.eax > all) all = di.ebx + di.eax;
            }
        }
        fid_check(d0.ebx == need, "cpuid.d.0.ebx", "size for the enabled XCR0 %u, components add up to %u", d0.ebx, need);
        fid_check(d0.ecx == all, "cpuid.d.0.ecx", "maximum size %u, supported components add up to %u", d0.ecx, all);
    }
    avx_usable = osxsave && ((c1.ecx >> 28) & 1) && (xcr0 & 6) == 6;

    for (i = 0; i < sizeof(features) / sizeof(features[0]); i++)
    {
        const struct feature *f = &features[i];
        int adv = bit_of(f->leaf, f->sub, f->reg, f->bit, max_basic, max_ext);
        int ran;
        char id[64];
        sprintf(id, "feature.%s", f->name);
        if (!adv && !probe_all)
        {
            fid_info(id, "not advertised");   /* Windows on ARM fast-fails some unadvertised instructions */
            continue;
        }
        memset(buf, 0, sizeof(buf));
        ran = run_probe(f->probe, buf) == 0;
        if (adv)
        {
            fid_check(ran, id, "advertised but raised %#lx", g_probe_code);
            if (f->xcr0_needed)
            {
                sprintf(id, "feature.%s.xcr0", f->name);
                fid_check((xcr0 & f->xcr0_needed) == f->xcr0_needed, id, "advertised, XCR0 %#llx lacks %#llx",
                          (unsigned long long)xcr0, (unsigned long long)f->xcr0_needed);
            }
            if (f->needs)
            {
                sprintf(id, "feature.%s.needs", f->name);
                fid_check(has_feature(f->needs, max_basic, max_ext), id, "advertised without %s", f->needs);
            }
        }
        else if (ran) fid_info(id, "not advertised, executes anyway");
        else fid_info(id, "not advertised, raises %#lx", g_probe_code);
    }

    if (osxsave) group_xsave(avx_usable, xcr0, max_basic, max_ext);
    if (avx_usable) group_avx_state();
    group_windows(avx_usable, xcr0, max_basic, max_ext);
    return fid_summary();
}
