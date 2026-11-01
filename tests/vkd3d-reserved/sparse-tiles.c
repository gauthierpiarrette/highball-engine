#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TILE 65536u
static unsigned passed, failed;
static ID3D12CommandQueue *queue;
static ID3D12CommandAllocator *allocator;
static ID3D12GraphicsCommandList *list;
static ID3D12Fence *fence;
static HANDLE event;
static UINT64 fence_value;

static int check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) ++passed; else ++failed;
    return ok;
}

#define HR(expr) do { HRESULT hr_ = (expr); if (!check(SUCCEEDED(hr_), #expr)) { \
    printf("HRESULT=%#lx\n", (unsigned long)hr_); goto done; } } while (0)
#define RELEASE(p) do { if (p) IUnknown_Release((IUnknown *)(p)); } while (0)

static int finish(void)
{
    ID3D12CommandList *commands[] = {(ID3D12CommandList *)list};
    HRESULT hr = ID3D12GraphicsCommandList_Close(list);
    if (!check(SUCCEEDED(hr), "commands.close")) return 0;
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, commands);
    hr = ID3D12CommandQueue_Signal(queue, fence, ++fence_value);
    if (!check(SUCCEEDED(hr), "queue.signal")) return 0;
    hr = ID3D12Fence_SetEventOnCompletion(fence, fence_value, event);
    if (!check(SUCCEEDED(hr), "fence.event")) return 0;
    return check(WaitForSingleObject(event, 20000) == WAIT_OBJECT_0, "fence.completed");
}

static void transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {0};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
}

static void mapping(ID3D12Resource *resource, UINT first, UINT count,
                    ID3D12Heap *heap, D3D12_TILE_RANGE_FLAGS flag)
{
    D3D12_TILED_RESOURCE_COORDINATE start = {first, 0, 0, 0};
    D3D12_TILE_REGION_SIZE region = {count, FALSE, 0, 0, 0};
    UINT offset = 0;
    ID3D12CommandQueue_UpdateTileMappings(queue, resource, 1, &start, &region,
                                        heap, 1, &flag, &offset, &count,
                                        D3D12_TILE_MAPPING_FLAG_NONE);
}

int main(int argc, char **argv)
{
    ID3D12Device *device = NULL;
    ID3D12Resource *reserved = NULL, *upload = NULL, *readback = NULL;
    ID3D12Heap *heap = NULL;
    HRESULT (WINAPI *create_device)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    HMODULE module = LoadLibraryA("d3d12.dll");
    create_device = module ? (void *)GetProcAddress(module, "D3D12CreateDevice") : NULL;
    if (!check(create_device != NULL, "api.d3d12")) goto done;
    HR(create_device(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&device));
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    HR(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS,
                                      &options, sizeof(options)));
    printf("INFO tiled-tier=%u\n", options.TiledResourcesTier);
    if (!check(options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2, "tiled-tier>=2")) goto done;
    D3D12_COMMAND_QUEUE_DESC qdesc = {0};
    HR(ID3D12Device_CreateCommandQueue(device, &qdesc, &IID_ID3D12CommandQueue, (void **)&queue));
    HR(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         &IID_ID3D12CommandAllocator, (void **)&allocator));
    HR(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                    NULL, &IID_ID3D12GraphicsCommandList, (void **)&list));
    HR(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!check(event != NULL, "event.create")) goto done;
    D3D12_RESOURCE_DESC desc = {0};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 2 * TILE;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HR(ID3D12Device_CreateReservedResource(device, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                          NULL, &IID_ID3D12Resource, (void **)&reserved));
    D3D12_HEAP_DESC hdesc = {0};
    hdesc.SizeInBytes = TILE;
    hdesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (argc == 2 && !strcmp(argv[1], "host-visible"))
    {
        hdesc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
        hdesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
        hdesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    }
    hdesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    HR(ID3D12Device_CreateHeap(device, &hdesc, &IID_ID3D12Heap, (void **)&heap));
    D3D12_HEAP_PROPERTIES props = {0};
    props.Type = D3D12_HEAP_TYPE_UPLOAD;
    HR(ID3D12Device_CreateCommittedResource(device, &props, D3D12_HEAP_FLAG_NONE, &desc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                           &IID_ID3D12Resource, (void **)&upload));
    props.Type = D3D12_HEAP_TYPE_READBACK;
    HR(ID3D12Device_CreateCommittedResource(device, &props, D3D12_HEAP_FLAG_NONE, &desc,
                                           D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                           &IID_ID3D12Resource, (void **)&readback));
    uint32_t *data = NULL;
    D3D12_RANGE empty = {0, 0};
    HR(ID3D12Resource_Map(upload, 0, &empty, (void **)&data));
    data[0] = 0x1234abcd;
    data[1] = 0x9876fedc;
    D3D12_RANGE written = {0, 8};
    ID3D12Resource_Unmap(upload, 0, &written);
    for (unsigned phase = 0; phase < 3; ++phase)
    {
        if (phase)
        {
            HR(ID3D12CommandAllocator_Reset(allocator));
            HR(ID3D12GraphicsCommandList_Reset(list, allocator, NULL));
        }
        if (!phase)
            mapping(reserved, 0, 2, heap, D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE);
        else
            mapping(reserved, 1, 1, phase == 1 ? NULL : heap,
                    phase == 1 ? D3D12_TILE_RANGE_FLAG_NULL : D3D12_TILE_RANGE_FLAG_NONE);
        if (phase != 1)
        {
            if (phase) transition(reserved, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, reserved, phase ? TILE : 0,
                                                      upload, phase ? 4 : 0, 4);
            transition(reserved, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, reserved, 0, 4);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 4, reserved, TILE, 4);
        if (!finish()) goto done;
        D3D12_RANGE read = {0, 8};
        HR(ID3D12Resource_Map(readback, 0, &read, (void **)&data));
        uint32_t expected0 = phase == 2 ? 0x9876fedc : 0x1234abcd;
        uint32_t expected1 = phase == 1 ? 0 : expected0;
        printf("INFO phase=%u tile0=%08x tile1=%08x expected=%08x/%08x\n",
               phase, data[0], data[1], expected0, expected1);
        check(data[0] == expected0, "tile0.physical-data");
        check(data[1] == expected1, phase == 1 ? "tile1.unmapped-zero" : "tile1.alias-data");
        ID3D12Resource_Unmap(readback, 0, &empty);
    }
done:
    if (event) CloseHandle(event);
    RELEASE(readback); RELEASE(upload); RELEASE(reserved); RELEASE(heap);
    RELEASE(list); RELEASE(allocator); RELEASE(queue); RELEASE(fence); RELEASE(device);
    if (module) FreeLibrary(module);
    printf("SUMMARY sparse-tiles pass=%u fail=%u\n", passed, failed);
    return failed != 0;
}
