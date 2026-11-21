#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CK(x) do{ VkResult r_=(x); if(r_){ printf("FAIL %s = %d\n", #x, r_); exit(1);} }while(0)
static VkPhysicalDevice pd; static VkDevice d; static VkQueue q; static VkCommandBuffer cb; static int fails;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags f){ VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp); for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((bits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&f)==f) return i; exit(2); }
typedef struct { VkImage img; VkImageView view; } Img;
static Img mkimg(VkSampleCountFlagBits s){ Img r; VkImageCreateInfo ic={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,0,0,VK_IMAGE_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM,{64,64,1},1,1,s,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,0,0,VK_IMAGE_LAYOUT_UNDEFINED};
  CK(vkCreateImage(d,&ic,0,&r.img)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(d,r.img,&mr); VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,mr.size,memtype(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)}; VkDeviceMemory m; CK(vkAllocateMemory(d,&ma,0,&m)); CK(vkBindImageMemory(d,r.img,m,0));
  VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,0,0,r.img,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM,{0},{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; CK(vkCreateImageView(d,&vi,0,&r.view)); return r; }
static void begin(void){ VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CK(vkResetCommandBuffer(cb,0)); CK(vkBeginCommandBuffer(cb,&bi)); }
static void submit(void){ CK(vkEndCommandBuffer(cb)); VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,0,0,0,0,1,&cb}; CK(vkQueueSubmit(q,1,&si,0)); CK(vkQueueWaitIdle(q)); }
static void barrier(void){ VkMemoryBarrier mb={VK_STRUCTURE_TYPE_MEMORY_BARRIER,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT}; vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,0,0,0); }
static void rendering(Img *a, VkAttachmentLoadOp load, VkAttachmentStoreOp store, float *clear, Img *resolve, VkRenderingFlags flags){
  VkRenderingAttachmentInfo ca={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,0,a->view,VK_IMAGE_LAYOUT_GENERAL,resolve?VK_RESOLVE_MODE_AVERAGE_BIT:VK_RESOLVE_MODE_NONE,resolve?resolve->view:0,VK_IMAGE_LAYOUT_GENERAL,load,store};
  if(clear) memcpy(ca.clearValue.color.float32,clear,16);
  VkRenderingInfo ri={VK_STRUCTURE_TYPE_RENDERING_INFO,0,flags,{{0,0},{64,64}},1,0,1,&ca}; vkCmdBeginRendering(cb,&ri); vkCmdEndRendering(cb); }
static uint32_t readpx(Img *a){ void*p; VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,0,0,64*64*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT}; VkBuffer b; CK(vkCreateBuffer(d,&bci,0,&b)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(d,b,&mr); VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,mr.size,memtype(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)}; VkDeviceMemory m; CK(vkAllocateMemory(d,&ma,0,&m)); CK(vkBindBufferMemory(d,b,m,0));
  begin(); barrier(); VkBufferImageCopy r={0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{64,64,1}}; vkCmdCopyImageToBuffer(cb,a->img,VK_IMAGE_LAYOUT_GENERAL,b,1,&r); submit(); CK(vkMapMemory(d,m,0,VK_WHOLE_SIZE,0,&p)); uint32_t v=((uint32_t*)p)[64*32+32]; vkUnmapMemory(d,m); return v; }
/* depth/stencil: a D32_SFLOAT_S8_UINT image read back one aspect at a time */
static Img mkds(void){ Img r; VkImageCreateInfo ic={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,0,0,VK_IMAGE_TYPE_2D,VK_FORMAT_D32_SFLOAT_S8_UINT,{64,64,1},1,1,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,0,0,VK_IMAGE_LAYOUT_UNDEFINED};
  CK(vkCreateImage(d,&ic,0,&r.img)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(d,r.img,&mr); VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,mr.size,memtype(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)}; VkDeviceMemory m; CK(vkAllocateMemory(d,&ma,0,&m)); CK(vkBindImageMemory(d,r.img,m,0));
  VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,0,0,r.img,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_D32_SFLOAT_S8_UINT,{0},{VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1}}; CK(vkCreateImageView(d,&vi,0,&r.view)); return r; }
