/* Wait timings and checks for an msync comparison: an event ping-pong, eight threads on a semaphore and a mutex,
 * WaitAny/WaitAll, APCs to an alertable wait, and a mutex abandoned by an exiting thread. The timings are INFO lines;
 * run on the same engine with and without WINEMSYNC=1. */
#include "fid.h"

enum { ROUNDS = 200000, WORKERS = 8, PER_WORKER = 20000, APCS = 1000, TIMEOUT = 5000 };

static HANDLE ev_a, ev_b, sem, mtx, done_ev, abandon_mtx;
static volatile LONG counter, apc_count;

static DWORD WINAPI pingpong(void *arg)
{
    for (int i = 0; i < ROUNDS; i++)
    {
        if (WaitForSingleObject(ev_a, TIMEOUT) != WAIT_OBJECT_0) return 1;
        SetEvent(ev_b);
    }
    return 0;
}

static DWORD WINAPI worker(void *arg)
{
    for (int i = 0; i < PER_WORKER; i++)
    {
        WaitForSingleObject(sem, INFINITE);
        WaitForSingleObject(mtx, INFINITE);
        counter++;
        ReleaseMutex(mtx);
    }
    return 0;
}

static void CALLBACK apc(ULONG_PTR data)
{
    InterlockedIncrement(&apc_count);
}

static DWORD WINAPI alertable(void *arg)
{
    while (WaitForSingleObjectEx(done_ev, INFINITE, TRUE) == WAIT_IO_COMPLETION) ;
    return 0;
}

/* Takes the mutex and exits without releasing it. */
static DWORD WINAPI abandon(void *arg)
{
    return WaitForSingleObject(abandon_mtx, TIMEOUT) == WAIT_OBJECT_0 ? 0 : 1;
}

static double seconds(LARGE_INTEGER t0, LARGE_INTEGER t1, LARGE_INTEGER f)
{
    return (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
}

int main(void)
{
    LARGE_INTEGER f, t0, t1;
    HANDLE th, w[WORKERS], objs[3], at;
    int ok = 1;

    fid_name = "msync-pingpong";
    QueryPerformanceFrequency(&f);

    ev_a = CreateEventW(NULL, FALSE, FALSE, NULL);
    ev_b = CreateEventW(NULL, FALSE, FALSE, NULL);
    th = CreateThread(NULL, 0, pingpong, NULL, 0, NULL);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < ROUNDS && ok; i++)
    {
        SetEvent(ev_a);
        ok = WaitForSingleObject(ev_b, TIMEOUT) == WAIT_OBJECT_0;
    }
    QueryPerformanceCounter(&t1);
    fid_check(ok && WaitForSingleObject(th, TIMEOUT) == WAIT_OBJECT_0, "pingpong", "a round trip timed out");
    fid_info("pingpong.us", "%.1f us per round trip", 1e6 * seconds(t0, t1, f) / ROUNDS);

    sem = CreateSemaphoreW(NULL, 0, WORKERS * PER_WORKER, NULL);
    mtx = CreateMutexW(NULL, FALSE, NULL);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < WORKERS; i++) w[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    for (int i = 0; i < WORKERS * PER_WORKER; i += 100) ReleaseSemaphore(sem, 100, NULL);
    fid_check(WaitForMultipleObjects(WORKERS, w, TRUE, 30000) == WAIT_OBJECT_0, "workers.waitall", "workers did not finish");
    QueryPerformanceCounter(&t1);
    fid_check(counter == WORKERS * PER_WORKER, "workers.counter", "counter=%ld, expected %d", counter, WORKERS * PER_WORKER);
    fid_info("workers.s", "%.3f s for %d threads x %d", seconds(t0, t1, f), WORKERS, PER_WORKER);

    objs[0] = CreateEventW(NULL, TRUE, FALSE, NULL);
    objs[1] = CreateSemaphoreW(NULL, 1, 1, NULL);
    objs[2] = CreateEventW(NULL, TRUE, FALSE, NULL);
    fid_check(WaitForMultipleObjects(3, objs, FALSE, 0) == WAIT_OBJECT_0 + 1, "waitany.semaphore", "the signaled semaphore was not picked");
    fid_check(WaitForMultipleObjects(3, objs, TRUE, 50) == WAIT_TIMEOUT, "waitall.timeout", "WaitAll returned with objects unsignaled");
    SetEvent(objs[0]);
    ReleaseSemaphore(objs[1], 1, NULL);
    SetEvent(objs[2]);
    fid_check(WaitForMultipleObjects(3, objs, TRUE, 1000) == WAIT_OBJECT_0, "waitall.signaled", "WaitAll did not return");
    fid_check(WaitForSingleObject(objs[1], 0) == WAIT_TIMEOUT, "waitall.consumed", "WaitAll left the semaphore count");

    done_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    at = CreateThread(NULL, 0, alertable, NULL, 0, NULL);
    Sleep(50);
    for (int i = 0; i < APCS; i++) QueueUserAPC(apc, at, 0);
    Sleep(300);
    SetEvent(done_ev);
    fid_check(WaitForSingleObject(at, TIMEOUT) == WAIT_OBJECT_0, "apc.exit", "the alertable thread did not exit");
    fid_check(apc_count == APCS, "apc.count", "%ld of %d APCs delivered", apc_count, APCS);

    abandon_mtx = CreateMutexW(NULL, FALSE, NULL);
    th = CreateThread(NULL, 0, abandon, NULL, 0, NULL);
    fid_check(WaitForSingleObject(th, TIMEOUT) == WAIT_OBJECT_0, "abandon.thread", "the owning thread did not exit");
    fid_check(WaitForSingleObject(abandon_mtx, TIMEOUT) == WAIT_ABANDONED_0, "abandon.wait", "an abandoned mutex did not report WAIT_ABANDONED");
    fid_check(ReleaseMutex(abandon_mtx), "abandon.release", "the new owner could not release it (%lu)", GetLastError());

    return fid_summary();
}
