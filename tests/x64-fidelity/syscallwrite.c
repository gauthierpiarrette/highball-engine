/* System calls that write into a page of x86-64 code that already ran.
 *
 * An emulator that write-protects translated code to see self-modifying code must still let the system (the
 * Windows kernel) write into such a page for the program: Windows has no such trap, and a read, a query or a copy
 * into a buffer that shares a page with code succeeds there. Each check runs code in a fresh read-write-execute page
 * first, so that the page holds translated code, then lets one system call write into the same page before the
 * program writes there itself. A copy protection asks for the SMBIOS table this way (Football Manager 27).
 * The rewrite checks change the code without a flush after those calls: the emulator must still see the change.
 * Two checks write while the system holds a lock (GetWriteWatch, NtQueryDirectoryFile). The last checks run two
 * threads: a query into a code page while the other thread rewrites code (no thread may wait for ever), and a copy
 * into a code page while the other thread rewrites and runs code in the same page.
 */
#include "fid.h"

typedef NTSTATUS (WINAPI *NtQuerySystemInformation_t)(ULONG, void *, ULONG, ULONG *);

struct io_status { ULONG_PTR status; ULONG_PTR information; };  /* IO_STATUS_BLOCK */
typedef NTSTATUS (WINAPI *NtQueryDirectoryFile_t)(HANDLE, HANDLE, void *, void *, struct io_status *, void *,
                                                  ULONG, ULONG, BOOLEAN, void *, BOOLEAN);
#define FileDirectoryInformation 1

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

static const unsigned char ret7[] = { 0xb8, 0x07, 0x00, 0x00, 0x00, 0xc3 };   /* mov eax, 7; ret */

/* fresh read-write-execute pages; the code at offset 0 of each page already ran */
static unsigned char *code_pages_that_ran(int pages)
{
    unsigned char *base = VirtualAlloc(NULL, pages * 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int i;

    if (!base) return NULL;
    for (i = 0; i < pages; i++)
    {
        unsigned char *page = base + i * 0x1000;
        int (*fn)(void) = (int (*)(void))page;

        memcpy(page, ret42, sizeof(ret42));
        FlushInstructionCache(GetCurrentProcess(), page, sizeof(ret42));
        if (fn() != 42) return NULL;
    }
    return base;
}

/* a fresh read-write-execute page whose code at offset 0 already ran */
static unsigned char *code_page_that_ran(void)
{
    return code_pages_that_ran(1);
}

/* a temporary file with 'size' bytes of 'fill' (at most 4 KB), positioned at offset 0; deleted on close */
static HANDLE temp_file(unsigned char fill, DWORD size)
{
    char tmp[MAX_PATH], path[MAX_PATH], data[0x1000];
    DWORD got = 0;
    HANDLE file;

    memset(data, fill, sizeof(data));
    GetTempPathA(sizeof(tmp), tmp);
    GetTempFileNameA(tmp, "scw", 0, path);
    file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE) return file;
    WriteFile(file, data, size < sizeof(data) ? size : sizeof(data), &got, NULL);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    return file;
}

/* the lock check: one thread runs the code of its page, then queries memory into the same page; the other thread
 * rewrites and runs the code of a second page. The query writes its answer while the system holds its lock for
 * memory, and the emulator changes page protections while it holds its lock for translated code. */
struct lock_race
{
    unsigned char *page;
    volatile LONG stop;
    LONG queries;
};

static DWORD WINAPI lock_querier(void *arg)
{
    struct lock_race *race = arg;
    int (*fn)(void) = (int (*)(void))race->page;

    while (!race->stop)
    {
        fn();  /* the emulator translates the page again and traps it */
        VirtualQuery(race->page, (MEMORY_BASIC_INFORMATION *)(race->page + 0x800), sizeof(MEMORY_BASIC_INFORMATION));
        race->queries++;
    }
    return 0;
}

/* a hang has no other way out: report it, then end the process */
static DWORD WINAPI lock_watchdog(void *arg)
{
    if (WaitForSingleObject((HANDLE)arg, 30000) == WAIT_TIMEOUT)
    {
        printf("FAIL lock.no-hang: the threads did not finish in 30 seconds\n");
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 3);
    }
    return 0;
}

/* the second-thread check: one thread copies into the page by a system call, the other rewrites and runs its code */
struct race
{
    unsigned char *page;
    volatile LONG stop;
    LONG copies;
};

