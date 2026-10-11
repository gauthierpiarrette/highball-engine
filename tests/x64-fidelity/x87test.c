/* x87test: x87 and SSE architectural state semantics (Intel SDM vol. 1 ch. 8 and 10-11, vol. 2 entries).
 *
 * FXAM on every class (normal, zero, infinity, NaN, denormal, unsupported encodings, empty) with C0-C3;
 * full tag word (FNSTENV/FNSAVE) and abridged tag (FXSAVE); TOP through pushes, FINCSTP/FDECSTP/FFREE;
 * FNSAVE re-initialisation and FRSTOR/FLDENV round trips; precision and rounding control; masked exception
 * flags (IE, DE, ZE, OE, UE, PE) and stack fault (SF, C1); FPREM/FPREM1 quotient bits; FCOM/FUCOM/FCOMI;
 * FIST out of range; FRNDINT per RC; MMX aliasing (TOP 0, tags valid, exponent 0xFFFF, EMMS);
 * MXCSR control/status (round trip, sticky flags, DAZ, FTZ, RC).
 * Build: x86_64-w64-mingw32-clang -O2 x87test.c -o x87test.exe */
#include "fid.h"

typedef struct f80 { uint8_t b[10]; } f80;
typedef struct env28 { uint16_t fcw, r0, fsw, r1, ftw, r2; uint32_t fip; uint16_t fcs, fop; uint32_t fdp; uint16_t fds, r3; } env28;
_Static_assert(sizeof(env28) == 28, "env");

static f80 mk(uint16_t sexp, uint64_t mant)
{
    f80 v;
    memcpy(v.b, &mant, 8);
    memcpy(v.b + 8, &sexp, 2);
    return v;
}
static uint64_t mant_of(const f80 *v) { uint64_t m; memcpy(&m, v->b, 8); return m; }
static uint16_t sexp_of(const f80 *v) { uint16_t e; memcpy(&e, v->b + 8, 2); return e; }

static const uint16_t FCW_DEFAULT = 0x27f;
#define C0 0x0100
#define C1 0x0200
#define C2 0x0400
#define C3 0x4000
#define CC (C0 | C1 | C2 | C3)

/* ---------------------------------------------------------------- FXAM */

static uint16_t fxam(f80 v)
{
    uint16_t sw;
    __asm__ volatile("fninit\n\tfldt %1\n\tfxam\n\tfnstsw %0\n\tfninit\n\tfldcw %2"
                     : "=m"(sw) : "m"(v), "m"(FCW_DEFAULT) : "memory");
    return sw;
}

static uint16_t fxam_empty(f80 stale)
{
    uint16_t sw;
    __asm__ volatile("fninit\n\tfldt %1\n\tffree %%st(0)\n\tfxam\n\tfnstsw %0\n\tfninit\n\tfldcw %2"
                     : "=m"(sw) : "m"(stale), "m"(FCW_DEFAULT) : "memory");
    return sw;
}

