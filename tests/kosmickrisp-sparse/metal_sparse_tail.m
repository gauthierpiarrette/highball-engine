// Metal-only check of placement sparse block-compressed textures, with no Vulkan or KosmicKrisp involved.
// Creates a WxH texture with a full mip chain, maps one 64 KiB heap page behind its mip tail, writes a
// different pattern into every level with blit copies, then reads every level back. Runs each size as a
// placement sparse texture and as an ordinary texture (the control), writing coarse to fine and fine to coarse.
// Build: clang -fobjc-arc -O2 metal_sparse_tail.m -framework Metal -framework Foundation -o metal_sparse_tail
// Usage: metal_sparse_tail [width height]...   (default: 4 4, 4 8, 4 16, 8 8, 16 4)
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

static id<MTLDevice> dev; static id<MTL4CommandQueue> queue; static id<MTL4CommandAllocator> alloc;
static id<MTLResidencySet> rset; static id<MTLBuffer> stage;

static NSUInteger mini(NSUInteger v, NSUInteger l) { v >>= l; return v ? v : 1; }
static NSUInteger blocks(NSUInteger px) { return (px + 3) / 4; }

static void run(void (^body)(id<MTL4ComputeCommandEncoder>)) {
    id<MTL4CommandBuffer> cb = [dev newCommandBuffer];
    [cb beginCommandBufferWithAllocator:alloc];
    id<MTL4ComputeCommandEncoder> enc = [cb computeCommandEncoder];
    body(enc);
    [enc barrierAfterStages:MTLStageBlit beforeQueueStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
    [enc endEncoding]; [cb endCommandBuffer];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    MTL4CommitOptions *opt = [MTL4CommitOptions new];
    [opt addFeedbackHandler:^(id<MTL4CommitFeedback> fb) { if (fb.error) NSLog(@"commit error %@", fb.error); dispatch_semaphore_signal(done); }];
    const id<MTL4CommandBuffer> cbs[1] = { cb };
    [queue commit:cbs count:1 options:opt];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
}

static int test(MTLPixelFormat fmt, const char *name, NSUInteger bytes, NSUInteger w, NSUInteger h, bool sparse, bool fine_first) {
    NSUInteger levels = 1; while ((w >> levels) || (h >> levels)) levels++;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:YES];
    d.mipmapLevelCount = levels; d.usage = MTLTextureUsageShaderRead;
    d.resourceOptions = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked;
    if (sparse) d.placementSparsePageSize = MTLSparsePageSize64;
    id<MTLTexture> tex = [dev newTextureWithDescriptor:d];
    if (!tex) { printf("  could not create the texture\n"); return 1; }
    id<MTLHeap> heap = nil;
    if (sparse) {
        MTLHeapDescriptor *hd = [MTLHeapDescriptor new]; hd.type = MTLHeapTypePlacement; hd.size = 1 << 20;
        hd.resourceOptions = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeUntracked;
        hd.sparsePageSize = MTLSparsePageSize16; hd.maxCompatiblePlacementSparsePageSize = MTLSparsePageSize64;
        heap = [dev newHeapWithDescriptor:hd]; [rset addAllocation:heap];
    }
    [rset addAllocation:tex]; [rset commit]; [rset requestResidency];
    NSUInteger tail = sparse ? tex.firstMipmapInTail : 0;
    if (sparse) {
        if (tail != 0) { printf("  tail starts at level %lu, this test only covers textures that are all tail\n", (unsigned long)tail); return 0; }
        MTL4UpdateSparseTextureMappingOperation op = { .mode = MTLSparseTextureMappingModeMap, .textureRegion = MTLRegionMake3D(0, 0, 0, 1, 1, 1), .textureLevel = tail, .textureSlice = 0, .heapOffset = 0 };
        [queue updateTextureMappings:tex heap:heap operations:&op count:1];
    }
    uint8_t *p = stage.contents; const NSUInteger slot = 16384;   // one slot per level in the staging buffer
    for (NSUInteger s = 0; s < levels; s++) {
        NSUInteger l = fine_first ? s : levels - 1 - s, lw = mini(w, l), lh = mini(h, l), n = blocks(lw) * blocks(lh) * bytes;
        for (NSUInteger k = 0; k < n; k++) p[l * slot + k] = (uint8_t)(0x11 * (l + 1) + k * 7 + 1);
        run(^(id<MTL4ComputeCommandEncoder> enc) {
            [enc copyFromBuffer:stage sourceOffset:l * slot sourceBytesPerRow:blocks(lw) * bytes sourceBytesPerImage:n sourceSize:MTLSizeMake(lw, lh, 1)
                      toTexture:tex destinationSlice:0 destinationLevel:l destinationOrigin:MTLOriginMake(0, 0, 0)];
        });
    }
    int fails = 0; const NSUInteger back = levels * slot;
    for (NSUInteger l = 0; l < levels; l++) {
        NSUInteger lw = mini(w, l), lh = mini(h, l), n = blocks(lw) * blocks(lh) * bytes;
        memset(p + back, 0xEE, n);
        run(^(id<MTL4ComputeCommandEncoder> enc) {
            [enc copyFromTexture:tex sourceSlice:0 sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(lw, lh, 1)
                        toBuffer:stage destinationOffset:back destinationBytesPerRow:blocks(lw) * bytes destinationBytesPerImage:n];
        });
        if (memcmp(p + back, p + l * slot, n)) {
            int zero = 1, other = -1; for (NSUInteger k = 0; k < n; k++) if (p[back + k]) zero = 0;
            for (NSUInteger o = 0; o < levels; o++) if (o != l && !memcmp(p + back, p + o * slot, bytes)) other = (int)o;
            printf("  FAIL level %lu (%lux%lu): %s", (unsigned long)l, (unsigned long)lw, (unsigned long)lh, zero ? "reads zero" : "wrong data");
            if (other >= 0) printf(", holds what was written to level %d", other);
            printf("\n"); fails++;
        }
    }
    printf("%-8s %lux%lu %-8s %-14s tail from level %lu%s: %s\n", name, (unsigned long)w, (unsigned long)h, sparse ? "sparse" : "ordinary", fine_first ? "fine to coarse" : "coarse to fine",
           (unsigned long)tail, sparse ? [[NSString stringWithFormat:@" (%lu bytes)", (unsigned long)tex.tailSizeInBytes] UTF8String] : "", fails ? "FAILED" : "ok");
    [rset removeAllocation:tex]; if (heap) [rset removeAllocation:heap]; [rset commit];
    return fails;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        dev = MTLCreateSystemDefaultDevice(); queue = [dev newMTL4CommandQueue]; alloc = [dev newCommandAllocator];
        NSError *err = nil; rset = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&err];
        stage = [dev newBufferWithLength:1 << 20 options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
        [rset addAllocation:stage]; [rset commit]; [queue addResidencySet:rset];
        printf("%s, macOS %s\n", dev.name.UTF8String, [[NSProcessInfo processInfo] operatingSystemVersionString].UTF8String);
        NSUInteger def[] = { 4, 4, 4, 8, 4, 16, 8, 8, 16, 4 }, sizes[64], ns = 0;
        if (argc > 2) for (int i = 1; i + 1 < argc && ns < 64; i += 2) { sizes[ns++] = atoi(argv[i]); sizes[ns++] = atoi(argv[i + 1]); }
        else { memcpy(sizes, def, sizeof(def)); ns = 10; }
        struct { MTLPixelFormat f; const char *n; NSUInteger b; } fmts[] = { { MTLPixelFormatBC1_RGBA, "BC1", 8 }, { MTLPixelFormatBC4_RUnorm, "BC4", 8 }, { MTLPixelFormatBC7_RGBAUnorm, "BC7", 16 }, { MTLPixelFormatRGBA8Unorm, "RGBA8", 0 } };
        int fails = 0;
        for (NSUInteger i = 0; i < ns; i += 2) for (int f = 0; f < 3; f++) for (int sp = 1; sp >= 0; sp--) for (int o = 0; o < 2; o++)
            fails += test(fmts[f].f, fmts[f].n, fmts[f].b, sizes[i], sizes[i + 1], sp, o);
        printf(fails ? "FAILURES: %d levels read back wrong\n" : "all levels read back (0 failures)\n", fails);
        return fails != 0;
    }
}
