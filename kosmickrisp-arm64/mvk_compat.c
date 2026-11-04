/* MoltenVK's vkGetMTLDeviceMVK for the KosmicKrisp line, whose libMoltenVK.dylib is the Khronos
 * loader. GStreamer's applemedia plugin (VideoToolbox decoding) imports it for its Vulkan output
 * and fails to load without it, so no H.264 video plays. Winegstreamer asks for system memory, so
 * the call is not expected to run; when it does it answers with the system's Metal device. */
#include <stddef.h>

typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
extern void *MTLCreateSystemDefaultDevice(void);

__attribute__((visibility("default"))) void vkGetMTLDeviceMVK(VkPhysicalDevice physicalDevice, void **pMTLDevice)
{
    (void)physicalDevice;
    if (pMTLDevice) *pMTLDevice = MTLCreateSystemDefaultDevice();
}