static void group_fxam(void)
{
    static const struct { const char *name; uint16_t sexp; uint64_t mant; uint16_t cc; int info; } cases[] =
    {
        { "pos_normal",   0x3fff, 0x8000000000000000ull, C2 },
        { "neg_normal",   0xbfff, 0x8000000000000000ull, C2 | C1 },
        { "max_normal",   0x7ffe, 0xffffffffffffffffull, C2 },
        { "min_normal",   0x0001, 0x8000000000000000ull, C2 },
        { "pos_zero",     0x0000, 0, C3 },
        { "neg_zero",     0x8000, 0, C3 | C1 },
        { "pos_inf",      0x7fff, 0x8000000000000000ull, C2 | C0 },
        { "neg_inf",      0xffff, 0x8000000000000000ull, C2 | C0 | C1 },
        { "pos_qnan",     0x7fff, 0xc000000000000000ull, C0 },
        { "neg_qnan",     0xffff, 0xc000000000000000ull, C0 | C1 },
        { "pos_snan",     0x7fff, 0xa000000000000000ull, C0 },
        { "pos_denormal", 0x0000, 0x0000000000000001ull, C3 | C2 },
        { "neg_denormal", 0x8000, 0x4000000000000000ull, C3 | C2 | C1 },
        { "unnormal",     0x3fff, 0x4000000000000000ull, 0 },
        { "pseudo_nan",   0x7fff, 0x4000000000000000ull, 0 },
        { "pseudo_inf",   0x7fff, 0x0000000000000000ull, 0 },
        { "pseudo_denormal", 0x0000, 0x8000000000000001ull, C3 | C2 },
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint16_t sw = fxam(mk(cases[i].sexp, cases[i].mant)) & CC;
        char id[64];
        sprintf(id, "fxam.%s", cases[i].name);
        if (cases[i].info) fid_info(id, "C3C2C1C0 %d%d%d%d", !!(sw & C3), !!(sw & C2), !!(sw & C1), !!(sw & C0));
        else fid_check(sw == cases[i].cc, id, "C3C2C1C0 %d%d%d%d want %d%d%d%d", !!(sw & C3), !!(sw & C2), !!(sw & C1), !!(sw & C0),
                       !!(cases[i].cc & C3), !!(cases[i].cc & C2), !!(cases[i].cc & C1), !!(cases[i].cc & C0));
    }
    {
        uint16_t sw = fxam_empty(mk(0xbfff, 0x8000000000000000ull)) & CC;
        fid_check((sw & (C3 | C2 | C0)) == (C3 | C0), "fxam.empty", "C3C2C0 %d%d%d want 101", !!(sw & C3), !!(sw & C2), !!(sw & C0));
        /* the SDM sets C1 to the sign of ST(0) whatever its class; Windows on AMD and Intel agree */
        fid_check(sw & C1, "fxam.empty.c1", "C1 clear for an empty register holding a stale negative value");
    }
}

/* ---------------------------------------------------------------- tag words, TOP, save/restore */

static void group_tags(void)
{
    f80 den = mk(0x0000, 1), inf = mk(0x7fff, 0x8000000000000000ull), nan = mk(0x7fff, 0xc000000000000000ull);
    env28 env, env2;
    static DECLSPEC_ALIGN(16) uint8_t fx[512];
    uint16_t sw;

    __asm__ volatile("fninit\n\t"
                     "fldt %2\n\tfldt %3\n\tfldt %4\n\tfldz\n\tfld1\n\t"
                     "fnstenv %0\n\tfxsave64 %1\n\t"
                     "fninit\n\tfldcw %5"
                     : "=m"(env), "=m"(fx) : "m"(den), "m"(inf), "m"(nan), "m"(FCW_DEFAULT) : "memory");
    fid_check(((env.fsw >> 11) & 7) == 3, "tags.top", "TOP %u after five pushes, want 3", (env.fsw >> 11) & 7);
    fid_check(env.ftw == 0xa93f, "tags.full", "tag word %#x want 0xa93f (7-5 special, 4 zero, 3 valid, 2-0 empty)", env.ftw);
    fid_check(fx[4] == 0xf8, "tags.abridged", "FXSAVE abridged tag %#x want 0xf8", fx[4]);

    /* FLDENV with ST0's tag set to empty: ST0 reads as empty */
    env2 = env;
    env2.ftw |= 3 << 6;
    __asm__ volatile("fninit\n\tfldt %1\n\tfldt %2\n\tfldt %3\n\tfldz\n\tfld1\n\t"
                     "fldenv %4\n\tfxam\n\tfnstsw %0\n\tfninit\n\tfldcw %5"
                     : "=m"(sw) : "m"(den), "m"(inf), "m"(nan), "m"(env2), "m"(FCW_DEFAULT) : "memory");
    fid_check((sw & (C3 | C2 | C0)) == (C3 | C0), "tags.fldenv_empty", "FXAM after FLDENV marking ST0 empty: C3C2C0 %d%d%d",
              !!(sw & C3), !!(sw & C2), !!(sw & C0));

    /* FINCSTP / FDECSTP move TOP only, FFREE empties */
    {
        uint16_t sw1, sw2, sw3, sw4;
        __asm__ volatile("fninit\n\tfld1\n\tfldz\n\t"            /* phys7 = 1.0, phys6 = 0.0, TOP 6 */
                         "fincstp\n\tfxam\n\tfnstsw %0\n\t"     /* TOP 7: ST0 = 1.0 (normal) */
                         "fdecstp\n\tfxam\n\tfnstsw %1\n\t"     /* TOP 6: ST0 = 0.0 (zero) */
                         "ffree %%st(0)\n\tfxam\n\tfnstsw %2\n\t" /* empty */
                         "fincstp\n\tfxam\n\tfnstsw %3\n\t"     /* TOP 7: still 1.0 */
                         "fninit\n\tfldcw %4"
                         : "=m"(sw1), "=m"(sw2), "=m"(sw3), "=m"(sw4) : "m"(FCW_DEFAULT) : "memory");
        fid_check(((sw1 >> 11) & 7) == 7 && (sw1 & (C3 | C2 | C0)) == C2, "tags.fincstp", "fsw %#x (want TOP 7, normal)", sw1);
        fid_check(((sw2 >> 11) & 7) == 6 && (sw2 & (C3 | C2 | C0)) == C3, "tags.fdecstp", "fsw %#x (want TOP 6, zero)", sw2);
        fid_check((sw3 & (C3 | C2 | C0)) == (C3 | C0), "tags.ffree", "fsw %#x (want empty)", sw3);
        fid_check(((sw4 >> 11) & 7) == 7 && (sw4 & (C3 | C2 | C0)) == C2, "tags.ffree_other", "fsw %#x (want TOP 7, normal)", sw4);
    }
}

