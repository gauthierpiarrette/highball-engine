/* Exercise alertable waits and thread exit while a file and event remain in use. */
#include "fid.h"

enum { THREADS = 2000, TIMEOUT = 5000 };

static void CALLBACK apc(ULONG_PTR data)
{
    InterlockedIncrement((volatile LONG *)data);
}

struct wait_args
{
    HANDLE ready, event;
};

static DWORD WINAPI alertable_wait(void *arg)
{
    const struct wait_args *wait = arg;

    if (!SetEvent(wait->ready)) return WAIT_FAILED;
    return WaitForSingleObjectEx(wait->event, TIMEOUT, TRUE);
}

static int check_file(HANDLE file, DWORD value)
{
    LARGE_INTEGER start = { 0 };
    DWORD readback, bytes;

    return SetFilePointerEx(file, start, NULL, FILE_BEGIN) &&
           WriteFile(file, &value, sizeof(value), &bytes, NULL) && bytes == sizeof(value) &&
           SetFilePointerEx(file, start, NULL, FILE_BEGIN) &&
           ReadFile(file, &readback, sizeof(readback), &bytes, NULL) && bytes == sizeof(readback) &&
           readback == value;
}

int main(void)
{
    wchar_t temp_dir[MAX_PATH], path[MAX_PATH];
    HANDLE file, event, ready;
    DWORD completed = 0, status, length;
    volatile LONG calls = 0;

    fid_name = "msync-thread-exit";
    length = GetTempPathW(MAX_PATH, temp_dir);
    if (!length || length >= MAX_PATH || !GetTempFileNameW(temp_dir, L"hbm", 0, path))
    {
        fid_check(0, "temp-file", "could not create a temporary file (%lu)", GetLastError());
        return fid_summary();
    }
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ready = CreateEventW(NULL, FALSE, FALSE, NULL);

    if (file != INVALID_HANDLE_VALUE && event && ready)
    {
        struct wait_args wait = { ready, event };

        for (; completed < THREADS; ++completed)
        {
            HANDLE thread = CreateThread(NULL, 0, alertable_wait, &wait, 0, NULL);
            BOOL queued;
            DWORD joined;

            if (!thread) break;
            queued = WaitForSingleObject(ready, TIMEOUT) == WAIT_OBJECT_0 &&
                     QueueUserAPC(apc, thread, (ULONG_PTR)&calls);
            joined = WaitForSingleObject(thread, 2 * TIMEOUT);
            if (joined != WAIT_OBJECT_0)
            {
                fid_check(0, "join", "thread %lu did not exit (wait %#lx)", completed, joined);
                ExitProcess(fid_summary());
            }
            queued = queued && GetExitCodeThread(thread, &status) && status == WAIT_IO_COMPLETION;
            CloseHandle(thread);
            if (!queued || !check_file(file, completed) || !SetEvent(event) ||
                WaitForSingleObject(event, 0) != WAIT_OBJECT_0)
                break;
        }
    }

    fid_check(completed == THREADS, "thread-exit", "%lu of %d complete alertable waits and file/event checks",
              completed, THREADS);
    fid_check(calls == THREADS, "apcs", "%ld of %d APCs delivered", calls, THREADS);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    else DeleteFileW(path);
    if (event) CloseHandle(event);
    if (ready) CloseHandle(ready);
    return fid_summary();
}
