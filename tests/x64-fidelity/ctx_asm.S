/* Assembly half of ctxtest.c (x86-64, AT&T syntax, llvm-mingw).
 *
 * struct regstate (ctxtest.c), offsets used below:
 *   0x000 gpr[16]   rax rcx rdx rbx rsp rbp rsi rdi r8..r15 (x86 encoding order)
 *   0x080 rflags
 *   0x088 rip        (unused here)
 *   0x090 mxcsr      (32 bit)
 *   0x094 fcw        (16 bit)
 *   0x0a0 ymm[16][32] (low 16 bytes = xmm)
 *   0x2a0 x87[3][16] three 80-bit values, pushed in order 0, 1, 2 (so ST0 = x87[2])
 *   0x2e0 fx[512]     fxsave64 image taken at the resume point
 */

#define RS_RFLAGS 0x80
#define RS_MXCSR  0x90
#define RS_FCW    0x94
#define RS_YMM    0xa0
#define RS_X87    0x2a0
#define RS_FX     0x2e0

    .text

/* LOAD_STATE sym: x87 stack and control word, MXCSR, vector registers, RFLAGS, then every GPR but rsp */
.macro LOAD_STATE sym
    fninit
    fldcw   \sym+RS_FCW(%rip)
    fldt    \sym+RS_X87+0x00(%rip)
    fldt    \sym+RS_X87+0x10(%rip)
    fldt    \sym+RS_X87+0x20(%rip)
    ldmxcsr \sym+RS_MXCSR(%rip)
    cmpb    $0, g_has_avx(%rip)
    je      1f
    vmovdqu \sym+RS_YMM+0x000(%rip), %ymm0
    vmovdqu \sym+RS_YMM+0x020(%rip), %ymm1
    vmovdqu \sym+RS_YMM+0x040(%rip), %ymm2
    vmovdqu \sym+RS_YMM+0x060(%rip), %ymm3
    vmovdqu \sym+RS_YMM+0x080(%rip), %ymm4
    vmovdqu \sym+RS_YMM+0x0a0(%rip), %ymm5
    vmovdqu \sym+RS_YMM+0x0c0(%rip), %ymm6
    vmovdqu \sym+RS_YMM+0x0e0(%rip), %ymm7
    vmovdqu \sym+RS_YMM+0x100(%rip), %ymm8
    vmovdqu \sym+RS_YMM+0x120(%rip), %ymm9
    vmovdqu \sym+RS_YMM+0x140(%rip), %ymm10
    vmovdqu \sym+RS_YMM+0x160(%rip), %ymm11
    vmovdqu \sym+RS_YMM+0x180(%rip), %ymm12
    vmovdqu \sym+RS_YMM+0x1a0(%rip), %ymm13
    vmovdqu \sym+RS_YMM+0x1c0(%rip), %ymm14
    vmovdqu \sym+RS_YMM+0x1e0(%rip), %ymm15
    jmp     2f
1:  movdqu  \sym+RS_YMM+0x000(%rip), %xmm0
    movdqu  \sym+RS_YMM+0x020(%rip), %xmm1
    movdqu  \sym+RS_YMM+0x040(%rip), %xmm2
    movdqu  \sym+RS_YMM+0x060(%rip), %xmm3
    movdqu  \sym+RS_YMM+0x080(%rip), %xmm4
    movdqu  \sym+RS_YMM+0x0a0(%rip), %xmm5
    movdqu  \sym+RS_YMM+0x0c0(%rip), %xmm6
    movdqu  \sym+RS_YMM+0x0e0(%rip), %xmm7
    movdqu  \sym+RS_YMM+0x100(%rip), %xmm8
    movdqu  \sym+RS_YMM+0x120(%rip), %xmm9
    movdqu  \sym+RS_YMM+0x140(%rip), %xmm10
    movdqu  \sym+RS_YMM+0x160(%rip), %xmm11
    movdqu  \sym+RS_YMM+0x180(%rip), %xmm12
    movdqu  \sym+RS_YMM+0x1a0(%rip), %xmm13
    movdqu  \sym+RS_YMM+0x1c0(%rip), %xmm14
    movdqu  \sym+RS_YMM+0x1e0(%rip), %xmm15