static void group_saverestore(void)
{
    static uint8_t area[108];
    static DECLSPEC_ALIGN(16) uint8_t fx[512];
    f80 a = mk(0x3fff, 0xc000000000000000ull), b = mk(0xc000, 0x9000000000000000ull), c = mk(0x4005, 0x8000000000000123ull);
    uint16_t cw_after_save, fcw = 0x0b7f;
    env28 env_after_save;
    unsigned i;

    __asm__ volatile("fninit\n\tfldcw %4\n\tfldt %5\n\tfldt %6\n\tfldt %7\n\t"
                     "fnsave %0\n\t"
                     "fnstcw %1\n\tfnstenv %2\n\t"
                     "fldz\n\tfldz\n\tfldz\n\tfldz\n\t"   /* disturb */
                     "frstor %0\n\t"
                     "fxsave64 %3\n\t"
                     "fninit\n\tfldcw %8"
                     : "+m"(area), "=m"(cw_after_save), "=m"(env_after_save), "=m"(fx)
                     : "m"(fcw), "m"(a), "m"(b), "m"(c), "m"(FCW_DEFAULT) : "memory");
    fid_check(cw_after_save == 0x37f && ((env_after_save.fsw >> 11) & 7) == 0 && env_after_save.ftw == 0xffff, "fnsave.reinit",
              "after FNSAVE: fcw %#x TOP %u tag %#x (want 0x37f 0 0xffff)", cw_after_save, (env_after_save.fsw >> 11) & 7,
              env_after_save.ftw);
    fid_check(*(uint16_t *)area == fcw && ((*(uint16_t *)(area + 4) >> 11) & 7) == 5, "fnsave.area",
              "saved fcw %#x TOP %u", *(uint16_t *)area, (*(uint16_t *)(area + 4) >> 11) & 7);
    fid_check(!memcmp(area + 28, c.b, 10) && !memcmp(area + 38, b.b, 10) && !memcmp(area + 48, a.b, 10), "fnsave.regs",
              "ST0-2 in the FNSAVE area differ");
    {
        int ok = *(uint16_t *)fx == fcw && ((*(uint16_t *)(fx + 2) >> 11) & 7) == 5 && fx[4] == 0xe0;
        for (i = 0; ok && i < 3; i++) ok = !memcmp(fx + 32 + 16 * i, i == 0 ? c.b : i == 1 ? b.b : a.b, 10);
        fid_check(ok, "frstor.state", "after FRSTOR: fcw %#x fsw %#x ftw %#x", *(uint16_t *)fx, *(uint16_t *)(fx + 2), fx[4]);
    }
    /* FNSTENV / FLDENV round trip of the control and status words, condition codes included */
    {
        env28 e1, e2;
        uint16_t sw_cc = 0x4700 | (3 << 11);    /* C3 C2 C1 C0 set, TOP 3 */
        __asm__ volatile("fninit\n\tfnstenv %0" : "=m"(e1) :: "memory");
        e1.fcw = 0x0e7f;
        e1.fsw = sw_cc;
        __asm__ volatile("fldenv %1\n\tfnstenv %0\n\tfninit\n\tfldcw %2" : "=m"(e2) : "m"(e1), "m"(FCW_DEFAULT) : "memory");
        fid_check(e2.fcw == 0x0e7f && (e2.fsw & 0x7f00) == (sw_cc & 0x7f00), "fldenv.roundtrip",
                  "fcw %#x fsw %#x want 0xe7f %#x", e2.fcw, e2.fsw, sw_cc);
    }
}

