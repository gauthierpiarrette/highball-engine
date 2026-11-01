.text
/* No calls into Wine/FEX are made while its three variables are substituted,
 * except the checker itself. Restore them before returning to translated code.
 * Use only registers permitted by the ARM64EC ABI. */
.globl "#PerformCallCheckerProbe"
.seh_proc "#PerformCallCheckerProbe"
"#PerformCallCheckerProbe":
  stp x29, x30, [sp, #-64]!
  .seh_save_fplr_x 64
  str x19, [sp, #16]
  .seh_save_reg x19, 16
  .seh_endprologue
  mov x19, x0
  ldr x1, [x19, #0]
  ldr x2, [x1]
  str x2, [sp, #24]
  ldr x2, [x19, #32]
  str x2, [x1]
  ldr x1, [x19, #8]
  ldr x2, [x1]
  str x2, [sp, #32]
  ldr x2, [x19, #40]
  str x2, [x1]
  ldr x1, [x19, #16]
  ldr x2, [x1]
  str x2, [sp, #40]
  ldr x2, [x19, #48]
  str x2, [x1]
  ldr x11, [x19, #56]
  ldr x8, [x19, #24]
  blr x8
  mov x0, x11
  ldr x1, [x19, #0]
  ldr x2, [sp, #24]
  str x2, [x1]
  ldr x1, [x19, #8]
  ldr x2, [sp, #32]
  str x2, [x1]
  ldr x1, [x19, #16]
  ldr x2, [sp, #40]
  str x2, [x1]
  .seh_startepilogue
  ldr x19, [sp, #16]
  .seh_save_reg x19, 16
  ldp x29, x30, [sp], #64
  .seh_save_fplr_x 64
  .seh_endepilogue
  ret
  .seh_endproc
