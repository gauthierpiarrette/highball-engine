/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Windows regression fixture for the test-only VM reader bridge. Never use its
 * instrumented ntdll in a gameplay/release engine. See VM-READER.md. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { ring_bytes = 16 * 1024 * 1024, slot_bytes = 32 };

typedef LONG (WINAPI *set_context_t)(HANDLE, const CONTEXT *);
static set_context_t set_context;
static int failures;

static void check(BOOL good, const char *label)
{
    printf("%s %s\n", good ? "PASS" : "FAIL", label);
    if (!good) ++failures;
}

static LONG snapshot(void **ring_pointer, void *pc, unsigned char output[32], BOOL fast)
{
    CONTEXT context = { 0 };
    context.ContextFlags = 0xa0e40000;
    context.Rdi = (uintptr_t)ring_pointer;
    context.Rip = (uintptr_t)pc;
    context.Rsi = (uintptr_t)output;
    context.R8 = fast;
    LONG result=set_context(GetCurrentThread(), &context);
    if(fast==2)printf("RAW result=%#lx source=%p\n",result,pc);
    return result;
}

static void test_read(BOOL fast)
{
    SYSTEM_INFO si;
    DWORD old;
    MEMORY_BASIC_INFORMATION info;
    unsigned char bytes[32] = { 0 };
    unsigned char *ring, *next;
    void **pointer;
    LONG size;

    if (fast) check(snapshot(NULL, NULL, bytes, 3) == 32, "AoE process scope enables VM tracking");
    printf("Testing %s Wine reader\n", fast ? "r3" : "r2");
    GetSystemInfo(&si);
    ring = VirtualAlloc(NULL, ring_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    next = VirtualAlloc(NULL, ring_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    pointer = VirtualAlloc(NULL, si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!ring || !next || !pointer) ExitProcess(2);
    *pointer = ring;
    memset(ring, 0x4d, slot_bytes);
    memset(next, 0x71, slot_bytes);
    size = snapshot(pointer, ring, bytes, fast);
    check(size == slot_bytes && bytes[0] == 0x4d && bytes[slot_bytes - 1] == 0x4d,
          "reads a live ring slot");
    ring[5] = 0xb1;
    check(snapshot(pointer, ring + 5, bytes, fast) == slot_bytes - 5 && bytes[0] == 0xb1,
          "observes a rewritten instruction at a slot offset");

    if (!VirtualProtect(ring, si.dwPageSize, PAGE_EXECUTE_READWRITE | PAGE_GUARD, &old)) ExitProcess(3);
    size = snapshot(pointer, ring, bytes, fast);
    VirtualQuery(ring, &info, sizeof(info));
    printf("Guard probe returned %ld bytes, protect=%#lx\n", size, info.Protect);
    check(size == 0, "refuses guarded fragment");
    check(info.Protect & PAGE_GUARD, "preserves guard-page protection");
    if (!VirtualProtect(ring, si.dwPageSize, PAGE_NOACCESS, &old)) ExitProcess(4);
    check(snapshot(pointer, ring, bytes, fast) == 0, "refuses no-access fragment");
    if (!VirtualFree(ring, 0, MEM_RELEASE)) ExitProcess(5);
    check(snapshot(pointer, ring, bytes, fast) == 0, "refuses released ring");

    *pointer = next;
    check(snapshot(pointer, ring, bytes, fast) == 0, "refuses address in the old ring after relocation");
    check(snapshot(pointer, next, bytes, fast) == slot_bytes && bytes[0] == 0x71,
          "reads the new ring after relocation");
    if (!VirtualProtect(pointer, si.dwPageSize, PAGE_READWRITE | PAGE_GUARD, &old)) ExitProcess(6);
    size = snapshot(pointer, next, bytes, fast);
    VirtualQuery(pointer, &info, sizeof(info));
    check(size == 0 && (info.Protect & PAGE_GUARD), "preserves guard on the ring pointer");
    if (!VirtualFree(pointer, 0, MEM_RELEASE)) ExitProcess(7);
    check(snapshot(pointer, next, bytes, fast) == 0, "refuses a released pointer page");
    VirtualFree(next, 0, MEM_RELEASE);
}

struct bench {
    unsigned char *ring;
    void **pointer;
    BOOL fast;
    int reads;
    unsigned int sum;
    int errors;
};
static DWORD WINAPI bench_thread(LPVOID opaque)
{
    struct bench *test = opaque;
    unsigned char data[32];
    for (int i = 0; i < test->reads; ++i)
    {
        unsigned char *slot = test->ring + ((unsigned)i & 1023) * slot_bytes;
        LONG len = snapshot(test->pointer, slot, data, test->fast);
        /* Game-like alternation: the next context resumes ordinary code. */
        if(snapshot(test->pointer, test->ring + ring_bytes + 4096, data, test->fast)!=0)
            ++test->errors;
        if (len != slot_bytes)
            ++test->errors;
        else
            test->sum += data[0] + data[slot_bytes - 1];
    }
    return 0;
}
static void benchmark(void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    for (int threads = 1; threads <= 2; ++threads)
    {
        for (int iter = 0; iter < 8; ++iter)
        {
            LARGE_INTEGER first, last;
            const BOOL fast = iter % 2;
            HANDLE worker = NULL;
            struct bench tests[2] = {0};
            for (int j = 0; j < threads; ++j)
            {
                tests[j].ring = VirtualAlloc(NULL, ring_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                tests[j].pointer = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (!tests[j].ring || !tests[j].pointer) ExitProcess(2);
                *tests[j].pointer = tests[j].ring;
                memset(tests[j].ring, 0xab, 1024 * slot_bytes);
                tests[j].fast = fast;
                tests[j].reads = 3000;
                bench_thread(&tests[j]);
                tests[j].reads = 80000;
            }
            /* Threads use the same process ring, as the game does. */
            if (threads == 2)
            {
                VirtualFree(tests[1].pointer, 0, MEM_RELEASE);
                VirtualFree(tests[1].ring, 0, MEM_RELEASE);
                tests[1] = tests[0];
            }
            QueryPerformanceCounter(&first);
            if (threads == 2)
            {
                worker = CreateThread(NULL, 0, bench_thread, &tests[1], 0, NULL);
                if (!worker) ExitProcess(9);
            }
            bench_thread(&tests[0]);
            if (worker)
            {
                WaitForSingleObject(worker, INFINITE);
                CloseHandle(worker);
            }
            QueryPerformanceCounter(&last);
            if (tests[0].errors || tests[1].errors || !tests[0].sum) ExitProcess(10);
            printf("BENCH %d %s %.1f ns/pair\n", threads, fast ? "candidate" : "r2",
                   1e9 * (last.QuadPart - first.QuadPart) / freq.QuadPart / (threads * tests[0].reads));
            VirtualFree(tests[0].pointer, 0, MEM_RELEASE);
            VirtualFree(tests[0].ring, 0, MEM_RELEASE);
        }
    }
}

static void test_nofault(void)
{
    unsigned char bytes[32];
    DWORD old;
    MEMORY_BASIC_INFORMATION info;
    void *memory=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(memory != NULL,"allocates nofault probe");
    if(!memory)ExitProcess(2);
    memset(memory,0x4b,32);
    check(snapshot(&memory,memory,bytes,2)==32 && bytes[0]==0x4b,"nofault copies valid bytes");
    VirtualProtect(memory,4096,PAGE_READWRITE|PAGE_GUARD,&old);
    check(snapshot(&memory,memory,bytes,2)==0,"nofault catches guarded read");
    VirtualQuery(memory,&info,sizeof(info));
    check(!!(info.Protect&PAGE_GUARD),"nofault leaves guard untouched");
    VirtualFree(memory,0,MEM_RELEASE);
    check(snapshot(&memory,memory,bytes,2)==0,"nofault catches unmapped read");
    check(snapshot(&memory,NULL,bytes,2)==0,"nofault catches null read");
}

struct mutation_test {
    void **pointer;
    unsigned char *ring;
    volatile LONG stop;
    BOOL fast;
    LONG errors, reads;
};
static DWORD WINAPI mutate_reader(LPVOID opaque)
{
    struct mutation_test *t=opaque;
    unsigned char bytes[32];
    while(!InterlockedCompareExchange(&t->stop,0,0)) {
        LONG size=snapshot(t->pointer,t->ring,bytes,t->fast);
        if(size!=0 && size!=32)++t->errors;
        if(size==32) for(int i=0;i<32;i++) if(bytes[i]!=0x61)++t->errors;
        ++t->reads;
    }
    return 0;
}
static void test_mutation(BOOL fast)
{
    struct mutation_test t={0};
    HANDLE thread;
    DWORD old;
    unsigned char bytes[32];
    MEMORY_BASIC_INFORMATION info;
    t.fast=fast;
    t.ring=VirtualAlloc(NULL,ring_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    t.pointer=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!t.ring || !t.pointer)ExitProcess(2);
    *t.pointer=t.ring;memset(t.ring,0x61,32);
    thread=CreateThread(NULL,0,mutate_reader,&t,0,NULL);
    if(!thread)ExitProcess(3);
    for(int i=0;i<3000;i++) {
        if(!VirtualProtect(t.ring,4096,PAGE_READWRITE|PAGE_GUARD,&old))ExitProcess(4);
        SwitchToThread();
        VirtualQuery(t.ring,&info,sizeof(info));
        if(!(info.Protect&PAGE_GUARD)) ++t.errors;
        if(!VirtualProtect(t.ring,4096,PAGE_READWRITE,&old))ExitProcess(4);
    }
    InterlockedExchange(&t.stop,1);
    WaitForSingleObject(thread,INFINITE);CloseHandle(thread);
    printf("Concurrent guarded reads: %ld, errors: %ld\n",t.reads,t.errors);
    check(t.reads>0 && !t.errors,"concurrent protection changes preserve guards and valid data");
    for(int i=0;i<100;i++) {
        check(snapshot(t.pointer,t.ring,bytes,fast)==32,"reads before same-address remap");
        if(!VirtualFree(t.ring,0,MEM_RELEASE))ExitProcess(5);
        check(snapshot(t.pointer,t.ring,bytes,fast)==0,"rejects freed mapping");
        if(VirtualAlloc(t.ring,ring_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)!=t.ring)ExitProcess(6);
        memset(t.ring,0x61,32);
        if(!VirtualFree(t.ring,4096,MEM_DECOMMIT))ExitProcess(7);
        check(snapshot(t.pointer,t.ring,bytes,fast)==0,"rejects decommitted cached page");
        if(!VirtualAlloc(t.ring,4096,MEM_COMMIT,PAGE_READWRITE))ExitProcess(8);
        memset(t.ring,0x61,32);
    }
    VirtualFree(t.ring,0,MEM_RELEASE);VirtualFree(t.pointer,0,MEM_RELEASE);
}

int main(int argc, char **argv)
{
    set_context = (set_context_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetContextThread");
    if (!set_context || argc != 2) return 8;
    if (!strcmp(argv[1], "scope-off"))
    {
        unsigned char bytes[32] = {0};
        check(snapshot(NULL, NULL, bytes, 3) == 0, "VM tracking stays disabled without opt-in");
    }
    else if (!strcmp(argv[1], "nofault"))
        test_nofault();
    else if (!strcmp(argv[1], "stress"))
        test_mutation(TRUE);
    else if (!strcmp(argv[1], "stress-r2"))
        test_mutation(FALSE);
    else if (!strcmp(argv[1], "bench"))
        benchmark();
    else
        test_read(!strcmp(argv[1], "r3"));
    return failures ? 1 : 0;
}
