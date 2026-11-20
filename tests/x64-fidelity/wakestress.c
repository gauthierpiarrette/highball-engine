/* WaitOnAddress / WakeByAddressSingle under load, as a job system uses them: producers add work to a counter and
 * wake, consumers take work or wait on the counter. Every item has to be consumed, and a wait timing out with work
 * pending is counted even if the work is later consumed. Records the cost of a wake with nobody
 * waiting and the items per second for 4, 16 and 24 producers with 8 consumers.
 * Arguments: [producers consumers items-per-producer] runs that one configuration only. */
#include "fid.h"
#include <stdlib.h>

static volatile LONG work, done, stop, pending_timeouts, wait_errors;
static LONG items;

static DWORD WINAPI consumer(void *arg)
{
    for (;;)
    {
        LONG w = work, zero = 0;

        if (w > 0)
        {
            if (InterlockedCompareExchange(&work, w - 1, w) == w) InterlockedIncrement(&done);
            continue;
        }
        if (stop) return 0;
        if (!WaitOnAddress((void *)&work, &zero, sizeof(LONG), 50))
        {
            if (GetLastError() != ERROR_TIMEOUT) InterlockedIncrement(&wait_errors);
            else if (InterlockedCompareExchange(&work, 0, 0) > 0) InterlockedIncrement(&pending_timeouts);
        }
    }
}

static DWORD WINAPI producer(void *arg)
{
    LONG i;

    for (i = 0; i < items; i++)
    {
        InterlockedIncrement(&work);
        WakeByAddressSingle((void *)&work);
        if (!(i & 63)) SwitchToThread();
    }
    return 0;
}

static void run(int producers, int consumers, LONG per_producer)
{
    HANDLE threads[128];
    LARGE_INTEGER freq, t0, t1;
    LONG expected = per_producer * producers;
    DWORD start;
    double seconds;
    char id[64];
    int i;

    work = done = stop = pending_timeouts = wait_errors = 0;
    items = per_producer;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < consumers; i++) threads[i] = CreateThread(NULL, 0, consumer, NULL, 0, NULL);
    for (i = 0; i < producers; i++) threads[consumers + i] = CreateThread(NULL, 0, producer, NULL, 0, NULL);
    WaitForMultipleObjects(producers, threads + consumers, TRUE, INFINITE);
    start = GetTickCount();
    while (done < expected && GetTickCount() - start < 10000) Sleep(1);
    QueryPerformanceCounter(&t1);
    stop = 1;
    WakeByAddressAll((void *)&work);
    WaitForMultipleObjects(consumers, threads, TRUE, 5000);
    for (i = 0; i < consumers + producers; i++) CloseHandle(threads[i]);

    seconds = (t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    snprintf(id, sizeof(id), "items.%dp%dc", producers, consumers);
    fid_check(done == expected, id, "%ld of %ld items consumed", done, expected);
    snprintf(id, sizeof(id), "wait.%dp%dc", producers, consumers);
    /* Work can also arrive just as a timeout returns, so report the observation as such. */
    fid_check(!pending_timeouts && !wait_errors, id, "%ld timeouts with work pending, %ld unexpected wait errors",
              pending_timeouts, wait_errors);
    snprintf(id, sizeof(id), "rate.%dp%dc", producers, consumers);
    fid_info(id, "%.2f million items/s (%ld items in %.3f s)", done / seconds / 1e6, done, seconds);
}

int main(int argc, char **argv)
{
    static volatile LONG lonely;
    LARGE_INTEGER freq, t0, t1;
    int i;

    fid_name = "wakestress";
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < 5000000; i++) WakeByAddressSingle((void *)&lonely);
    QueryPerformanceCounter(&t1);
    fid_info("wake.no-waiter", "%.1f ns", (t1.QuadPart - t0.QuadPart) * 1e9 / freq.QuadPart / 5000000);

    if (argc > 3)
    {
        int producers = atoi(argv[1]), consumers = atoi(argv[2]);
        if (producers < 1 || consumers < 1 || producers + consumers > 128) return 255;
        run(producers, consumers, atoi(argv[3]));
    }
    else
    {
        run(4, 8, 300000);
        run(16, 8, 300000);
        run(24, 8, 300000);
    }
    return fid_summary();
}
