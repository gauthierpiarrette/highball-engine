#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int probe_code(void *code);
extern char nx_landing[];
__asm__(".globl _probe_code\n_probe_code:\nmov 4(%esp),%eax\ncall *%eax\n"
        ".globl _nx_landing\n_nx_landing:\nret\n");
static volatile LONG probing, nx_fault;
static LONG CALLBACK handler(EXCEPTION_POINTERS *ep)
{
    if (!probing || ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    nx_fault = ep->ExceptionRecord->NumberParameters == 2 &&
        ep->ExceptionRecord->ExceptionInformation[0] == 8;
    ep->ContextRecord->Eip = (DWORD)(ULONG_PTR)nx_landing;
    ep->ContextRecord->Esp += 4;
    return EXCEPTION_CONTINUE_EXECUTION;
}

int main(void)
{
    DWORD old;
    unsigned pass = 0, fail = 0, write_fail = 0, stale = 0;
    BYTE *p = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    BYTE body[] = {0xb8, 42, 0, 0, 0, 0xc3};
    PVOID veh = AddVectoredExceptionHandler(1, handler);
    if (!p || !veh) return 2;
    memcpy(p, body, sizeof(body));
    if (!VirtualProtect(p, 4096, PAGE_NOACCESS, &old)) return 2;
    probing = 1;
    probe_code(p);
    probing = 0;
    if (nx_fault) ++pass; else ++fail;
    printf("%s noaccess.execute\n", nx_fault ? "PASS" : "FAIL");
    if (!VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &old)) return 2;
    for (DWORD i = 0; i < 250; ++i)
    {
        SIZE_T done = 0;
        DWORD want = 1000 + i;
        if (probe_code(p) != (i ? want - 1 : 42)) ++stale;
        if (!WriteProcessMemory(GetCurrentProcess(), p + 1, &want, sizeof(want), &done) || done != sizeof(want))
            ++write_fail;
        else if (probe_code(p) != want) ++stale;
    }
    if (!write_fail && !stale) ++pass; else ++fail;
    printf("%s code.writeprocessmemory failed=%u stale=%u\n", !write_fail && !stale ? "PASS" : "FAIL", write_fail, stale);
    VirtualFree(p, 0, MEM_RELEASE);
    RemoveVectoredExceptionHandler(veh);
    printf("SUMMARY wow64-smc pass=%u fail=%u\n", pass, fail);
    return fail ? 1 : 0;
}
