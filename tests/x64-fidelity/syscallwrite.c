/* System calls that write into a page of x86-64 code that already ran.
 *
 * An emulator that write-protects translated code to see self-modifying code must still let the system (the
 * Windows kernel) write into such a page for the program: Windows has no such trap, and a read, a query or a copy
 * into a buffer that shares a page with code succeeds there. Each check runs code in a fresh read-write-execute page
 * first, so that the page holds translated code, then lets one system call write into the same page before the
 * program writes there itself. A copy protection asks for the SMBIOS table this way (Football Manager 27).
 * The last check rewrites the code without a flush after those calls: the emulator must still see the change.
 */
#include "fid.h"

typedef NTSTATUS (WINAPI *NtQuerySystemInformation_t)(ULONG, void *, ULONG, ULONG *);

#define SystemFirmwareTableInformation 76

struct firmware_table_info  /* SYSTEM_FIRMWARE_TABLE_INFORMATION */
{
    ULONG ProviderSignature;
    ULONG Action;               /* 1: get */
    ULONG TableID;
    ULONG TableBufferLength;
    UCHAR TableBuffer[1];
};

static const unsigned char ret42[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };  /* mov eax, 42; ret */

/* a fresh read-write-execute page whose code at offset 0 already ran */
static unsigned char *code_page_that_ran(void)
{
    unsigned char *page = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int (*fn)(void) = (int (*)(void))page;

    if (!page) return NULL;
    memcpy(page, ret42, sizeof(ret42));
    FlushInstructionCache(GetCurrentProcess(), page, sizeof(ret42));
    if (fn() != 42) return NULL;
    return page;
}

int main(void)
{
    NtQuerySystemInformation_t pNtQuerySystemInformation =
        (NtQuerySystemInformation_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
    char source[64], tmp[MAX_PATH], path[MAX_PATH];
    unsigned char *page;
    SIZE_T done = 0;
    DWORD got = 0;
    ULONG len = 0;
    NTSTATUS status;
    HANDLE file;
    BOOL ok;
    int (*fn)(void);

    fid_name = "syscallwrite";
    memset(source, 0x5a, sizeof(source));

    /* NtReadVirtualMemory */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.rvm", "no code page");
    if (page)
    {
        ok = ReadProcessMemory(GetCurrentProcess(), source, page + 0x900, sizeof(source), &done);
        fid_check(ok && done == sizeof(source) && page[0x900] == 0x5a, "readprocessmemory.code-page",
                  "ok %d error %lu bytes %lu", ok, ok ? 0 : GetLastError(), (unsigned long)done);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* NtReadFile */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.readfile", "no code page");
    if (page)
    {
        GetTempPathA(sizeof(tmp), tmp);
        GetTempFileNameA(tmp, "scw", 0, path);
        file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
        WriteFile(file, source, sizeof(source), &got, NULL);
        SetFilePointer(file, 0, NULL, FILE_BEGIN);
        ok = ReadFile(file, page + 0xa00, sizeof(source), &got, NULL);
        fid_check(ok && got == sizeof(source) && page[0xa00] == 0x5a, "readfile.code-page",
                  "ok %d error %lu bytes %lu", ok, ok ? 0 : GetLastError(), got);
        CloseHandle(file);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* NtQuerySystemInformation(SystemFirmwareTableInformation): the SMBIOS table, as a copy protection asks for it */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.firmware", "no code page");
    if (page && pNtQuerySystemInformation)
    {
        struct firmware_table_info header = { 0x52534d42 /* 'RSMB' */, 1, 0, 0 };
        struct firmware_table_info *info = (struct firmware_table_info *)(page + 0x448);
        SIZE_T copied = 0;

        /* the header goes in by a system call too, so no write of the program's own lands on the page */
        ok = ReadProcessMemory(GetCurrentProcess(), &header, info, FIELD_OFFSET(struct firmware_table_info, TableBuffer), &copied);
        fid_check(ok, "firmwaretable.header", "error %lu", ok ? 0 : GetLastError());
        status = pNtQuerySystemInformation(SystemFirmwareTableInformation, info, 0x1000 - 0x448, &len);
        fid_check(status == 0 || status == (NTSTATUS)0xc0000023 /* STATUS_BUFFER_TOO_SMALL */, "firmwaretable.code-page",
                  "status %#lx length %lu", (unsigned long)status, (unsigned long)len);
        fid_info("firmwaretable.length", "%lu", (unsigned long)len);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* after such calls, the code still runs and a rewrite without a flush takes effect */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.rewrite", "no code page");
    if (page)
    {
        fn = (int (*)(void))page;
        ok = ReadProcessMemory(GetCurrentProcess(), source, page + 0x900, sizeof(source), &done);
        fid_check(ok, "rewrite.syscall-first", "error %lu", ok ? 0 : GetLastError());
        CHECK_EQ64("rewrite.still-runs", fn(), 42);
        page[1] = 0x07;  /* mov eax, 7, without FlushInstructionCache */
        CHECK_EQ64("rewrite.no-flush", fn(), 7);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    return fid_summary();
}
