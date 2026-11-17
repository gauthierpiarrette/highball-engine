#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef int BOOL;
typedef uintptr_t UINT_PTR;
BOOL aoe_game_verified=1;
BOOL aoe_cache_auto_discovered;
uint64_t test_ring;
uint64_t aoe_cache_search_start, aoe_cache_search_end;
uint64_t aoe_cache_pointer_address=(uintptr_t)&test_ring;
typedef unsigned char BYTE; typedef size_t SIZE_T;
static int main_argc=1; static char *main_argv[]={"RelicCardinal.exe"};
#define GetCurrentProcess() 0
#define MEM_RESERVE 1
#define MEM_COMMIT 2
#define MEM_RELEASE 4
#define PAGE_EXECUTE_READWRITE 7
#define PAGE_EXECUTE_READ 8
#define PAGE_EXECUTE_WRITECOPY 9
#define MEM_PRIVATE 0x20000
#define MemoryBasicInformation 0
typedef struct { void *AllocationBase, *BaseAddress; size_t RegionSize; int State, Type, Protect; } MEMORY_BASIC_INFORMATION;
static int ring_region_size=0x1000000, ring_queries;
static int NtQueryVirtualMemory(int process, const void *addr, int kind, MEMORY_BASIC_INFORMATION *info,
                                size_t size, size_t *result_size)
{
    (void)process;(void)kind;(void)size;(void)result_size;
    ++ring_queries;
    if ((uintptr_t)addr!=test_ring) return 1;
    *info=(MEMORY_BASIC_INFORMATION){ (void *)(uintptr_t)test_ring, (void *)(uintptr_t)test_ring,
                                    ring_region_size, MEM_COMMIT, MEM_PRIVATE, PAGE_EXECUTE_READWRITE };
    return 0;
}
static int NtAllocateVirtualMemory(int h, void **p, int z, SIZE_T *n, int flags, int prot) {
 (void)h;(void)z;(void)flags;(void)prot;
 void *r=mmap(*p,*n,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(r==MAP_FAILED) return 1;
 if(r!=*p){munmap(r,*n);return 1;}
 *p=r;return 0;
}
static int NtFreeVirtualMemory(int h,void **p,SIZE_T *n,int flags) { (void)h;(void)n;(void)flags;return munmap(*p,262144*64); }
static SIZE_T virtual_uninterrupted_read_memory(const void *p,void *q,SIZE_T n) {memcpy(q,p,n);return n;}
static BOOL fail_snapshot;
static SIZE_T virtual_read_aoe_fragment(const void *pointer, UINT_PTR pc, UINT_PTR *ring, BYTE *bytes)
{
    SIZE_T length;

    if (fail_snapshot) return 0;
    memcpy(ring, pointer, sizeof(*ring));
    if (!*ring || (*ring & 31) || *ring > UINTPTR_MAX - 0x1000000 ||
        pc < *ring || pc - *ring >= 0x1000000) return 0;
    length = 32 - (pc - *ring) % 32;
    memcpy(bytes, (void *)pc, length);
    return length;
}
#include "aoe_code_cache.h"
int main(int argc,char **argv) {
 unsetenv("HB_AOE4");
 unsetenv("AOE4_CACHE");
 unsetenv("AOE4_DISABLED");
 int auto_invalid=argc>1 && !strcmp(argv[1],"auto-invalid");
 aoe_cache_auto_discovered=auto_invalid || (argc>1 && !strcmp(argv[1],"auto"));
 if(argc>1 && !strcmp(argv[1],"unverified")){aoe_game_verified=0;setenv("HB_AOE4","1",1);}
 if(argc>1 && !strcmp(argv[1],"other-game")){main_argv[0]="other.exe";setenv("HB_AOE4","1",1);}
 if(argc>1 && !strcmp(argv[1],"missing-profile")){aoe_cache_pointer_address=0;setenv("HB_AOE4","1",1);}
 if(argc>1 && !strcmp(argv[1],"cache-off")){setenv("HB_AOE4","1",1);setenv("AOE4_CACHE","0",1);}
 if(argc>1 && !aoe_cache_auto_discovered){assert(aoe_cache_target(0x140100000ULL)==0x140100000ULL);assert(!aoe_cache_attempted);puts("PASS: cache refuses disabled / unverified / other-game profile");return 0;}
 BYTE *ring=mmap((void*)0x140100000ULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(ring!=(void*)MAP_FAILED && ring==(void*)0x140100000ULL);
 BYTE code[]={0xb8,42,0,0,0,0xc3}; memcpy(ring,code,sizeof code);memcpy(ring+32,code,sizeof code);
 setenv("HB_AOE4","1",1);test_ring=(uintptr_t)ring;
 if(auto_invalid){
  ring_region_size=4096;
  assert(aoe_cache_target(test_ring)==test_ring && !aoe_cache_attempted);
  assert(aoe_cache_target(test_ring)==test_ring && ring_queries==2);
  puts("PASS: discovered cache rejects a truncated live allocation");
  return 0;
 }
 uint64_t a=aoe_cache_target((uint64_t)ring),b=aoe_cache_target((uint64_t)ring+32);assert(a!=(uint64_t)ring);assert(a==b);assert(aoe_cache_used==1);
 assert(((int(*)(void))(uintptr_t)a)()==42);assert(!memcmp(ring,code,sizeof code));assert(aoe_cache_logical_pc(b+2)==(uint64_t)ring+34);
 fail_snapshot=1;
 assert(aoe_cache_target((uintptr_t)ring)==(uintptr_t)ring && aoe_cache_used==1);
 fail_snapshot=0;
 ring[64]=0xe8;assert(aoe_cache_target((uint64_t)ring+64)==(uint64_t)ring+64);
 assert(aoe_cache_target(0x1000)==0x1000);
 test_ring=(uintptr_t)ring+1;assert(aoe_cache_target((uintptr_t)ring)==(uintptr_t)ring);
 test_ring=0;assert(aoe_cache_target((uintptr_t)ring)==(uintptr_t)ring);test_ring=(uintptr_t)ring;
 ring[1]=43;uint64_t changed=aoe_cache_target((uintptr_t)ring);
 assert(changed!=a && ((int(*)(void))(uintptr_t)changed)()==43 && aoe_cache_used==2);
 /* The same instruction and absolute target can appear in different ring slots. */
 BYTE branch[] = {0x31, 0xc0, 0xe9, 0, 0, 0, 0};
 const unsigned first=96, second=128, destination=1024;
 int32_t displacement=destination-first-sizeof(branch);
 memcpy(branch+3,&displacement,sizeof(displacement));
 memcpy(ring+first,branch,sizeof(branch));
 memcpy(ring+second,branch,sizeof(branch)); /* same relative bytes, different absolute target */
 ring[destination]=ring[destination+(second-first)]=0xc3;
 uint64_t branch_a=aoe_cache_target((uintptr_t)ring+first);
 uint64_t branch_b=aoe_cache_target((uintptr_t)ring+second);
 assert(branch_a!=(uintptr_t)ring+first && branch_b!=(uintptr_t)ring+second && branch_a!=branch_b);
 displacement=destination-second-sizeof(branch);
 memcpy(ring+second+3,&displacement,sizeof(displacement));
 assert(aoe_cache_target((uintptr_t)ring+second)==branch_a && aoe_cache_stats[5]>0);
 assert(((int(*)(void))(uintptr_t)branch_a)()==0);
 /* Reuse is refused if the same target is now inside the source slot. */
 displacement=-(int32_t)sizeof(branch);
 memcpy(branch+3,&displacement,sizeof(displacement));
 memcpy(ring+destination,branch,sizeof(branch));
 assert(aoe_cache_target((uintptr_t)ring+destination)==(uintptr_t)ring+destination);
 assert(ring_queries==(aoe_cache_auto_discovered ? 1 : 0));
 puts("PASS: deduplication, executable result, original bytes, PC mapping, fallback");
}
