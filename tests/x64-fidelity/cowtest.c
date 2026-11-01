/* Executable copy-on-write mappings retain protection, code coherence and file isolation. */
#include "fid.h"

static void check(int ok, const char *name, DWORD got)
{
    fid_check(ok, name, "value %#lx", got);
}
int main(void)
{
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    BYTE body[3 * 4096] = {0xb8, 42, 0, 0, 0, 0xc3};
    HANDLE file, mapping;
    BYTE *copy, *shared;
    DWORD done, old;
    MEMORY_BASIC_INFORMATION info = {0};
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_name = "cowtest";
    fid_environment();
    memcpy(body + 4096, body, 6);
    memcpy(body + 8192, body, 6);
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"hbc", 0, path)) return 2;
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE || !WriteFile(file, body, sizeof(body), &done, NULL) || done != sizeof(body)) return 2;
    mapping = CreateFileMappingW(file, NULL, PAGE_EXECUTE_WRITECOPY, 0, sizeof(body), NULL);
    if (!mapping) { printf("mapping error=%lu\n", GetLastError()); return 2; }
    copy = MapViewOfFile(mapping, FILE_MAP_COPY | FILE_MAP_EXECUTE, 0, 0, 0);
    shared = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, 0);
    if (!copy || !shared) { printf("view error=%lu\n", GetLastError()); return 2; }
    VirtualQuery(copy, &info, sizeof(info));
    check(info.Protect == PAGE_EXECUTE_WRITECOPY, "copy.before", info.Protect);
    check(((int (*)(void))copy)() == 42, "copy.execute", 0);
    VirtualQuery(copy, &info, sizeof(info));
    check(info.Protect == PAGE_EXECUTE_WRITECOPY, "copy.after_execute", info.Protect);
    if (!VirtualProtect(copy, sizeof(body), PAGE_EXECUTE_READ, &old)) return 2;
    check(old == PAGE_EXECUTE_WRITECOPY, "copy.old_protection", old);
    if (!VirtualProtect(copy, sizeof(body), PAGE_EXECUTE_WRITECOPY, &old)) return 2;
    check(((int (*)(void))copy)() == 42, "copy.execute_again", 0);

    /* Genuine RX boundaries must survive SMC trapping and later merge back into COW. */
    if (!VirtualProtect(copy + 4096, 4096, PAGE_EXECUTE_READ, &old)) return 2;
    for (unsigned page = 0; page < 3; ++page)
    {
        char id[64];
        DWORD protection = page == 1 ? PAGE_EXECUTE_READ : PAGE_EXECUTE_WRITECOPY;
        BYTE *address = copy + page * 4096;
        sprintf(id, "copy.split.execute%u", page);
        check(((int (*)(void))address)() == 42, id, 0);
        memset(&info, 0, sizeof(info));
        sprintf(id, "copy.split.query%u", page);
        int ok = VirtualQuery(address, &info, sizeof(info)) == sizeof(info) &&
            info.Protect == protection && info.RegionSize == 4096 &&
            info.BaseAddress == address && info.AllocationBase == copy && info.Type == MEM_MAPPED;
        fid_check(ok, id, "protect %#lx size %#llx base %p allocation %p type %#lx",
                  info.Protect, (unsigned long long)info.RegionSize, info.BaseAddress, info.AllocationBase, info.Type);
    }
    if (!VirtualProtect(copy + 4096, 4096, PAGE_EXECUTE_WRITECOPY, &old)) return 2;
    check(old == PAGE_EXECUTE_READ, "copy.restore_old", old);
    VirtualQuery(copy, &info, sizeof(info));
    fid_check(info.Protect == PAGE_EXECUTE_WRITECOPY && info.RegionSize == sizeof(body),
              "copy.merged", "protect %#lx size %#llx", info.Protect, (unsigned long long)info.RegionSize);
    *(DWORD *)(copy + 1) = 43;
    if (!FlushInstructionCache(GetCurrentProcess(), copy, 6)) return 2;
    check(((int (*)(void))copy)() == 43, "copy.rewrite", 0);
    check(((int (*)(void))shared)() == 42, "copy.shared_unchanged", 0);
    for (unsigned page = 0; page < 3; ++page)
    {
        char id[64];
        BYTE *address = copy + page * 4096;
        DWORD value = 43 + page;
        memcpy(address + 1, &value, sizeof(value));
        if (!FlushInstructionCache(GetCurrentProcess(), address, 6)) return 2;
        sprintf(id, "copy.multi.rewrite%u", page);
        check(((int (*)(void))address)() == (int)value, id, 0);
        sprintf(id, "copy.multi.shared%u", page);
        check(((int (*)(void))(shared + page * 4096))() == 42, id, 0);
    }
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    memset(body, 0, 6);
    if (!ReadFile(file, body, 6, &done, NULL) || done != 6) return 2;
    check(body[1] == 42, "copy.file_unchanged", body[1]);
    UnmapViewOfFile(shared); UnmapViewOfFile(copy);
    CloseHandle(mapping); CloseHandle(file);
    return fid_summary();
}
