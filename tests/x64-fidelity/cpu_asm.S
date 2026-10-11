/* Assembly helpers for cpuidtest.c and x87test.c (x86-64, AT&T syntax, llvm-mingw).
 * Windows x64 ABI: arguments in rcx, rdx, r8, r9; xmm6-15 nonvolatile. */
    .text

/* landing pad for probes that raise #UD: the vectored handler sets Rip here (rsp untouched) */
    .globl probe_ud_landing
probe_ud_landing:
    movl    $-1, %eax
    ret

    .globl probe_sse3
probe_sse3:
    haddps %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_ssse3
probe_ssse3:
    pshufb %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_sse41
probe_sse41:
    ptest %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_sse42
probe_sse42:
    crc32l %eax, %eax
    xorl    %eax, %eax
    ret

    .globl probe_popcnt
probe_popcnt:
    popcntl %eax, %eax
    xorl    %eax, %eax
    ret

    .globl probe_movbe
probe_movbe:
    movbel (%rsp), %eax
    xorl    %eax, %eax
    ret

    .globl probe_aes
probe_aes:
    aesenc %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_pclmul
probe_pclmul:
    pclmulqdq $0, %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_avx
probe_avx:
    vaddps %ymm0, %ymm0, %ymm0
    vzeroupper
    xorl    %eax, %eax
    ret

    .globl probe_avx2
probe_avx2:
    vpaddd %ymm0, %ymm0, %ymm0
    vzeroupper
    xorl    %eax, %eax
    ret

    .globl probe_fma
probe_fma:
    vfmadd231ps %xmm0, %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_f16c
probe_f16c:
    vcvtph2ps %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_bmi1
probe_bmi1:
    andnl %eax, %eax, %eax
    xorl    %eax, %eax
    ret

    .globl probe_bmi2
probe_bmi2:
    pdepl %eax, %eax, %eax
    xorl    %eax, %eax
    ret

    .globl probe_lzcnt
probe_lzcnt:
    lzcntl %eax, %eax
    xorl    %eax, %eax
    ret

    .globl probe_adx
probe_adx:
    adcxl %eax, %eax
    xorl    %eax, %eax
    ret

    .globl probe_rdrand
probe_rdrand:
    rdrandl %eax
    xorl    %eax, %eax
    ret

    .globl probe_rdseed
probe_rdseed:
    rdseedl %eax
    xorl    %eax, %eax
    ret

    .globl probe_sha
probe_sha:
    sha1nexte %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_rdtscp
probe_rdtscp:
    rdtscp
    xorl    %eax, %eax
    ret

    .globl probe_rdpid
probe_rdpid:
    rdpid %rax
    xorl    %eax, %eax
    ret

    .globl probe_xgetbv1
probe_xgetbv1:
    movl $1, %ecx
    xgetbv
    xorl    %eax, %eax
    ret

    .globl probe_xsaveopt
probe_xsaveopt:
    movl $3, %eax
    xorl %edx, %edx
    xsaveopt64 (%rcx)
    xorl    %eax, %eax
    ret

    .globl probe_xsavec
probe_xsavec:
    movl $3, %eax
    xorl %edx, %edx
    xsavec64 (%rcx)
    xorl    %eax, %eax
    ret

    .globl probe_avx512
probe_avx512:
    vpxord %zmm0, %zmm0, %zmm0
    vzeroupper
    xorl    %eax, %eax
    ret

    .globl probe_vaes
probe_vaes:
    vaesenc %ymm0, %ymm0, %ymm0
    vzeroupper
    xorl    %eax, %eax
    ret

    .globl probe_vpclmul
probe_vpclmul:
    vpclmulqdq $0, %ymm0, %ymm0, %ymm0
    vzeroupper
    xorl    %eax, %eax
    ret

    .globl probe_clflushopt
probe_clflushopt:
    clflushopt (%rsp)
    xorl    %eax, %eax
    ret

    .globl probe_clwb
