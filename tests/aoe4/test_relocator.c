#include <assert.h>
#include <stdio.h>
#include "aoe_relocate.h"
static int32_t rel(const uint8_t *p) { int32_t n; memcpy(&n,p,4); return n; }
int main(void) {
 struct aoe_fragment_key k; uint8_t out[40]; uint64_t pc=0x140010000ULL, shadow=0x190010000ULL;
 const uint8_t ret[]={0xb8,42,0,0,0,0xc3};
 assert(aoe_fragment_key(ret,sizeof ret,pc,&k)); assert(k.len==6); assert(aoe_fragment_emit(&k,shadow,out)); assert(!memcmp(ret,out,6));
 const uint8_t rip[]={0x48,0x8b,0x05,0x10,0,0,0,0xc3};
 assert(aoe_fragment_key(rip,sizeof rip,pc,&k)); assert(k.targets[0]==pc+23); assert(aoe_fragment_emit(&k,shadow,out)); assert(shadow+7+rel(out+3)==pc+23);
 const uint8_t jcc[]={0x0f,0x85,0x20,0,0,0,0xc3};
 assert(aoe_fragment_key(jcc,sizeof jcc,pc,&k)); assert(aoe_fragment_emit(&k,shadow,out)); assert(shadow+6+rel(out+2)==pc+38);
 assert(!aoe_fragment_emit(&k,0x700000000000ULL,out));
 const uint8_t internal[]={0x0f,0x85,0,0,0,0,0xc3}; assert(!aoe_fragment_key(internal,sizeof internal,pc,&k));
 const uint8_t call[]={0xe8,0x20,0,0,0,0xc3}; assert(!aoe_fragment_key(call,sizeof call,pc,&k));
 const uint8_t shortjcc[]={0x75,0x20,0xc3}; assert(!aoe_fragment_key(shortjcc,sizeof shortjcc,pc,&k));
 const uint8_t jump[]={0xeb,0x20}; assert(!aoe_fragment_key(jump,sizeof jump,pc,&k));
 assert(!aoe_fragment_key(rip,3,pc,&k));
 puts("PASS: relocations preserve targets; unsupported control flow and overflow refused");
}