/* ---------------------------------------------------------------- arithmetic control */

static f80 div_1_3(uint16_t fcw)
{
    static const double three = 3.0;
    f80 r;
    __asm__ volatile("fninit\n\tfldcw %1\n\tfld1\n\tfdivl %2\n\tfstpt %0\n\tfninit\n\tfldcw %3"
                     : "=m"(r) : "m"(fcw), "m"(three), "m"(FCW_DEFAULT) : "memory");
    return r;
}

static void group_control(void)
{
    static const struct { const char *name; uint16_t fcw; uint64_t mant; } cases[] =
    {
        { "pc24",       0x007f, 0xaaaaab0000000000ull },
        { "pc53",       0x027f, 0xaaaaaaaaaaaaa800ull },
        { "pc64",       0x037f, 0xaaaaaaaaaaaaaaabull },
        { "pc64_down",  0x077f, 0xaaaaaaaaaaaaaaaaull },
        { "pc64_up",    0x0b7f, 0xaaaaaaaaaaaaaaabull },
        { "pc64_zero",  0x0f7f, 0xaaaaaaaaaaaaaaaaull },
        { "pc24_up",    0x087f, 0xaaaaab0000000000ull },
        { "pc24_down",  0x047f, 0xaaaaaa0000000000ull },
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        f80 r = div_1_3(cases[i].fcw);
        char id[64];
        sprintf(id, "control.%s", cases[i].name);
        fid_check(mant_of(&r) == cases[i].mant && sexp_of(&r) == 0x3ffd, id, "1/3 = %04x:%016llx want 3ffd:%016llx",
                  sexp_of(&r), (unsigned long long)mant_of(&r), (unsigned long long)cases[i].mant);
    }
    /* FRNDINT honours RC; ties go to even under round-to-nearest */
    {
        static const double vals[2] = { 2.5, -2.5 };
        static const double want[2][4] = { { 2, 2, 3, 2 }, { -2, -3, -2, -2 } };
        unsigned v, rc;
        for (v = 0; v < 2; v++)
            for (rc = 0; rc < 4; rc++)
            {
                double out;
                uint16_t fcw = 0x027f | (rc << 10);
                char id[64];
                __asm__ volatile("fninit\n\tfldcw %1\n\tfldl %2\n\tfrndint\n\tfstpl %0\n\tfninit\n\tfldcw %3"
                                 : "=m"(out) : "m"(fcw), "m"(vals[v]), "m"(FCW_DEFAULT) : "memory");
                sprintf(id, "frndint.%s.rc%u", v ? "neg" : "pos", rc);
                fid_check(out == want[v][rc], id, "frndint(%g) = %g want %g", vals[v], out, want[v][rc]);
            }
    }
}

/* ---------------------------------------------------------------- exception flags (masked) */

