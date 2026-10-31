/* Exercise reserved buffer creation on the selected D3D12 renderer. */
#define COBJMACROS
#include "../x64-fidelity/fid.h"
#include <d3d12.h>

int main(void)
{
    ID3D12Device *device = NULL;
    ID3D12Resource *resource = NULL;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    D3D12_RESOURCE_DESC desc = {0};
    HRESULT (WINAPI *create_device)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    HRESULT hr;
    HMODULE module = LoadLibraryA("d3d12.dll");
    fid_name = "reserved";
    fid_environment();
    create_device = module ? (void *)GetProcAddress(module, "D3D12CreateDevice") : NULL;
    fid_check(create_device != NULL, "api.d3d12", "error=%lu", GetLastError());
    if (!create_device) return fid_summary();
    hr = create_device(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&device);
    fid_check(SUCCEEDED(hr), "device.create", "hr=%#lx", (unsigned long)hr);
    if (FAILED(hr)) return fid_summary();
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    fid_info("device.tiled-tier", "%u (hr=%#lx)", options.TiledResourcesTier, (unsigned long)hr);
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 16 * 1024 * 1024;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateReservedResource(device, &desc, D3D12_RESOURCE_STATE_COMMON,
                                            NULL, &IID_ID3D12Resource, (void **)&resource);
    fid_check(SUCCEEDED(hr), "buffer.create-reserved", "hr=%#lx", (unsigned long)hr);
    if (SUCCEEDED(hr))
    {
        D3D12_GPU_VIRTUAL_ADDRESS address = ID3D12Resource_GetGPUVirtualAddress(resource);
        fid_check(address != 0, "buffer.gpu-address", "zero GPU address");
        ID3D12Resource_Release(resource);
    }
    ID3D12Device_Release(device);
    FreeLibrary(module);
    return fid_summary();
}