static void rendering_ds(Img *c, VkAttachmentLoadOp cload, Img *ds, VkAttachmentLoadOp dsload, float depth, uint32_t stencil, VkQueryPool qp){
  VkRenderingAttachmentInfo ca={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,0,c->view,VK_IMAGE_LAYOUT_GENERAL,VK_RESOLVE_MODE_NONE,0,VK_IMAGE_LAYOUT_GENERAL,cload,VK_ATTACHMENT_STORE_OP_STORE};
  VkRenderingAttachmentInfo da={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,0,ds?ds->view:0,VK_IMAGE_LAYOUT_GENERAL,VK_RESOLVE_MODE_NONE,0,VK_IMAGE_LAYOUT_GENERAL,dsload,VK_ATTACHMENT_STORE_OP_STORE};
  da.clearValue.depthStencil.depth=depth; da.clearValue.depthStencil.stencil=stencil;
  VkRenderingInfo ri={VK_STRUCTURE_TYPE_RENDERING_INFO,0,0,{{0,0},{64,64}},1,0,1,&ca,ds?&da:0,ds?&da:0};
  vkCmdBeginRendering(cb,&ri); if(qp){ vkCmdBeginQuery(cb,qp,0,0); vkCmdEndQuery(cb,qp,0); } vkCmdEndRendering(cb); }
static uint32_t readds(Img *a, VkImageAspectFlags aspect){ void*p; VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,0,0,64*64*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT}; VkBuffer b; CK(vkCreateBuffer(d,&bci,0,&b)); VkMemoryRequirements mr; vkGetBufferMemoryRequirements(d,b,&mr); VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,mr.size,memtype(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)}; VkDeviceMemory m; CK(vkAllocateMemory(d,&ma,0,&m)); CK(vkBindBufferMemory(d,b,m,0));
  begin(); barrier(); VkBufferImageCopy r={0,0,0,{aspect,0,0,1},{0,0,0},{64,64,1}}; vkCmdCopyImageToBuffer(cb,a->img,VK_IMAGE_LAYOUT_GENERAL,b,1,&r); submit(); CK(vkMapMemory(d,m,0,VK_WHOLE_SIZE,0,&p));
  uint32_t v = aspect==VK_IMAGE_ASPECT_STENCIL_BIT ? ((uint8_t*)p)[64*32+32] : ((uint32_t*)p)[64*32+32]; vkUnmapMemory(d,m); return v; }
