#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CK(x) do{ VkResult r_=(x); if(r_){ printf("FAIL %s = %d\n", #x, r_); exit(1);} }while(0)
static uint32_t memtype(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags f){ VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp); for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((bits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&f)==f) return i; exit(2); }
static VkBuffer mkbuf(VkDevice d, VkPhysicalDevice pd, VkDeviceSize sz, VkBufferUsageFlags u, VkDeviceMemory *m){ VkBufferCreateInfo bi={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,0,0,sz,u}; VkBuffer b; CK(vkCreateBuffer(d,&bi,0,&b)); VkMemoryRequirements r; vkGetBufferMemoryRequirements(d,b,&r); VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,r.size,memtype(pd,r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)}; CK(vkAllocateMemory(d,&ai,0,m)); CK(vkBindBufferMemory(d,b,*m,0)); return b; }
int main(void){
  VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"robust",1,0,0,VK_API_VERSION_1_3};
  const char *iext[]={"VK_KHR_portability_enumeration"};
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,&app,0,0,1,iext};
  VkInstance inst; CK(vkCreateInstance(&ici,0,&inst)); uint32_t n=1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst,&n,&pd);
  VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(pd,&pp); printf("device: %s\n", pp.deviceName);
  VkPhysicalDeviceRobustness2FeaturesEXT rb={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT}; rb.robustBufferAccess2=VK_TRUE;
  VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&rb}; f2.features.robustBufferAccess=VK_TRUE;
  float pri=1; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,0,0,0,1,&pri};
  const char *dext[]={"VK_EXT_robustness2"};
  VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&f2,0,1,&qi,0,0,1,dext,0};
  VkDevice d; CK(vkCreateDevice(pd,&dci,0,&d)); VkQueue q; vkGetDeviceQueue(d,0,0,&q);
  const uint32_t N=1u<<22; VkDeviceMemory m0,m1,m2;
  VkBuffer bs=mkbuf(d,pd,N*4ull,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&m0), bd=mkbuf(d,pd,N*4ull,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&m1), bu=mkbuf(d,pd,4096,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&m2);
  void *p; vkMapMemory(d,m0,0,VK_WHOLE_SIZE,0,&p); for(uint32_t i=0;i<N;i++) ((uint32_t*)p)[i]=i*7u; vkMapMemory(d,m2,0,VK_WHOLE_SIZE,0,&p); memset(p,3,4096);
  FILE*fs=fopen("storage-robustness.spv","rb"); fseek(fs,0,2); long sz=ftell(fs); rewind(fs); uint32_t*code=malloc(sz); fread(code,1,sz,fs);
  VkShaderModuleCreateInfo smi={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,0,0,(size_t)sz,code}; VkShaderModule sm; CK(vkCreateShaderModule(d,&smi,0,&sm));
  VkDescriptorSetLayoutBinding bnd[3]={{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT},{2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT}};
  VkDescriptorSetLayoutCreateInfo dl={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,0,0,3,bnd}; VkDescriptorSetLayout dsl; CK(vkCreateDescriptorSetLayout(d,&dl,0,&dsl));
  VkPushConstantRange pcr={VK_SHADER_STAGE_COMPUTE_BIT,0,8}; VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,0,0,1,&dsl,1,&pcr}; VkPipelineLayout pl; CK(vkCreatePipelineLayout(d,&pli,0,&pl));
  VkComputePipelineCreateInfo cpi={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,0,0,{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,0,0,VK_SHADER_STAGE_COMPUTE_BIT,sm,"main"},pl}; VkPipeline pipe; CK(vkCreateComputePipelines(d,0,1,&cpi,0,&pipe));
  VkDescriptorPoolSize ps[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}}; VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,0,0,1,2,ps}; VkDescriptorPool dp; CK(vkCreateDescriptorPool(d,&dpi,0,&dp));
  VkDescriptorSetAllocateInfo dsai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,0,dp,1,&dsl}; VkDescriptorSet ds; CK(vkAllocateDescriptorSets(d,&dsai,&ds));
  VkDescriptorBufferInfo bi[3]={{bs,0,VK_WHOLE_SIZE},{bd,0,VK_WHOLE_SIZE},{bu,0,4096}}; VkWriteDescriptorSet w[3]; for(int i=0;i<3;i++){ w[i]=(VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,0,ds,(uint32_t)i,0,1,i<2?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&bi[i],0}; } vkUpdateDescriptorSets(d,3,w,0,0);
  VkQueryPoolCreateInfo qpi={VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,0,0,VK_QUERY_TYPE_TIMESTAMP,2}; VkQueryPool qp; CK(vkCreateQueryPool(d,&qpi,0,&qp));
  VkCommandPoolCreateInfo cpo={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,0,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,0}; VkCommandPool cp; CK(vkCreateCommandPool(d,&cpo,0,&cp));
  VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,0,cp,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1}; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(d,&cai,&cb));
  double best=1e9;
  for(int rep=0;rep<8;rep++){
    VkCommandBufferBeginInfo cbi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CK(vkBeginCommandBuffer(cb,&cbi)); vkCmdResetQueryPool(cb,qp,0,2);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe); vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,0,0);
    uint32_t pcv[2]={N,N-1}; vkCmdPushConstants(cb,pl,VK_SHADER_STAGE_COMPUTE_BIT,0,8,pcv);
    vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,qp,0);
    for(int k=0;k<20;k++) vkCmdDispatch(cb,N/256,1,1);
    vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,qp,1); CK(vkEndCommandBuffer(cb));
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,0,0,0,0,1,&cb}; CK(vkQueueSubmit(q,1,&si,0)); CK(vkQueueWaitIdle(q));
    uint64_t ts[2]; CK(vkGetQueryPoolResults(d,qp,0,2,sizeof ts,ts,8,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));
    double ms=(ts[1]-ts[0])*pp.limits.timestampPeriod/1e6; if(rep>0 && ms<best) best=ms; }
  printf("20 dispatches x 4M threads x 64 scattered SSBO+UBO reads: best %.2f ms\n", best);
  return 0; }
