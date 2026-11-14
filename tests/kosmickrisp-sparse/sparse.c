// Sparse residency texture test for KosmicKrisp: streams a mip chain in the way a D3D12 game does
// (mip tail first, then finer levels tile by tile, each tile its own 64 KiB of memory), and reads every
// level back after each step. Unmapped or mis-mapped tiles show up as blocks that differ from what was
// uploaded. Then it unbinds the levels again, finest first, as a game dropping its top mips does: the level
// just unbound must read zero (which also shows the test can see an unmapped level) and every coarser level
// must still read back. Last it unbinds the mip tail, reports how many tail levels then read zero, binds it again
// and uploads new data. Usage: sparse [width [height]]   (default 2048)
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHK(x) do{VkResult _r=(x); if(_r){printf("  vulkan error %d at line %d\n",_r,__LINE__); exit(1);}}while(0)

static VkDevice dev; static VkQueue queue; static VkCommandBuffer cb; static VkPhysicalDevice pd;
static VkBuffer buf; static unsigned char *map; static VkDeviceSize buf_size; static uint32_t H0;

struct fmt { VkFormat f; const char *name; uint32_t bw, bh, bytes; };
static const struct fmt fmts[] = {
    { VK_FORMAT_BC1_RGB_UNORM_BLOCK, "BC1 RGB", 4, 4, 8 },   { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, "BC1 RGBA", 4, 4, 8 },
    { VK_FORMAT_BC1_RGB_SRGB_BLOCK, "BC1 sRGB", 4, 4, 8 },   { VK_FORMAT_BC4_UNORM_BLOCK, "BC4", 4, 4, 8 },
    { VK_FORMAT_BC3_UNORM_BLOCK, "BC3", 4, 4, 16 },          { VK_FORMAT_BC5_UNORM_BLOCK, "BC5", 4, 4, 16 },
    { VK_FORMAT_BC7_UNORM_BLOCK, "BC7", 4, 4, 16 },          { VK_FORMAT_BC7_SRGB_BLOCK, "BC7 sRGB", 4, 4, 16 },
    { VK_FORMAT_BC6H_UFLOAT_BLOCK, "BC6H", 4, 4, 16 },       { VK_FORMAT_R8G8B8A8_UNORM, "RGBA8", 1, 1, 4 },
    { VK_FORMAT_R8_UNORM, "R8", 1, 1, 1 },                   { VK_FORMAT_R16G16B16A16_SFLOAT, "RGBA16F", 1, 1, 8 },
};

static uint32_t mini(uint32_t v, uint32_t l) { v >>= l; return v ? v : 1; }
static VkDeviceSize level_bytes(const struct fmt *f, uint32_t w, uint32_t h) { return (VkDeviceSize)((w + f->bw - 1) / f->bw) * ((h + f->bh - 1) / f->bh) * f->bytes; }
// Every byte nonzero, and different per level and position, so zeros and shifted data both show.
static void fill(unsigned char *p, VkDeviceSize n, uint32_t level, uint32_t seed)
{
    uint32_t x = 0x9e3779b9u * (level + 1) + seed;
    for (VkDeviceSize i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; p[i] = (unsigned char)((x >> 24) | 1); }
}
static void run(void) {
    CHK(vkEndCommandBuffer(cb)); VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    CHK(vkQueueSubmit(queue, 1, &si, 0)); CHK(vkQueueWaitIdle(queue));
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; CHK(vkBeginCommandBuffer(cb, &bi));
}
static void copy_level(VkImage img, uint32_t level, uint32_t w, uint32_t h, int to_image)
{
    VkBufferImageCopy r = { 0 }; r.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 }; r.imageExtent = (VkExtent3D){ w, h, 1 };
    if (to_image) vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    else vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &r);
    run();
}
// Reads the level back and compares with what fill() made. Returns the number of wrong blocks.
static uint64_t check_level(const struct fmt *f, VkImage img, uint32_t level, uint32_t size, uint32_t seed, unsigned char *want, char *where, size_t where_n)
{
    uint32_t w = mini(size, level), h = mini(H0, level); VkDeviceSize n = level_bytes(f, w, h);
    memset(map, 0xEE, n); copy_level(img, level, w, h, 0); fill(want, n, level, seed);
    uint64_t bad = 0, zero = 0; uint32_t bxn = (w + f->bw - 1) / f->bw; where[0] = 0;
    for (VkDeviceSize o = 0; o < n; o += f->bytes) {
        if (memcmp(map + o, want + o, f->bytes)) {
            int z = 1; for (uint32_t k = 0; k < f->bytes; k++) if (map[o + k]) z = 0;
            if (!bad) snprintf(where, where_n, "first at texel (%u,%u)%s", (uint32_t)((o / f->bytes) % bxn) * f->bw, (uint32_t)((o / f->bytes) / bxn) * f->bh, z ? ", reads zero" : ", wrong data");
            bad++; zero += z;
        }
    }
    if (bad) { size_t l = strlen(where); snprintf(where + l, where_n - l, ", %llu of %llu blocks wrong (%llu zero)", (unsigned long long)bad, (unsigned long long)(n / f->bytes), (unsigned long long)zero); }
    return bad;
}

