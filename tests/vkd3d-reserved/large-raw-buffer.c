/* Check full-range reads, writes and atomics when a companion texel view is too big.
 * No reserved resources: this isolates descriptor creation from sparse binding. */
#define COBJMACROS
#include "../x64-fidelity/fid.h"
#include <d3d12.h>
#include <d3dcompiler.h>

#define RELEASE(p) do { if (p) IUnknown_Release((IUnknown *)(p)); } while (0)
#define CHECK_HR(id, call) do { hr = (call); \
    fid_check(SUCCEEDED(hr), id, "hr=%#lx", (unsigned long)hr); \
    if (FAILED(hr)) goto cleanup; } while (0)

static const UINT64 offsets[] = {0, (256ull << 20) - 4, 256ull << 20,
    512ull << 20, (1ull << 30) - 4, 1ull << 30, (2ull << 30) - 4};

static void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

int main(void)
{
    static const char shader[] =
        "ByteAddressBuffer raw : register(t0);\n"
        "StructuredBuffer<uint> structured : register(t1);\n"
        "RWByteAddressBuffer result : register(u0);\n"
        "static const uint offsets[7] = {0, 268435452, 268435456, 536870912,"
        "1073741820, 1073741824, 2147483644};\n"
        "[numthreads(1,1,1)] void main() {"
        " for (uint i = 0; i < 7; ++i) {"
        " result.Store(i * 8, raw.Load(offsets[i]));"
        " result.Store(i * 8 + 4, structured[offsets[i] / 4]); }}";
    static const char write_shader[] =
        "RWByteAddressBuffer target : register(u1);\n"
        "RWByteAddressBuffer result : register(u0);\n"
        "static const uint offsets[7] = {0, 268435452, 268435456, 536870912,"
        "1073741820, 1073741824, 2147483644};\n"
        "[numthreads(8,1,1)] void main(uint3 tid : SV_GroupThreadID) {"
        " for (uint i = 0; i < 7; ++i) {"
        " if (tid.x == 0) target.Store(offsets[i], 0x11220000u + i * 16);"
        " DeviceMemoryBarrierWithGroupSync();"
        " uint previous; target.InterlockedAdd(offsets[i], 1, previous);"
        " result.Store((i * 8 + tid.x) * 4, previous);"
        " DeviceMemoryBarrierWithGroupSync();"
        " if (tid.x == 0) result.Store((56 + i) * 4, target.Load(offsets[i]));"
        " GroupMemoryBarrierWithGroupSync(); }}";
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12Fence *fence = NULL;
    ID3D12Resource *big = NULL, *upload = NULL, *output = NULL, *readback = NULL;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12RootSignature *root = NULL;
    ID3D12PipelineState *pipeline = NULL, *write_pipeline = NULL;
    ID3DBlob *code = NULL, *write_code = NULL, *signature = NULL, *errors = NULL;
    D3D12_HEAP_PROPERTIES properties = {0};
    D3D12_RESOURCE_DESC desc = {0};
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {0};
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{0}};
    D3D12_ROOT_PARAMETER parameters[2] = {{0}};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {0};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {0};
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {0};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    UINT increment, i, *mapped;
    HRESULT hr;
    HANDLE event = NULL;
    DWORD wait_result;
    fid_name = "large-raw-buffer";
    fid_environment();

    CHECK_HR("device", D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0,
                                        &IID_ID3D12Device, (void **)&device));
    CHECK_HR("queue", ID3D12Device_CreateCommandQueue(device, &queue_desc,
                                            &IID_ID3D12CommandQueue, (void **)&queue));
    CHECK_HR("allocator", ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            &IID_ID3D12CommandAllocator, (void **)&allocator));
    CHECK_HR("list", ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list));
    CHECK_HR("fence", ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                                            &IID_ID3D12Fence, (void **)&fence));
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 2ull << 30;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    CHECK_HR("buffer.2gb", ID3D12Device_CreateCommittedResource(device, &properties,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            NULL, &IID_ID3D12Resource, (void **)&big));
    desc.Width = 4096;
    CHECK_HR("output", ID3D12Device_CreateCommittedResource(device, &properties,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void **)&output));
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    CHECK_HR("upload", ID3D12Device_CreateCommittedResource(device, &properties,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, &IID_ID3D12Resource, (void **)&upload));
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    CHECK_HR("readback", ID3D12Device_CreateCommittedResource(device, &properties,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            NULL, &IID_ID3D12Resource, (void **)&readback));
    CHECK_HR("upload.map", ID3D12Resource_Map(upload, 0, NULL, (void **)&mapped));
    for (i = 0; i < ARRAYSIZE(offsets); ++i) mapped[i] = 0xa5a50000u + i;
    ID3D12Resource_Unmap(upload, 0, NULL);
    for (i = 0; i < ARRAYSIZE(offsets); ++i)
        ID3D12GraphicsCommandList_CopyBufferRegion(list, big, offsets[i], upload, i * 4, 4);
    transition(list, big, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 3;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CHECK_HR("heap", ID3D12Device_CreateDescriptorHeap(device, &heap_desc,
                                    &IID_ID3D12DescriptorHeap, (void **)&heap));
    increment = ID3D12Device_GetDescriptorHandleIncrementSize(device, heap_desc.Type);
    heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap, &cpu);
    heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap, &gpu);
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R32_TYPELESS;
    srv.Buffer.NumElements = (2u << 30) / 4;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    fid_info("views", "creating full 2 GB raw SRV");
    ID3D12Device_CreateShaderResourceView(device, big, &srv, cpu);
    cpu.ptr += increment;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    srv.Buffer.StructureByteStride = 4;
    fid_info("views", "creating full 2 GB structured SRV");
    ID3D12Device_CreateShaderResourceView(device, big, &srv, cpu);
    cpu.ptr += increment;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.Buffer.NumElements = (2u << 30) / 4;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    fid_info("views", "creating full 2 GB raw UAV");
    ID3D12Device_CreateUnorderedAccessView(device, big, NULL, &uav, cpu);

    hr = D3DCompile(shader, sizeof(shader) - 1, "large-raw-buffer", NULL, NULL,
                    "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) fid_info("shader.messages", "%s", (char *)ID3D10Blob_GetBufferPointer(errors));
    fid_check(SUCCEEDED(hr), "shader.compile", "hr=%#lx", (unsigned long)hr);
    if (FAILED(hr)) goto cleanup;
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 2;
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = ARRAYSIZE(ranges);
    parameters[0].DescriptorTable.pDescriptorRanges = ranges;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    CHECK_HR("root.serialize", D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                          &signature, NULL));
    CHECK_HR("root.create", ID3D12Device_CreateRootSignature(device, 0,
            ID3D10Blob_GetBufferPointer(signature), ID3D10Blob_GetBufferSize(signature),
            &IID_ID3D12RootSignature, (void **)&root));
    pipeline_desc.pRootSignature = root;
    pipeline_desc.CS.pShaderBytecode = ID3D10Blob_GetBufferPointer(code);
    pipeline_desc.CS.BytecodeLength = ID3D10Blob_GetBufferSize(code);
    CHECK_HR("pipeline", ID3D12Device_CreateComputePipelineState(device, &pipeline_desc,
                                                &IID_ID3D12PipelineState, (void **)&pipeline));
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1,
                                                    ID3D12Resource_GetGPUVirtualAddress(output));
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline);
    ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
    transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, output, 0, 4096);
    CHECK_HR("list.close", ID3D12GraphicsCommandList_Close(list));
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    fid_check(event != NULL, "event", "error=%lu", GetLastError());
    if (!event) goto cleanup;
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList **)&list);
    CHECK_HR("queue.signal", ID3D12CommandQueue_Signal(queue, fence, 1));
    CHECK_HR("fence.event", ID3D12Fence_SetEventOnCompletion(fence, 1, event));
    wait_result = WaitForSingleObject(event, 30000);
    fid_check(wait_result == WAIT_OBJECT_0, "gpu.completed", "wait=%lu", wait_result);
    if (wait_result != WAIT_OBJECT_0) return fid_summary(); /* GPU may still own resources. */
    CHECK_HR("readback.map", ID3D12Resource_Map(readback, 0, NULL, (void **)&mapped));
    for (i = 0; i < ARRAYSIZE(offsets); ++i)
    {
        char name[64];
        snprintf(name, sizeof(name), "raw.byte-%llu", (unsigned long long)offsets[i]);
        fid_check(mapped[2*i] == 0xa5a50000u + i, name, "got %#x", mapped[2*i]);
        snprintf(name, sizeof(name), "structured.byte-%llu", (unsigned long long)offsets[i]);
        fid_check(mapped[2*i+1] == 0xa5a50000u + i, name, "got %#x", mapped[2*i+1]);
    }
    ID3D12Resource_Unmap(readback, 0, NULL);

    /* Reuse the same full-range descriptor for stores and contended atomics.
     * Copy the final words independently, so a matching shader read cannot hide
     * a write to the wrong location or a truncated descriptor range. */
    RELEASE(errors); errors = NULL;
    hr = D3DCompile(write_shader, sizeof(write_shader) - 1, "large-raw-buffer-uav", NULL, NULL,
                    "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &write_code, &errors);
    if (errors) fid_info("uav.shader.messages", "%s", (char *)ID3D10Blob_GetBufferPointer(errors));
    fid_check(SUCCEEDED(hr), "uav.shader.compile", "hr=%#lx", (unsigned long)hr);
    if (FAILED(hr)) goto cleanup;
    pipeline_desc.CS.pShaderBytecode = ID3D10Blob_GetBufferPointer(write_code);
    pipeline_desc.CS.BytecodeLength = ID3D10Blob_GetBufferSize(write_code);
    CHECK_HR("uav.pipeline", ID3D12Device_CreateComputePipelineState(device, &pipeline_desc,
                                                &IID_ID3D12PipelineState, (void **)&write_pipeline));
    CHECK_HR("uav.allocator.reset", ID3D12CommandAllocator_Reset(allocator));
    CHECK_HR("uav.list.reset", ID3D12GraphicsCommandList_Reset(list, allocator, write_pipeline));
    transition(list, big, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(list, output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list, 1,
                                                    ID3D12Resource_GetGPUVirtualAddress(output));
    ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
    transition(list, big, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, output, 0, 4096);
    for (i = 0; i < ARRAYSIZE(offsets); ++i)
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 512 + i * 4, big, offsets[i], 4);
    CHECK_HR("uav.list.close", ID3D12GraphicsCommandList_Close(list));
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList **)&list);
    CHECK_HR("uav.queue.signal", ID3D12CommandQueue_Signal(queue, fence, 2));
    CHECK_HR("uav.fence.event", ID3D12Fence_SetEventOnCompletion(fence, 2, event));
    wait_result = WaitForSingleObject(event, 30000);
    fid_check(wait_result == WAIT_OBJECT_0, "uav.gpu.completed", "wait=%lu", wait_result);
    if (wait_result != WAIT_OBJECT_0) return fid_summary();
    CHECK_HR("uav.readback.map", ID3D12Resource_Map(readback, 0, NULL, (void **)&mapped));
    for (i = 0; i < ARRAYSIZE(offsets); ++i)
    {
        UINT lane, mask = 0, base = 0x11220000u + i * 16;
        char name[64];
        for (lane = 0; lane < 8; ++lane)
        {
            UINT value = mapped[i * 8 + lane];
            if (value >= base && value < base + 8) mask |= 1u << (value - base);
        }
        snprintf(name, sizeof(name), "atomic.old-values.byte-%llu", (unsigned long long)offsets[i]);
        fid_check(mask == 0xff, name, "mask=%#x expected 0xff", mask);
        snprintf(name, sizeof(name), "uav.read.byte-%llu", (unsigned long long)offsets[i]);
        fid_check(mapped[56 + i] == base + 8, name, "got %#x", mapped[56 + i]);
        snprintf(name, sizeof(name), "uav.copy.byte-%llu", (unsigned long long)offsets[i]);
        fid_check(mapped[128 + i] == base + 8, name, "got %#x", mapped[128 + i]);
    }
    ID3D12Resource_Unmap(readback, 0, NULL);
cleanup:
    if (event) CloseHandle(event);
    RELEASE(write_pipeline); RELEASE(pipeline); RELEASE(root); RELEASE(heap);
    RELEASE(signature); RELEASE(write_code); RELEASE(code); RELEASE(errors);
    RELEASE(list); RELEASE(allocator); RELEASE(queue); RELEASE(fence);
    RELEASE(big); RELEASE(upload); RELEASE(output); RELEASE(readback); RELEASE(device);
    return fid_summary();
}
