/* Instruction-defined state after a direct x64 SYSCALL. Read service numbers
 * from this runtime's own ntdll stubs, never from a Windows version table.
 * Do not constrain other volatile GPRs or SIMD registers: SYSCALL itself
 * does not promise their preservation by the operating system. */
#include "fid.h"

uint32_t syscall_number;
uint64_t syscall_flags, syscall_before, syscall_after, syscall_rcx, syscall_r11, syscall_status;
uint64_t syscall_stack_after[5];
extern void syscall_registers(void), syscall_next(void);
extern void syscall_registers_int2e(void);
extern void syscall_registers_rsp8(void), syscall_registers_int2e_rsp8(void);

int main(int argc, char **argv)
{
    static const uint64_t patterns[] = {0, 1, 4, 0x10, 0x40, 0x80, 0x800, 0x400, 0xcd5};
    static const char *names[] = {"NtYieldExecution", "NtClose", "invalid-service"};
    unsigned kind, i;
    int int2e = 0, rsp8 = 0;
    for (i = 1; i < (unsigned)argc; i++)
    {
        if (!strcmp(argv[i], "int2e")) int2e = 1;
        if (!strcmp(argv[i], "rsp8")) rsp8 = 1;
    }
    fid_name = "syscalltest";
    fid_environment();
    for (kind = 0; kind < 3; kind++)
    {
        if (kind < 2)
        {
            const unsigned char *p = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), names[kind]);
            fid_check(p && !memcmp(p, "\x4c\x8b\xd1\xb8", 4), "syscall.stub", "%s has no recognized service-number stub", names[kind]);
            if (!p || memcmp(p, "\x4c\x8b\xd1\xb8", 4)) return fid_summary();
            memcpy(&syscall_number, p + 4, sizeof(syscall_number));
        }
        else syscall_number = ~0u;
        fid_info("syscall.service", "%s number=%#lx", names[kind], (unsigned long)syscall_number);
        for (i = 0; i < sizeof(patterns) / sizeof(*patterns); i++)
        {
            LONG status;
            syscall_flags = 0x202 | patterns[i];
            if (int2e) { if (rsp8) syscall_registers_int2e_rsp8(); else syscall_registers_int2e(); }
            else { if (rsp8) syscall_registers_rsp8(); else syscall_registers(); }
            fid_info("syscall.pattern", "%s flags=%#llx", names[kind], (unsigned long long)patterns[i]);
            /* RSP alignment variants use identical code up to the continuation.
             * The assembler exports the expected address for each variant. */
            if (!int2e)
            {
                extern void syscall_next_rsp8(void);
                CHECK_EQ64("syscall.rcx", syscall_rcx, (uintptr_t)(rsp8 ? syscall_next_rsp8 : syscall_next));
            }
            CHECK_EQ64("syscall.r11", syscall_r11, int2e ? 0x100 : syscall_before);
            CHECK_EQ64("syscall.rflags", syscall_after, syscall_before);
            status = (LONG)syscall_status;
            fid_check(kind == 0 ? (status == 0 || status == 0x40000024) :
                      kind == 1 ? status == (LONG)0xc0000008 : status == (LONG)0xc000001c,
                      "syscall.status", "%s returned %#lx", names[kind], (unsigned long)status);
            for (unsigned slot = 0; slot < 5; slot++)
                CHECK_EQ64("syscall.stack_unchanged", syscall_stack_after[slot], 0x11223300u + slot);
        }
    }
    return fid_summary();
}