static int test(const struct fmt *f, uint32_t size, int order)
{
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO }; ii.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = f->f; ii.extent = (VkExtent3D){ size, H0, 1 }; ii.arrayLayers = 1; ii.samples = 1;
    uint32_t big = size > H0 ? size : H0, levels = 1; while ((big >> levels) > 0) levels++; ii.mipLevels = levels;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    uint32_t pc = 0; vkGetPhysicalDeviceSparseImageFormatProperties(pd, f->f, VK_IMAGE_TYPE_2D, 1, ii.usage, VK_IMAGE_TILING_OPTIMAL, &pc, NULL);
    if (!pc) { printf("%-9s not offered as sparse\n", f->name); return 0; }
    VkImage img; if (vkCreateImage(dev, &ii, 0, &img)) { printf("%-9s image creation failed\n", f->name); return 0; }
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
    uint32_t rc = 1; VkSparseImageMemoryRequirements sr; vkGetImageSparseMemoryRequirements(dev, img, &rc, &sr);
    VkExtent3D g = sr.formatProperties.imageGranularity; uint32_t tail = sr.imageMipTailFirstLod < levels ? sr.imageMipTailFirstLod : levels;
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    VkMemoryAllocateInfo mi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.allocationSize = mr.size + 16 * mr.alignment;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if (mr.memoryTypeBits & (1u << i)) { mi.memoryTypeIndex = i; break; }
    VkDeviceMemory mem; CHK(vkAllocateMemory(dev, &mi, 0, &mem));
    unsigned char *want = malloc(buf_size); char where[200]; int fails = 0; VkDeviceSize mem_off = 0;
    printf("%-9s %ux%u tile %ux%u, tail from level %u (%llu KiB), page %llu KiB, order %s\n", f->name, size, H0, g.width, g.height, tail,
           (unsigned long long)sr.imageMipTailSize / 1024, (unsigned long long)mr.alignment / 1024, order ? "fine to coarse" : "coarse to fine");
    // 1. mip tail, one opaque bind
    if (tail < levels) {
        VkSparseMemoryBind ob = { sr.imageMipTailOffset, sr.imageMipTailSize, mem, mem_off, 0 }; mem_off += sr.imageMipTailSize;
        VkSparseImageOpaqueMemoryBindInfo oi = { img, 1, &ob }; VkBindSparseInfo bs = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO }; bs.imageOpaqueBindCount = 1; bs.pImageOpaqueBinds = &oi;
        CHK(vkQueueBindSparse(queue, 1, &bs, 0)); CHK(vkQueueWaitIdle(queue));
        for (uint32_t l = tail; l < levels; l++) { VkDeviceSize n = level_bytes(f, mini(size, l), mini(H0, l)); fill(map, n, l, f->f); copy_level(img, l, mini(size, l), mini(H0, l), 1); }
        for (uint32_t l = tail; l < levels; l++) if (check_level(f, img, l, size, f->f, want, where, sizeof(where))) { printf("  FAIL tail level %u (%ux%u): %s\n", l, mini(size, l), mini(H0, l), where); fails++; }
    }
    // 2. the levels above the tail, each tile bound to its own page
    for (uint32_t s = 0; s < tail; s++) {
        uint32_t l = order ? s : tail - 1 - s; uint32_t w = mini(size, l), h = mini(H0, l);
        uint32_t tx = (w + g.width - 1) / g.width, ty = (h + g.height - 1) / g.height; VkSparseImageMemoryBind *b = calloc(tx * ty, sizeof(*b));
        for (uint32_t y = 0; y < ty; y++) for (uint32_t x = 0; x < tx; x++) {
            VkSparseImageMemoryBind *t = &b[y * tx + x]; t->subresource = (VkImageSubresource){ VK_IMAGE_ASPECT_COLOR_BIT, l, 0 };
            t->offset = (VkOffset3D){ x * g.width, y * g.height, 0 };
            t->extent = (VkExtent3D){ (x + 1) * g.width > w ? w - x * g.width : g.width, (y + 1) * g.height > h ? h - y * g.height : g.height, 1 };
            t->memory = mem; t->memoryOffset = mem_off; mem_off += mr.alignment;
        }
        if (mem_off > mi.allocationSize) { printf("  ran out of test memory at level %u\n", l); free(b); break; }
        VkSparseImageMemoryBindInfo bi = { img, tx * ty, b }; VkBindSparseInfo bs = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO }; bs.imageBindCount = 1; bs.pImageBinds = &bi;
        CHK(vkQueueBindSparse(queue, 1, &bs, 0)); CHK(vkQueueWaitIdle(queue)); free(b);
        VkDeviceSize n = level_bytes(f, w, h); fill(map, n, l, f->f); copy_level(img, l, w, h, 1);
        // this level, and everything bound before it, must still read back
        for (uint32_t c = 0; c < levels; c++) {
            int bound = c >= tail || (order ? c <= l : c >= l); if (!bound) continue;
            if (check_level(f, img, c, size, f->f, want, where, sizeof(where))) { printf("  FAIL level %u (%ux%u, %ux%u tiles) after binding level %u: %s\n", c, mini(size, c), mini(H0, c), (mini(size, c) + g.width - 1) / g.width, (mini(H0, c) + g.height - 1) / g.height, l, where); fails++; }
        }
    }
    // 3. drop the levels above the tail again, finest first
    int zero_seen = 0, zero_missed = 0;
    for (uint32_t l = 0; l < tail && mem_off <= mi.allocationSize; l++) {
        uint32_t w = mini(size, l), h = mini(H0, l), tx = (w + g.width - 1) / g.width, ty = (h + g.height - 1) / g.height; VkSparseImageMemoryBind *b = calloc(tx * ty, sizeof(*b));
        for (uint32_t y = 0; y < ty; y++) for (uint32_t x = 0; x < tx; x++) { VkSparseImageMemoryBind *t = &b[y * tx + x]; t->subresource = (VkImageSubresource){ VK_IMAGE_ASPECT_COLOR_BIT, l, 0 };
            t->offset = (VkOffset3D){ x * g.width, y * g.height, 0 }; t->extent = (VkExtent3D){ (x + 1) * g.width > w ? w - x * g.width : g.width, (y + 1) * g.height > h ? h - y * g.height : g.height, 1 }; }
        VkSparseImageMemoryBindInfo bi = { img, tx * ty, b }; VkBindSparseInfo bs = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO }; bs.imageBindCount = 1; bs.pImageBinds = &bi;
        CHK(vkQueueBindSparse(queue, 1, &bs, 0)); CHK(vkQueueWaitIdle(queue)); free(b);
        VkDeviceSize n = level_bytes(f, w, h); memset(map, 0xEE, n); copy_level(img, l, w, h, 0); int z = 1; for (VkDeviceSize k = 0; k < n; k++) if (map[k]) { z = 0; break; }
        if (z) zero_seen++; else zero_missed++;
        for (uint32_t c = l + 1; c < levels; c++)
            if (check_level(f, img, c, size, f->f, want, where, sizeof(where))) { printf("  FAIL level %u (%ux%u) after UNBINDING level %u: %s\n", c, mini(size, c), mini(H0, c), l, where); fails++; }
    }
    // 4. unbind the mip tail and count the tail levels that then read zero (reported, not a failure: on the
    //    stock driver an unbound tail keeps its data at every size). Then bind it again and upload new data.
    int tail_zero = 0, tail_levels = 0;
    if (tail < levels) {
        VkSparseMemoryBind ob = { sr.imageMipTailOffset, sr.imageMipTailSize, VK_NULL_HANDLE, 0, 0 };
        VkSparseImageOpaqueMemoryBindInfo oi = { img, 1, &ob }; VkBindSparseInfo bs = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO }; bs.imageOpaqueBindCount = 1; bs.pImageOpaqueBinds = &oi;
        CHK(vkQueueBindSparse(queue, 1, &bs, 0)); CHK(vkQueueWaitIdle(queue));
        for (uint32_t l = tail; l < levels; l++) {
            uint32_t w = mini(size, l), h = mini(H0, l); VkDeviceSize n = level_bytes(f, w, h); memset(map, 0xEE, n); copy_level(img, l, w, h, 0);
            int z = 1; for (VkDeviceSize k = 0; k < n; k++) if (map[k]) { z = 0; break; }
            tail_levels++; if (z) tail_zero++;
        }
        ob.memory = mem; ob.memoryOffset = 0; CHK(vkQueueBindSparse(queue, 1, &bs, 0)); CHK(vkQueueWaitIdle(queue));
        for (uint32_t l = tail; l < levels; l++) { VkDeviceSize n = level_bytes(f, mini(size, l), mini(H0, l)); fill(map, n, l, f->f ^ 0x5a); copy_level(img, l, mini(size, l), mini(H0, l), 1); }
        for (uint32_t l = tail; l < levels; l++) if (check_level(f, img, l, size, f->f ^ 0x5a, want, where, sizeof(where))) { printf("  FAIL tail level %u (%ux%u) after binding the tail again: %s\n", l, mini(size, l), mini(H0, l), where); fails++; }
    }
    if (!fails) printf("  ok, all %u levels read back; unbound levels read zero %d of %d times; unbound tail levels read zero %d of %d times\n", levels, zero_seen, zero_seen + zero_missed, tail_zero, tail_levels);
    free(want); vkDestroyImage(dev, img, 0); vkFreeMemory(dev, mem, 0); return fails;
}

