// Sparse residency texture sampling test for KosmicKrisp. Each mip level holds one solid colour of its
// own. A compute shader samples the texture through image views that start at mip 0..6, as a game that
// streams textures does (view of the first resident mip), and the colours it gets are compared with the
// level each sample should have hit. Two modes: fully bound, and "streaming" (only the levels the view
// covers are bound when it is sampled). Build: glslangValidator -V samp.comp -o samp.spv; xxd -i samp.spv
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "samp_spv.h"
#define CHK(x) do{VkResult _r=(x); if(_r){printf("  vulkan error %d at line %d\n",_r,__LINE__); exit(1);}}while(0)

static VkDevice dev; static VkQueue queue; static VkCommandBuffer cb; static VkPhysicalDevice pd;
static VkBuffer buf, outbuf; static unsigned char *map; static float *out;
static VkPipeline pipe; static VkPipelineLayout pl; static VkDescriptorSet ds; static VkSampler samp; static float BIAS;

struct fmt { VkFormat f; const char *name; int kind; uint32_t bw, bytes; }; // kind 0 RGBA8, 1 BC1, 2 BC4
static const struct fmt fmts[] = { { VK_FORMAT_R8G8B8A8_UNORM, "RGBA8", 0, 1, 4 }, { VK_FORMAT_BC1_RGB_UNORM_BLOCK, "BC1", 1, 4, 8 },
                                   { VK_FORMAT_BC4_UNORM_BLOCK, "BC4", 2, 4, 8 }, { VK_FORMAT_BC1_RGB_SRGB_BLOCK, "BC1 sRGB", 3, 4, 8 } };
