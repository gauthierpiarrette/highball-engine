.text
/* The 0x40 frame deliberately leaves RSP 8-byte aligned before CALL.
 * The target only records six integer arguments and does not use aligned
 * vector stack accesses. Both wrappers restore exactly the original RSP.
 * No nonvolatile registers are changed. */
.macro query name, adjustment
.globl \name
.seh_proc \name
\name:
  sub $\adjustment, %rsp
  .seh_stackalloc \adjustment
  .seh_endprologue
  mov %rcx, %rax
  mov %rdx, %rcx
  mov $0x22334455, %edx
  mov $0x33445566, %r8d
  mov $0x44556677, %r9d
  movq $0x55667711, 0x20(%rsp)
  movq $0x66771122, 0x28(%rsp)
  call *%rax
  add $\adjustment, %rsp
  ret
.seh_endproc
.endm
query CallAligned, 0x38
query CallMisaligned, 0x40