static DWORD WINAPI race_copier(void *arg)
{
    struct race *race = arg;
    char source[0x400];
    SIZE_T done;

    memset(source, 0x5a, sizeof(source));
    while (!race->stop)
    {
        ReadProcessMemory(GetCurrentProcess(), source, race->page + 0x800, sizeof(source), &done);
        race->copies++;
    }
    return 0;
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

    /* the system call's own write over code that already ran: x86-64 runs the new code, with no flush */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.overwrite", "no code page");
    if (page)
    {
        fn = (int (*)(void))page;
        ok = ReadProcessMemory(GetCurrentProcess(), ret7, page, sizeof(ret7), &done);
        fid_check(ok && page[1] == 0x07, "overwrite.syscall", "ok %d error %lu", ok, ok ? 0 : GetLastError());
        CHECK_EQ64("overwrite.runs-new-code", fn(), 7);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* the region looks the same to the program after a system call wrote into it */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.query", "no code page");
    if (page)
    {
        MEMORY_BASIC_INFORMATION before = { 0 }, after = { 0 };

        VirtualQuery(page, &before, sizeof(before));
        ok = ReadProcessMemory(GetCurrentProcess(), source, page + 0x900, sizeof(source), &done);
        fid_check(ok, "query.syscall", "error %lu", ok ? 0 : GetLastError());
        VirtualQuery(page, &after, sizeof(after));
        fid_check(after.Protect == PAGE_EXECUTE_READWRITE && after.State == MEM_COMMIT &&
                  after.RegionSize == before.RegionSize && after.BaseAddress == before.BaseAddress,
                  "query.unchanged", "protect %#lx state %#lx size %#lx (before %#lx %#lx %#lx)",
                  after.Protect, after.State, (unsigned long)after.RegionSize,
                  before.Protect, before.State, (unsigned long)before.RegionSize);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* one buffer over three pages of code that ran: all bytes arrive, and each page's code still changes */
    page = code_pages_that_ran(3);
    fid_check(page != NULL, "setup.pages", "no code pages");
    if (page)
    {
        static char big[0x1e00];
        int i, same = 1, runs = 1, rewrites = 1;

        /* the copy covers 0x900 to 0x2700; at the start of the second and third page it brings the same code back */
        memset(big, 0x5a, sizeof(big));
        memcpy(big + 0x1000 - 0x900, ret42, sizeof(ret42));
        memcpy(big + 0x2000 - 0x900, ret42, sizeof(ret42));
        ok = ReadProcessMemory(GetCurrentProcess(), big, page + 0x900, sizeof(big), &done);
        for (i = 0; i < (int)sizeof(big); i++) if (page[0x900 + i] != (unsigned char)big[i]) same = 0;
        fid_check(ok && done == sizeof(big) && same, "pages.copy", "ok %d error %lu bytes %lu all %d",
                  ok, ok ? 0 : GetLastError(), (unsigned long)done, same);
        for (i = 0; i < 3; i++)
        {
            fn = (int (*)(void))(page + i * 0x1000);
            if (fn() != 42) runs = 0;
            page[i * 0x1000 + 1] = 0x07;  /* without FlushInstructionCache */
            if (fn() != 7) rewrites = 0;
        }
        fid_check(runs, "pages.still-run", "a page's code did not run");
        fid_check(rewrites, "pages.no-flush", "a page's rewrite did not take effect");
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* a page the program itself made read-execute: system calls still may not write into it (no mark too many) */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.readonly", "no code page");
    if (page)
    {
        struct firmware_table_info header = { 0x52534d42 /* 'RSMB' */, 1, 0, 0 };
        DWORD old = 0;

        fn = (int (*)(void))page;
        memcpy(page + 0x448, &header, FIELD_OFFSET(struct firmware_table_info, TableBuffer));  /* before the protection */
        ok = VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
        fid_check(ok && fn() == 42, "readonly.protect", "ok %d error %lu", ok, ok ? 0 : GetLastError());

        file = temp_file(0x5a, sizeof(source));
        ok = ReadFile(file, page + 0xa00, sizeof(source), &got, NULL);
        fid_check(!ok && GetLastError() == ERROR_NOACCESS && page[0xa00] == 0, "readonly.readfile",
                  "ok %d error %lu byte %#x", ok, ok ? 0 : GetLastError(), page[0xa00]);
        CloseHandle(file);

        if (pNtQuerySystemInformation)
        {
            status = pNtQuerySystemInformation(SystemFirmwareTableInformation, page + 0x448, 0x1000 - 0x448, &len);
            fid_check(status == (NTSTATUS)0xc0000005 /* STATUS_ACCESS_VIOLATION */, "readonly.firmwaretable",
                      "status %#lx", (unsigned long)status);
        }
        ok = ReadProcessMemory(GetCurrentProcess(), source, page + 0x900, sizeof(source), &done);
        fid_info("readonly.readprocessmemory", "ok %d error %lu bytes %lu", ok, ok ? 0 : GetLastError(), (unsigned long)done);
        fid_check(page[0x900] == 0, "readonly.unchanged", "byte %#x", page[0x900]);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* GetWriteWatch fills its list while the system holds its lock for memory; a rewrite after it must take effect */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.writewatch", "no code page");
    if (page)
    {
        unsigned char *watched = VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
        ULONG_PTR count = 16;
        ULONG granularity = 0;
        UINT res = 1;

        fn = (int (*)(void))page;
        if (watched)
        {
            watched[0] = 1;
            watched[0x2000] = 1;
            res = GetWriteWatch(0, watched, 0x10000, (void **)(page + 0x800), &count, &granularity);
        }
        fid_check(watched && res == 0 && count >= 2, "writewatch.syscall", "result %u count %lu", res, (unsigned long)count);
        fid_check(fn() == 42, "writewatch.still-runs", "got %#x", fn());
        page[1] = 7;  /* mov eax, 7, without FlushInstructionCache */
        fid_check(fn() == 7, "writewatch.no-flush", "got %#x", fn());
        if (watched) VirtualFree(watched, 0, MEM_RELEASE);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* NtQueryDirectoryFile writes its entries while the system holds its lock for directories */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.directory", "no code page");
    if (page)
    {
        NtQueryDirectoryFile_t pNtQueryDirectoryFile =
            (NtQueryDirectoryFile_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryDirectoryFile");
        struct io_status io = { 0, 0 };
        HANDLE dir;

        fn = (int (*)(void))page;
        GetTempPathA(sizeof(tmp), tmp);
        dir = CreateFileA(tmp, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        status = (dir != INVALID_HANDLE_VALUE && pNtQueryDirectoryFile)
            ? pNtQueryDirectoryFile(dir, NULL, NULL, NULL, &io, page + 0x800, 0x400, FileDirectoryInformation,
                                    FALSE, NULL, TRUE)
            : (NTSTATUS)0xc0000001;
        fid_check(status == 0 && io.information > 0, "directory.syscall", "status %#lx bytes %lu",
                  (unsigned long)status, (unsigned long)io.information);
        fid_check(fn() == 42, "directory.still-runs", "got %#x", fn());
        page[1] = 7;  /* mov eax, 7, without FlushInstructionCache */
        fid_check(fn() == 7, "directory.no-flush", "got %#x", fn());
        if (dir != INVALID_HANDLE_VALUE) CloseHandle(dir);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* a second thread queries memory into its code page while this thread rewrites the code of another page */
    page = code_pages_that_ran(2);
    fid_check(page != NULL, "setup.lock", "no code page");
    if (page)
    {
        struct lock_race race = { page, 0, 0 };
        HANDLE done = CreateEventA(NULL, TRUE, FALSE, NULL), thread, watchdog;
        DWORD start = GetTickCount();
        LONG runs = 0, wrong = 0;

        fn = (int (*)(void))(page + 0x1000);
        watchdog = CreateThread(NULL, 0, lock_watchdog, done, 0, NULL);
        thread = CreateThread(NULL, 0, lock_querier, &race, 0, NULL);
        while (GetTickCount() - start < 2000)
        {
            unsigned char value = 1 + (runs % 120);

            page[0x1001] = value;  /* mov eax, value, without FlushInstructionCache */
            if (fn() != value) wrong++;
            runs++;
        }
        race.stop = 1;
        WaitForSingleObject(thread, INFINITE);
        SetEvent(done);
        WaitForSingleObject(watchdog, INFINITE);
        CloseHandle(thread);
        CloseHandle(watchdog);
        CloseHandle(done);
        fid_info("lock.counts", "runs %ld queries %ld", runs, race.queries);
        fid_check(1, "lock.no-hang", "");
        fid_check(wrong == 0, "lock.no-stale-code", "%ld of %ld runs ran old code", wrong, runs);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    /* a second thread copies into the page by system calls while this thread rewrites the page's code and runs it */
    page = code_page_that_ran();
    fid_check(page != NULL, "setup.thread", "no code page");
    if (page)
    {
        struct race race = { page, 0, 0 };
        DWORD start = GetTickCount();
        LONG runs = 0, stale = 0;
        HANDLE thread;

        fn = (int (*)(void))page;
        thread = CreateThread(NULL, 0, race_copier, &race, 0, NULL);
        while (GetTickCount() - start < 2000)
        {
            unsigned char value = 1 + (runs % 120);

            page[1] = value;  /* mov eax, value, without FlushInstructionCache */
            if (fn() != value) stale++;
            runs++;
        }
        race.stop = 1;
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        fid_info("thread.counts", "runs %ld copies %ld", runs, race.copies);
        fid_check(stale == 0, "thread.no-stale-code", "%ld of %ld runs ran old code", stale, runs);
        VirtualFree(page, 0, MEM_RELEASE);
    }

    return fid_summary();
}