probe_clwb:
    clwb (%rsp)
    xorl    %eax, %eax
    ret

    .globl probe_avxvnni
probe_avxvnni:
    {vex} vpdpbusd %ymm0, %ymm0, %ymm0
    vzeroupper
    xorl    %eax, %eax
    ret

    .globl probe_gfni
probe_gfni:
    gf2p8mulb %xmm0, %xmm0
    xorl    %eax, %eax
    ret

    .globl probe_cx16
probe_cx16:
    subq $24, %rsp
    movq %rbx, 16(%rsp)
    movq $0, (%rsp)
    movq $0, 8(%rsp)
    xorl %eax, %eax
    xorl %edx, %edx
    xorl %ecx, %ecx
    xorl %ebx, %ebx
    lock cmpxchg16b (%rsp)
    movq 16(%rsp), %rbx
    addq $24, %rsp
    xorl    %eax, %eax
    ret

/* uint64 xgetbv0(void) */
    .globl xgetbv0
xgetbv0:
    xorl    %ecx, %ecx
    xgetbv
    shlq    $32, %rdx
    orq     %rdx, %rax
    ret

/* void do_xsave(void *buf, uint64 rfbm), do_xrstor, do_xsavec, do_fxsave, do_fxrstor */
    .globl do_xsave, do_xrstor, do_xsavec, do_fxsave, do_fxrstor, do_xsaveopt
do_xsave:
    movq    %rdx, %rax
    shrq    $32, %rdx
    xsave64 (%rcx)
    ret
do_xsaveopt:
    movq    %rdx, %rax
    shrq    $32, %rdx
    xsaveopt64 (%rcx)
    ret
do_xsavec:
    movq    %rdx, %rax
    shrq    $32, %rdx
    xsavec64 (%rcx)
    ret
do_xrstor:
    movq    %rdx, %rax
    shrq    $32, %rdx
    xrstor64 (%rcx)
    ret
do_fxsave:
    fxsave64 (%rcx)
    ret
do_fxrstor:
    fxrstor64 (%rcx)
    ret

/* struct fpstate (cpuidtest.c): 0x00 fcw (u16), 0x04 mxcsr (u32), 0x08 avx (u32), 0x10 rfbm (u64),
 * 0x20 x87[3][16] pushed in order (ST0 = x87[2]), 0x60 ymm[16][32], 0x260 fx[512] (fxsave image) */
.macro LOAD_FP s
    fninit
    fldcw   0(\s)
    fldt    0x20(\s)
    fldt    0x30(\s)
    fldt    0x40(\s)
    ldmxcsr 4(\s)
    cmpl    $0, 8(\s)
    je      1f
    vmovdqu 0x060(\s), %ymm0
    vmovdqu 0x080(\s), %ymm1
    vmovdqu 0x0a0(\s), %ymm2
    vmovdqu 0x0c0(\s), %ymm3
    vmovdqu 0x0e0(\s), %ymm4
    vmovdqu 0x100(\s), %ymm5
    vmovdqu 0x120(\s), %ymm6
    vmovdqu 0x140(\s), %ymm7
    vmovdqu 0x160(\s), %ymm8
    vmovdqu 0x180(\s), %ymm9
    vmovdqu 0x1a0(\s), %ymm10
    vmovdqu 0x1c0(\s), %ymm11
    vmovdqu 0x1e0(\s), %ymm12
    vmovdqu 0x200(\s), %ymm13
    vmovdqu 0x220(\s), %ymm14
    vmovdqu 0x240(\s), %ymm15
    jmp     2f
1:  movdqu  0x060(\s), %xmm0
    movdqu  0x080(\s), %xmm1
    movdqu  0x0a0(\s), %xmm2
    movdqu  0x0c0(\s), %xmm3
    movdqu  0x0e0(\s), %xmm4
    movdqu  0x100(\s), %xmm5
    movdqu  0x120(\s), %xmm6
    movdqu  0x140(\s), %xmm7
    movdqu  0x160(\s), %xmm8
    movdqu  0x180(\s), %xmm9
    movdqu  0x1a0(\s), %xmm10
    movdqu  0x1c0(\s), %xmm11
    movdqu  0x1e0(\s), %xmm12
    movdqu  0x200(\s), %xmm13
    movdqu  0x220(\s), %xmm14
    movdqu  0x240(\s), %xmm15