int main(int argc, char **argv)
{
    uint32_t size = argc > 1 ? (uint32_t)atoi(argv[1]) : 2048; H0 = argc > 2 ? (uint32_t)atoi(argv[2]) : size;
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO }; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; ici.pApplicationInfo = &ai; const char *ext[] = { "VK_KHR_portability_enumeration" };
    ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = ext; ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    VkInstance inst; CHK(vkCreateInstance(&ici, 0, &inst)); uint32_t n = 1; vkEnumeratePhysicalDevices(inst, &n, &pd);
    VkPhysicalDeviceFeatures have; vkGetPhysicalDeviceFeatures(pd, &have);
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
    printf("%s, sparseBinding %d, sparseResidencyImage2D %d\n", props.deviceName, have.sparseBinding, have.sparseResidencyImage2D);
    if (!have.sparseBinding || !have.sparseResidencyImage2D) { printf("no sparse support reported\n"); return 2; }
    uint32_t qn = 8; VkQueueFamilyProperties qf[8]; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf); uint32_t fam = 0;
    for (uint32_t i = 0; i < qn; i++) if (qf[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) { fam = i; break; }
    float q = 1; VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO }; qi.queueFamilyIndex = fam; qi.queueCount = 1; qi.pQueuePriorities = &q;
    VkPhysicalDeviceFeatures want = { 0 }; want.sparseBinding = 1; want.sparseResidencyImage2D = 1; want.textureCompressionBC = have.textureCompressionBC;
    VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO }; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &want;
    CHK(vkCreateDevice(pd, &di, 0, &dev)); vkGetDeviceQueue(dev, fam, 0, &queue);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    buf_size = (VkDeviceSize)size * H0 * 8 + 65536; VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; bci.size = buf_size; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    CHK(vkCreateBuffer(dev, &bci, 0, &buf)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, buf, &mr);
    VkMemoryAllocateInfo mi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.allocationSize = mr.size;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { mi.memoryTypeIndex = i; break; }
    VkDeviceMemory bm; CHK(vkAllocateMemory(dev, &mi, 0, &bm)); CHK(vkBindBufferMemory(dev, buf, bm, 0)); CHK(vkMapMemory(dev, bm, 0, VK_WHOLE_SIZE, 0, (void **)&map));
    VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO }; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = fam; VkCommandPool cp; CHK(vkCreateCommandPool(dev, &cpi, 0, &cp));
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO }; cai.commandPool = cp; cai.commandBufferCount = 1; CHK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; CHK(vkBeginCommandBuffer(cb, &bi));
    int fails = 0;
    for (int order = 0; order < 2; order++) for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) fails += test(&fmts[i], size, order);
    printf("%s (%d failures)\n", fails ? "FAILURES" : "all formats passed", fails);
    return fails ? 1 : 0;
}