static uint32_t mini(uint32_t v, uint32_t l) { v >>= l; return v ? v : 1; }
static float s2l(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
// The colour level l is filled with, as the shader should see it, and one texel block of it.
static void colour(const struct fmt *f, uint32_t l, float rgba[4], unsigned char block[16])
{
    memset(block, 0, 16);
    if (f->kind == 0) { block[0] = 20 * l + 15; block[1] = 250 - 20 * l; block[2] = 5 + l; block[3] = 255; for (int i = 0; i < 4; i++) rgba[i] = block[i] / 255.0f; }
    else if (f->kind == 1 || f->kind == 3) {
        uint32_t r = 2 * l + 3, g = 60 - 4 * l, b = l + 1; uint16_t c = (uint16_t)(r << 11 | g << 5 | b);
        block[0] = c & 255; block[1] = c >> 8; block[2] = block[0]; block[3] = block[1];
        rgba[0] = ((r << 3) | (r >> 2)) / 255.0f; rgba[1] = ((g << 2) | (g >> 4)) / 255.0f; rgba[2] = ((b << 3) | (b >> 2)) / 255.0f; rgba[3] = 1;
        if (f->kind == 3) for (int i = 0; i < 3; i++) rgba[i] = s2l(rgba[i]);
    } else { block[0] = block[1] = 20 * l + 15; rgba[0] = block[0] / 255.0f; rgba[1] = rgba[2] = 0; rgba[3] = 1; }
}
static void run(void) {
    CHK(vkEndCommandBuffer(cb)); VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    CHK(vkQueueSubmit(queue, 1, &si, 0)); CHK(vkQueueWaitIdle(queue));
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; CHK(vkBeginCommandBuffer(cb, &bi));
}
static void upload(const struct fmt *f, VkImage img, uint32_t l, uint32_t size)
{
    uint32_t w = mini(size, l); uint32_t nb = ((w + f->bw - 1) / f->bw); float c[4]; unsigned char block[16]; colour(f, l, c, block);
    for (uint32_t i = 0; i < nb * nb; i++) memcpy(map + (size_t)i * f->bytes, block, f->bytes);
    VkBufferImageCopy r = { 0 }; r.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1 }; r.imageExtent = (VkExtent3D){ w, w, 1 };
    vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_GENERAL, 1, &r); run();
}
// Samples through a view of levels [base, levels) and compares. Returns the number of wrong samples.
static int sample_view(const struct fmt *f, VkImage img, uint32_t base, uint32_t levels, const char *mode)
{
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO }; vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = f->f;
    vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, base, levels - base, 0, 1 };
    VkImageView view; CHK(vkCreateImageView(dev, &vi, 0, &view));
    VkDescriptorImageInfo di = { samp, view, VK_IMAGE_LAYOUT_GENERAL }; VkDescriptorBufferInfo dbi = { outbuf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET } };
    w[0].dstSet = ds; w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &di;
    w[1].dstSet = ds; w[1].dstBinding = 1; w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev, 2, w, 0, 0);
    for (int i = 0; i < 32 * 4; i++) out[i] = -7;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, 0);
    vkCmdDispatch(cb, 1, 1, 1); run();
    int bad = 0; char line[600] = ""; uint32_t n = levels - base;
    for (int k = 0; k < 20; k++) {
        if (k == 16) k = 17; // v[16] is the size query
        // k < 8: textureLod(k); 8..15: texelFetch(k - 8); 17: implicit LOD (0 in compute); 18, 19: textureLod(-2), (-0.5).
        // The sampler's LOD bias adds to every LOD but the fetches', and the sum clamps to the view's levels: a
        // negative LOD must give the view's first mip, not reach the image's levels below it.
        uint32_t lod = k < 8 ? k : k < 16 ? k - 8 : 0; if (k >= 8 && k < 16 && lod >= n) continue;
        if (k < 8 || k >= 17) { float fl = (k < 8 ? (float)k : k == 18 ? -2.0f : k == 19 ? -0.5f : 0.0f) + BIAS; lod = fl <= 0.0f ? 0u : (uint32_t)(fl + 0.5f); }
        uint32_t l = base + (lod < n ? lod : n - 1); float want[4]; unsigned char b[16]; colour(f, l, want, b); const float *g = out + 4 * k; int ok = 1;
        for (int c = 0; c < 4; c++) if (fabsf(g[c] - want[c]) > 0.012f) ok = 0;
        if (!ok) { bad++; size_t L = strlen(line); if (L < 480) snprintf(line + L, sizeof(line) - L, " %s%u->(%.2f %.2f %.2f %.2f) want level %u (%.2f %.2f %.2f)", k < 8 ? "lod" : k < 16 ? "fetch" : k == 17 ? "auto" : k == 18 ? "lod-2:" : "lod-0.5:", lod, g[0], g[1], g[2], g[3], l, want[0], want[1], want[2]); }
    }
    int qn = (int)out[64], qw = (int)out[65];
    if (qn != (int)n) { bad++; size_t L = strlen(line); snprintf(line + L, sizeof(line) - L, " [levels query %d, want %u; size %d]", qn, n, qw); }
    if (bad) printf("  FAIL %-9s view from mip %u:%s\n", mode, base, line);
    vkDestroyImageView(dev, view, 0); return bad;
}
static int test(const struct fmt *f, uint32_t size)
{
    int fails = 0;
    for (int streaming = 0; streaming < 2; streaming++) {
        VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO }; ii.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = f->f; ii.extent = (VkExtent3D){ size, size, 1 }; ii.arrayLayers = 1; ii.samples = 1;
        uint32_t levels = 1; while ((size >> levels) > 0) levels++; ii.mipLevels = levels; ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkImage img; if (vkCreateImage(dev, &ii, 0, &img)) { printf("%-9s image creation failed\n", f->name); return 0; }
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr); uint32_t rc = 1; VkSparseImageMemoryRequirements sr; vkGetImageSparseMemoryRequirements(dev, img, &rc, &sr);
        VkExtent3D g = sr.formatProperties.imageGranularity; uint32_t tail = sr.imageMipTailFirstLod < levels ? sr.imageMipTailFirstLod : levels;
        VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp); VkMemoryAllocateInfo mi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.allocationSize = mr.size + 16 * mr.alignment;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if (mr.memoryTypeBits & (1u << i)) { mi.memoryTypeIndex = i; break; }
        VkDeviceMemory mem; CHK(vkAllocateMemory(dev, &mi, 0, &mem)); VkDeviceSize off = 0; int before = fails;
        VkSparseMemoryBind ob = { sr.imageMipTailOffset, sr.imageMipTailSize, mem, off, 0 }; off += sr.imageMipTailSize;
        VkSparseImageOpaqueMemoryBindInfo oi = { img, 1, &ob }; VkBindSparseInfo bs = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO }; bs.imageOpaqueBindCount = 1; bs.pImageOpaqueBinds = &oi;
        CHK(vkQueueBindSparse(queue, 1, &bs, 0)); CHK(vkQueueWaitIdle(queue));
        for (uint32_t l = tail; l < levels; l++) upload(f, img, l, size);
        if (streaming) fails += sample_view(f, img, tail, levels, "streaming");
        for (uint32_t s = 0; s < tail; s++) {
            uint32_t l = tail - 1 - s, w = mini(size, l), tx = (w + g.width - 1) / g.width, ty = (w + g.height - 1) / g.height; VkSparseImageMemoryBind *b = calloc(tx * ty, sizeof(*b));
            for (uint32_t y = 0; y < ty; y++) for (uint32_t x = 0; x < tx; x++) { VkSparseImageMemoryBind *t = &b[y * tx + x]; t->subresource = (VkImageSubresource){ VK_IMAGE_ASPECT_COLOR_BIT, l, 0 };
                t->offset = (VkOffset3D){ x * g.width, y * g.height, 0 }; t->extent = (VkExtent3D){ (x + 1) * g.width > w ? w - x * g.width : g.width, (y + 1) * g.height > w ? w - y * g.height : g.height, 1 };
                t->memory = mem; t->memoryOffset = off; off += mr.alignment; }
            VkSparseImageMemoryBindInfo bi = { img, tx * ty, b }; VkBindSparseInfo b2 = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO }; b2.imageBindCount = 1; b2.pImageBinds = &bi;
            CHK(vkQueueBindSparse(queue, 1, &b2, 0)); CHK(vkQueueWaitIdle(queue)); free(b); upload(f, img, l, size);
            // streaming: the view of the first resident mip, sampled as soon as that mip is in (and the coarser views again)
            if (streaming) for (uint32_t v = l; v <= tail && v < levels; v++) fails += sample_view(f, img, v, levels, "streaming");
        }
        if (!streaming) for (uint32_t v = 0; v < 7 && v < levels; v++) fails += sample_view(f, img, v, levels, "all bound");
        printf("%-9s %ux%u tile %ux%u tail %u, sampler bias %g, %s: %s\n", f->name, size, size, g.width, g.height, tail, BIAS, streaming ? "streaming" : "all bound", fails == before ? "ok" : "FAILED");
        vkDestroyImage(dev, img, 0); vkFreeMemory(dev, mem, 0);
    }
    return fails;
}
int main(int argc, char **argv)
{
    uint32_t size = argc > 1 ? (uint32_t)atoi(argv[1]) : 2048;
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO }; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; ici.pApplicationInfo = &ai; const char *ext[] = { "VK_KHR_portability_enumeration" };
    ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = ext; ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    VkInstance inst; CHK(vkCreateInstance(&ici, 0, &inst)); uint32_t n = 1; vkEnumeratePhysicalDevices(inst, &n, &pd);
    VkPhysicalDeviceFeatures have; vkGetPhysicalDeviceFeatures(pd, &have); VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
    printf("%s, sparseBinding %d, sparseResidencyImage2D %d\n", props.deviceName, have.sparseBinding, have.sparseResidencyImage2D);
    if (!have.sparseBinding || !have.sparseResidencyImage2D) return 2;
    uint32_t qn = 8; VkQueueFamilyProperties qf[8]; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf); uint32_t fam = 0;
    for (uint32_t i = 0; i < qn; i++) if (qf[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) { fam = i; break; }
    float q = 1; VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO }; qi.queueFamilyIndex = fam; qi.queueCount = 1; qi.pQueuePriorities = &q;
    VkPhysicalDeviceFeatures want = { 0 }; want.sparseBinding = 1; want.sparseResidencyImage2D = 1; want.textureCompressionBC = have.textureCompressionBC;
    VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO }; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &want;
    CHK(vkCreateDevice(pd, &di, 0, &dev)); vkGetDeviceQueue(dev, fam, 0, &queue);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    VkBuffer *bs[2] = { &buf, &outbuf }; VkDeviceSize sz[2] = { (VkDeviceSize)size * size * 4, 32 * 16 }; void **maps[2] = { (void **)&map, (void **)&out };
    for (int i = 0; i < 2; i++) {
        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; bci.size = sz[i]; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        CHK(vkCreateBuffer(dev, &bci, 0, bs[i])); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, *bs[i], &mr); VkMemoryAllocateInfo mi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.allocationSize = mr.size;
        for (uint32_t t = 0; t < mp.memoryTypeCount; t++) if ((mr.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { mi.memoryTypeIndex = t; break; }
        VkDeviceMemory m; CHK(vkAllocateMemory(dev, &mi, 0, &m)); CHK(vkBindBufferMemory(dev, *bs[i], m, 0)); CHK(vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, maps[i]));
    }
    VkSamplerCreateInfo sci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO }; sci.magFilter = sci.minFilter = VK_FILTER_NEAREST; sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; sci.maxLod = 1000; BIAS = argc > 2 ? (float)atof(argv[2]) : 0.0f; sci.mipLodBias = BIAS; CHK(vkCreateSampler(dev, &sci, 0, &samp));
    VkDescriptorSetLayoutBinding lb[2] = { { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT }, { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT } };
    VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO }; li.bindingCount = 2; li.pBindings = lb; VkDescriptorSetLayout dl; CHK(vkCreateDescriptorSetLayout(dev, &li, 0, &dl));
    VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO }; pli.setLayoutCount = 1; pli.pSetLayouts = &dl; CHK(vkCreatePipelineLayout(dev, &pli, 0, &pl));
    VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO }; smi.codeSize = samp_spv_len; smi.pCode = (const uint32_t *)samp_spv; VkShaderModule sm; CHK(vkCreateShaderModule(dev, &smi, 0, &sm));
    VkComputePipelineCreateInfo cpi = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO }; cpi.stage = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main", 0 }; cpi.layout = pl;
    CHK(vkCreateComputePipelines(dev, 0, 1, &cpi, 0, &pipe));
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } };
    VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO }; dpi.maxSets = 1; dpi.poolSizeCount = 2; dpi.pPoolSizes = ps; VkDescriptorPool dp; CHK(vkCreateDescriptorPool(dev, &dpi, 0, &dp));
    VkDescriptorSetAllocateInfo dai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO }; dai.descriptorPool = dp; dai.descriptorSetCount = 1; dai.pSetLayouts = &dl; CHK(vkAllocateDescriptorSets(dev, &dai, &ds));
    VkCommandPoolCreateInfo cpo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO }; cpo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpo.queueFamilyIndex = fam; VkCommandPool cp; CHK(vkCreateCommandPool(dev, &cpo, 0, &cp));
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO }; cai.commandPool = cp; cai.commandBufferCount = 1; CHK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; CHK(vkBeginCommandBuffer(cb, &bi));
    int fails = 0; for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) fails += test(&fmts[i], size);
    printf("%s (%d wrong samples)\n", fails ? "FAILURES" : "all passed", fails); return fails ? 1 : 0;
}