2:  pushq   \sym+RS_RFLAGS(%rip)
    popfq
    movq    \sym+0x00(%rip), %rax
    movq    \sym+0x08(%rip), %rcx
    movq    \sym+0x10(%rip), %rdx
    movq    \sym+0x18(%rip), %rbx
    movq    \sym+0x28(%rip), %rbp
    movq    \sym+0x30(%rip), %rsi
    movq    \sym+0x38(%rip), %rdi
    movq    \sym+0x40(%rip), %r8
    movq    \sym+0x48(%rip), %r9
    movq    \sym+0x50(%rip), %r10
    movq    \sym+0x58(%rip), %r11
    movq    \sym+0x60(%rip), %r12
    movq    \sym+0x68(%rip), %r13
    movq    \sym+0x70(%rip), %r14
    movq    \sym+0x78(%rip), %r15
.endm

/* STORE_STATE sym: every GPR, RFLAGS, MXCSR, FCW, an fxsave64 image, vector registers */
.macro STORE_STATE sym
    movq    %rax, \sym+0x00(%rip)
    movq    %rcx, \sym+0x08(%rip)
    movq    %rdx, \sym+0x10(%rip)
    movq    %rbx, \sym+0x18(%rip)
    movq    %rsp, \sym+0x20(%rip)
    movq    %rbp, \sym+0x28(%rip)
    movq    %rsi, \sym+0x30(%rip)
    movq    %rdi, \sym+0x38(%rip)
    movq    %r8,  \sym+0x40(%rip)
    movq    %r9,  \sym+0x48(%rip)
    movq    %r10, \sym+0x50(%rip)
    movq    %r11, \sym+0x58(%rip)
    movq    %r12, \sym+0x60(%rip)
    movq    %r13, \sym+0x68(%rip)
    movq    %r14, \sym+0x70(%rip)
    movq    %r15, \sym+0x78(%rip)
    pushfq
    popq    \sym+RS_RFLAGS(%rip)
    stmxcsr \sym+RS_MXCSR(%rip)
    fnstcw  \sym+RS_FCW(%rip)
    fxsave64 \sym+RS_FX(%rip)
    cmpb    $0, g_has_avx(%rip)
    je      1f
    vmovdqu %ymm0,  \sym+RS_YMM+0x000(%rip)
    vmovdqu %ymm1,  \sym+RS_YMM+0x020(%rip)
    vmovdqu %ymm2,  \sym+RS_YMM+0x040(%rip)
    vmovdqu %ymm3,  \sym+RS_YMM+0x060(%rip)
    vmovdqu %ymm4,  \sym+RS_YMM+0x080(%rip)
    vmovdqu %ymm5,  \sym+RS_YMM+0x0a0(%rip)
    vmovdqu %ymm6,  \sym+RS_YMM+0x0c0(%rip)
    vmovdqu %ymm7,  \sym+RS_YMM+0x0e0(%rip)
    vmovdqu %ymm8,  \sym+RS_YMM+0x100(%rip)
    vmovdqu %ymm9,  \sym+RS_YMM+0x120(%rip)
    vmovdqu %ymm10, \sym+RS_YMM+0x140(%rip)
    vmovdqu %ymm11, \sym+RS_YMM+0x160(%rip)
    vmovdqu %ymm12, \sym+RS_YMM+0x180(%rip)
    vmovdqu %ymm13, \sym+RS_YMM+0x1a0(%rip)
    vmovdqu %ymm14, \sym+RS_YMM+0x1c0(%rip)
    vmovdqu %ymm15, \sym+RS_YMM+0x1e0(%rip)
    jmp     2f
