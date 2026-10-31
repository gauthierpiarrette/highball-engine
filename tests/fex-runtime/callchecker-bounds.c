#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef uintptr_t (WINAPI *probe_t)(uintptr_t *);
/* Two sentinel entries are physically allocated, but outside the logical table. */
static uint32_t table[4098];
/* Read the unstripped module's COFF symbols, rather than assuming build-specific RVAs. */
static int find_runtime_symbols(HMODULE module, uintptr_t addresses[4])
{
    const char *names[] = {"NtDllBase", "NtDllRedirectionLUTSize", "NtDllRedirectionLUT", "CheckCall"};
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((BYTE *)module + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    WCHAR filename[32768];
    if (!GetModuleFileNameW(module, filename, 32768)) return 0;
    FILE *file = _wfopen(filename, L"rb");
    if (!file || !nt->FileHeader.PointerToSymbolTable) { if (file) fclose(file); return 0; }
    unsigned found = 0;
    for (DWORD i = 0; i < nt->FileHeader.NumberOfSymbols;) {
        unsigned char entry[18];
        DWORD offset = nt->FileHeader.PointerToSymbolTable + i * 18;
        if (fseek(file, offset, SEEK_SET) || fread(entry, 1, 18, file) != 18) break;
        DWORD zero, string_offset, value;
        SHORT section;
        memcpy(&zero, entry, 4); memcpy(&string_offset, entry + 4, 4);
        memcpy(&value, entry + 8, 4); memcpy(&section, entry + 12, 2);
        char name[80] = {0};
        if (zero) memcpy(name, entry, 8);
        else if (string_offset >= 4) {
            uint64_t position = (uint64_t)nt->FileHeader.PointerToSymbolTable + (uint64_t)nt->FileHeader.NumberOfSymbols * 18 + string_offset;
            if (position > LONG_MAX || fseek(file, (long)position, SEEK_SET)) break;
            for (unsigned j = 0; j < sizeof(name) - 1; ++j) {
                int c = fgetc(file);
                if (c == EOF || !c) break;
                name[j] = (char)c;
            }
        }
        if (section > 0 && section <= nt->FileHeader.NumberOfSections) {
            uintptr_t rva = sections[section - 1].VirtualAddress + value;
            for (unsigned j = 0; j < 4; ++j) if (!strcmp(name, names[j])) {
                DWORD required = j == 3 ? IMAGE_SCN_MEM_EXECUTE : IMAGE_SCN_MEM_WRITE;
                if (rva + 8 > nt->OptionalHeader.SizeOfImage || !(sections[section - 1].Characteristics & required)) continue;
                addresses[j] = (uintptr_t)module + rva;
                found |= 1u << j;
            }
        }
        i += 1 + entry[17];
    }
    fclose(file);
    return found == 15;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) { puts("Usage: callchecker-bounds.exe path-to-callchecker-native.dll"); return 2; }
    uintptr_t module = (uintptr_t)GetModuleHandleW(L"xtajit64.dll");
    HMODULE helper = LoadLibraryA(argv[1]);
    probe_t probe = helper ? (probe_t)GetProcAddress(helper, "RunCallCheckerProbe") : NULL;
    if (!module || !probe) { printf("FAIL setup module=%p helper=%p error=%lu\n", (void *)module, helper, GetLastError()); return 2; }
    uintptr_t args[8];
    if (!find_runtime_symbols((HMODULE)module, args)) { puts("FAIL runtime symbols unavailable; use an unstripped FEX build"); return 2; }
    uint64_t original[3];
    for (unsigned i = 0; i < 3; ++i) original[i] = *(uint64_t *)args[i];
    printf("INFO actual table_size=%u adjacent=%08x\n", (unsigned)original[1], (unsigned)(original[1] >> 32));
    for (unsigned i = 0; i < 4098; ++i) table[i] = i + 0x20;
    table[4096] = 0xdead; table[4097] = 0xbeef;
    args[4] = UINT64_C(0x100000000);
    args[6] = (uintptr_t)table;
    unsigned offsets[] = {2, 4096, 4097};
    unsigned pass = 0, fail = 0;
    for (unsigned i = 0; i < 3; ++i) {
        args[5] = i == 2 ? UINT64_C(0x100001000) : 4096;
        args[7] = args[4] + offsets[i];
        uintptr_t expected = i ? args[7] : args[4] + table[offsets[i]];
        uintptr_t got = probe(args);
        int ok = got == expected;
        if (ok) ++pass; else ++fail;
        printf("%s case=%u got=%llx expected=%llx\n", ok ? "PASS" : "FAIL", i, (unsigned long long)got, (unsigned long long)expected);
    }
    int restored = 1;
    for (unsigned i = 0; i < 3; ++i) if (*(uint64_t *)args[i] != original[i]) restored = 0;
    if (restored) ++pass; else ++fail;
    printf("%s restored original runtime variables\n", restored ? "PASS" : "FAIL");
    FreeLibrary(helper);
    printf("SUMMARY callchecker-bounds pass=%u fail=%u\n", pass, fail);
    return fail;
}