static uint16_t op_status(int which, f80 *result)
{
    static const double zero = 0.0, three = 3.0, tiny = 1e-300, dden = 4.9406564584124654e-324;
    f80 big = mk(0x7ffe, 0xffffffffffffffffull), small = mk(0x0001, 0x8000000000000000ull);
    uint16_t sw = 0;
    f80 r = { { 0 } };
    switch (which)
    {
    case 0: /* 1/3: PE */
        __asm__ volatile("fninit\n\tfld1\n\tfdivl %2\n\tfnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %3"
                         : "=m"(sw), "=m"(r) : "m"(three), "m"(FCW_DEFAULT) : "memory"); break;
    case 1: /* 1/0: ZE */
        __asm__ volatile("fninit\n\tfld1\n\tfdivl %2\n\tfnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %3"
                         : "=m"(sw), "=m"(r) : "m"(zero), "m"(FCW_DEFAULT) : "memory"); break;
    case 2: /* 0/0: IE, real indefinite */
        __asm__ volatile("fninit\n\tfldz\n\tfdivl %2\n\tfnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %3"
                         : "=m"(sw), "=m"(r) : "m"(zero), "m"(FCW_DEFAULT) : "memory"); break;
    case 3: /* max*max: OE PE */
        __asm__ volatile("fninit\n\tfldt %2\n\tfmul %%st(0), %%st\n\tfnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %3"
                         : "=m"(sw), "=m"(r) : "m"(big), "m"(FCW_DEFAULT) : "memory"); break;
    case 4: /* min_normal * 1e-300: UE PE */
        __asm__ volatile("fninit\n\tfldt %2\n\tfmull %3\n\tfnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %4"
                         : "=m"(sw), "=m"(r) : "m"(small), "m"(tiny), "m"(FCW_DEFAULT) : "memory"); break;
    case 5: /* load a denormal double: DE */
        __asm__ volatile("fninit\n\tfldl %2\n\tfnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %3"
                         : "=m"(sw), "=m"(r) : "m"(dden), "m"(FCW_DEFAULT) : "memory"); break;
    case 6: /* ninth push: stack overflow, IE SF C1=1, ST0 = indefinite */
        __asm__ volatile("fninit\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\t"
                         "fnstsw %0\n\tfstpt %1\n\tfninit\n\tfldcw %2"
                         : "=m"(sw), "=m"(r) : "m"(FCW_DEFAULT) : "memory"); break;
    case 7: /* pop from an empty stack: stack underflow, IE SF C1=0 */
        __asm__ volatile("fninit\n\tfstpt %1\n\tfnstsw %0\n\tfninit\n\tfldcw %2"
                         : "=m"(sw), "=m"(r) : "m"(FCW_DEFAULT) : "memory"); break;
    }
    if (result) *result = r;
    return sw;
}

static void group_flags(void)
{
    static const struct { const char *name; uint16_t want, mask; } cases[] =
    {
        { "pe",        0x0020, 0x007f },
        { "ze",        0x0004, 0x007f },
        { "ie",        0x0001, 0x007f },
        { "oe",        0x0028, 0x007f },
        { "ue",        0x0030, 0x007f },
        { "de",        0x0002, 0x007f },
        { "overflow",  0x0041 | C1, 0x007f | C1 },
        { "underflow", 0x0041, 0x007f | C1 },
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        f80 r;
        uint16_t sw = op_status(i, &r);
        char id[64];
        sprintf(id, "flags.%s", cases[i].name);
        fid_check((sw & cases[i].mask) == cases[i].want, id, "status word %#x, want %#x under mask %#x", sw, cases[i].want, cases[i].mask);
        if (i == 2 || i == 6)
        {
            sprintf(id, "flags.%s.indefinite", cases[i].name);
            fid_check(sexp_of(&r) == 0xffff && mant_of(&r) == 0xc000000000000000ull, id, "result %04x:%016llx want ffff:c000000000000000",
                      sexp_of(&r), (unsigned long long)mant_of(&r));
        }
        if (i == 0)
            fid_check(sw & C1, "flags.pe.c1", "C1 clear after 1/3 rounded up (fsw %#x)", sw);
    }
}

/* ---------------------------------------------------------------- comparisons, FPREM, FIST */

