/* fiber_asm.S: helpers for fibertest.c (x86-64, AT&T, llvm-mingw).
 *
 * uint32 switch_checked(void *target, const uint64 gpr[8], const uint8 xmm[10][16])
 *   loads rbx rbp rsi rdi r12 r13 r14 r15 from gpr[] and xmm6..15 from xmm[], calls SwitchToFiber(target)
 *   with an ABI-aligned stack, and when this fiber is resumed compares the same registers with the same
 *   arrays. Returns a mask: bit i for gpr[i], bit 8+j for xmm(6+j). The caller's nonvolatile state is
 *   saved and restored around all of it. */
    .text
    .globl  switch_checked
    .def    switch_checked; .scl 2; .type 32; .endef
    .seh_proc switch_checked
switch_checked:
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
    subq    $0xd8, %rsp
    .seh_stackalloc 0xd8
    movdqa  %xmm6,  0x30(%rsp)
    .seh_savexmm %xmm6, 0x30
    movdqa  %xmm7,  0x40(%rsp)
    .seh_savexmm %xmm7, 0x40
    movdqa  %xmm8,  0x50(%rsp)
    .seh_savexmm %xmm8, 0x50
    movdqa  %xmm9,  0x60(%rsp)
    .seh_savexmm %xmm9, 0x60
    movdqa  %xmm10, 0x70(%rsp)
    .seh_savexmm %xmm10, 0x70
    movdqa  %xmm11, 0x80(%rsp)
    .seh_savexmm %xmm11, 0x80
    movdqa  %xmm12, 0x90(%rsp)
    .seh_savexmm %xmm12, 0x90
    movdqa  %xmm13, 0xa0(%rsp)
    .seh_savexmm %xmm13, 0xa0
    movdqa  %xmm14, 0xb0(%rsp)
    .seh_savexmm %xmm14, 0xb0
    movdqa  %xmm15, 0xc0(%rsp)
    .seh_savexmm %xmm15, 0xc0
    .seh_endprologue
    movq    %rdx, 0x20(%rsp)            /* gpr pattern, kept on this fiber's stack */
    movq    %r8,  0x28(%rsp)            /* xmm pattern */
    movq    0x00(%rdx), %rbx
    movq    0x08(%rdx), %rbp
    movq    0x10(%rdx), %rsi
    movq    0x18(%rdx), %rdi
    movq    0x20(%rdx), %r12
    movq    0x28(%rdx), %r13
    movq    0x30(%rdx), %r14
    movq    0x38(%rdx), %r15
    movdqu  0x00(%r8), %xmm6
    movdqu  0x10(%r8), %xmm7
    movdqu  0x20(%r8), %xmm8
    movdqu  0x30(%r8), %xmm9
    movdqu  0x40(%r8), %xmm10
    movdqu  0x50(%r8), %xmm11
    movdqu  0x60(%r8), %xmm12
    movdqu  0x70(%r8), %xmm13
    movdqu  0x80(%r8), %xmm14
    movdqu  0x90(%r8), %xmm15
    callq   *g_pSwitchToFiber(%rip)     /* rcx = target; 0x00-0x1f of our frame is the shadow space */
    xorl    %eax, %eax
    movq    0x20(%rsp), %rdx
    movq    0x28(%rsp), %r8
#define CHKG(reg, off, bit) cmpq off(%rdx), reg; je 1f; orl $(1 << bit), %eax; 1:
#define CHKX(reg, off, bit) movdqu off(%r8), %xmm0; pcmpeqb reg, %xmm0; pmovmskb %xmm0, %ecx; cmpl $0xffff, %ecx; je 1f; orl $(1 << bit), %eax; 1:
    CHKG(%rbx, 0x00, 0)
    CHKG(%rbp, 0x08, 1)
    CHKG(%rsi, 0x10, 2)
    CHKG(%rdi, 0x18, 3)
    CHKG(%r12, 0x20, 4)
    CHKG(%r13, 0x28, 5)
    CHKG(%r14, 0x30, 6)
    CHKG(%r15, 0x38, 7)
    CHKX(%xmm6,  0x00, 8)
    CHKX(%xmm7,  0x10, 9)
    CHKX(%xmm8,  0x20, 10)
    CHKX(%xmm9,  0x30, 11)
    CHKX(%xmm10, 0x40, 12)
    CHKX(%xmm11, 0x50, 13)
    CHKX(%xmm12, 0x60, 14)
    CHKX(%xmm13, 0x70, 15)
    CHKX(%xmm14, 0x80, 16)
    CHKX(%xmm15, 0x90, 17)
    movdqa  0x30(%rsp), %xmm6
    movdqa  0x40(%rsp), %xmm7
    movdqa  0x50(%rsp), %xmm8
    movdqa  0x60(%rsp), %xmm9
    movdqa  0x70(%rsp), %xmm10
    movdqa  0x80(%rsp), %xmm11
    movdqa  0x90(%rsp), %xmm12
    movdqa  0xa0(%rsp), %xmm13
    movdqa  0xb0(%rsp), %xmm14
    movdqa  0xc0(%rsp), %xmm15
    addq    $0xd8, %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rdi
    popq    %rsi
    popq    %rbp
    popq    %rbx
    ret
    .seh_endproc

/* void raise_int3(void): an int3 the vectored handler steps over */
    .globl raise_int3
raise_int3:
    int3
    ret
