/* WriteProcessMemory and NtWriteVirtualMemory on the current process: data, the program's own image code, and
 * generated code that has already run, as runtime patchers (overlay hooks, mod loaders, copy protections) do.
 * Each case writes, checks the bytes and, for code, calls it to see the new instructions run. */
#include "fid.h"

typedef NTSTATUS (WINAPI *NtWriteVirtualMemory_t)(HANDLE, void *, const void *, SIZE_T, SIZE_T *);
typedef int (*ret_fn)(void);

static NtWriteVirtualMemory_t pNtWriteVirtualMemory;

/* mov eax, value; ret */
static void make_ret(unsigned char *code, int value)
{
    code[0] = 0xb8;
    memcpy(code + 1, &value, 4);
    code[5] = 0xc3;
}

/* called through a volatile pointer, so the compiler cannot fold its result into the caller */
__attribute__((noinline)) int image_function(void)
{
    return 7;
}
static ret_fn volatile image_function_ptr = image_function;

static void check_generated(const char *id, DWORD protect, int direct)
{
    unsigned char *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char code[6];
    char name[96];
    DWORD old;
    SIZE_T written;
    BOOL ok;
    int i, got;

    if (!page) { fid_check(0, id, "VirtualAlloc %lu", GetLastError()); return; }
    make_ret(page, 1);
    FlushInstructionCache(GetCurrentProcess(), page, 6);
    if (protect != PAGE_EXECUTE_READWRITE) VirtualProtect(page, 4096, protect, &old);
    got = ((ret_fn)page)();
    snprintf(name, sizeof(name), "%s.first-run", id);
    fid_check(got == 1, name, "returned %d", got);

    for (i = 2; i < 34; i++)
    {
        make_ret(code, i);
        written = 0;
        if (direct)
        {
            NTSTATUS status = pNtWriteVirtualMemory(GetCurrentProcess(), page, code, sizeof(code), &written);
            ok = !status;
            if (!ok) SetLastError(status);
        }
        else ok = WriteProcessMemory(GetCurrentProcess(), page, code, sizeof(code), &written);
        if (!ok || written != sizeof(code)) break;
        FlushInstructionCache(GetCurrentProcess(), page, sizeof(code));
        if ((got = ((ret_fn)page)()) != i) break;
    }
    snprintf(name, sizeof(name), "%s.rewrite-32", id);
    fid_check(i == 34, name, "round %d: ok %d written %Iu error %#lx returned %d", i, ok, written,
              ok ? 0 : GetLastError(), got);

    if (protect == PAGE_EXECUTE_READWRITE)
    {
        MEMORY_BASIC_INFORMATION info;
        VirtualQuery(page, &info, sizeof(info));
        snprintf(name, sizeof(name), "%s.protection-kept", id);
        fid_check(info.Protect == PAGE_EXECUTE_READWRITE, name, "Protect %#lx", info.Protect);
    }
    VirtualFree(page, 0, MEM_RELEASE);
}

int main(void)
{
    static volatile int data = 1;
    int value = 2;
    SIZE_T written = 0;
    unsigned char code[6], saved[6];
    unsigned char *page;
    BOOL ok;
    int got;

    fid_name = "wpmtest";
    pNtWriteVirtualMemory = (NtWriteVirtualMemory_t)GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                                                    "NtWriteVirtualMemory");

    ok = WriteProcessMemory(GetCurrentProcess(), (void *)&data, &value, sizeof(value), &written);
    fid_check(ok && written == sizeof(value) && data == 2, "data", "ok %d written %Iu data %d error %lu",
              ok, written, data, ok ? 0 : GetLastError());

    /* the program's own .text: read-execute in the image, made writable for the write and restored */
    memcpy(saved, (void *)image_function, sizeof(saved));
    make_ret(code, 9);
    ok = WriteProcessMemory(GetCurrentProcess(), (void *)image_function, code, sizeof(code), &written);
    got = ok ? image_function_ptr() : -1;
    fid_check(ok && got == 9, "image-code", "ok %d error %lu returned %d", ok, ok ? 0 : GetLastError(), got);
    if (ok) WriteProcessMemory(GetCurrentProcess(), (void *)image_function, saved, sizeof(saved), &written);
    {
        MEMORY_BASIC_INFORMATION info;
        VirtualQuery((void *)image_function, &info, sizeof(info));
        fid_check(!(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)), "image-code.protection-restored",
                  "Protect %#lx", info.Protect);
    }

    /* generated code that already ran: an emulator may have write-protected the page to see changes to it */
    check_generated("generated-rwx", PAGE_EXECUTE_READWRITE, 0);
    check_generated("generated-rx", PAGE_EXECUTE_READ, 0);
    if (pNtWriteVirtualMemory) check_generated("generated-rwx.nt", PAGE_EXECUTE_READWRITE, 1);

    /* a page the program cannot write: WriteProcessMemory makes no-access pages no less inaccessible */
    page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    written = 12345;
    ok = WriteProcessMemory(GetCurrentProcess(), page, code, sizeof(code), &written);
    fid_check(!ok, "noaccess", "the write succeeded");
    fid_info("noaccess.error", "%lu, written %Iu", ok ? 0 : GetLastError(), written);
    VirtualFree(page, 0, MEM_RELEASE);

    return fid_summary();
}