2:
.endm

/* store: fxsave64 image to out->fx (16-aligned) and full ymm to out->ymm */
.macro STORE_FP o
    fxsave64 0x260(\o)
    cmpl    $0, 8(\o)
    je      1f
    vmovdqu %ymm0,  0x060(\o)
    vmovdqu %ymm1,  0x080(\o)
    vmovdqu %ymm2,  0x0a0(\o)
    vmovdqu %ymm3,  0x0c0(\o)
    vmovdqu %ymm4,  0x0e0(\o)
    vmovdqu %ymm5,  0x100(\o)
    vmovdqu %ymm6,  0x120(\o)
    vmovdqu %ymm7,  0x140(\o)
    vmovdqu %ymm8,  0x160(\o)
    vmovdqu %ymm9,  0x180(\o)
    vmovdqu %ymm10, 0x1a0(\o)
    vmovdqu %ymm11, 0x1c0(\o)
    vmovdqu %ymm12, 0x1e0(\o)
    vmovdqu %ymm13, 0x200(\o)
    vmovdqu %ymm14, 0x220(\o)
    vmovdqu %ymm15, 0x240(\o)
    jmp     2f
1:  movdqu  %xmm0,  0x060(\o)
    movdqu  %xmm1,  0x080(\o)
    movdqu  %xmm2,  0x0a0(\o)
    movdqu  %xmm3,  0x0c0(\o)
    movdqu  %xmm4,  0x0e0(\o)
    movdqu  %xmm5,  0x100(\o)
    movdqu  %xmm6,  0x120(\o)
    movdqu  %xmm7,  0x140(\o)
    movdqu  %xmm8,  0x160(\o)
    movdqu  %xmm9,  0x180(\o)
    movdqu  %xmm10, 0x1a0(\o)
    movdqu  %xmm11, 0x1c0(\o)
    movdqu  %xmm12, 0x1e0(\o)
    movdqu  %xmm13, 0x200(\o)
    movdqu  %xmm14, 0x220(\o)
    movdqu  %xmm15, 0x240(\o)
2:
.endm

/* Destroy every piece of x87/SSE/AVX state this file loads */
.macro DESTROY_FP avxflag
    fninit
    fldz
    fldz
    fldz
    fldz
    ffree   %st(0)
    ffree   %st(1)
    ffree   %st(2)
    ffree   %st(3)
    fninit
    movl    $0x1f80, 0xa4(%rsp)
    ldmxcsr 0xa4(%rsp)
    cmpl    $0, \avxflag
    je      1f
    vpcmpeqb %ymm0, %ymm0, %ymm0
    vmovdqa %ymm0, %ymm1
    vmovdqa %ymm0, %ymm2
    vmovdqa %ymm0, %ymm3
    vmovdqa %ymm0, %ymm4
    vmovdqa %ymm0, %ymm5
    vmovdqa %ymm0, %ymm6
    vmovdqa %ymm0, %ymm7
    vmovdqa %ymm0, %ymm8
    vmovdqa %ymm0, %ymm9
    vmovdqa %ymm0, %ymm10
    vmovdqa %ymm0, %ymm11
    vmovdqa %ymm0, %ymm12
    vmovdqa %ymm0, %ymm13
    vmovdqa %ymm0, %ymm14
    vmovdqa %ymm0, %ymm15
    jmp     2f
