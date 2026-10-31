/* Memory queries on a late-loaded executable image use page boundaries. */
#include "fid.h"

int main(void)
{
    HMODULE module;
    BYTE *code;
    void *(*get_code)(void);
    DWORD old;
    fid_name = "imagequery";
    setvbuf(stdout, NULL, _IONBF, 0);
    fid_environment();
    module = LoadLibraryA("imagequery-helper.dll");
    if (!module)
    { fid_check(0, "setup.load", "error %lu", GetLastError()); return fid_summary(); }
    get_code = (void *)GetProcAddress(module, "GetCode");
    if (!get_code)
    { fid_check(0, "setup.export", "error %lu", GetLastError()); FreeLibrary(module); return fid_summary(); }
    code = get_code();
    for (unsigned phase = 0; phase < 3; ++phase)
    {
        MEMORY_BASIC_INFORMATION info = {0};
        char id[64];
        if (phase == 1)
            fid_check(((int (*)(void))code)() == 42, "image.execute", "incorrect generated-code result");
        if (phase == 2)
        {
            if (!VirtualProtect(code, 6, PAGE_EXECUTE_WRITECOPY, &old))
            { fid_check(0, "setup.protect", "error %lu", GetLastError()); break; }
            fid_check(old == PAGE_EXECUTE_WRITECOPY, "image.old_protection", "old %#lx", old);
            code[1] = 43;
            if (!FlushInstructionCache(GetCurrentProcess(), code, 6))
            { fid_check(0, "setup.flush", "error %lu", GetLastError()); break; }
            fid_check(((int (*)(void))code)() == 43, "image.rewrite", "incorrect rewritten-code result");
        }
        SIZE_T bytes = VirtualQuery(code, &info, sizeof(info));
        int ok = bytes == sizeof(info) && info.RegionSize == 4096 && info.BaseAddress == code &&
            info.AllocationBase == module && info.State == MEM_COMMIT && info.Type == MEM_IMAGE;
        /* A private COW fault may change the physical protection after the write. */
        if (phase < 2) ok = ok && info.Protect == PAGE_EXECUTE_WRITECOPY;
        sprintf(id, "image.query%u", phase);
        fid_check(ok, id, "base %p allocation %p protect %#lx size %#llx state %#lx type %#lx",
                  info.BaseAddress, info.AllocationBase, info.Protect, (unsigned long long)info.RegionSize,
                  info.State, info.Type);
    }
    FreeLibrary(module);
    return fid_summary();
}
