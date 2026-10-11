/* mem_asm.S: probes for memtest.c. A fault inside a probe is caught by the vectored handler, which
 * sets Rip to mprobe_landing (rsp unchanged, so its ret returns to the probe's caller with -1). */
    .text
    .globl mprobe_landing, mprobe_read1, mprobe_read8, mprobe_write1, mprobe_write8, mprobe_exec
mprobe_landing:
    movq    $-1, %rax
    ret
mprobe_read1:
    movzbl  (%rcx), %eax
    ret
mprobe_read8:
    movq    (%rcx), %rax
    ret
mprobe_write1:
    movb    %dl, (%rcx)
    xorl    %eax, %eax
    ret
mprobe_write8:
    movq    %rdx, (%rcx)
    xorl    %eax, %eax
    ret
mprobe_exec:
    jmp     *%rcx                   /* the target's ret returns to our caller */

/* Inline self-modifying code, copied into an RWX page by memtest.c:
 *   inline_smc(uint32 v): stores v into the immediate of the very next instruction, then executes it. */
    .globl inline_smc_start, inline_smc_end
inline_smc_start:
    leaq    1f+1(%rip), %rax        /* the imm32 of the mov below */
    movl    %ecx, (%rax)
1:  movl    $0x11111111, %eax
    ret
inline_smc_end:
