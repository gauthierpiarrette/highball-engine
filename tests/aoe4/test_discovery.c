#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include "aoe_ring_discovery.h"

static void put32(unsigned char *p, uint32_t value)
{
    for (unsigned int i=0; i<4; ++i) p[i]=value>>(i*8);
}

static void test_file(const char *path, const char *rva)
{
    struct aoe_image_layout image;
    struct stat info;
    uint64_t pointer;
    int fd=open(path,O_RDONLY);
    assert(fd>=0 && !fstat(fd,&info) && aoe_read_layout(fd,&image));
    pointer=aoe_discover_ring_pointer(fd,&image,info.st_size);
    assert(pointer && pointer-image.base==strtoull(rva,NULL,16));
    printf("PASS: discovered pointer RVA %#llx\n",(unsigned long long)(pointer-image.base));
    close(fd);
}

int main(int argc, char **argv)
{
    struct aoe_image_layout image = {
        .text_start=0x140001000ULL, .text_end=0x140013000ULL,
        .data_start=0x141000000ULL, .data_end=0x141001000ULL,
        .text_offset=0x600, .text_file_size=0x12000
    };
    unsigned char init[] = {
        0x41,0xbe,0x00,0x10,0x00,0x01,0x49,0x03,0xde,
        0x41,0xb9,0x40,0x00,0x00,0x00,0x41,0xb8,0x00,0x30,0x00,0x00,
        0x49,0x8b,0xd6,0x48,0x8b,0xcb,0xe8,0,0,0,0,
        0x48,0x8b,0xf8,0x48,0x89,0x05,0,0,0,0,0x48,0x85,0xc0,0x74,0xd7
    };
    unsigned char writer[] = {
        0x81,0xe3,0xff,0xff,0x07,0x00,0x48,0xc1,0xe3,0x05,0x48,0x03,0x1d,0,0,0,0
    };
    const unsigned int init_offset=0x100, writer_offset=65530, duplicate_offset=0x11000;
    const uint64_t pointer=image.data_start+0x100;
    const off_t size=image.text_offset+image.text_file_size;
    char name[]="/tmp/aoe4-discovery-XXXXXX";
    int fd;

    if (argc==3)
    {
        test_file(argv[1],argv[2]);
        return 0;
    }

    fd=mkstemp(name);
    assert(fd>=0);
    unlink(name);
    assert(!ftruncate(fd,size));

    put32(init+28,0x100); /* Call into .text. */
    put32(init+38,pointer-(image.text_start+init_offset+42));
    put32(writer+13,pointer-(image.text_start+writer_offset+sizeof(writer)));
    assert(pwrite(fd,writer,sizeof(writer),image.text_offset+writer_offset)==sizeof(writer));
    assert(!aoe_discover_ring_pointer(fd,&image,size)); /* Writer without allocator. */
    assert(pwrite(fd,init,sizeof(init),image.text_offset+init_offset)==sizeof(init));
    assert(aoe_discover_ring_pointer(fd,&image,size)==pointer);
    assert(!aoe_discover_ring_pointer(fd,&image,size-1));

    writer[9]=6; /* Slot shift changed. */
    assert(pwrite(fd,writer,sizeof(writer),image.text_offset+writer_offset)==sizeof(writer));
    assert(!aoe_discover_ring_pointer(fd,&image,size));
    writer[9]=5;

    put32(writer+13,pointer+8-(image.text_start+writer_offset+sizeof(writer)));
    assert(pwrite(fd,writer,sizeof(writer),image.text_offset+writer_offset)==sizeof(writer));
    assert(!aoe_discover_ring_pointer(fd,&image,size)); /* Cross-references disagree. */
    put32(writer+13,pointer-(image.text_start+writer_offset+sizeof(writer)));
    assert(pwrite(fd,writer,sizeof(writer),image.text_offset+writer_offset)==sizeof(writer));

    put32(writer+13,pointer-(image.text_start+duplicate_offset+sizeof(writer)));
    assert(pwrite(fd,writer,sizeof(writer),image.text_offset+duplicate_offset)==sizeof(writer));
    assert(!aoe_discover_ring_pointer(fd,&image,size));
    close(fd);
    puts("PASS: ring signature, cross-references, chunk boundary, and conservative refusals");
}