static void group_compare(void)
{
    static const double one = 1.0, two = 2.0, seven = 7.0, thirteen = 13.0, big = 3e10;
    double qnan;
    uint64_t qbits = 0x7ff8000000000000ull;
    unsigned i;
    memcpy(&qnan, &qbits, 8);
    /* FCOM / FUCOM: C3 C2 C0 */
    {
        static const struct { const char *name; int unordered; double a, b; uint16_t cc; } c[] =
        {
            { "gt", 0, 2.0, 1.0, 0 }, { "lt", 0, 1.0, 2.0, C0 }, { "eq", 0, 1.0, 1.0, C3 }, { "un", 1, 1.0, 0, C3 | C2 | C0 },
        };
        for (i = 0; i < 4; i++)
        {
            uint16_t sw_fcom, sw_fucom;
            double b = c[i].unordered ? qnan : c[i].b;
            char id[64];
            __asm__ volatile("fninit\n\tfldl %2\n\tfcoml %3\n\tfnstsw %0\n\tfninit\n\t"
                             "fldl %3\n\tfldl %2\n\tfucom %%st(1)\n\tfnstsw %1\n\tfninit\n\tfldcw %4"
                             : "=m"(sw_fcom), "=m"(sw_fucom) : "m"(c[i].a), "m"(b), "m"(FCW_DEFAULT) : "memory");
            sprintf(id, "fcom.%s", c[i].name);
            fid_check((sw_fcom & (C3 | C2 | C0)) == c[i].cc, id, "fsw %#x", sw_fcom);
            sprintf(id, "fucom.%s", c[i].name);
            fid_check((sw_fucom & (C3 | C2 | C0)) == c[i].cc, id, "fsw %#x", sw_fucom);
            if (c[i].unordered)
            {
                fid_check(sw_fcom & 1, "fcom.un.ie", "FCOM with a QNaN did not set IE (fsw %#x)", sw_fcom);
                fid_check(!(sw_fucom & 1), "fucom.un.ie", "FUCOM with a QNaN set IE (fsw %#x)", sw_fucom);
            }
        }
        /* FCOMI: ZF PF CF */
        for (i = 0; i < 4; i++)
        {
            uint8_t zf, pf, cf;
            double b = c[i].unordered ? qnan : c[i].b;
            char id[64];
            __asm__ volatile("fninit\n\tfldl %4\n\tfldl %3\n\tfcomi %%st(1), %%st\n\tsetz %0\n\tsetp %1\n\tsetc %2\n\tfninit\n\tfldcw %5"
                             : "=m"(zf), "=m"(pf), "=m"(cf) : "m"(c[i].a), "m"(b), "m"(FCW_DEFAULT) : "memory", "cc");
            sprintf(id, "fcomi.%s", c[i].name);
            {
                int wz = !!(c[i].cc & C3), wp = !!(c[i].cc & C2), wc = !!(c[i].cc & C0);
                fid_check(zf == wz && pf == wp && cf == wc, id, "ZF PF CF %d %d %d want %d %d %d", zf, pf, cf, wz, wp, wc);
            }
        }
    }
    /* FPREM: quotient bits Q2 Q1 Q0 in C0 C3 C1; FPREM1 rounds the quotient to nearest */
    {
        uint16_t sw1, sw2, sw3;
        double r1, r2, r3;
        __asm__ volatile("fninit\n\tfldl %6\n\tfldl %7\n\tfprem\n\tfnstsw %0\n\tfstpl %3\n\tfninit\n\t"
                         "fldl %8\n\tfldl %9\n\tfprem\n\tfnstsw %1\n\tfstpl %4\n\tfninit\n\t"
                         "fldl %6\n\tfldl %7\n\tfprem1\n\tfnstsw %2\n\tfstpl %5\n\tfninit\n\tfldcw %10"
                         : "=m"(sw1), "=m"(sw2), "=m"(sw3), "=m"(r1), "=m"(r2), "=m"(r3)
                         : "m"(two), "m"(seven), "m"(*(const double[]){ 3.0 }), "m"(thirteen), "m"(FCW_DEFAULT) : "memory");
        fid_check(r1 == 1.0 && (sw1 & CC) == (C3 | C1), "fprem.7_2", "7 mod 2 = %g, C3C2C1C0 %d%d%d%d (want 1, 1010: Q=3)", r1,
                  !!(sw1 & C3), !!(sw1 & C2), !!(sw1 & C1), !!(sw1 & C0));
        fid_check(r2 == 1.0 && (sw2 & CC) == C0, "fprem.13_3", "13 mod 3 = %g, C3C2C1C0 %d%d%d%d (want 1, 0001: Q=4)", r2,
                  !!(sw2 & C3), !!(sw2 & C2), !!(sw2 & C1), !!(sw2 & C0));
        fid_check(r3 == -1.0 && (sw3 & CC) == C0, "fprem1.7_2", "7 rem 2 = %g, C3C2C1C0 %d%d%d%d (want -1, 0001: Q=4)", r3,
                  !!(sw3 & C3), !!(sw3 & C2), !!(sw3 & C1), !!(sw3 & C0));
    }
    /* FIST out of range and of a NaN: integer indefinite, IE */
    {
        int32_t i32 = 0;
        int64_t i64 = 0;
        uint16_t sw1, sw2;
        __asm__ volatile("fninit\n\tfldl %4\n\tfistpl %0\n\tfnstsw %2\n\tfninit\n\tfldl %5\n\tfistpll %1\n\tfnstsw %3\n\tfninit\n\tfldcw %6"
                         : "=m"(i32), "=m"(i64), "=m"(sw1), "=m"(sw2) : "m"(big), "m"(qnan), "m"(FCW_DEFAULT) : "memory");
        fid_check(i32 == (int32_t)0x80000000 && (sw1 & 1), "fist.range", "fistp m32 of 3e10 = %#x fsw %#x (want 0x80000000, IE)",
                  (unsigned)i32, sw1);
        fid_check(i64 == (int64_t)0x8000000000000000ull && (sw2 & 1), "fist.nan", "fistp m64 of NaN = %#llx fsw %#x",
                  (unsigned long long)i64, sw2);
    }
    (void)one;
}