1:  pcmpeqb %xmm0, %xmm0
    movdqa  %xmm0, %xmm1
    movdqa  %xmm0, %xmm2
    movdqa  %xmm0, %xmm3
    movdqa  %xmm0, %xmm4
    movdqa  %xmm0, %xmm5
    movdqa  %xmm0, %xmm6
    movdqa  %xmm0, %xmm7
    movdqa  %xmm0, %xmm8
    movdqa  %xmm0, %xmm9
    movdqa  %xmm0, %xmm10
    movdqa  %xmm0, %xmm11
    movdqa  %xmm0, %xmm12
    movdqa  %xmm0, %xmm13
    movdqa  %xmm0, %xmm14
    movdqa  %xmm0, %xmm15
2:
.endm

/* void fp_roundtrip(const struct fpstate *in, void *area, struct fpstate *out, int mode)
 *   load `in`; save into `area` with mode 0 xsave(rfbm=in->rfbm) / 1 fxsave / 2 xsaveopt / 3 xsavec /
 *   4 nothing (the caller prepared `area`);
 *   destroy the registers; restore from `area` with mode 0,2 xrstor(rfbm) / 1 fxrstor / 3 xrstor;
 *   store what is in the registers into `out` (fxsave image + ymm).
 * xmm6-15 of the caller are saved and restored. */
    .globl fp_roundtrip
    .def    fp_roundtrip; .scl 2; .type 32; .endef
    .seh_proc fp_roundtrip
fp_roundtrip:
    pushq   %rbx
    .seh_pushreg %rbx
    pushq   %rsi
    .seh_pushreg %rsi
    pushq   %rdi
    .seh_pushreg %rdi
    subq    $0xb0, %rsp
    .seh_stackalloc 0xb0
    movdqu  %xmm6,  0x00(%rsp)
    movdqu  %xmm7,  0x10(%rsp)
    movdqu  %xmm8,  0x20(%rsp)
    movdqu  %xmm9,  0x30(%rsp)
    movdqu  %xmm10, 0x40(%rsp)
    movdqu  %xmm11, 0x50(%rsp)
    movdqu  %xmm12, 0x60(%rsp)
    movdqu  %xmm13, 0x70(%rsp)
    movdqu  %xmm14, 0x80(%rsp)
    movdqu  %xmm15, 0x90(%rsp)
    .seh_endprologue
    movq    %rcx, %rsi              /* in */
    movq    %rdx, %rdi              /* area */
    movq    %r8,  %rbx              /* out */
    movl    %r9d, 0xa0(%rsp)        /* mode */
    LOAD_FP %rsi
    movl    0xa0(%rsp), %ecx
    movq    0x10(%rsi), %rax
    movq    %rax, %rdx
    shrq    $32, %rdx
    cmpl    $4, %ecx                /* restore only, from an area the caller prepared */
    je      20f
    cmpl    $1, %ecx
    je      10f
    cmpl    $2, %ecx
    je      11f
    cmpl    $3, %ecx
    je      12f
    xsave64 (%rdi)
    jmp     20f
10: fxsave64 (%rdi)
    jmp     20f
11: xsaveopt64 (%rdi)
    jmp     20f
12: xsavec64 (%rdi)
20: DESTROY_FP 8(%rsi)
    movl    0xa0(%rsp), %ecx
    movq    0x10(%rsi), %rax
    movq    %rax, %rdx
    shrq    $32, %rdx
    cmpl    $1, %ecx
    je      21f
    xrstor64 (%rdi)
    jmp     30f
21: fxrstor64 (%rdi)
30: STORE_FP %rbx
    fninit
    movl    $0x27f, 0xa4(%rsp)
    fldcw   0xa4(%rsp)
    movl    $0x1f80, 0xa4(%rsp)
    ldmxcsr 0xa4(%rsp)
    cmpl    $0, 8(%rsi)
    je      31f
    vzeroupper
31: movdqu  0x00(%rsp), %xmm6
    movdqu  0x10(%rsp), %xmm7
    movdqu  0x20(%rsp), %xmm8
    movdqu  0x30(%rsp), %xmm9
    movdqu  0x40(%rsp), %xmm10
    movdqu  0x50(%rsp), %xmm11
    movdqu  0x60(%rsp), %xmm12
    movdqu  0x70(%rsp), %xmm13
    movdqu  0x80(%rsp), %xmm14
    movdqu  0x90(%rsp), %xmm15
    addq    $0xb0, %rsp
    popq    %rdi
    popq    %rsi
    popq    %rbx
    ret
    .seh_endproc