1:  movdqu  %xmm0,  \sym+RS_YMM+0x000(%rip)
    movdqu  %xmm1,  \sym+RS_YMM+0x020(%rip)
    movdqu  %xmm2,  \sym+RS_YMM+0x040(%rip)
    movdqu  %xmm3,  \sym+RS_YMM+0x060(%rip)
    movdqu  %xmm4,  \sym+RS_YMM+0x080(%rip)
    movdqu  %xmm5,  \sym+RS_YMM+0x0a0(%rip)
    movdqu  %xmm6,  \sym+RS_YMM+0x0c0(%rip)
    movdqu  %xmm7,  \sym+RS_YMM+0x0e0(%rip)
    movdqu  %xmm8,  \sym+RS_YMM+0x100(%rip)
    movdqu  %xmm9,  \sym+RS_YMM+0x120(%rip)
    movdqu  %xmm10, \sym+RS_YMM+0x140(%rip)
    movdqu  %xmm11, \sym+RS_YMM+0x160(%rip)
    movdqu  %xmm12, \sym+RS_YMM+0x180(%rip)
    movdqu  %xmm13, \sym+RS_YMM+0x1a0(%rip)
    movdqu  %xmm14, \sym+RS_YMM+0x1c0(%rip)
    movdqu  %xmm15, \sym+RS_YMM+0x1e0(%rip)
2:
.endm

/* Back to a clean C environment: default x87 and MXCSR state, DF clear, upper YMM clean */
.macro CLEAN_ENV
    fninit
    fldcw   g_fcw_default(%rip)
    ldmxcsr g_mxcsr_default(%rip)
    cld
    cmpb    $0, g_has_avx(%rip)
    je      1f
    vzeroupper
1:
.endm

/* Prologue/epilogue saving everything the Windows x64 ABI makes nonvolatile, with unwind info */
.macro SAVE_NONVOL
    pushq   %rbx
    .seh_pushreg %rbx
    pushq   %rbp
    .seh_pushreg %rbp
    pushq   %rsi
    .seh_pushreg %rsi
    pushq   %rdi
    .seh_pushreg %rdi
    pushq   %r12
    .seh_pushreg %r12
    pushq   %r13
    .seh_pushreg %r13
    pushq   %r14
    .seh_pushreg %r14
    pushq   %r15
    .seh_pushreg %r15
    subq    $0xa8, %rsp
    .seh_stackalloc 0xa8
    movdqa  %xmm6,  0x00(%rsp)
    .seh_savexmm %xmm6, 0x00
    movdqa  %xmm7,  0x10(%rsp)
    .seh_savexmm %xmm7, 0x10
    movdqa  %xmm8,  0x20(%rsp)
    .seh_savexmm %xmm8, 0x20
    movdqa  %xmm9,  0x30(%rsp)
    .seh_savexmm %xmm9, 0x30
    movdqa  %xmm10, 0x40(%rsp)
    .seh_savexmm %xmm10, 0x40
    movdqa  %xmm11, 0x50(%rsp)
    .seh_savexmm %xmm11, 0x50
    movdqa  %xmm12, 0x60(%rsp)
    .seh_savexmm %xmm12, 0x60
    movdqa  %xmm13, 0x70(%rsp)
    .seh_savexmm %xmm13, 0x70
    movdqa  %xmm14, 0x80(%rsp)
    .seh_savexmm %xmm14, 0x80
    movdqa  %xmm15, 0x90(%rsp)
    .seh_savexmm %xmm15, 0x90
    .seh_endprologue
.endm

.macro RESTORE_NONVOL
    movdqa  0x00(%rsp), %xmm6
    movdqa  0x10(%rsp), %xmm7
    movdqa  0x20(%rsp), %xmm8
    movdqa  0x30(%rsp), %xmm9
    movdqa  0x40(%rsp), %xmm10
    movdqa  0x50(%rsp), %xmm11
    movdqa  0x60(%rsp), %xmm12
    movdqa  0x70(%rsp), %xmm13
    movdqa  0x80(%rsp), %xmm14
    movdqa  0x90(%rsp), %xmm15
    addq    $0xa8, %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rdi
    popq    %rsi
    popq    %rbp
    popq    %rbx
.endm

/* void run_scenario(void): load g_before, record rsp, jump to *g_fault_target; the handler sends the
 * thread to the stub's continuation, which jumps back to scenario_resume. */
    .globl  run_scenario
    .def    run_scenario; .scl 2; .type 32; .endef
    .seh_proc run_scenario