/* ---------------------------------------------------------------- MMX aliasing */

static void group_mmx(void)
{
    static const uint64_t pat = 0x0123456789abcdefull;
    env28 e1, e2;
    static DECLSPEC_ALIGN(16) uint8_t fx[512];
    __asm__ volatile("fninit\n\tmovq %3, %%mm0\n\tfnstenv %0\n\tfxsave64 %1\n\temms\n\tfnstenv %2\n\tfninit\n\tfldcw %4"
                     : "=m"(e1), "=m"(fx), "=m"(e2) : "m"(pat), "m"(FCW_DEFAULT) : "memory");
    {
        /* every register non-empty: no 11 pair in the full tag word (the pairs classify the contents) */
        unsigned i, empty = 0;
        for (i = 0; i < 8; i++) if (((e1.ftw >> (2 * i)) & 3) == 3) empty++;
        fid_check(((e1.fsw >> 11) & 7) == 0 && !empty, "mmx.tags", "after movq mm0: TOP %u tag %#x (%u registers empty)",
                  (e1.fsw >> 11) & 7, e1.ftw, empty);
    }
    fid_check(!memcmp(fx + 32, &pat, 8) && fx[40] == 0xff && fx[41] == 0xff, "mmx.alias", "ST0 bytes %02x%02x:%016llx (want ffff:%016llx)",
              fx[41], fx[40], (unsigned long long)*(uint64_t *)(fx + 32), (unsigned long long)pat);
    fid_check(fx[4] == 0xff, "mmx.abridged", "abridged tag %#x want 0xff", fx[4]);
    fid_check(e2.ftw == 0xffff, "mmx.emms", "tag word after EMMS %#x want 0xffff", e2.ftw);
}

/* ---------------------------------------------------------------- FIP / FOP (recorded) */

extern char fip_marker[];
static void group_pointers(void)
{
    env28 e;
    static double d = 1.5;
    static DECLSPEC_ALIGN(16) uint8_t fx[512];
    __asm__ volatile("fninit\n\t"
                     ".globl fip_marker\nfip_marker:\n\t"
                     "faddl %2\n\t"               /* last non-control x87 instruction; FDP = &d */
                     "fnstenv %0\n\tfxsave64 %1\n\tfninit\n\tfldcw %3"
                     : "=m"(e), "=m"(fx) : "m"(d), "m"(FCW_DEFAULT) : "memory");
    fid_info("pointers.fnstenv", "FIP %#x (marker %#x) FOP %#x FDP %#x (&d %#x)", e.fip, (unsigned)(uintptr_t)fip_marker,
             e.fop, e.fdp, (unsigned)(uintptr_t)&d);
    /* FNSTENV stores the last non-control instruction's address, opcode (DC 05 -> 0x405) and operand */
    fid_check(e.fip == (uint32_t)(uintptr_t)fip_marker && (e.fop & 0x7ff) == 0x405 && e.fdp == (uint32_t)(uintptr_t)&d,
              "pointers.fnstenv.values", "FIP %#x FOP %#x FDP %#x", e.fip, e.fop, e.fdp);
    fid_info("pointers.fxsave", "FOP %#x FIP %#llx FDP %#llx", *(uint16_t *)(fx + 6), (unsigned long long)*(uint64_t *)(fx + 8),
             (unsigned long long)*(uint64_t *)(fx + 16));
}

/* ---------------------------------------------------------------- MXCSR */

static uint32_t getcsr(void) { uint32_t v; __asm__ volatile("stmxcsr %0" : "=m"(v)); return v; }
static void setcsr(uint32_t v) { __asm__ volatile("ldmxcsr %0" :: "m"(v)); }

