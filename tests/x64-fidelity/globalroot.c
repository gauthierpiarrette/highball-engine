/* GLOBALROOT file paths must reach the same file as their DOS spelling.
 * A mapped image's NT name can be prefixed with \\.\GLOBALROOT and reopened.
 * Build and run unchanged on Windows as well as Wine. */
#include "fid.h"
#include <psapi.h>
#include <wchar.h>

static void check_open(const char *id, const WCHAR *path, const void *expected, DWORD length)
{
    char data[32] = {0};
    DWORD got = 0, error;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    BOOL ok = file != INVALID_HANDLE_VALUE;
    error = GetLastError();
    if (ok)
    {
        ok = ReadFile(file, data, length, &got, NULL);
        error = GetLastError();
        CloseHandle(file);
    }
    fid_check(ok && got == length && !memcmp(data, expected, length), id,
              "error=%lu bytes=%lu path=%ls", error, got, path);
}

int main(void)
{
    WCHAR temp[MAX_PATH], file[MAX_PATH], alias[2048], mapped[1024];
    static const char payload[] = "globalroot path probe";
    static const WCHAR *prefixes[] = {
        L"\\\\.\\GLOBALROOT\\??\\",
        L"\\\\.\\gLoBaLrOoT\\??\\",
        L"\\\\.\\GLOBALROOT\\DosDevices\\",
        L"\\\\.\\GLOBALROOT\\??\\GLOBALROOT\\??\\"
    };
    unsigned i;
    DWORD written;
    HANDLE handle;
    fid_name = "globalroot";
    fid_environment();
    if (!GetTempPathW(ARRAYSIZE(temp), temp) || !GetTempFileNameW(temp, L"hbg", 0, file))
    {
        fid_check(0, "setup", "GetTempFileName error=%lu", GetLastError());
        return fid_summary();
    }
    handle = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE ||
        !WriteFile(handle, payload, sizeof(payload), &written, NULL) || written != sizeof(payload))
    {
        fid_check(0, "setup", "WriteFile error=%lu", GetLastError());
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        DeleteFileW(file);
        return fid_summary();
    }
    CloseHandle(handle);
    check_open("file.dos", file, payload, sizeof(payload));
    for (i = 0; i < ARRAYSIZE(prefixes); ++i)
    {
        char id[64];
        swprintf(alias, ARRAYSIZE(alias), L"%ls%ls", prefixes[i], file);
        snprintf(id, sizeof(id), "file.globalroot.%u", i);
        check_open(id, alias, payload, sizeof(payload));
        snprintf(id, sizeof(id), "attributes.globalroot.%u", i);
        DWORD attributes = GetFileAttributesW(alias), error = GetLastError();
        fid_check(attributes != INVALID_FILE_ATTRIBUTES, id, "error=%lu", error);
    }
    if (GetMappedFileNameW(GetCurrentProcess(), GetModuleHandleW(NULL), mapped, ARRAYSIZE(mapped)))
    {
        swprintf(alias, ARRAYSIZE(alias), L"\\\\.\\GLOBALROOT%ls", mapped);
        check_open("image.mapped-name", alias, "MZ", 2);
    }
    else fid_check(0, "image.mapped-name", "GetMappedFileName error=%lu", GetLastError());
    handle = CreateFileW(L"\\\\.\\GLOBALROOT\\Device\\Null", GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    fid_check(handle != INVALID_HANDLE_VALUE, "device.native-root", "error=%lu", GetLastError());
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    swprintf(alias, ARRAYSIZE(alias), L"\\\\.\\GLOBALROOTx\\??\\%ls", file);
    handle = CreateFileW(alias, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    fid_check(handle == INVALID_HANDLE_VALUE, "file.not-an-alias", "unexpected successful open");
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    swprintf(alias, ARRAYSIZE(alias), L"\\\\.\\GLOBALROOT\\??\\%ls.missing", file);
    handle = CreateFileW(alias, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    fid_check(handle == INVALID_HANDLE_VALUE, "file.missing", "unexpected successful open");
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    DeleteFileW(file);
    return fid_summary();
}