run_scenario:
    SAVE_NONVOL
    movq    %rsp, g_saved_rsp(%rip)
    LOAD_STATE g_before
    movq    %rsp, g_before+0x20(%rip)
    jmp     *g_fault_target(%rip)
    .globl  scenario_resume
scenario_resume:
    STORE_STATE g_after
    movq    g_saved_rsp(%rip), %rsp
    CLEAN_ENV
    RESTORE_NONVOL
    ret
    .seh_endproc

/* Fault stubs. Each faulting instruction is followed by a label the handler resumes at. */
    .globl stub_int3, stub_int3_end
stub_int3:      int3
stub_int3_end:  jmp scenario_resume

    .globl stub_int3_long, stub_int3_long_end
stub_int3_long: int $3
stub_int3_long_end: jmp scenario_resume

    .globl stub_ud2, stub_ud2_end
stub_ud2:       ud2
stub_ud2_end:   jmp scenario_resume

    .globl stub_div0, stub_div0_end
stub_div0:      divq %rcx
stub_div0_end:  jmp scenario_resume

    .globl stub_idivovf, stub_idivovf_end
stub_idivovf:   idivq %rcx
stub_idivovf_end: jmp scenario_resume

    .globl stub_read, stub_read_end
stub_read:      movq (%rsi), %rax
stub_read_end:  jmp scenario_resume

    .globl stub_write, stub_write_end
stub_write:     movq %rax, (%rsi)
stub_write_end: jmp scenario_resume

    .globl stub_exec, stub_exec_end
stub_exec:      callq *%rsi
stub_exec_end:  jmp scenario_resume

    .globl stub_hlt, stub_hlt_end
stub_hlt:       hlt
stub_hlt_end:   jmp scenario_resume

    .globl stub_in, stub_in_end
stub_in:        inb %dx, %al
stub_in_end:    jmp scenario_resume

    .globl stub_split, stub_split_end
stub_split:     movq (%rsi), %rax          /* rsi = 4 bytes before a no-access page */
stub_split_end: jmp scenario_resume

    .globl stub_noncanon, stub_noncanon_end
stub_noncanon:  movq (%rsi), %rax          /* rsi = a non-canonical address */
stub_noncanon_end: jmp scenario_resume

    .globl stub_movaps, stub_movaps_end
stub_movaps:    movaps (%rsi), %xmm0       /* rsi misaligned by 8 */
stub_movaps_end: jmp scenario_resume

    /* std; rep movsb backwards across into a no-access page, resumed by committing it */
    .globl stub_repmovs, stub_repmovs_end
stub_repmovs:   rep movsb
stub_repmovs_end: jmp scenario_resume

    /* trap flag: the single-step trap is taken after the nop */
    .globl stub_tf, stub_tf_after
stub_tf:        pushfq
                orq $0x100, (%rsp)
                popfq
                nop
stub_tf_after:  jmp scenario_resume

/* void handler_clobber(void): called by the C handler just before it returns, so the live state the
 * dispatcher sees differs from the context it is asked to continue with. Volatile state only. */
    .globl handler_clobber
handler_clobber:
    pushq   g_clobber_rflags(%rip)
    popfq
    cmpb    $0, g_has_avx(%rip)
    je      1f
    vmovdqu g_clobber_vec(%rip), %ymm0
    vmovdqu g_clobber_vec(%rip), %ymm1
    vmovdqu g_clobber_vec(%rip), %ymm2
    vmovdqu g_clobber_vec(%rip), %ymm3
    vmovdqu g_clobber_vec(%rip), %ymm4
    vmovdqu g_clobber_vec(%rip), %ymm5
    /* upper halves of the nonvolatile ymm6..15 are volatile in the Windows x64 ABI */
    vinsertf128 $1, g_clobber_vec(%rip), %ymm6, %ymm6
    vinsertf128 $1, g_clobber_vec(%rip), %ymm7, %ymm7
    vinsertf128 $1, g_clobber_vec(%rip), %ymm8, %ymm8
    vinsertf128 $1, g_clobber_vec(%rip), %ymm9, %ymm9
    vinsertf128 $1, g_clobber_vec(%rip), %ymm10, %ymm10
    vinsertf128 $1, g_clobber_vec(%rip), %ymm11, %ymm11
    vinsertf128 $1, g_clobber_vec(%rip), %ymm12, %ymm12
    vinsertf128 $1, g_clobber_vec(%rip), %ymm13, %ymm13
    vinsertf128 $1, g_clobber_vec(%rip), %ymm14, %ymm14
    vinsertf128 $1, g_clobber_vec(%rip), %ymm15, %ymm15
    jmp     2f
