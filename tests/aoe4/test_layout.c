#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include "aoe_build_guard.h"
static void put16(unsigned char *p,unsigned value){p[0]=value;p[1]=value>>8;}
static void put32(unsigned char *p,uint32_t value){unsigned i;for(i=0;i<4;i++)p[i]=value>>(i*8);}
static int profile(const char *text,const char *hash,const struct aoe_image_layout *image,uint64_t *pointer)
{FILE *f=tmpfile();int result;assert(f);fputs(text,f);rewind(f);result=aoe_cache_profile(f,hash,image,pointer);fclose(f);return result;}
int main(void)
{
 unsigned char pe[1024]={0}; char name[]="/tmp/aoe4-pe-XXXXXX";
 const char *hash="5380c577805565817f528af6eac385263413fa6815553f9a31fa62561cb45e8c";
 struct aoe_image_layout image;
 uint64_t pointer;
 int fd=mkstemp(name);assert(fd>=0);unlink(name);
 memcpy(pe,"MZ",2);put32(pe+60,128);memcpy(pe+128,"PE\0\0",4);
 put16(pe+132,0x8664);put16(pe+134,2);put16(pe+148,240);put16(pe+152,0x20b);
 put32(pe+176,0x40000000);put32(pe+180,1);put32(pe+208,0x8000000);
 memcpy(pe+392,".text",5);put32(pe+400,0x580e95c);put32(pe+404,0x1000);put32(pe+428,0x60000020);
 memcpy(pe+432,".data",5);put32(pe+440,0x1000000);put32(pe+444,0x7000000);put32(pe+468,0xc0000040);
 assert(write(fd,pe,sizeof(pe))==sizeof(pe));assert(aoe_read_layout(fd,&image));
 assert(image.text_start==0x140001000 && image.text_end==0x14580f45c);
 assert(image.data_start==0x147000000 && image.data_end==0x148000000);
 assert(profile("aoe4-cache-v1 5380c577805565817f528af6eac385263413fa6815553f9a31fa62561cb45e8c 7af7ad0 1000000 20\n",hash,&image,&pointer));
 assert(pointer==0x147af7ad0);
 assert(!profile("aoe4-cache-v1 wrong-hash 7af7ad0 1000000 20",hash,&image,&pointer));
 assert(!profile("aoe4-cache-v1 5380c577805565817f528af6eac385263413fa6815553f9a31fa62561cb45e8c 7af7ad1 1000000 20",hash,&image,&pointer));
 assert(!profile("aoe4-cache-v1 5380c577805565817f528af6eac385263413fa6815553f9a31fa62561cb45e8c 1000 1000000 20",hash,&image,&pointer));
 assert(!profile("aoe4-cache-v1 5380c577805565817f528af6eac385263413fa6815553f9a31fa62561cb45e8c 7af7ad0 2000000 20",hash,&image,&pointer));
 assert(!profile("aoe4-cache-v1 5380c577805565817f528af6eac385263413fa6815553f9a31fa62561cb45e8c 7af7ad0 1000000 20 extra",hash,&image,&pointer));
 put32(pe+400,0x6500000);assert(pwrite(fd,pe,sizeof(pe),0)==sizeof(pe));assert(aoe_read_layout(fd,&image));
 pe[132]=0x4c;assert(pwrite(fd,pe,sizeof(pe),0)==sizeof(pe));assert(!aoe_read_layout(fd,&image));
 pe[132]=0x64;put32(pe+60,0xfffffff0);assert(pwrite(fd,pe,sizeof(pe),0)==sizeof(pe));assert(!aoe_read_layout(fd,&image));
 put32(pe+60,128);put32(pe+400,0xfffffff0);assert(pwrite(fd,pe,sizeof(pe),0)==sizeof(pe));assert(!aoe_read_layout(fd,&image));
 assert(!ftruncate(fd,63));assert(!aoe_read_layout(fd,&image));close(fd);
 puts("PASS: current-layout bounds, profile digest, pointer/data bounds, malformed PE/profile refusals");
}
