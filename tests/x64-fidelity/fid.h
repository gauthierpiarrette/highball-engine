/* Shared helpers for the x86-64 fidelity tests (Windows x64 PE, llvm-mingw).
 * Each test prints one line per check:
 *   PASS <id>            expectation met
 *   FAIL <id>: <detail>  expectation not met
 *   INFO <id>: <detail>  recorded value, no expectation (compared across machines by hand)
 * and ends with "SUMMARY <test> pass=N fail=M". Exit code = failures (capped at 255).
 * Expectations come from the Intel SDM and the documented Windows x64 behaviour; every test is run on
 * real x86-64 Windows first, and a check that fails there is a wrong check, not a finding. */
#ifndef FID_H
#define FID_H

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <intrin.h>

static int fid_pass, fid_fail;
static const char *fid_name = "test";

static void fid_check(int ok, const char *id, const char *fmt, ...)
{
    if (ok) { fid_pass++; printf("PASS %s\n", id); }
    else
    {
        va_list va;
        fid_fail++;
        printf("FAIL %s: ", id);
        va_start(va, fmt); vprintf(fmt, va); va_end(va);
        printf("\n");
    }
    fflush(stdout);
}

static void fid_info(const char *id, const char *fmt, ...)
{
    va_list va;
    printf("INFO %s: ", id);
    va_start(va, fmt); vprintf(fmt, va); va_end(va);
    printf("\n");
    fflush(stdout);
}

/* check that two 64-bit values are equal */
#define CHECK_EQ64(id, got, want) \
    fid_check((uint64_t)(got) == (uint64_t)(want), id, "got %#llx want %#llx", \
              (unsigned long long)(got), (unsigned long long)(want))

static int fid_summary(void)
{
    printf("SUMMARY %s pass=%d fail=%d\n", fid_name, fid_pass, fid_fail);
    fflush(stdout);
    return fid_fail > 255 ? 255 : fid_fail;
}

static void fid_hex(char *out, const void *p, size_t n)
{
    const unsigned char *b = p;
    size_t i;
    for (i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[n - 1 - i]); /* most significant first */
    out[2 * n] = 0;
}

/* Where are we running: Windows version, Wine, emulation, CPU brand */
static void fid_environment(void)
{
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOEXW *);
    typedef const char *(CDECL *wine_get_version_t)(void);
    typedef BOOL (WINAPI *IsWow64Process2_t)(HANDLE, USHORT *, USHORT *);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), k32 = GetModuleHandleA("kernel32.dll");
    RtlGetVersion_t pRtlGetVersion = (void *)GetProcAddress(ntdll, "RtlGetVersion");
    wine_get_version_t pwine = (void *)GetProcAddress(ntdll, "wine_get_version");
    IsWow64Process2_t pIsWow64Process2 = (void *)GetProcAddress(k32, "IsWow64Process2");
    OSVERSIONINFOEXW v = { sizeof(v) };
    int regs[4];
    char brand[49] = { 0 };
    unsigned i;

    if (pRtlGetVersion) pRtlGetVersion(&v);
    fid_info("env.windows", "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    fid_info("env.wine", "%s", pwine ? pwine() : "no");
    if (pIsWow64Process2)
    {
        USHORT proc = 0, native = 0;
        pIsWow64Process2(GetCurrentProcess(), &proc, &native);
        fid_info("env.machine", "process %#x native %#x", proc, native);
    }
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] >= 0x80000004)
        for (i = 0; i < 3; i++) __cpuid((int *)(brand + 16 * i), 0x80000002 + i);
    fid_info("env.cpu", "%s", brand);
}

#endif