1:  movdqu  g_clobber_vec(%rip), %xmm0
    movdqu  g_clobber_vec(%rip), %xmm1
    movdqu  g_clobber_vec(%rip), %xmm2
    movdqu  g_clobber_vec(%rip), %xmm3
    movdqu  g_clobber_vec(%rip), %xmm4
    movdqu  g_clobber_vec(%rip), %xmm5
2:  /* overwrite the physical x87 registers, leave the stack empty */
    fldt    g_clobber_x87(%rip)
    fldt    g_clobber_x87(%rip)
    fldt    g_clobber_x87(%rip)
    fldt    g_clobber_x87(%rip)
    fstp    %st(0)
    fstp    %st(0)
    fstp    %st(0)
    fstp    %st(0)
    ret

/* nested_int3(): an int3 raised from inside the vectored handler */
    .globl nested_int3, nested_int3_end
nested_int3:
    int3
nested_int3_end:
    ret

/* uint64 read_rflags(void) */
    .globl read_rflags
read_rflags:
    pushfq
    popq %rax
    ret

/* ---------- SEH unwinding: nonvolatile registers restored by RtlUnwindEx ----------
 * seh_outer(): sets every nonvolatile register to g_outer (gpr/ymm layout of struct regstate),
 * calls seh_inner(), which saves them in its prologue (unwind codes), loads g_inner and faults.
 * seh_outer's language handler (C) unwinds to seh_outer_catch, which stores what it finds in g_after.
 * Windows restores the outer values from seh_inner's prologue saves. */
    .globl  seh_outer
    .def    seh_outer; .scl 2; .type 32; .endef
    .seh_proc seh_outer
    .seh_handler seh_outer_handler, @except
seh_outer:
    SAVE_NONVOL
    movq    %rsp, g_saved_rsp(%rip)
    movq    g_outer+0x18(%rip), %rbx
    movq    g_outer+0x28(%rip), %rbp
    movq    g_outer+0x30(%rip), %rsi
    movq    g_outer+0x38(%rip), %rdi
    movq    g_outer+0x60(%rip), %r12
    movq    g_outer+0x68(%rip), %r13
    movq    g_outer+0x70(%rip), %r14
    movq    g_outer+0x78(%rip), %r15
    movdqu  g_outer+RS_YMM+0x0c0(%rip), %xmm6
    movdqu  g_outer+RS_YMM+0x0e0(%rip), %xmm7
    movdqu  g_outer+RS_YMM+0x100(%rip), %xmm8
    movdqu  g_outer+RS_YMM+0x120(%rip), %xmm9
    movdqu  g_outer+RS_YMM+0x140(%rip), %xmm10
    movdqu  g_outer+RS_YMM+0x160(%rip), %xmm11
    movdqu  g_outer+RS_YMM+0x180(%rip), %xmm12
    movdqu  g_outer+RS_YMM+0x1a0(%rip), %xmm13
    movdqu  g_outer+RS_YMM+0x1c0(%rip), %xmm14
    movdqu  g_outer+RS_YMM+0x1e0(%rip), %xmm15
    callq   seh_inner
    nop
    .globl  seh_outer_catch
seh_outer_catch:
    STORE_STATE g_after
    movq    g_saved_rsp(%rip), %rsp
    CLEAN_ENV
    RESTORE_NONVOL
    ret
    .seh_endproc

    .def    seh_inner; .scl 2; .type 32; .endef
    .seh_proc seh_inner