/* int avx_preempt_check(const uint8 ymm[16][32], uint64 iterations): loads ymm0-15, spins without
 * calls for `iterations` rounds (other threads keep the cores busy, so the thread is preempted many
 * times), then compares every ymm register with the input. Returns 0 when intact, else 1 + reg. */
    .globl avx_preempt_check
    .def    avx_preempt_check; .scl 2; .type 32; .endef
    .seh_proc avx_preempt_check
avx_preempt_check:
    subq    $0xb8, %rsp
    .seh_stackalloc 0xb8
    movdqu  %xmm6,  0x00(%rsp)
    movdqu  %xmm7,  0x10(%rsp)
    movdqu  %xmm8,  0x20(%rsp)
    movdqu  %xmm9,  0x30(%rsp)
    movdqu  %xmm10, 0x40(%rsp)
    movdqu  %xmm11, 0x50(%rsp)
    movdqu  %xmm12, 0x60(%rsp)
    movdqu  %xmm13, 0x70(%rsp)
    movdqu  %xmm14, 0x80(%rsp)
    movdqu  %xmm15, 0x90(%rsp)
    .seh_endprologue
    vmovdqu 0x000(%rcx), %ymm0
    vmovdqu 0x020(%rcx), %ymm1
    vmovdqu 0x040(%rcx), %ymm2
    vmovdqu 0x060(%rcx), %ymm3
    vmovdqu 0x080(%rcx), %ymm4
    vmovdqu 0x0a0(%rcx), %ymm5
    vmovdqu 0x0c0(%rcx), %ymm6
    vmovdqu 0x0e0(%rcx), %ymm7
    vmovdqu 0x100(%rcx), %ymm8
    vmovdqu 0x120(%rcx), %ymm9
    vmovdqu 0x140(%rcx), %ymm10
    vmovdqu 0x160(%rcx), %ymm11
    vmovdqu 0x180(%rcx), %ymm12
    vmovdqu 0x1a0(%rcx), %ymm13
    vmovdqu 0x1c0(%rcx), %ymm14
    vmovdqu 0x1e0(%rcx), %ymm15
1:  imulq   $3, %rdx, %rax
    decq    %rdx
    jnz     1b
    xorl    %eax, %eax
#define CMPY(n, off) vpcmpeqb off(%rcx), %ymm##n, %ymm##n; vpmovmskb %ymm##n, %r8d; cmpl $-1, %r8d; je 2f; movl $(n + 1), %eax; jmp 3f; 2:
    CMPY(0, 0x000)
    CMPY(1, 0x020)
    CMPY(2, 0x040)
    CMPY(3, 0x060)
    CMPY(4, 0x080)
    CMPY(5, 0x0a0)
    CMPY(6, 0x0c0)
    CMPY(7, 0x0e0)
    CMPY(8, 0x100)
    CMPY(9, 0x120)
    CMPY(10, 0x140)
    CMPY(11, 0x160)
    CMPY(12, 0x180)
    CMPY(13, 0x1a0)
    CMPY(14, 0x1c0)
    CMPY(15, 0x1e0)
3:  vzeroupper
    movdqu  0x00(%rsp), %xmm6
    movdqu  0x10(%rsp), %xmm7
    movdqu  0x20(%rsp), %xmm8
    movdqu  0x30(%rsp), %xmm9
    movdqu  0x40(%rsp), %xmm10
    movdqu  0x50(%rsp), %xmm11
    movdqu  0x60(%rsp), %xmm12
    movdqu  0x70(%rsp), %xmm13
    movdqu  0x80(%rsp), %xmm14
    movdqu  0x90(%rsp), %xmm15
    addq    $0xb8, %rsp
    ret
    .seh_endproc
