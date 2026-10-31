/* SEC_IMAGE mappings execute their PE sections without going through LoadLibrary. */
#include "fid.h"
#include <wchar.h>

static BYTE *code;
static volatile LONG faults;

static LONG CALLBACK handle_fault(EXCEPTION_POINTERS *exception)
{
    EXCEPTION_RECORD *record = exception->ExceptionRecord;
    CONTEXT *context = exception->ContextRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->ExceptionAddress != code ||
        record->NumberParameters < 2 || record->ExceptionInformation[0] != 8)
        return EXCEPTION_CONTINUE_SEARCH;
    ++faults;
    context->Rip = *(uintptr_t *)context->Rsp;
    context->Rsp += sizeof(uintptr_t);
    context->Rax = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

int main(void)
{
    WCHAR filename[MAX_PATH], *name;
    static const WCHAR helper[] = L"imagequery-helper.dll";
    HANDLE file, mapping;
    BYTE *image;
    MEMORY_BASIC_INFORMATION info = {0};
    DWORD old;
    void *handler;
    int value;
    fid_name = "imagemap";
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_environment();
    DWORD length = GetModuleFileNameW(NULL, filename, MAX_PATH);
    if (!length || length >= MAX_PATH || !(name = wcsrchr(filename, L'\\')) ||
        name + 1 - filename + sizeof(helper) / sizeof(helper[0]) > MAX_PATH) return 2;
    memcpy(name + 1, helper, sizeof(helper));
    file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 2;
    mapping = CreateFileMappingW(file, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    if (!mapping) return 2;
    image = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!image) return 2;
    IMAGE_NT_HEADERS64 *nt = (void *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (!memcmp(section[i].Name, ".cowcode", 8)) code = image + section[i].VirtualAddress;
    if (!code) return 2;
    handler = AddVectoredExceptionHandler(1, handle_fault);
    if (!handler) return 2;
    for (unsigned phase = 0; phase < 2; ++phase)
    {
        SIZE_T bytes = VirtualQuery(code, &info, sizeof(info));
        fid_check(bytes == sizeof(info) && info.BaseAddress == code && info.AllocationBase == image &&
                  info.Type == MEM_IMAGE && info.Protect == PAGE_EXECUTE_WRITECOPY && info.RegionSize == 4096,
                  phase ? "image.after_execute" : "image.before_execute",
                  "base %p allocation %p type %#lx protect %#lx size %#llx", info.BaseAddress,
                  info.AllocationBase, info.Type, info.Protect, (unsigned long long)info.RegionSize);
        if (!phase)
        {
            value = ((int (*)(void))code)();
            fid_check(value == 42 && !faults, "image.direct_execute", "result %d execute faults %ld", value, faults);
        }
    }
    if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old)) return 2;
    faults = 0;
    value = ((int (*)(void))code)();
    fid_check(value == 42 && !faults, "image.explicit_protect_execute", "result %d execute faults %ld", value, faults);
    RemoveVectoredExceptionHandler(handler);
    UnmapViewOfFile(image);
    CloseHandle(mapping);
    CloseHandle(file);
    return fid_summary();
}