seh_inner:
    SAVE_NONVOL
    movq    g_inner+0x18(%rip), %rbx
    movq    g_inner+0x28(%rip), %rbp
    movq    g_inner+0x30(%rip), %rsi
    movq    g_inner+0x38(%rip), %rdi
    movq    g_inner+0x60(%rip), %r12
    movq    g_inner+0x68(%rip), %r13
    movq    g_inner+0x70(%rip), %r14
    movq    g_inner+0x78(%rip), %r15
    movdqu  g_inner+RS_YMM+0x0c0(%rip), %xmm6
    movdqu  g_inner+RS_YMM+0x0e0(%rip), %xmm7
    movdqu  g_inner+RS_YMM+0x100(%rip), %xmm8
    movdqu  g_inner+RS_YMM+0x120(%rip), %xmm9
    movdqu  g_inner+RS_YMM+0x140(%rip), %xmm10
    movdqu  g_inner+RS_YMM+0x160(%rip), %xmm11
    movdqu  g_inner+RS_YMM+0x180(%rip), %xmm12
    movdqu  g_inner+RS_YMM+0x1a0(%rip), %xmm13
    movdqu  g_inner+RS_YMM+0x1c0(%rip), %xmm14
    movdqu  g_inner+RS_YMM+0x1e0(%rip), %xmm15
    .globl  seh_inner_fault
seh_inner_fault:
    ud2
    RESTORE_NONVOL
    ret
    .seh_endproc

/* ---------- RtlCaptureContext ----------
 * capture_probe(CONTEXT *ctx): loads g_before into every register but rcx (the argument) and rsp,
 * calls RtlCaptureContext(ctx) and records the return address in g_capture_ret. */
    .globl  capture_probe
    .def    capture_probe; .scl 2; .type 32; .endef
    .seh_proc capture_probe
capture_probe:
    SAVE_NONVOL
    movq    %rcx, g_capture_ctx(%rip)
    movq    %rsp, g_saved_rsp(%rip)
    subq    g_capture_pad(%rip), %rsp   /* before LOAD_STATE: sub changes the flags. 0x20 keeps the ABI's
                                           16-byte alignment at the call; 0x28 is the misaligned variant */
    LOAD_STATE g_before
    movq    g_capture_ctx(%rip), %rcx
    movq    %rsp, g_before+0x20(%rip)
    callq   *g_pRtlCaptureContext(%rip)
    .globl  capture_ret
capture_ret:
    STORE_STATE g_after
    movq    g_saved_rsp(%rip), %rsp
    CLEAN_ENV
    RESTORE_NONVOL
    ret
    .seh_endproc

/* restore_probe(CONTEXT *ctx): RtlRestoreContext(ctx, NULL) with ctx->Rip = scenario_resume and
 * ctx->Rsp inside this frame; the stored state must be the context's. */
    .globl  restore_probe
    .def    restore_probe; .scl 2; .type 32; .endef
    .seh_proc restore_probe
restore_probe:
    SAVE_NONVOL
    movq    %rsp, g_saved_rsp(%rip)
    subq    $0x20, %rsp
    xorl    %edx, %edx
    callq   *g_pRtlRestoreContext(%rip)
    ud2
    .seh_endproc

/* ---------- other-thread context ----------
 * spin_thread(): loads g_before, records rsp, then spins at spin_loop until another thread moves it.
 * Before spinning it bumps g_spin_ready so the main thread knows the state is loaded. */
    .globl  spin_thread
    .def    spin_thread; .scl 2; .type 32; .endef
    .seh_proc spin_thread
spin_thread:
    SAVE_NONVOL
    movq    %rsp, g_spin_saved_rsp(%rip)
    LOAD_STATE g_before
    movq    %rsp, g_before+0x20(%rip)
    lock incl g_spin_ready(%rip)       /* changes flags: the pattern's flags are set again below */
    pushq   g_before+RS_RFLAGS(%rip)
    popfq
    .globl  spin_loop
