.text
.macro syscall_probe name,next,int2e,frame
.globl \name
.seh_proc \name
\name:
    subq $\frame,%rsp
    .seh_stackalloc \frame
    .seh_endprologue
    movq $0x11223300,0(%rsp)
    movq $0x11223301,8(%rsp)
    movq $0x11223302,16(%rsp)
    movq $0x11223303,24(%rsp)
    movq $0x11223304,32(%rsp)
    movl syscall_number(%rip),%eax
    movq $0,%r10
    .if \int2e
    movq $0x100,%r11
    .endif
    pushq syscall_flags(%rip)
    popfq
    pushfq
    popq syscall_before(%rip)
    .if \int2e
    int $0x2e
    .else
    syscall
    .endif
.globl \next
\next:
    movq %rcx,syscall_rcx(%rip)
    movq %r11,syscall_r11(%rip)
    movq %rax,syscall_status(%rip)
    pushfq
    popq syscall_after(%rip)
    movq 0(%rsp),%rax
    movq %rax,syscall_stack_after+0(%rip)
    movq 8(%rsp),%rax
    movq %rax,syscall_stack_after+8(%rip)
    movq 16(%rsp),%rax
    movq %rax,syscall_stack_after+16(%rip)
    movq 24(%rsp),%rax
    movq %rax,syscall_stack_after+24(%rip)
    movq 32(%rsp),%rax
    movq %rax,syscall_stack_after+32(%rip)
    cld
    addq $\frame,%rsp
    ret
.seh_endproc
.endm

syscall_probe syscall_registers,syscall_next,0,0x28
syscall_probe syscall_registers_int2e,int2e_next,1,0x28
syscall_probe syscall_registers_rsp8,syscall_next_rsp8,0,0x30
syscall_probe syscall_registers_int2e_rsp8,int2e_next_rsp8,1,0x30
