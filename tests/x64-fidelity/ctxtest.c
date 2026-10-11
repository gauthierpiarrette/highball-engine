/* ctxtest: exception and CONTEXT fidelity for x86-64 Windows programs.
 *
 * Groups (argv[1], comma separated, default all):
 *   exc      one fault of each kind; the vectored handler records EXCEPTION_RECORD and CONTEXT and
 *            continues at the next instruction without touching anything else
 *   modify   the handler rewrites GPRs, flags, MXCSR, x87 control word, XMM and YMM state and continues
 *   repmovs  std; rep movsb faulting into an uncommitted page, resumed after committing it
 *   nested   an int3 inside the handler of a read fault
 *   repeat   1000 faults of three kinds with changing patterns
 *   seh      frame-based handler + RtlUnwindEx restoring nonvolatile GPRs and XMM6-15
 *   capture  RtlCaptureContext and RtlRestoreContext
 *   thread   GetThreadContext/SetThreadContext on a suspended thread
 *   stress   suspend/resume a thread that checks its own x87, MXCSR, DF and YMM state
 *
 * Expectations: Intel SDM and the Windows x64 exception model (KiUserExceptionDispatcher, NtContinue,
 * RtlUnwindEx). Built with llvm-mingw: x86_64-w64-mingw32-clang -O2 ctxtest.c ctx_asm.S -o ctxtest.exe */
#include "fid.h"
#include <excpt.h>
#include <stddef.h>
#include <intrin.h>

typedef struct DECLSPEC_ALIGN(64) regstate
{
    uint64_t gpr[16];        /* 0x000 rax rcx rdx rbx rsp rbp rsi rdi r8..r15 */
    uint64_t rflags;         /* 0x080 */
    uint64_t rip;            /* 0x088 */
    uint32_t mxcsr;          /* 0x090 */
    uint16_t fcw;            /* 0x094 */
    uint16_t pad0;
    uint64_t pad1;
    uint8_t  ymm[16][32];    /* 0x0a0 */
    uint8_t  x87[3][16];     /* 0x2a0 pushed in order 0,1,2: ST0 = x87[2] */
    uint8_t  pad2[16];
    uint8_t  fx[512];        /* 0x2e0 fxsave64 image */
} regstate;

_Static_assert(offsetof(regstate, rflags) == 0x80, "layout");
_Static_assert(offsetof(regstate, mxcsr) == 0x90, "layout");
_Static_assert(offsetof(regstate, fcw) == 0x94, "layout");
_Static_assert(offsetof(regstate, ymm) == 0xa0, "layout");
_Static_assert(offsetof(regstate, x87) == 0x2a0, "layout");
_Static_assert(offsetof(regstate, fx) == 0x2e0, "layout");

/* shared with ctx_asm.S */
regstate g_before, g_after, g_outer, g_inner;
uint64_t g_saved_rsp, g_spin_saved_rsp;
void *g_fault_target;
uint8_t g_has_avx;
uint16_t g_fcw_default = 0x27f;
uint32_t g_mxcsr_default = 0x1f80;
uint64_t g_clobber_rflags;
DECLSPEC_ALIGN(32) uint8_t g_clobber_vec[32];
DECLSPEC_ALIGN(16) uint8_t g_clobber_x87[16];
CONTEXT *g_capture_ctx;
uint64_t g_capture_pad = 0x20;
void *g_pRtlCaptureContext, *g_pRtlRestoreContext;
volatile LONG g_spin_ready, g_stress_stop;
volatile uint64_t g_stress_iter, g_stress_bad;
uint16_t g_stress_fcw;
DECLSPEC_ALIGN(16) uint8_t g_stress_st0[16];
uint32_t g_stress_mxcsr;
DECLSPEC_ALIGN(16) uint8_t g_stress_vec[16];

extern void run_scenario(void), handler_clobber(void), seh_outer(void), nested_int3(void);
extern void capture_probe(CONTEXT *), restore_probe(CONTEXT *);
extern uint64_t read_rflags(void);
extern DWORD WINAPI spin_thread(void *), stress_thread(void *);
extern char stub_int3[], stub_int3_end[], stub_int3_long[], stub_int3_long_end[], stub_ud2[], stub_ud2_end[];
extern char stub_div0[], stub_div0_end[], stub_idivovf[], stub_idivovf_end[], stub_read[], stub_read_end[];
extern char stub_write[], stub_write_end[], stub_exec[], stub_exec_end[], stub_hlt[], stub_hlt_end[];
extern char stub_in[], stub_in_end[], stub_split[], stub_split_end[], stub_noncanon[], stub_noncanon_end[];
extern char stub_movaps[], stub_movaps_end[], stub_repmovs[], stub_repmovs_end[], stub_tf[], stub_tf_after[];
extern char scenario_resume[], seh_outer_catch[], seh_inner_fault[], capture_ret[], spin_loop[], spin_exit[];
extern char nested_int3_end[];

#define FLAGS_MASK 0xcd5u  /* CF PF AF ZF SF DF OF */
#define DF 0x400u
#define TF 0x100u

static DWORD64 (WINAPI *pGetEnabledXStateFeatures)(void);
static BOOL (WINAPI *pInitializeContext)(void *, DWORD, CONTEXT **, DWORD *);
static BOOL (WINAPI *pSetXStateFeaturesMask)(CONTEXT *, DWORD64);
static BOOL (WINAPI *pGetXStateFeaturesMask)(CONTEXT *, DWORD64 *);
static void *(WINAPI *pLocateXStateFeature)(CONTEXT *, DWORD, DWORD *);
static void (WINAPI *pRtlUnwindEx)(void *, void *, EXCEPTION_RECORD *, void *, CONTEXT *, void *);

/* ---------------------------------------------------------------- patterns */

static void set_x87_value(uint8_t *dst, uint64_t mant, uint16_t sexp)
{
    memset(dst, 0, 16);
    memcpy(dst, &mant, 8);
    memcpy(dst + 8, &sexp, 2);
}

static void make_pattern(regstate *r, unsigned seed, uint32_t flags, uint32_t mxcsr, uint16_t fcw)
{
    unsigned i, j;
    memset(r, 0, sizeof(*r));
    for (i = 0; i < 16; i++)
        r->gpr[i] = 0xa500000000000000ull ^ ((uint64_t)(i + 1) << 48) ^ ((uint64_t)(seed & 0xffff) << 16) ^ (i * 0x1111);
    r->gpr[4] = 0;
    r->rflags = 0x2 | flags;
    r->mxcsr = mxcsr;
    r->fcw = fcw;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 32; j++) r->ymm[i][j] = (uint8_t)(0x10 * i + j + seed * 7 + 1);
    set_x87_value(r->x87[0], 0xc000000000000000ull, 0x3fff);                 /* 1.5 */
    set_x87_value(r->x87[1], 0x9000000000000000ull, 0xc000);                 /* -2.25 */
    set_x87_value(r->x87[2], 0x8000000000000000ull | seed, 0x4005);
}

/* ---------------------------------------------------------------- handler */

enum mode { MODE_PLAIN, MODE_MODIFY, MODE_COMMIT, MODE_TF, MODE_EXEC, MODE_NESTED };

static volatile int g_armed, g_mode, g_depth, g_calls;
static void *g_resume_rip;
static regstate g_mod;
static void *g_commit_lo, *g_commit_hi;

typedef struct seen
{
    int valid;
    EXCEPTION_RECORD rec;
    DECLSPEC_ALIGN(16) CONTEXT ctx;
    int has_ymmh;
    DWORD64 xmask;
    uint8_t ymmh[16][16];
    uint64_t entry_flags;
    uint32_t entry_mxcsr;
    uint16_t entry_fcw, entry_fsw;
} seen;
static seen g_seen, g_seen_inner;

static void record(seen *s, EXCEPTION_POINTERS *ep, uint64_t entry_flags)
{
    void *ymmh;
    DWORD len = 0;
    s->valid = 1;
    s->rec = *ep->ExceptionRecord;
    s->ctx = *ep->ContextRecord;
    s->entry_flags = entry_flags;
    __asm__ volatile("stmxcsr %0; fnstcw %1; fnstsw %2" : "=m"(s->entry_mxcsr), "=m"(s->entry_fcw), "=m"(s->entry_fsw));
    s->has_ymmh = 0;
    s->xmask = 0;
    if ((ep->ContextRecord->ContextFlags & 0x40) && pLocateXStateFeature &&
        (ymmh = pLocateXStateFeature(ep->ContextRecord, XSTATE_AVX, &len)) && len >= 256)
    {
        s->has_ymmh = 1;
        memcpy(s->ymmh, ymmh, 256);
        if (pGetXStateFeaturesMask) pGetXStateFeaturesMask(ep->ContextRecord, &s->xmask);
    }
}

static void apply_modification(CONTEXT *ctx)
{
    unsigned i;
    void *ymmh;
    DWORD len = 0;
    for (i = 0; i < 16; i++) if (i != 4) (&ctx->Rax)[i] = g_mod.gpr[i];
    ctx->EFlags = (ctx->EFlags & ~FLAGS_MASK) | (g_mod.rflags & FLAGS_MASK);
    ctx->MxCsr = g_mod.mxcsr;
    ctx->FltSave.MxCsr = g_mod.mxcsr;
    ctx->FltSave.ControlWord = g_mod.fcw;
    for (i = 0; i < 16; i++) memcpy(&ctx->FltSave.XmmRegisters[i], g_mod.ymm[i], 16);
    if ((ctx->ContextFlags & 0x40) && pLocateXStateFeature &&
        (ymmh = pLocateXStateFeature(ctx, XSTATE_AVX, &len)) && len >= 256)
    {
        DWORD64 mask = 0;
        for (i = 0; i < 16; i++) memcpy((uint8_t *)ymmh + 16 * i, g_mod.ymm[i] + 16, 16);
        if (pGetXStateFeaturesMask) pGetXStateFeaturesMask(ctx, &mask);
        if (pSetXStateFeaturesMask) pSetXStateFeaturesMask(ctx, mask | XSTATE_MASK_AVX);
    }
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    uint64_t entry_flags;
    CONTEXT *ctx = ep->ContextRecord;
    EXCEPTION_RECORD *rec = ep->ExceptionRecord;

    /* first thing: DF must be clear for handlers; clear it before any C code could use string ops */
    __asm__ volatile("pushfq; popq %0; cld" : "=r"(entry_flags));
    if (!g_armed) return EXCEPTION_CONTINUE_SEARCH;
    if (++g_calls > 64) { ctx->Rip = (DWORD64)g_resume_rip; return EXCEPTION_CONTINUE_EXECUTION; }

    if (g_depth > 0)   /* the nested int3 */
    {
        record(&g_seen_inner, ep, entry_flags);
        ctx->Rip = (DWORD64)nested_int3_end;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    switch (g_mode)
    {
    case MODE_COMMIT:
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2 &&
            (void *)rec->ExceptionInformation[1] >= g_commit_lo && (void *)rec->ExceptionInformation[1] < g_commit_hi)
        {
            if (!g_seen.valid) record(&g_seen, ep, entry_flags);
            VirtualAlloc((void *)(rec->ExceptionInformation[1] & ~0xfffull), 0x1000, MEM_COMMIT, PAGE_READWRITE);
            handler_clobber();
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        record(&g_seen, ep, entry_flags);
        ctx->Rip = (DWORD64)g_resume_rip;
        return EXCEPTION_CONTINUE_EXECUTION;
    case MODE_TF:
        record(&g_seen, ep, entry_flags);
        ctx->EFlags &= ~TF;
        handler_clobber();
        return EXCEPTION_CONTINUE_EXECUTION;
    case MODE_EXEC:
        record(&g_seen, ep, entry_flags);
        ctx->Rip = *(DWORD64 *)ctx->Rsp;
        ctx->Rsp += 8;
        handler_clobber();
        return EXCEPTION_CONTINUE_EXECUTION;
    case MODE_NESTED:
        record(&g_seen, ep, entry_flags);
        g_depth++;
        nested_int3();
        g_depth--;
        ctx->Rip = (DWORD64)g_resume_rip;
        handler_clobber();
        return EXCEPTION_CONTINUE_EXECUTION;
    case MODE_MODIFY:
        record(&g_seen, ep, entry_flags);
        apply_modification(ctx);
        ctx->Rip = (DWORD64)g_resume_rip;
        handler_clobber();
        return EXCEPTION_CONTINUE_EXECUTION;
    default:
        record(&g_seen, ep, entry_flags);
        ctx->Rip = (DWORD64)g_resume_rip;
        handler_clobber();
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *ep)
{
    printf("FAIL unhandled: code %#lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    fid_summary();
    fflush(stdout);
    ExitProcess(254);
}

/* ---------------------------------------------------------------- comparisons */

static char g_why[256];

static int cmp_gprs(const uint64_t *got, const uint64_t *want, unsigned mask)
{
    static const char *names[16] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
    unsigned i;
    for (i = 0; i < 16; i++)
        if ((mask & (1u << i)) && got[i] != want[i])
        {
            sprintf(g_why, "%s got %#llx want %#llx", names[i], (unsigned long long)got[i], (unsigned long long)want[i]);
            return 0;
        }
    return 1;
}

static int cmp_vec(const uint8_t *got, const uint8_t *want, unsigned n, unsigned stride_got, unsigned stride_want,
                   unsigned bytes, const char *what)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (memcmp(got + i * stride_got, want + i * stride_want, bytes))
        {
            char g[65], w[65];
            fid_hex(g, got + i * stride_got, bytes);
            fid_hex(w, want + i * stride_want, bytes);
            sprintf(g_why, "%s%u got %s want %s", what, i, g, w);
            return 0;
        }
    return 1;
}

#define NONVOL_GPRS 0xf0e8u   /* rbx rbp rsi rdi r12-r15 */
#define ALL_BUT_RSP 0xffefu

/* checks a CONTEXT a handler received or GetThreadContext returned against the loaded state */
static void check_context(const char *id, const CONTEXT *c, const regstate *want, const void *want_rip,
                          uint64_t want_rsp, unsigned gpr_mask, int has_ymmh, const uint8_t (*ymmh)[16])
{
    char name[128];
    unsigned i, top;
    uint8_t want_xmm[16][16];

    sprintf(name, "%s.ctx.flags", id);
    fid_check((c->ContextFlags & 0x10000b) == 0x10000b, name, "ContextFlags %#lx lacks control/integer/fp", c->ContextFlags);
    fid_info(name, "ContextFlags %#lx", c->ContextFlags);
    if (want_rip)
    {
        sprintf(name, "%s.ctx.rip", id);
        CHECK_EQ64(name, c->Rip, (uintptr_t)want_rip);
    }
    sprintf(name, "%s.ctx.rsp", id);
    CHECK_EQ64(name, c->Rsp, want_rsp);
    sprintf(name, "%s.ctx.gpr", id);
    fid_check(cmp_gprs(&c->Rax, want->gpr, gpr_mask), name, "%s", g_why);
    sprintf(name, "%s.ctx.eflags", id);
    fid_check((c->EFlags & FLAGS_MASK) == (want->rflags & FLAGS_MASK) && !(c->EFlags & TF) && (c->EFlags & 2), name,
              "EFlags %#lx want %#llx (mask %#x)", c->EFlags, (unsigned long long)want->rflags, FLAGS_MASK);
    sprintf(name, "%s.ctx.eflags", id);
    fid_info(name, "EFlags %#lx", c->EFlags);
    sprintf(name, "%s.ctx.segs", id);
    fid_check(c->SegCs == 0x33 && c->SegSs == 0x2b, name, "cs %#x ss %#x", c->SegCs, c->SegSs);
    fid_info(name, "cs %#x ds %#x es %#x fs %#x gs %#x ss %#x", c->SegCs, c->SegDs, c->SegEs, c->SegFs, c->SegGs, c->SegSs);
    sprintf(name, "%s.ctx.mxcsr", id);
    fid_check(c->MxCsr == want->mxcsr && c->FltSave.MxCsr == want->mxcsr, name, "MxCsr %#lx FltSave.MxCsr %#lx want %#x",
              c->MxCsr, c->FltSave.MxCsr, want->mxcsr);
    sprintf(name, "%s.ctx.fcw", id);
    fid_check(c->FltSave.ControlWord == want->fcw, name, "ControlWord %#x want %#x", c->FltSave.ControlWord, want->fcw);
    top = (c->FltSave.StatusWord >> 11) & 7;
    sprintf(name, "%s.ctx.x87top", id);
    fid_check(top == 5, name, "StatusWord %#x (TOP %u) want TOP 5", c->FltSave.StatusWord, top);
    sprintf(name, "%s.ctx.x87tag", id);
    fid_check(c->FltSave.TagWord == 0xe0, name, "TagWord %#x want 0xe0", c->FltSave.TagWord);
    sprintf(name, "%s.ctx.x87regs", id);
    {
        int ok = 1;
        for (i = 0; i < 3 && ok; i++)
            ok = cmp_vec((const uint8_t *)&c->FltSave.FloatRegisters[i], want->x87[2 - i], 1, 16, 16, 10, "st");
        fid_check(ok, name, "%s", g_why);
    }
    for (i = 0; i < 16; i++) memcpy(want_xmm[i], want->ymm[i], 16);
    sprintf(name, "%s.ctx.xmm", id);
    fid_check(cmp_vec((const uint8_t *)c->FltSave.XmmRegisters, (const uint8_t *)want_xmm, 16, 16, 16, 16, "xmm"), name, "%s", g_why);
    if (g_has_avx)
    {
        sprintf(name, "%s.ctx.ymmh", id);
        if (has_ymmh)
            fid_check(cmp_vec((const uint8_t *)ymmh, want->ymm[0] + 16, 16, 16, 32, 16, "ymmh"), name, "%s", g_why);
        else
            fid_check(0, name, "no AVX state in the context (ContextFlags %#lx)", c->ContextFlags);
    }
}

/* checks the state found at the resume point against what the thread should be running with */
static void check_after(const char *id, const regstate *got, const regstate *want, uint64_t want_rsp, unsigned gpr_mask,
                        int check_ymmh, int want_top, uint8_t want_tag, const uint8_t (*want_st)[16])
{
    char name[128];
    unsigned i;
    uint16_t fsw = *(uint16_t *)(got->fx + 2);
    uint8_t ftw = got->fx[4];

    sprintf(name, "%s.after.rsp", id);
    CHECK_EQ64(name, got->gpr[4], want_rsp);
    sprintf(name, "%s.after.gpr", id);
    fid_check(cmp_gprs(got->gpr, want->gpr, gpr_mask), name, "%s", g_why);
    sprintf(name, "%s.after.eflags", id);
    fid_check((got->rflags & FLAGS_MASK) == (want->rflags & FLAGS_MASK) && !(got->rflags & TF), name,
              "rflags %#llx want %#llx (mask %#x)", (unsigned long long)got->rflags, (unsigned long long)want->rflags, FLAGS_MASK);
    sprintf(name, "%s.after.mxcsr", id);
    fid_check(got->mxcsr == want->mxcsr, name, "mxcsr %#x want %#x", got->mxcsr, want->mxcsr);
    sprintf(name, "%s.after.fcw", id);
    fid_check(got->fcw == want->fcw, name, "fcw %#x want %#x", got->fcw, want->fcw);
    sprintf(name, "%s.after.x87", id);
    {
        int ok = ((fsw >> 11) & 7) == (unsigned)want_top && ftw == want_tag;
        if (!ok) sprintf(g_why, "fsw %#x (TOP %u) ftw %#x, want TOP %d ftw %#x", fsw, (fsw >> 11) & 7, ftw, want_top, want_tag);
        for (i = 0; ok && i < (unsigned)(8 - want_top); i++)
            ok = cmp_vec(got->fx + 32 + 16 * i, want_st[i], 1, 16, 16, 10, "st");
        fid_check(ok, name, "%s", g_why);
    }
    sprintf(name, "%s.after.xmm", id);
    fid_check(cmp_vec(got->ymm[0], want->ymm[0], 16, 32, 32, 16, "xmm"), name, "%s", g_why);
    if (g_has_avx && check_ymmh)
    {
        sprintf(name, "%s.after.ymmh", id);
        fid_check(cmp_vec(got->ymm[0] + 16, want->ymm[0] + 16, 16, 32, 32, 16, "ymmh"), name, "%s", g_why);
    }
}

static void check_entry(const char *id, const seen *s)
{
    char name[128];
    sprintf(name, "%s.handler.df", id);
    fid_check(!(s->entry_flags & DF), name, "handler entered with DF set (rflags %#llx)", (unsigned long long)s->entry_flags);
    sprintf(name, "%s.handler.tf", id);
    fid_check(!(s->entry_flags & TF), name, "handler entered with TF set (rflags %#llx)", (unsigned long long)s->entry_flags);
    sprintf(name, "%s.handler.state", id);
    fid_info(name, "rflags %#llx mxcsr %#x fcw %#x fsw %#x", (unsigned long long)s->entry_flags, s->entry_mxcsr,
             s->entry_fcw, s->entry_fsw);
}

static void check_record(const char *id, const EXCEPTION_RECORD *r, DWORD code, const void *addr, DWORD nparams,
                         ULONG_PTR p0, ULONG_PTR p1)
{
    char name[128];
    sprintf(name, "%s.rec.code", id);
    CHECK_EQ64(name, r->ExceptionCode, code);
    sprintf(name, "%s.rec.flags", id);
    CHECK_EQ64(name, r->ExceptionFlags, 0);
    sprintf(name, "%s.rec.address", id);
    CHECK_EQ64(name, (uintptr_t)r->ExceptionAddress, (uintptr_t)addr);
    sprintf(name, "%s.rec.params", id);
    if (nparams == 0) fid_check(r->NumberParameters == 0, name, "NumberParameters %lu", r->NumberParameters);
    else if (nparams == 1)
        fid_check(r->NumberParameters == 1 && r->ExceptionInformation[0] == p0, name, "n %lu [0] %#llx want 1, %#llx",
                  r->NumberParameters, (unsigned long long)r->ExceptionInformation[0], (unsigned long long)p0);
    else
        fid_check(r->NumberParameters == 2 && r->ExceptionInformation[0] == p0 && r->ExceptionInformation[1] == p1, name,
                  "n %lu [0] %#llx [1] %#llx want 2, %#llx, %#llx", r->NumberParameters,
                  (unsigned long long)r->ExceptionInformation[0], (unsigned long long)r->ExceptionInformation[1],
                  (unsigned long long)p0, (unsigned long long)p1);
    sprintf(name, "%s.rec", id);
    fid_info(name, "code %#lx flags %#lx address %p n %lu [0] %#llx [1] %#llx", r->ExceptionCode, r->ExceptionFlags,
             r->ExceptionAddress, r->NumberParameters, (unsigned long long)r->ExceptionInformation[0],
             (unsigned long long)r->ExceptionInformation[1]);
}

/* ---------------------------------------------------------------- scenarios */

static void init_clobber(void)
{
    memset(g_clobber_vec, 0xee, sizeof(g_clobber_vec));
    set_x87_value(g_clobber_x87, 0xdeadbeefcafef00dull, 0x7ffe);
    g_clobber_rflags = 0x2 | (~g_before.rflags & 0x8d5);
}

static int run(void *target, void *resume, int mode)
{
    g_fault_target = target;
    g_resume_rip = resume;
    g_mode = mode;
    g_calls = 0;
    g_seen.valid = 0;
    g_seen_inner.valid = 0;
    memset(&g_after, 0, sizeof(g_after));
    init_clobber();
    g_armed = 1;
    run_scenario();
    g_armed = 0;
    return g_seen.valid;
}

static const uint8_t (*st_of(const regstate *r, uint8_t (*buf)[16]))[16]
{
    memcpy(buf[0], r->x87[2], 16);
    memcpy(buf[1], r->x87[1], 16);
    memcpy(buf[2], r->x87[0], 16);
    return (const uint8_t (*)[16])buf;
}

/* one fault kind with the plain continuation (nothing but Rip changed) */
static void exc_case(const char *id, void *stub, void *stub_end, int mode, DWORD code, const void *addr, DWORD nparams,
                     ULONG_PTR p0, ULONG_PTR p1, const void *want_rip, int64_t rsp_adjust, unsigned seed, uint32_t flags)
{
    uint8_t st[3][16];
    regstate want;
    if (!run(stub, stub_end, mode))
    {
        char name[128];
        sprintf(name, "%s.raised", id);
        fid_check(0, name, "no exception reached the handler");
        return;
    }
    check_record(id, &g_seen.rec, code, addr, nparams, p0, p1);
    check_context(id, &g_seen.ctx, &g_before, want_rip, g_before.gpr[4] + rsp_adjust, ALL_BUT_RSP, g_seen.has_ymmh,
                  (const uint8_t (*)[16])g_seen.ymmh);
    check_entry(id, &g_seen);
    want = g_before;
    check_after(id, &g_after, &want, g_before.gpr[4], ALL_BUT_RSP, g_seen.has_ymmh || 1, 5, 0xe0, st_of(&g_before, st));
    (void)seed; (void)flags;
}

static void group_exc(void)
{
    uint8_t *page = VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
    uint8_t *rw = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t *split = VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
    uint32_t fl = 0xc95; /* CF PF AF SF DF OF */
    VirtualAlloc(split, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    rw[0] = 0xc3; /* ret */

    make_pattern(&g_before, 1, fl, 0x3f80, 0x0b7f);
    exc_case("exc.int3", stub_int3, stub_int3_end, MODE_PLAIN, EXCEPTION_BREAKPOINT, stub_int3, 1, 0, 0, stub_int3, 0, 1, fl);

    make_pattern(&g_before, 2, fl, 0x3f80, 0x0b7f);
    exc_case("exc.ud2", stub_ud2, stub_ud2_end, MODE_PLAIN, EXCEPTION_ILLEGAL_INSTRUCTION, stub_ud2, 0, 0, 0, stub_ud2, 0, 2, fl);

    make_pattern(&g_before, 3, 0x046, 0x3f80, 0x0b7f);
    g_before.gpr[1] = 0;                                   /* rcx = 0 */
    exc_case("exc.div0", stub_div0, stub_div0_end, MODE_PLAIN, EXCEPTION_INT_DIVIDE_BY_ZERO, stub_div0, 0, 0, 0, stub_div0, 0, 3, 0x046);

    make_pattern(&g_before, 4, fl, 0x3f80, 0x0b7f);
    g_before.gpr[0] = 0x8000000000000000ull; g_before.gpr[2] = ~0ull; g_before.gpr[1] = ~0ull;  /* INT64_MIN / -1 */
    if (run(stub_idivovf, stub_idivovf_end, MODE_PLAIN))
    {
        fid_info("exc.idivovf.rec", "code %#lx address %p (stub %p)", g_seen.rec.ExceptionCode, g_seen.rec.ExceptionAddress, stub_idivovf);
        check_context("exc.idivovf", &g_seen.ctx, &g_before, stub_idivovf, g_before.gpr[4], ALL_BUT_RSP, g_seen.has_ymmh,
                      (const uint8_t (*)[16])g_seen.ymmh);
    }
    else fid_check(0, "exc.idivovf.raised", "no exception");

    make_pattern(&g_before, 5, fl, 0x3f80, 0x0b7f);
    g_before.gpr[6] = (uintptr_t)page + 0x1008;            /* rsi: reserved, not committed */
    exc_case("exc.read", stub_read, stub_read_end, MODE_PLAIN, EXCEPTION_ACCESS_VIOLATION, stub_read, 2, 0, (ULONG_PTR)page + 0x1008,
             stub_read, 0, 5, fl);

    make_pattern(&g_before, 6, fl, 0x3f80, 0x0b7f);
    g_before.gpr[6] = (uintptr_t)page + 0x2010;
    exc_case("exc.write", stub_write, stub_write_end, MODE_PLAIN, EXCEPTION_ACCESS_VIOLATION, stub_write, 2, 1, (ULONG_PTR)page + 0x2010,
             stub_write, 0, 6, fl);

    make_pattern(&g_before, 7, 0x046, 0x3f80, 0x0b7f);
    g_before.gpr[6] = (uintptr_t)rw;                       /* call into a read-write page: DEP */
    if (run(stub_exec, stub_exec_end, MODE_EXEC))
    {
        check_record("exc.exec", &g_seen.rec, EXCEPTION_ACCESS_VIOLATION, rw, 2, 8, (ULONG_PTR)rw);
        check_context("exc.exec", &g_seen.ctx, &g_before, rw, g_before.gpr[4] - 8, ALL_BUT_RSP, g_seen.has_ymmh,
                      (const uint8_t (*)[16])g_seen.ymmh);
    }
    else fid_check(0, "exc.exec.raised", "code in a PAGE_READWRITE page ran: no DEP fault");

    make_pattern(&g_before, 8, fl, 0x3f80, 0x0b7f);
    exc_case("exc.hlt", stub_hlt, stub_hlt_end, MODE_PLAIN, EXCEPTION_PRIV_INSTRUCTION, stub_hlt, 0, 0, 0, stub_hlt, 0, 8, fl);

    make_pattern(&g_before, 9, fl, 0x3f80, 0x0b7f);
    exc_case("exc.in", stub_in, stub_in_end, MODE_PLAIN, EXCEPTION_PRIV_INSTRUCTION, stub_in, 0, 0, 0, stub_in, 0, 9, fl);

    /* an 8-byte read whose last 4 bytes are on a no-access page: Windows reports the first byte of that page */
    make_pattern(&g_before, 10, fl, 0x3f80, 0x0b7f);
    g_before.gpr[6] = (uintptr_t)split + 0x1000 - 4;
    if (run(stub_split, stub_split_end, MODE_PLAIN))
    {
        fid_info("exc.split.rec", "code %#lx [0] %#llx [1] %#llx (access %p, page %p)", g_seen.rec.ExceptionCode,
                 (unsigned long long)g_seen.rec.ExceptionInformation[0], (unsigned long long)g_seen.rec.ExceptionInformation[1],
                 split + 0x1000 - 4, split + 0x1000);
        fid_check(g_seen.rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                  g_seen.rec.ExceptionInformation[1] == (ULONG_PTR)split + 0x1000, "exc.split.address",
                  "fault address %#llx want %p", (unsigned long long)g_seen.rec.ExceptionInformation[1], split + 0x1000);
        fid_check(g_seen.ctx.Rip == (uintptr_t)stub_split, "exc.split.rip", "Rip %#llx want %p",
                  (unsigned long long)g_seen.ctx.Rip, stub_split);
    }
    else fid_check(0, "exc.split.raised", "no exception");

    /* non-canonical address: #GP, which Windows reports as an access violation at address -1 */
    make_pattern(&g_before, 11, fl, 0x3f80, 0x0b7f);
    g_before.gpr[6] = 0x8000000000001000ull;
    if (run(stub_noncanon, stub_noncanon_end, MODE_PLAIN))
        fid_info("exc.noncanon.rec", "code %#lx n %lu [0] %#llx [1] %#llx address %p", g_seen.rec.ExceptionCode,
                 g_seen.rec.NumberParameters, (unsigned long long)g_seen.rec.ExceptionInformation[0],
                 (unsigned long long)g_seen.rec.ExceptionInformation[1], g_seen.rec.ExceptionAddress);
    else fid_info("exc.noncanon.rec", "no exception (read of a non-canonical address went through)");

    /* movaps from an address aligned to 8 only: #GP on x86 */
    {
        static DECLSPEC_ALIGN(16) uint8_t buf[64];
        make_pattern(&g_before, 12, fl, 0x3f80, 0x0b7f);
        g_before.gpr[6] = (uintptr_t)buf + 8;
        if (run(stub_movaps, stub_movaps_end, MODE_PLAIN))
            fid_info("exc.movaps.rec", "code %#lx n %lu [0] %#llx [1] %#llx", g_seen.rec.ExceptionCode, g_seen.rec.NumberParameters,
                     (unsigned long long)g_seen.rec.ExceptionInformation[0], (unsigned long long)g_seen.rec.ExceptionInformation[1]);
        else fid_info("exc.movaps.rec", "no exception (misaligned movaps went through)");
    }

    /* int $3 (CD 03): Windows reports the byte after the opcode */
    make_pattern(&g_before, 13, fl, 0x3f80, 0x0b7f);
    if (run(stub_int3_long, stub_int3_long_end, MODE_PLAIN))
        fid_info("exc.int3long.rec", "code %#lx address %+lld rip %+lld (relative to the instruction)", g_seen.rec.ExceptionCode,
                 (long long)((char *)g_seen.rec.ExceptionAddress - stub_int3_long), (long long)((char *)g_seen.ctx.Rip - stub_int3_long));
    else fid_check(0, "exc.int3long.raised", "no exception");

    /* single step: trap after the nop that follows popfq */
    make_pattern(&g_before, 14, 0x0846, 0x3f80, 0x0b7f);
    if (run(stub_tf, stub_tf_after, MODE_TF))
    {
        uint8_t st[3][16];
        check_record("exc.tf", &g_seen.rec, EXCEPTION_SINGLE_STEP, stub_tf_after, 0, 0, 0);
        fid_info("exc.tf.ctx", "Rip %+lld EFlags %#lx Dr6 %#llx", (long long)((char *)g_seen.ctx.Rip - stub_tf_after),
                 g_seen.ctx.EFlags, (unsigned long long)g_seen.ctx.Dr6);
        check_after("exc.tf", &g_after, &g_before, g_before.gpr[4], ALL_BUT_RSP, 1, 5, 0xe0, st_of(&g_before, st));
    }
    else fid_check(0, "exc.tf.raised", "no single-step exception");

    VirtualFree(page, 0, MEM_RELEASE);
    VirtualFree(rw, 0, MEM_RELEASE);
    VirtualFree(split, 0, MEM_RELEASE);
}

static void group_modify(void)
{
    static const char *ids[2] = { "modify.read", "modify.int3" };
    uint8_t *page = VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    unsigned k;
    for (k = 0; k < 2; k++)
    {
        uint8_t st[3][16];
        make_pattern(&g_before, 20 + k, 0xc95, 0x3f80, 0x0b7f);
        make_pattern(&g_mod, 40 + k, 0x046, 0x5f80, 0x067f);
        g_mod.gpr[4] = 0;
        if (k == 0) g_before.gpr[6] = (uintptr_t)page + 0x10;
        if (!run(k == 0 ? (void *)stub_read : (void *)stub_int3, k == 0 ? (void *)stub_read_end : (void *)stub_int3_end, MODE_MODIFY))
        {
            char name[64];
            sprintf(name, "%s.raised", ids[k]);
            fid_check(0, name, "no exception");
            continue;
        }
        check_entry(ids[k], &g_seen);
        /* the x87 stack is not modified by the handler: it must still be the loaded one */
        check_after(ids[k], &g_after, &g_mod, g_before.gpr[4], ALL_BUT_RSP, g_seen.has_ymmh, 5, 0xe0, st_of(&g_before, st));
        if (!g_seen.has_ymmh) fid_info(ids[k], "no AVX state in the handler's context, YMM upper halves not modified");
    }
    VirtualFree(page, 0, MEM_RELEASE);
}

/* std; rep movsb backwards from a committed page into an uncommitted one; the handler commits it */
static void group_repmovs(void)
{
    uint8_t *dst = VirtualAlloc(NULL, 0x3000, MEM_RESERVE, PAGE_NOACCESS);
    uint8_t *src = VirtualAlloc(NULL, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const size_t n = 0x2000 + 100;     /* page 2, page 1 and the last 100 bytes of page 0 */
    size_t i, bad = 0;
    uint8_t st[3][16];
    regstate want;

    VirtualAlloc(dst + 0x1000, 0x2000, MEM_COMMIT, PAGE_READWRITE);
    for (i = 0; i < 0x3000; i++) src[i] = (uint8_t)(i * 7 + 3);
    make_pattern(&g_before, 30, 0xc95 | DF, 0x3f80, 0x0b7f);
    g_before.gpr[6] = (uintptr_t)src + 0x3000 - 1;      /* rsi */
    g_before.gpr[7] = (uintptr_t)dst + 0x3000 - 1;      /* rdi */
    g_before.gpr[1] = n;                                /* rcx */
    g_commit_lo = dst;
    g_commit_hi = dst + 0x1000;
    run(stub_repmovs, stub_repmovs_end, MODE_COMMIT);
    /* the instruction restarts at stub_repmovs after the commit and runs to its end */
    fid_check(g_seen.valid, "repmovs.raised", "no fault on the uncommitted page");
    if (g_seen.valid)
    {
        check_record("repmovs", &g_seen.rec, EXCEPTION_ACCESS_VIOLATION, stub_repmovs, 2, 1, (ULONG_PTR)dst + 0xfff);
        fid_info("repmovs.ctx", "rcx %#llx rsi %+lld rdi %+lld EFlags %#lx", (unsigned long long)g_seen.ctx.Rcx,
                 (long long)(g_seen.ctx.Rsi - (uintptr_t)src), (long long)(g_seen.ctx.Rdi - (uintptr_t)dst), g_seen.ctx.EFlags);
        fid_check(g_seen.ctx.EFlags & DF, "repmovs.ctx.df", "EFlags %#lx: DF missing in the handler's context", g_seen.ctx.EFlags);
        check_entry("repmovs", &g_seen);
    }
    for (i = 0; i < n; i++) if (dst[0x3000 - 1 - i] != src[0x3000 - 1 - i]) bad++;
    fid_check(!bad, "repmovs.data", "%zu of %zu bytes wrong after the resumed backward copy", bad, n);
    want = g_before;
    want.gpr[1] = 0;
    want.gpr[6] = (uintptr_t)src + 0x3000 - 1 - n;
    want.gpr[7] = (uintptr_t)dst + 0x3000 - 1 - n;
    check_after("repmovs", &g_after, &want, g_before.gpr[4], ALL_BUT_RSP, 1, 5, 0xe0, st_of(&g_before, st));
    VirtualFree(dst, 0, MEM_RELEASE);
    VirtualFree(src, 0, MEM_RELEASE);
}

static void group_nested(void)
{
    uint8_t *page = VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    uint8_t st[3][16];
    make_pattern(&g_before, 50, 0xc95, 0x3f80, 0x0b7f);
    g_before.gpr[6] = (uintptr_t)page + 0x20;
    if (!run(stub_read, stub_read_end, MODE_NESTED)) { fid_check(0, "nested.raised", "no exception"); return; }
    fid_check(g_seen_inner.valid, "nested.inner", "the int3 inside the handler did not reach the handler");
    if (g_seen_inner.valid)
    {
        check_record("nested.inner", &g_seen_inner.rec, EXCEPTION_BREAKPOINT, nested_int3, 1, 0, 0);
        CHECK_EQ64("nested.inner.ctx.rip", g_seen_inner.ctx.Rip, (uintptr_t)nested_int3);
    }
    check_record("nested.outer", &g_seen.rec, EXCEPTION_ACCESS_VIOLATION, stub_read, 2, 0, (ULONG_PTR)page + 0x20);
    check_after("nested", &g_after, &g_before, g_before.gpr[4], ALL_BUT_RSP, 1, 5, 0xe0, st_of(&g_before, st));
    VirtualFree(page, 0, MEM_RELEASE);
}

static void group_repeat(unsigned count)
{
    uint8_t *page = VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    unsigned i, bad = 0, first = ~0u;
    char firstwhy[300] = "";
    for (i = 0; i < count; i++)
    {
        void *stubs[3] = { stub_read, stub_int3, stub_ud2 }, *ends[3] = { stub_read_end, stub_int3_end, stub_ud2_end };
        int modify = i & 1, ok;
        uint32_t fl = (i & 2) ? 0xc95 : 0x046;
        make_pattern(&g_before, 100 + i, fl, (i & 4) ? 0x3f80 : 0x1f80, (i & 8) ? 0x0b7f : 0x027f);
        make_pattern(&g_mod, 5000 + i, fl ^ 0x8d1, 0x5f80, 0x067f);
        g_mod.gpr[4] = 0;
        g_before.gpr[6] = (uintptr_t)page + (i & 0xff) * 8;
        if (!run(stubs[i % 3], ends[i % 3], modify ? MODE_MODIFY : MODE_PLAIN)) { ok = 0; strcpy(g_why, "no exception"); }
        else
        {
            const regstate *want = modify ? &g_mod : &g_before;
            uint16_t fsw = *(uint16_t *)(g_after.fx + 2);
            ok = cmp_gprs(g_after.gpr, want->gpr, ALL_BUT_RSP);
            if (ok && (g_after.rflags & FLAGS_MASK) != (want->rflags & FLAGS_MASK))
            { ok = 0; sprintf(g_why, "rflags %#llx want %#llx", (unsigned long long)g_after.rflags, (unsigned long long)want->rflags); }
            if (ok && (g_after.mxcsr != want->mxcsr || g_after.fcw != want->fcw))
            { ok = 0; sprintf(g_why, "mxcsr %#x fcw %#x want %#x %#x", g_after.mxcsr, g_after.fcw, want->mxcsr, want->fcw); }
            if (ok && ((fsw >> 11) & 7) != 5) { ok = 0; sprintf(g_why, "x87 TOP %u want 5", (fsw >> 11) & 7); }
            if (ok) ok = cmp_vec(g_after.ymm[0], want->ymm[0], 16, 32, 32, 16, "xmm");
        }
        if (!ok)
        {
            bad++;
            if (first == ~0u) { first = i; snprintf(firstwhy, sizeof(firstwhy), "%s", g_why); }
        }
    }
    fid_check(!bad, "repeat.all", "%u of %u iterations wrong, first %u: %s", bad, count, first, firstwhy);
    VirtualFree(page, 0, MEM_RELEASE);
}

/* ---------------------------------------------------------------- SEH unwinding */

static volatile int g_seh_called;
static EXCEPTION_RECORD g_seh_rec;
static DECLSPEC_ALIGN(16) CONTEXT g_seh_ctx, g_seh_unwind_ctx;
static void *g_seh_frame;
static ULONG64 g_seh_controlpc;

EXCEPTION_DISPOSITION seh_outer_handler(EXCEPTION_RECORD *rec, void *frame, CONTEXT *ctx, DISPATCHER_CONTEXT *disp)
{
    if (rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
    g_seh_called++;
    g_seh_rec = *rec;
    g_seh_ctx = *ctx;
    g_seh_frame = frame;
    g_seh_controlpc = disp->ControlPc;
    pRtlUnwindEx(frame, seh_outer_catch, rec, NULL, &g_seh_unwind_ctx, disp->HistoryTable);
    return ExceptionContinueSearch;
}

static void group_seh(void)
{
    uint64_t nonvol_want[16];
    unsigned i;
    char why[300];
    make_pattern(&g_outer, 60, 0x046, 0x1f80, 0x027f);
    make_pattern(&g_inner, 61, 0x046, 0x1f80, 0x027f);
    g_seh_called = 0;
    memset(&g_after, 0, sizeof(g_after));
    seh_outer();
    fid_check(g_seh_called == 1, "seh.handler", "language handler called %d times", g_seh_called);
    if (!g_seh_called) return;
    CHECK_EQ64("seh.rec.code", g_seh_rec.ExceptionCode, EXCEPTION_ILLEGAL_INSTRUCTION);
    CHECK_EQ64("seh.ctx.rip", g_seh_ctx.Rip, (uintptr_t)seh_inner_fault);
    CHECK_EQ64("seh.frame", (uintptr_t)g_seh_frame, g_saved_rsp);
    fid_info("seh.controlpc", "%+lld relative to seh_outer_catch", (long long)(g_seh_controlpc - (uintptr_t)seh_outer_catch));
    /* the handler's context holds the inner values (live at the fault) */
    fid_check(cmp_gprs(&g_seh_ctx.Rax, g_inner.gpr, NONVOL_GPRS), "seh.ctx.gpr", "%s", g_why);
    fid_check(cmp_vec((const uint8_t *)&g_seh_ctx.FltSave.XmmRegisters[6], g_inner.ymm[6], 10, 16, 32, 16, "xmm6+"),
              "seh.ctx.xmm", "%s", g_why);
    /* after RtlUnwindEx to seh_outer_catch: outer values, restored from seh_inner's prologue saves */
    for (i = 0; i < 16; i++) nonvol_want[i] = g_outer.gpr[i];
    fid_check(cmp_gprs(g_after.gpr, nonvol_want, NONVOL_GPRS), "seh.after.gpr", "%s", g_why);
    strcpy(why, "");
    fid_check(cmp_vec(g_after.ymm[6], g_outer.ymm[6], 10, 32, 32, 16, "xmm6+"), "seh.after.xmm", "%s", g_why);
    CHECK_EQ64("seh.after.rsp", g_after.gpr[4], g_saved_rsp);
    CHECK_EQ64("seh.after.rax", g_after.gpr[0], 0);
}

/* ---------------------------------------------------------------- RtlCaptureContext / RtlRestoreContext */

static void group_capture(void)
{
    static DECLSPEC_ALIGN(16) CONTEXT c, r;
    static DECLSPEC_ALIGN(16) uint8_t fake_stack[0x400];
    uint8_t st[2][16];
    unsigned i;

    /* a caller that breaks the ABI's stack alignment: only recorded */
    memset(&c, 0xcc, sizeof(c));
    make_pattern(&g_before, 69, 0x0897, 0x3f80, 0x0b7f);
    g_capture_pad = 0x28;
    capture_probe(&c);
    g_capture_pad = 0x20;
    fid_info("capture.misaligned", "rip %+lld rsp %+lld (relative to the return address and the caller's rsp)",
             (long long)(c.Rip - (uintptr_t)capture_ret), (long long)(c.Rsp - g_before.gpr[4]));

    memset(&c, 0xcc, sizeof(c));
    make_pattern(&g_before, 70, 0x0897, 0x3f80, 0x0b7f);     /* no DF across a call */
    capture_probe(&c);
    CHECK_EQ64("capture.rip", c.Rip, (uintptr_t)capture_ret);
    CHECK_EQ64("capture.rsp", c.Rsp, g_before.gpr[4]);
    fid_check(cmp_gprs(&c.Rax, g_before.gpr, NONVOL_GPRS), "capture.gpr.nonvol", "%s", g_why);
    {
        uint64_t want[16];
        memcpy(want, g_before.gpr, sizeof(want));
        want[1] = (uintptr_t)&c;
        /* volatile registers at a call carry no meaning in the ABI: recorded, not judged */
        if (!cmp_gprs(&c.Rax, want, 0x0f07)) fid_info("capture.gpr.volatile", "%s", g_why);
        else fid_info("capture.gpr.volatile", "all equal to the values at the call");
    }
    fid_check(!(c.EFlags & DF) && (c.EFlags & 2), "capture.eflags", "EFlags %#lx", c.EFlags);
    fid_info("capture.eflags", "EFlags %#lx, flags at the call %#llx", c.EFlags, (unsigned long long)g_before.rflags);
    fid_check((c.ContextFlags & 0x10000f) == 0x10000f, "capture.contextflags", "ContextFlags %#lx want 0x10000f", c.ContextFlags);
    fid_check(c.SegCs == 0x33 && c.SegSs == 0x2b, "capture.segs", "cs %#x ss %#x", c.SegCs, c.SegSs);
    fid_check(c.MxCsr == g_before.mxcsr, "capture.mxcsr", "MxCsr %#lx want %#x", c.MxCsr, g_before.mxcsr);
    fid_check(c.FltSave.ControlWord == g_before.fcw, "capture.fcw", "ControlWord %#x want %#x", c.FltSave.ControlWord, g_before.fcw);
    fid_check(cmp_vec((const uint8_t *)&c.FltSave.XmmRegisters[6], g_before.ymm[6], 10, 16, 32, 16, "xmm6+"), "capture.xmm.nonvol", "%s", g_why);
    fid_check(cmp_vec((const uint8_t *)&c.FltSave.XmmRegisters[0], g_before.ymm[0], 6, 16, 32, 16, "xmm"), "capture.xmm.volatile", "%s", g_why);
    fid_info("capture.flags", "ContextFlags %#lx EFlags %#lx StatusWord %#x TagWord %#x", c.ContextFlags, c.EFlags,
             c.FltSave.StatusWord, c.FltSave.TagWord);
    /* the probe's own nonvolatile state survived the call */
    fid_check(cmp_gprs(g_after.gpr, g_before.gpr, NONVOL_GPRS), "capture.after.gpr", "%s", g_why);
    fid_check(g_after.fcw == g_before.fcw && (g_after.mxcsr & 0xffc0) == (g_before.mxcsr & 0xffc0), "capture.after.control",
              "fcw %#x mxcsr %#x want %#x %#x", g_after.fcw, g_after.mxcsr, g_before.fcw, g_before.mxcsr);

    /* RtlRestoreContext with every register, flags (DF too), MXCSR and an x87 stack of two values */
    r = c;
    make_pattern(&g_mod, 71, 0x0cd5 & ~0x40, 0x5f80, 0x067f);
    for (i = 0; i < 16; i++) if (i != 4) (&r.Rax)[i] = g_mod.gpr[i];
    r.ContextFlags = CONTEXT_FULL;
    r.Rip = (uintptr_t)scenario_resume;
    r.Rsp = ((uintptr_t)fake_stack + sizeof(fake_stack) - 0x80) & ~15ull;
    r.EFlags = 0x202 | (g_mod.rflags & FLAGS_MASK);
    r.SegCs = 0x33; r.SegSs = 0x2b; r.SegDs = r.SegEs = r.SegGs = 0x2b; r.SegFs = 0x53;
    r.MxCsr = r.FltSave.MxCsr = g_mod.mxcsr;
    r.FltSave.MxCsr_Mask = 0;
    r.FltSave.ControlWord = g_mod.fcw;
    r.FltSave.StatusWord = 6 << 11;
    r.FltSave.TagWord = 0xc0;
    set_x87_value(st[0], 0xa000000000000000ull, 0x4001);   /* 5.0 */
    set_x87_value(st[1], 0xe000000000000000ull, 0xbffe);   /* -0.875 */
    memset(r.FltSave.FloatRegisters, 0, sizeof(r.FltSave.FloatRegisters));
    memcpy(&r.FltSave.FloatRegisters[0], st[0], 16);
    memcpy(&r.FltSave.FloatRegisters[1], st[1], 16);
    for (i = 0; i < 16; i++) memcpy(&r.FltSave.XmmRegisters[i], g_mod.ymm[i], 16);
    memset(&g_after, 0, sizeof(g_after));
    restore_probe(&r);
    check_after("restore", &g_after, &g_mod, r.Rsp, ALL_BUT_RSP, 0, 6, 0xc0, (const uint8_t (*)[16])st);
}

/* ---------------------------------------------------------------- other thread */

static CONTEXT *alloc_context(DWORD flags, void **buf)
{
    DWORD len = 0;
    CONTEXT *ctx = NULL;
    if (g_has_avx && pInitializeContext && pSetXStateFeaturesMask && (pGetEnabledXStateFeatures() & XSTATE_MASK_AVX))
    {
        pInitializeContext(NULL, flags | 0x40, NULL, &len);
        *buf = malloc(len);
        if (pInitializeContext(*buf, flags | 0x40, &ctx, &len))
        {
            pSetXStateFeaturesMask(ctx, XSTATE_MASK_AVX);
            return ctx;
        }
        free(*buf);
    }
    *buf = _aligned_malloc(sizeof(CONTEXT), 16);
    ctx = *buf;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = flags;
    return ctx;
}

static void free_context(void *buf, CONTEXT *ctx)
{
    if ((void *)ctx == buf) _aligned_free(buf); else free(buf);
}

static void group_thread(void)
{
    HANDLE h;
    void *buf;
    CONTEXT *c;
    uint8_t *ymmh = NULL;
    DWORD len = 0;
    uint8_t st[3][16], st2[2][16];
    unsigned i;

    make_pattern(&g_before, 80, 0xc95, 0x3f80, 0x0b7f);
    g_spin_ready = 0;
    memset(&g_after, 0, sizeof(g_after));
    h = CreateThread(NULL, 0, spin_thread, NULL, 0, NULL);
    while (!g_spin_ready) Sleep(1);
    Sleep(50);
    SuspendThread(h);
    c = alloc_context(CONTEXT_ALL, &buf);
    fid_check(GetThreadContext(h, c), "thread.get", "GetThreadContext failed %lu", GetLastError());
    if ((c->ContextFlags & 0x40) && pLocateXStateFeature) ymmh = pLocateXStateFeature(c, XSTATE_AVX, &len);
    check_context("thread", c, &g_before, spin_loop, g_before.gpr[4], ALL_BUT_RSP, ymmh != NULL, (const uint8_t (*)[16])ymmh);

    /* move it to spin_exit with new registers, flags, MXCSR, x87 stack and vector state */
    make_pattern(&g_mod, 81, 0x046 | DF, 0x5f80, 0x067f);
    for (i = 0; i < 16; i++) if (i != 4) (&c->Rax)[i] = g_mod.gpr[i];
    c->Rip = (uintptr_t)spin_exit;
    c->EFlags = (c->EFlags & ~FLAGS_MASK) | (g_mod.rflags & FLAGS_MASK);
    c->MxCsr = c->FltSave.MxCsr = g_mod.mxcsr;
    c->FltSave.ControlWord = g_mod.fcw;
    c->FltSave.StatusWord = 6 << 11;
    c->FltSave.TagWord = 0xc0;
    set_x87_value(st2[0], 0xb000000000000000ull, 0x4002);   /* 11.0 */
    set_x87_value(st2[1], 0x8800000000000000ull, 0x3ffd);   /* 0.265625 */
    memcpy(&c->FltSave.FloatRegisters[0], st2[0], 16);
    memcpy(&c->FltSave.FloatRegisters[1], st2[1], 16);
    for (i = 0; i < 16; i++) memcpy(&c->FltSave.XmmRegisters[i], g_mod.ymm[i], 16);
    if (ymmh) for (i = 0; i < 16; i++) memcpy(ymmh + 16 * i, g_mod.ymm[i] + 16, 16);
    fid_check(SetThreadContext(h, c), "thread.set", "SetThreadContext failed %lu", GetLastError());
    ResumeThread(h);
    fid_check(WaitForSingleObject(h, 5000) == WAIT_OBJECT_0, "thread.exit", "thread did not reach spin_exit");
    check_after("thread.set", &g_after, &g_mod, g_before.gpr[4], ALL_BUT_RSP, ymmh != NULL, 6, 0xc0, (const uint8_t (*)[16])st2);
    if (!ymmh) fid_info("thread", "no AVX state through Get/SetThreadContext");
    CloseHandle(h);
    free_context(buf, c);
    (void)st;
}

static void group_stress(unsigned count)
{
    HANDLE h;
    void *buf;
    CONTEXT *c;
    unsigned i, done = 0, ctxbad = 0;
    char firstwhy[200] = "";
    DWORD start;

    make_pattern(&g_before, 90, 0x0c95, 0x3f80, 0x0b7f);
    g_stress_stop = 0;
    g_stress_bad = 0;
    g_stress_iter = 0;
    h = CreateThread(NULL, 0, stress_thread, NULL, 0, NULL);
    while (!g_stress_iter) Sleep(1);
    c = alloc_context(CONTEXT_FULL, &buf);
    start = GetTickCount();
    for (i = 0; i < count && GetTickCount() - start < 8000; i++)
    {
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) break;
        if (SuspendThread(h) == (DWORD)-1) break;
        c->ContextFlags = CONTEXT_FULL | (c->ContextFlags & 0x40);
        if (GetThreadContext(h, c))
        {
            unsigned top = (c->FltSave.StatusWord >> 11) & 7;
            if (c->FltSave.ControlWord != g_before.fcw || (c->MxCsr & 0xffc0) != (g_before.mxcsr & 0xffc0) ||
                !(c->EFlags & DF) || (top != 5 && top != 4))
            {
                if (!ctxbad++)
                    sprintf(firstwhy, "fcw %#x mxcsr %#lx eflags %#lx top %u at rip %#llx", c->FltSave.ControlWord, c->MxCsr,
                            c->EFlags, top, (unsigned long long)c->Rip);
            }
        }
        ResumeThread(h);
        done++;
        if (i & 1) Sleep(0);
    }
    g_stress_stop = 1;
    WaitForSingleObject(h, 5000);
    fid_check(!g_stress_bad, "stress.thread", "the thread's own state check failed at iteration %llu after %u suspensions",
              (unsigned long long)g_stress_bad, done);
    fid_check(!ctxbad, "stress.context", "%u of %u GetThreadContext results wrong, first: %s", ctxbad, done, firstwhy);
    fid_info("stress", "%u suspensions, %llu loop iterations", done, (unsigned long long)g_stress_iter);
    CloseHandle(h);
    free_context(buf, c);
}

/* ---------------------------------------------------------------- main */

static DWORD WINAPI watchdog(void *arg)
{
    Sleep((DWORD)(uintptr_t)arg);
    printf("FAIL watchdog: still running after %lu ms\n", (unsigned long)(uintptr_t)arg);
    fid_summary();
    fflush(stdout);
    ExitProcess(253);
}

static int want_group(const char *list, const char *g)
{
    size_t n = strlen(g);
    const char *p = list;
    if (!list) return 1;
    while ((p = strstr(p, g)))
    {
        if ((p == list || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) return 1;
        p += n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll"), ntdll = GetModuleHandleA("ntdll.dll");
    const char *groups = argc > 1 && strcmp(argv[1], "all") ? argv[1] : NULL;
    unsigned count = argc > 2 ? (unsigned)atoi(argv[2]) : 1000;
    int regs[4];

    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "ctxtest";
    fid_environment();
    pGetEnabledXStateFeatures = (void *)GetProcAddress(k32, "GetEnabledXStateFeatures");
    pInitializeContext = (void *)GetProcAddress(k32, "InitializeContext");
    pSetXStateFeaturesMask = (void *)GetProcAddress(k32, "SetXStateFeaturesMask");
    pGetXStateFeaturesMask = (void *)GetProcAddress(k32, "GetXStateFeaturesMask");
    pLocateXStateFeature = (void *)GetProcAddress(k32, "LocateXStateFeature");
    pRtlUnwindEx = (void *)GetProcAddress(ntdll, "RtlUnwindEx");
    g_pRtlCaptureContext = (void *)GetProcAddress(ntdll, "RtlCaptureContext");
    g_pRtlRestoreContext = (void *)GetProcAddress(ntdll, "RtlRestoreContext");

    __cpuid(regs, 1);
    if ((regs[2] & (1 << 28)) && (regs[2] & (1 << 27)))
    {
        uint32_t lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        g_has_avx = (lo & 6) == 6;
    }
    fid_info("env.avx", "%d (enabled xstate features %#llx)", g_has_avx,
             pGetEnabledXStateFeatures ? (unsigned long long)pGetEnabledXStateFeatures() : 0ull);

    AddVectoredExceptionHandler(1, veh);
    SetUnhandledExceptionFilter(unhandled);
    CreateThread(NULL, 0, watchdog, (void *)(uintptr_t)180000, 0, NULL);

    if (want_group(groups, "exc")) group_exc();
    if (want_group(groups, "modify")) group_modify();
    if (want_group(groups, "repmovs")) group_repmovs();
    if (want_group(groups, "nested")) group_nested();
    if (want_group(groups, "repeat")) group_repeat(count);
    if (want_group(groups, "seh")) group_seh();
    if (want_group(groups, "capture")) group_capture();
    if (want_group(groups, "thread")) group_thread();
    if (want_group(groups, "stress")) group_stress(count * 3);
    return fid_summary();
}