spin_loop:
    jmp     spin_loop
    .globl  spin_exit
spin_exit:                              /* SetThreadContext target */
    STORE_STATE g_after
    movq    g_spin_saved_rsp(%rip), %rsp
    CLEAN_ENV
    RESTORE_NONVOL
    xorl    %eax, %eax
    ret
    .seh_endproc

/* ---------- suspend/resume stress ----------
 * stress_thread(): keeps an x87 stack of three values (TOP=5), a non-default FCW and MXCSR, DF set,
 * and known ymm values, and verifies them in a loop while another thread suspends and resumes it.
 * Any mismatch stores the iteration in g_stress_bad and stops. Runs until g_stress_stop is set. */
    .globl  stress_thread
    .def    stress_thread; .scl 2; .type 32; .endef
    .seh_proc stress_thread
stress_thread:
    SAVE_NONVOL
    movq    %rsp, g_spin_saved_rsp(%rip)
    LOAD_STATE g_before
    xorq    %r12, %r12                  /* iteration counter */
3:  incq    %r12
    movq    %r12, g_stress_iter(%rip)
    /* integer registers loaded by LOAD_STATE must be intact */
    cmpq    g_before+0x18(%rip), %rbx
    jne     9f
    cmpq    g_before+0x30(%rip), %rsi
    jne     9f
    cmpq    g_before+0x70(%rip), %r14
    jne     9f
    /* PF from a known operation: 0x03 has even parity (PF=1); then check AF after it */
    movb    $0x0f, %al
    addb    $0x01, %al                  /* 0x10: AF=1 (carry out of bit 3), PF=0 (one bit set) */
    pause
    jp      9f
    pushfq
    popq    %r13
    testq   $0x10, %r13                 /* AF */
    jz      9f
    testq   $0x400, %r13                /* DF still set */
    jz      9f
    /* x87: TOP must be 5 and ST0..2 the pushed values; FCW as loaded */
    fnstsw  %ax
    shrw    $11, %ax
    andw    $7, %ax
    cmpw    $5, %ax
    jne     9f
    fnstcw  g_stress_fcw(%rip)
    movw    g_stress_fcw(%rip), %ax
    cmpw    g_before+RS_FCW(%rip), %ax
    jne     9f
    fld     %st(0)                      /* compare ST0 with x87[2] by storing it */
    fstpt   g_stress_st0(%rip)
    movq    g_stress_st0(%rip), %rax
    cmpq    g_before+RS_X87+0x20(%rip), %rax
    jne     9f
    stmxcsr g_stress_mxcsr(%rip)
    movl    g_stress_mxcsr(%rip), %eax
    andl    $0xffc0, %eax               /* control bits only */
    movl    g_before+RS_MXCSR(%rip), %ecx
    andl    $0xffc0, %ecx
    cmpl    %ecx, %eax
    jne     9f
    cmpb    $0, g_has_avx(%rip)
    je      4f
    vextractf128 $1, %ymm9, g_stress_vec(%rip)
    movq    g_stress_vec(%rip), %rax
    cmpq    g_before+RS_YMM+0x130(%rip), %rax      /* upper half of ymm9 */
    jne     9f
    vextractf128 $1, %ymm2, g_stress_vec(%rip)
    movq    g_stress_vec+8(%rip), %rax
    cmpq    g_before+RS_YMM+0x058(%rip), %rax      /* upper half of ymm2, high qword */
    jne     9f
4:  movdqu  %xmm3, g_stress_vec(%rip)
    movq    g_stress_vec(%rip), %rax
    cmpq    g_before+RS_YMM+0x060(%rip), %rax
    jne     9f
    pushq   g_before+RS_RFLAGS(%rip)    /* back to the pattern (DF set) for the next round */
    popfq
    cmpl    $0, g_stress_stop(%rip)
    je      3b
    jmp     8f
9:  movq    %r12, g_stress_bad(%rip)
8:  movq    g_spin_saved_rsp(%rip), %rsp
    CLEAN_ENV
    RESTORE_NONVOL
    xorl    %eax, %eax
    ret
    .seh_endproc