static void expect(const char *name, uint32_t got, uint32_t want){ printf("%-58s got %08x want %08x  %s\n", name, got, want, got==want?"OK":"FAIL"); if(got!=want) fails++; }
int main(void){
  VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"passes",1,0,0,VK_API_VERSION_1_3}; const char *iext[]={"VK_KHR_portability_enumeration"};
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,&app,0,0,1,iext}; VkInstance inst; CK(vkCreateInstance(&ici,0,&inst)); uint32_t n=1; vkEnumeratePhysicalDevices(inst,&n,&pd);
  VkPhysicalDeviceVulkan13Features v13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; v13.dynamicRendering=1; VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&v13};
  float pri=1; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,0,0,0,1,&pri}; VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&f2,0,1,&qi}; CK(vkCreateDevice(pd,&dci,0,&d)); vkGetDeviceQueue(d,0,0,&q);
  VkCommandPoolCreateInfo cpo={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,0,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,0}; VkCommandPool cp; CK(vkCreateCommandPool(d,&cpo,0,&cp)); VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,0,cp,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1}; CK(vkAllocateCommandBuffers(d,&cai,&cb));
  float red[4]={1,0,0,1}, green[4]={0,1,0,1}, blue[4]={0,0,1,1};
  Img a=mkimg(VK_SAMPLE_COUNT_1_BIT);
  begin(); { VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,0,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,0,a.img,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,0,0,0,1,&b); }
  rendering(&a,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,red,0,0); submit();
  expect("1 clear to red (empty rendering, CLEAR: must run)", readpx(&a), 0xff0000ff);
  begin(); rendering(&a,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,0,0,0); submit();
  expect("2 empty LOAD/STORE keeps red", readpx(&a), 0xff0000ff);
  begin(); rendering(&a,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,green,0,0); submit();
  expect("3 empty CLEAR to green in its own submit", readpx(&a), 0xff00ff00);
  begin(); rendering(&a,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,0,0,VK_RENDERING_SUSPENDING_BIT); rendering(&a,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,0,0,VK_RENDERING_RESUMING_BIT); submit();
  expect("4 suspend+resume empty pair keeps green", readpx(&a), 0xff00ff00);
  begin(); rendering(&a,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,0,0,0); barrier(); rendering(&a,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,blue,0,0); barrier(); rendering(&a,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,0,0,0); submit();
  expect("5 empty, CLEAR blue, empty in one command buffer -> blue", readpx(&a), 0xffff0000);
  Img ms=mkimg(VK_SAMPLE_COUNT_4_BIT), rs=mkimg(VK_SAMPLE_COUNT_1_BIT);
  begin(); { VkImageMemoryBarrier b[2]={{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,0,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,0,ms.img,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}},{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,0,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,0,rs.img,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}}; vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,0,0,0,2,b); }
  rendering(&rs,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,red,0,0); barrier(); rendering(&ms,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,green,0,0); submit();
  begin(); rendering(&ms,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,0,&rs,0); submit();
  expect("6 empty MSAA LOAD + resolve must resolve green into target", readpx(&rs), 0xff00ff00);
  /* depth and stencil: an empty rendering that clears them must run, one that only loads them must keep them */
  Img c2=mkimg(VK_SAMPLE_COUNT_1_BIT), ds=mkds();
  begin(); { VkImageMemoryBarrier b[2]={{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,0,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,0,c2.img,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}},{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,0,0,VK_ACCESS_MEMORY_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,0,ds.img,{VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1}}}; vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,0,0,0,2,b); }
  rendering(&c2,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,red,0,0); barrier();
  rendering_ds(&c2,VK_ATTACHMENT_LOAD_OP_LOAD,&ds,VK_ATTACHMENT_LOAD_OP_CLEAR,0.25f,7,0); submit();
  { float f=0.25f; uint32_t want; memcpy(&want,&f,4); expect("7 color LOAD, depth+stencil CLEAR: depth cleared", readds(&ds,VK_IMAGE_ASPECT_DEPTH_BIT), want); }
  expect("7 color LOAD, depth+stencil CLEAR: stencil cleared", readds(&ds,VK_IMAGE_ASPECT_STENCIL_BIT), 7);
  expect("7 color LOAD, depth+stencil CLEAR: color kept", readpx(&c2), 0xff0000ff);
  begin(); rendering_ds(&c2,VK_ATTACHMENT_LOAD_OP_LOAD,&ds,VK_ATTACHMENT_LOAD_OP_LOAD,0,0,0); submit();
  { float f=0.25f; uint32_t want; memcpy(&want,&f,4); expect("8 empty LOAD color+depth+stencil keeps depth", readds(&ds,VK_IMAGE_ASPECT_DEPTH_BIT), want); }
  expect("8 empty LOAD color+depth+stencil keeps stencil", readds(&ds,VK_IMAGE_ASPECT_STENCIL_BIT), 7);
  /* an occlusion query begun and ended inside an empty rendering: available, and 0 samples */
  { VkQueryPoolCreateInfo qci={VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,0,0,VK_QUERY_TYPE_OCCLUSION,1}; VkQueryPool qp; CK(vkCreateQueryPool(d,&qci,0,&qp));
    begin(); vkCmdResetQueryPool(cb,qp,0,1); rendering_ds(&c2,VK_ATTACHMENT_LOAD_OP_LOAD,&ds,VK_ATTACHMENT_LOAD_OP_LOAD,0,0,qp); submit();
    uint64_t res[2]={~0ull,0}; VkResult r=vkGetQueryPoolResults(d,qp,0,1,sizeof(res),res,sizeof(res),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    expect("9 occlusion query in an empty rendering: available", r==VK_SUCCESS && res[1], 1);
    expect("9 occlusion query in an empty rendering: 0 samples", (uint32_t)res[0], 0); }
  printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", fails); return fails; }