static void group_mxcsr(void)
{
    static const uint32_t vals[] = { 0x1f80, 0x9fc0, 0x7f80, 0x1fbf, 0x3f80, 0x5f80 };
    static DECLSPEC_ALIGN(16) uint8_t fx[512];
    uint32_t mask;
    unsigned i;
    const float den = 1e-40f, zero = 0.0f, tiny = 1e-30f;

    __asm__ volatile("fxsave64 %0" : "=m"(fx));
    mask = *(uint32_t *)(fx + 28);
    fid_info("mxcsr.mask", "MXCSR_MASK %#x", mask);
    for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        char id[64];
        uint32_t got;
        if (vals[i] & ~mask) { sprintf(id, "mxcsr.roundtrip.%x", vals[i]); fid_info(id, "skipped, outside MXCSR_MASK"); continue; }
        setcsr(vals[i]);
        got = getcsr();
        setcsr(0x1f80);
        sprintf(id, "mxcsr.roundtrip.%x", vals[i]);
        fid_check(got == vals[i], id, "ldmxcsr %#x then stmxcsr %#x", vals[i], got);
    }
    /* sticky precision flag: the arithmetic is in asm so nothing moves it around stmxcsr */
    {
        uint32_t c1, c2, start = 0x1f80;
        float a = 1.0f, b = 3.0f, two = 2.0f;
        __asm__ volatile("ldmxcsr %4\n\tdivss %3, %0\n\tstmxcsr %1\n\taddss %3, %3\n\tmulss %5, %3\n\tstmxcsr %2\n\tldmxcsr %4"
                         : "+x"(a), "=m"(c1), "=m"(c2), "+x"(b) : "m"(start), "x"(two));
        fid_check((c1 & 0x20) && (c2 & 0x20), "mxcsr.sticky_pe", "PE after 1/3 %#x, after exact ops %#x", c1, c2);
    }
    /* DAZ: a denormal input reads as zero */
    {
        uint32_t daz = 0x1fc0, nodaz = 0x1f80;
        float x = den, y = den, z = zero;
        __asm__ volatile("ldmxcsr %2\n\taddss %3, %0\n\tldmxcsr %4" : "+x"(x), "+x"(z) : "m"(daz), "x"(zero), "m"(nodaz));
        fid_check(x == 0.0f, "mxcsr.daz", "1e-40 + 0 with DAZ = %g (want 0)", x);
        __asm__ volatile("ldmxcsr %1\n\taddss %2, %0" : "+x"(y) : "m"(nodaz), "x"(zero));
        fid_check(y != 0.0f, "mxcsr.nodaz", "1e-40 + 0 without DAZ = %g", y);
    }
    /* FTZ: an underflowing result is flushed to zero and UE PE are set */
    {
        uint32_t ftz = 0x9f80, def = 0x1f80, csr;
        float x = tiny;
        __asm__ volatile("ldmxcsr %2\n\tmulss %3, %0\n\tstmxcsr %1\n\tldmxcsr %4" : "+x"(x), "=m"(csr) : "m"(ftz), "x"(tiny), "m"(def));
        fid_check(x == 0.0f, "mxcsr.ftz", "1e-30*1e-30 with FTZ = %g", x);
        fid_check((csr & 0x30) == 0x30, "mxcsr.ftz.flags", "MXCSR after a flushed underflow %#x (want UE|PE)", csr);
    }
    /* RC on conversions */
    {
        static const float vals2[2] = { 2.5f, -2.5f };
        static const int want[2][4] = { { 2, 2, 3, 2 }, { -2, -3, -2, -2 } };
        unsigned v, rc;
        for (v = 0; v < 2; v++)
            for (rc = 0; rc < 4; rc++)
            {
                int out;
                char id[64];
                setcsr(0x1f80 | (rc << 13));
                __asm__ volatile("cvtss2si %1, %0" : "=r"(out) : "m"(vals2[v]));
                setcsr(0x1f80);
                sprintf(id, "mxcsr.rc.%s.%u", v ? "neg" : "pos", rc);
                fid_check(out == want[v][rc], id, "cvtss2si(%g) = %d want %d", vals2[v], out, want[v][rc]);
            }
    }
    setcsr(0x1f80);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "x87test";
    fid_environment();
    group_fxam();
    group_tags();
    group_saverestore();
    group_control();
    group_flags();
    group_compare();
    group_mmx();
    group_pointers();
    group_mxcsr();
    return fid_summary();
}
