#include <windows.h>
#include <stdio.h>

/* Read-only check of the three ARM64EC dispatch slots patched during FEX startup. */
int main(void)
{
    BYTE *module = (BYTE *)GetModuleHandleW(L"xtajit64.dll");
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_LOAD_CONFIG_DIRECTORY64 *config;
    DWORD *metadata;
    unsigned pass = 0, fail = 0;
    if (!module) { puts("FEX module is not loaded"); return 2; }
    dos = (IMAGE_DOS_HEADER *)module;
    nt = (IMAGE_NT_HEADERS64 *)(module + dos->e_lfanew);
    config = (IMAGE_LOAD_CONFIG_DIRECTORY64 *)(module +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress);
    if (!config->CHPEMetadataPointer) return 2;
    metadata = (DWORD *)(ULONG_PTR)config->CHPEMetadataPointer;
    for (unsigned i = 7; i <= 9; ++i)
    {
        MEMORY_BASIC_INFORMATION mbi;
        IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
        DWORD rva = metadata[i], expected = 0;
        int found = 0, writable;
        for (unsigned j = 0; j < nt->FileHeader.NumberOfSections; ++j)
            if (rva >= sections[j].VirtualAddress &&
                rva < sections[j].VirtualAddress + sections[j].Misc.VirtualSize)
            { expected = sections[j].Characteristics; found = 1; break; }
        if (!rva || !found || !VirtualQuery(module + rva, &mbi, sizeof(mbi))) return 2;
        writable = !!(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        int ok = writable == !!(expected & IMAGE_SCN_MEM_WRITE);
        printf("%s dispatch[%u] rva=%08lx protection=%lx section=%08lx\n",
               ok ? "PASS" : "FAIL", i - 7, rva, mbi.Protect, expected);
        if (ok) ++pass; else ++fail;
    }
    printf("SUMMARY dispatch-protection pass=%u fail=%u\n", pass, fail);
    return fail ? 1 : 0;
}
