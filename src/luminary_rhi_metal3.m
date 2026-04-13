#include "luminary_rhi.h"
#include "luminary_rhi_internal.h"

#include <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdatomic.h>

typedef struct LRHIDeviceMetal3 {
    LRHIDeviceBase base;
    id<MTLDevice> device;
    uint8_t enable_debug;
} LRHIDeviceMetal3;

typedef struct LRHITextureMetal3 {
    LRHITextureBase base;
    id<MTLTexture> texture;
    LRHITextureInfo info;
} LRHITextureMetal3;

typedef struct LRHIBufferMetal3 {
    LRHIBufferBase base;
    id<MTLBuffer> buffer;
    LRHIBufferInfo info;
} LRHIBufferMetal3;

typedef struct LRHICommandQueueMetal3 {
    LRHICommandQueueBase base;
    id<MTLCommandQueue>  queue;
    id<MTLDevice>        device;
} LRHICommandQueueMetal3;

typedef struct LRHIFenceWaiterMetal3 {
    dispatch_semaphore_t          semaphore;
    uint64_t                      target_value;
    struct LRHIFenceWaiterMetal3* next;
} LRHIFenceWaiterMetal3;

typedef struct LRHIFenceMetal3 {
    LRHIFenceBase          base;
    _Atomic uint64_t       value;
    pthread_mutex_t        waiters_mutex;
    LRHIFenceWaiterMetal3* waiters;
} LRHIFenceMetal3;

typedef struct LRHICommandListMetal3 {
    LRHICommandListBase base;
    id<MTLCommandBuffer> command_buffer;
} LRHICommandListMetal3;

typedef struct LRHICopyPassMetal3 {
    LRHICopyPassBase base;
    id<MTLBlitCommandEncoder> blit_encoder;
} LRHICopyPassMetal3;

typedef struct LRHIResidencySetMetal3 {
    LRHIResidencySetBase base;
    id<MTLResidencySet> residency_set;
} LRHIResidencySetMetal3;

typedef struct LRHISwapChainMetal3 {
    LRHISwapChainBase   base;
    CAMetalLayer*       layer;
    id<CAMetalDrawable> current_drawable;  // nil between present and next get_current_texture
    LRHITextureMetal3   current_texture;   // embedded — no heap allocation per frame
    LRHISwapChainInfo   info;
} LRHISwapChainMetal3;

typedef struct LRHITextureViewMetal3 {
    LRHITextureViewBase base;
    LRHITextureViewInfo info;
    id<MTLTexture> texture_view;
    uint32_t bindless_index; // For bindless resource indexing, if supported
} LRHITextureViewMetal3;

typedef struct LRHIRenderPassMetal3 {
    LRHIRenderPassBase base;
    id<MTLRenderCommandEncoder> render_encoder;
} LRHIRenderPassMetal3;

// Forward declarations
static MTLPixelFormat  lrhi_metal3_pixel_format(LRHITextureFormat format);
static MTLTextureUsage lrhi_metal3_texture_usage(LRHITextureUsage usage);
static MTLTextureType  lrhi_metal3_texture_type(LRHITextureDimensions type);
static void            lrhi_metal3_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error);
static MTLStages       lrhi_metal3_render_stage_to_mtl(LRHIRenderStage stage);
static MTLRenderStages lrhi_metal3_render_stages_to_mtl(LRHIRenderStage stages);
static MTLLoadAction   lrhi_metal3_load_action_to_mtl(LRHIRenderPassAction load_op);
static MTLStoreAction  lrhi_metal3_store_action_to_mtl(LRHIRenderPassAction store_op);

static void            lrhi_metal3_destroy_device(LRHIDevice device);
static LRHIDeviceInfo  lrhi_metal3_get_device_info(LRHIDevice device);

static void            lrhi_metal3_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
static void            lrhi_metal3_destroy_texture(LRHITexture texture);
static void            lrhi_metal3_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
static void            lrhi_metal3_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal3_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal3_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);

static void            lrhi_metal3_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
static void            lrhi_metal3_destroy_buffer(LRHIBuffer buffer);
static void            lrhi_metal3_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
static void*           lrhi_metal3_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal3_buffer_unmap(LRHIBuffer buffer);
static void            lrhi_metal3_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);

static void            lrhi_metal3_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
static void            lrhi_metal3_destroy_command_queue(LRHICommandQueue queue);
static void            lrhi_metal3_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
static void            lrhi_metal3_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
static void            lrhi_metal3_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error);
static void            lrhi_metal3_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error);

static void            lrhi_metal3_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
static void            lrhi_metal3_destroy_fence(LRHIFence fence);
static uint64_t        lrhi_metal3_fence_get_value(LRHIFence fence);
static void            lrhi_metal3_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);
static void            lrhi_metal3_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
static void            lrhi_metal3_fence_update_value(LRHIFenceMetal3* fence, uint64_t new_value);

static void            lrhi_metal3_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error);
static void            lrhi_metal3_destroy_command_list(LRHICommandList command_list);
static void            lrhi_metal3_command_list_begin(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal3_command_list_end(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal3_command_list_reset(LRHICommandList command_list, LRHIError* out_error);
static LRHICopyPass    lrhi_metal3_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error);

static void           lrhi_metal3_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error) { (void)copy_pass; (void)out_error; /* No-op since Metal automatically handles synchronization between blit commands within the same encoder */ }
static void           lrhi_metal3_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error);

static void            lrhi_metal3_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error);
static void            lrhi_metal3_destroy_residency_set(LRHIResidencySet residency_set);
static void            lrhi_metal3_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void            lrhi_metal3_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal3_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void            lrhi_metal3_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal3_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error);

static void            lrhi_metal3_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error);
static void            lrhi_metal3_destroy_swap_chain(LRHISwapChain swap_chain);
static LRHITexture     lrhi_metal3_swap_chain_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error);
static void            lrhi_metal3_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error);

static void            lrhi_metal3_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error);
static void            lrhi_metal3_destroy_texture_view(LRHITextureView texture_view);
static void            lrhi_metal3_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info);
static uint32_t        lrhi_metal3_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error);

static LRHIRenderPass  lrhi_metal3_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error);
static void            lrhi_metal3_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error);
static void            lrhi_metal3_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void            lrhi_metal3_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);

// Vtable instances

static const LRHIDeviceVTable lrhi_metal3_device_vtable = {
    .destroy_device       = lrhi_metal3_destroy_device,
    .get_device_info      = lrhi_metal3_get_device_info,
    .create_texture       = lrhi_metal3_create_texture,
    .create_buffer        = lrhi_metal3_create_buffer,
    .texture_readback     = lrhi_metal3_texture_readback,
    .buffer_readback      = lrhi_metal3_buffer_readback,
    .create_command_queue  = lrhi_metal3_create_command_queue,
    .create_fence          = lrhi_metal3_create_fence,
    .create_residency_set  = lrhi_metal3_create_residency_set,
    .create_swap_chain     = lrhi_metal3_create_swap_chain,
    .create_texture_view   = lrhi_metal3_create_texture_view,
};

static const LRHICommandQueueVTable lrhi_metal3_command_queue_vtable = {
    .create_command_list   = lrhi_metal3_create_command_list,
    .destroy_command_queue = lrhi_metal3_destroy_command_queue,
    .signal_fence          = lrhi_metal3_command_queue_signal,
    .wait_fence            = lrhi_metal3_command_queue_wait,
    .submit_command_lists  = lrhi_metal3_command_queue_submit,
    .add_residency_set     = lrhi_metal3_command_queue_add_residency_set,
};

static const LRHIFenceVTable lrhi_metal3_fence_vtable = {
    .destroy_fence = lrhi_metal3_destroy_fence,
    .get_value     = lrhi_metal3_fence_get_value,
    .signal        = lrhi_metal3_fence_signal,
    .wait          = lrhi_metal3_fence_wait,
};

static const LRHITextureVTable lrhi_metal3_texture_vtable = {
    .destroy_texture        = lrhi_metal3_destroy_texture,
    .get_texture_info       = lrhi_metal3_get_texture_info,
    .texture_replace_region = lrhi_metal3_texture_replace_region,
    .texture_read_region    = lrhi_metal3_texture_read_region,
};

static const LRHIBufferVTable lrhi_metal3_buffer_vtable = {
    .destroy_buffer  = lrhi_metal3_destroy_buffer,
    .get_buffer_info = lrhi_metal3_get_buffer_info,
    .buffer_map      = lrhi_metal3_buffer_map,
    .buffer_unmap    = lrhi_metal3_buffer_unmap,
};

static const LRHICommandListVTable lrhi_metal3_command_list_vtable = {
    .destroy_command_list = lrhi_metal3_destroy_command_list,
    .command_list_begin   = lrhi_metal3_command_list_begin,
    .command_list_end     = lrhi_metal3_command_list_end,
    .command_list_reset   = lrhi_metal3_command_list_reset,
    .copy_pass_begin      = lrhi_metal3_command_list_begin_copy_pass,
    .render_pass_begin    = lrhi_metal3_render_pass_begin,
};

static const LRHICopyPassVTable lrhi_metal3_copy_pass_vtable = {
    .copy_pass_end                = lrhi_metal3_copy_pass_end,
    .copy_pass_intra_barrier      = lrhi_metal3_copy_pass_intra_barrier,
    .copy_pass_encoder_barrier    = lrhi_metal3_copy_pass_encoder_barrier,
    .copy_texture_to_texture      = lrhi_metal3_copy_pass_copy_texture_to_texture,
    .copy_buffer_to_buffer        = lrhi_metal3_copy_pass_copy_buffer_to_buffer,
    .copy_buffer_to_texture       = lrhi_metal3_copy_pass_copy_buffer_to_texture,
    .copy_texture_to_buffer       = lrhi_metal3_copy_pass_copy_texture_to_buffer,
};

static const LRHIResidencySetVTable lrhi_metal3_residency_set_vtable = {
    .destroy_residency_set = lrhi_metal3_destroy_residency_set,
    .add_texture = lrhi_metal3_residency_set_add_texture,
    .add_buffer = lrhi_metal3_residency_set_add_buffer,
    .remove_texture = lrhi_metal3_residency_set_remove_texture,
    .remove_buffer = lrhi_metal3_residency_set_remove_buffer,
    .update = lrhi_metal3_residency_set_update,
};

static const LRHITextureViewVTable lrhi_metal3_texture_view_vtable = {
    .destroy_texture_view = lrhi_metal3_destroy_texture_view,
    .get_texture_view_info = lrhi_metal3_get_texture_view_info,
    .get_bindless_index = lrhi_metal3_texture_view_get_bindless_index,
};

static void lrhi_metal3_swap_chain_texture_destroy_noop(LRHITexture texture) { (void)texture; }

static const LRHITextureVTable lrhi_metal3_swap_chain_texture_vtable = {
    .destroy_texture        = lrhi_metal3_swap_chain_texture_destroy_noop,
    .get_texture_info       = lrhi_metal3_get_texture_info,
    .texture_replace_region = lrhi_metal3_texture_replace_region,
    .texture_read_region    = lrhi_metal3_texture_read_region,
};

static const LRHISwapChainVTable lrhi_metal3_swap_chain_vtable = {
    .destroy_swap_chain  = lrhi_metal3_destroy_swap_chain,
    .get_current_texture = lrhi_metal3_swap_chain_get_current_texture,
    .present             = lrhi_metal3_swap_chain_present,
};

static const LRHIRenderPassVTable lrhi_metal3_render_pass_vtable = {
    .end = lrhi_metal3_render_pass_end,
    .intra_barrier = lrhi_metal3_render_pass_intra_barrier,
    .encoder_barrier = lrhi_metal3_render_pass_encoder_barrier,
};

// Device

void lrhi_metal3_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    LRHIDeviceMetal3* device = malloc(sizeof(LRHIDeviceMetal3));
    device->base.vtable = &lrhi_metal3_device_vtable;
    device->device = MTLCreateSystemDefaultDevice();
    device->enable_debug = enable_debug;
    if ([device->device supportsFamily:MTLGPUFamilyMetal3] == NO) {
        free(device);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Metal 3 is not supported on this device");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_device = NULL;
        return;
    }
    *out_device = (LRHIDevice)device;
}

static void lrhi_metal3_destroy_device(LRHIDevice device)
{
    free(device);
}

static LRHIDeviceInfo lrhi_metal3_get_device_info(LRHIDevice device)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

    LRHIDeviceInfo info = {0};
    info.backend = LUMINARY_RHI_BACKEND_METAL3;
    snprintf(info.device_name, sizeof(info.device_name), "%s", [metal_device->device.name UTF8String]);
    info.features.ray_tracing = [metal_device->device supportsRaytracing];
    info.features.mesh_shading = [metal_device->device supportsFamily:MTLGPUFamilyApple7];
    info.features.bindless_resources = [metal_device->device supportsFamily:MTLGPUFamilyApple7];
    info.features.multi_draw_indirect = [metal_device->device supportsFamily:MTLGPUFamilyApple7];
    info.limits.max_texture_dimension_2d = 16384;
    info.limits.max_texture_dimension_3d = 2048;
    info.limits.max_texture_array_layers = 2048;
    info.limits.max_buffer_size = [metal_device->device maxBufferLength];

    return info;
}

// Textures

static void lrhi_metal3_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    lrhi_metal3_validate_texture_info(info, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        *out_texture = NULL;
        return;
    }

    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.pixelFormat = lrhi_metal3_pixel_format(info->format);
    descriptor.width = info->width;
    descriptor.height = info->height;
    descriptor.depth = info->depth;
    descriptor.mipmapLevelCount = info->mip_levels;
    descriptor.arrayLength = info->array_layers;
    descriptor.storageMode = MTLStorageModeShared; // Apple Silicon only. If you have an Intel GPU, just buy a new Macbook :p
    descriptor.textureType = lrhi_metal3_texture_type(info->dimensions);
    descriptor.usage = lrhi_metal3_texture_usage(info->usage);

    id<MTLTexture> texture = [metal_device->device newTextureWithDescriptor:descriptor];
    if (!texture) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create texture");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_texture = NULL;
        return;
    }

    LRHITextureMetal3* out = malloc(sizeof(LRHITextureMetal3));
    out->base.vtable = &lrhi_metal3_texture_vtable;
    out->texture = texture;
    out->info = *info;
    *out_texture = (LRHITexture)out;
}

static void lrhi_metal3_destroy_texture(LRHITexture texture)
{
    free(texture);
}

static void lrhi_metal3_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info)
{
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)texture;
    *out_info = metal_texture->info;
}

static void lrhi_metal3_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO: Validate region, mip level, array layer, and data size against texture info
    MTLRegion metal_region = MTLRegionMake3D(region->x, region->y, region->z, region->width, region->height, region->depth);
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)texture;
    [metal_texture->texture replaceRegion:metal_region mipmapLevel:mip_level slice:array_layer withBytes:data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image];
}

static void lrhi_metal3_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO: Validate region, mip level, array layer, and data size against texture info
    MTLRegion metal_region = MTLRegionMake3D(region->x, region->y, region->z, region->width, region->height, region->depth);
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)texture;
    [metal_texture->texture getBytes:out_data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image fromRegion:metal_region mipmapLevel:mip_level slice:array_layer];
}

static void lrhi_metal3_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // No synchronization needed with Metal since we're using shared storage mode
    lrhi_metal3_texture_read_region(texture, region, mip_level, array_layer, out_data, data_size, bytes_per_row, bytes_per_image, out_error);
}

// Buffers

static void lrhi_metal3_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

    MTLResourceOptions options = MTLResourceStorageModeShared;
    id<MTLBuffer> buffer = [metal_device->device newBufferWithLength:info->size options:options];
    if (!buffer) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create buffer");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_buffer = NULL;
        return;
    }

    LRHIBufferMetal3* out = malloc(sizeof(LRHIBufferMetal3));
    out->base.vtable = &lrhi_metal3_buffer_vtable;
    out->buffer = buffer;
    out->info = *info;
    *out_buffer = (LRHIBuffer)out;
}

static void lrhi_metal3_destroy_buffer(LRHIBuffer buffer)
{
    free(buffer);
}

static void lrhi_metal3_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info)
{
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;
    *out_info = metal_buffer->info;
}

static void* lrhi_metal3_buffer_map(LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;
    return [metal_buffer->buffer contents];
}

static void lrhi_metal3_buffer_unmap(LRHIBuffer buffer)
{
    (void)buffer; // No-op since we're using shared storage mode
}

static void lrhi_metal3_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
    // No synchronization needed with Metal since we're using shared storage mode
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;
    void* contents = [metal_buffer->buffer contents];
    if (contents) {
        memcpy(out_data, contents, data_size);
    } else {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to read buffer data");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
    }
}

// Command queue and fence

static void lrhi_metal3_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    id<MTLCommandQueue> queue = [metal_device->device newCommandQueue];
    if (!queue) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command queue");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_queue = NULL;
        return;
    }

    LRHICommandQueueMetal3* out = malloc(sizeof(LRHICommandQueueMetal3));
    out->base.vtable = &lrhi_metal3_command_queue_vtable;
    out->queue = queue;
    out->device = metal_device->device;
    *out_queue = (LRHICommandQueue)out;
}

static void lrhi_metal3_destroy_command_queue(LRHICommandQueue queue)
{
    free(queue);
}

static void lrhi_metal3_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    LRHICommandQueueMetal3* metal_queue = (LRHICommandQueueMetal3*)queue;
    LRHIFenceMetal3* metal_fence = (LRHIFenceMetal3*)fence;

    id<MTLCommandBuffer> cmd = [metal_queue->queue commandBuffer];
    if (!cmd) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command buffer for queue signal");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    [cmd addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buffer) {
        (void)buffer;
        lrhi_metal3_fence_update_value(metal_fence, value);
    }];
    [cmd commit];
}

static void lrhi_metal3_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    (void)queue;
    // Metal 3 has no queue-level event wait. Block CPU submission thread instead.
    lrhi_metal3_fence_wait(fence, value, timeout_ns, out_error);
}

static void lrhi_metal3_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error)
{
    LRHICommandQueueMetal3* metal_queue = (LRHICommandQueueMetal3*)queue;

    // Wait on the CPU if needed since Metal 3 doesn't support GPU-side waits
    if (wait_fence) {
        lrhi_metal3_fence_wait(wait_fence, wait_value, UINT64_MAX, out_error);
        if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            return;
        }
    }

    for (uint32_t i = 0; i < command_list_count; i++) {
        LRHICommandListMetal3* cmd_list = (LRHICommandListMetal3*)command_lists[i];
        [cmd_list->command_buffer commit];
    }

    if (signal_fence) {
        lrhi_metal3_command_queue_signal(queue, signal_fence, signal_value, out_error);
    }
}

static void lrhi_metal3_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error)
{
    LRHICommandQueueMetal3* metal_queue = (LRHICommandQueueMetal3*)queue;
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    [metal_queue->queue addResidencySet:metal_residency_set->residency_set];
}

static void lrhi_metal3_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error)
{
    (void)device;
    LRHIFenceMetal3* out = malloc(sizeof(LRHIFenceMetal3));
    out->base.vtable = &lrhi_metal3_fence_vtable;
    atomic_init(&out->value, initial_value);
    out->waiters = NULL;

    int mutex_result = pthread_mutex_init(&out->waiters_mutex, NULL);
    if (mutex_result != 0) {
        free(out);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to initialize fence wait mutex");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_fence = NULL;
        return;
    }

    *out_fence = (LRHIFence)out;
}

static void lrhi_metal3_destroy_fence(LRHIFence fence)
{
    LRHIFenceMetal3* metal_fence = (LRHIFenceMetal3*)fence;
    pthread_mutex_destroy(&metal_fence->waiters_mutex);
    free(metal_fence);
}

static uint64_t lrhi_metal3_fence_get_value(LRHIFence fence)
{
    LRHIFenceMetal3* metal_fence = (LRHIFenceMetal3*)fence;
    return atomic_load_explicit(&metal_fence->value, memory_order_acquire);
}

static void lrhi_metal3_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    (void)out_error;
    LRHIFenceMetal3* metal_fence = (LRHIFenceMetal3*)fence;
    lrhi_metal3_fence_update_value(metal_fence, value);
}

static void lrhi_metal3_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    LRHIFenceMetal3* metal_fence = (LRHIFenceMetal3*)fence;

    if (atomic_load_explicit(&metal_fence->value, memory_order_acquire) >= value)
        return;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    LRHIFenceWaiterMetal3 waiter = {
        .semaphore = sem,
        .target_value = value,
        .next = NULL,
    };

    pthread_mutex_lock(&metal_fence->waiters_mutex);
    if (atomic_load_explicit(&metal_fence->value, memory_order_acquire) >= value) {
        pthread_mutex_unlock(&metal_fence->waiters_mutex);
        return;
    }

    waiter.next = metal_fence->waiters;
    metal_fence->waiters = &waiter;
    pthread_mutex_unlock(&metal_fence->waiters_mutex);

    dispatch_time_t deadline = (timeout_ns == UINT64_MAX)
        ? DISPATCH_TIME_FOREVER
        : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ns);

    if (dispatch_semaphore_wait(sem, deadline) != 0) {
        pthread_mutex_lock(&metal_fence->waiters_mutex);
        LRHIFenceWaiterMetal3** it = &metal_fence->waiters;
        while (*it) {
            if (*it == &waiter) {
                *it = waiter.next;
                break;
            }
            it = &(*it)->next;
        }
        pthread_mutex_unlock(&metal_fence->waiters_mutex);

        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Fence wait timed out");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
        }
    }
}

static void lrhi_metal3_fence_update_value(LRHIFenceMetal3* fence, uint64_t new_value)
{
    uint64_t observed = atomic_load_explicit(&fence->value, memory_order_acquire);
    while (new_value > observed && !atomic_compare_exchange_weak_explicit(&fence->value, &observed, new_value, memory_order_acq_rel, memory_order_acquire)) {
    }

    uint64_t current_value = atomic_load_explicit(&fence->value, memory_order_acquire);
    pthread_mutex_lock(&fence->waiters_mutex);
    LRHIFenceWaiterMetal3** it = &fence->waiters;
    while (*it) {
        LRHIFenceWaiterMetal3* waiter = *it;
        if (waiter->target_value <= current_value) {
            *it = waiter->next;
            dispatch_semaphore_signal(waiter->semaphore);
            continue;
        }
        it = &waiter->next;
    }
    pthread_mutex_unlock(&fence->waiters_mutex);
}

// Command lists
static void lrhi_metal3_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error)
{
    LRHICommandQueueMetal3* metal_queue = (LRHICommandQueueMetal3*)queue;
    id<MTLCommandBuffer> cmd_buffer = [metal_queue->queue commandBuffer];
    if (!cmd_buffer) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command buffer for command list");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_command_list = NULL;
        return;
    }

    LRHICommandListMetal3* out = malloc(sizeof(LRHICommandListMetal3));
    out->base.vtable = &lrhi_metal3_command_list_vtable;
    out->command_buffer = cmd_buffer;
    *out_command_list = (LRHICommandList)out;
}

static void lrhi_metal3_destroy_command_list(LRHICommandList command_list)
{
    free(command_list);
}

static void lrhi_metal3_command_list_begin(LRHICommandList command_list, LRHIError* out_error)
{
    // No explicit begin needed for Metal command buffers
    (void)command_list;
    (void)out_error;
}

static void lrhi_metal3_command_list_end(LRHICommandList command_list, LRHIError* out_error)
{
    // No explicit end needed for Metal command buffers
    (void)command_list;
    (void)out_error;
}

static void lrhi_metal3_command_list_reset(LRHICommandList command_list, LRHIError* out_error)
{
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;
    id<MTLCommandBuffer> new_cmd_buffer = [metal_cmd_list->command_buffer.commandQueue commandBuffer];
    if (!new_cmd_buffer) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create new command buffer for command list reset");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }
    metal_cmd_list->command_buffer = new_cmd_buffer;
}

static LRHICopyPass lrhi_metal3_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error)
{
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;
    id<MTLBlitCommandEncoder> blit_encoder = [metal_cmd_list->command_buffer blitCommandEncoder];
    if (!blit_encoder) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create blit command encoder for copy pass");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    LRHICopyPassMetal3* out = malloc(sizeof(LRHICopyPassMetal3));
    out->base.vtable = &lrhi_metal3_copy_pass_vtable;
    out->blit_encoder = blit_encoder;
    return (LRHICopyPass)out;
}

static void lrhi_metal3_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    [metal_copy_pass->blit_encoder barrierAfterQueueStages:lrhi_metal3_render_stage_to_mtl(afterStage) beforeStages:MTLStageBlit];
}

// Copy pass

static void lrhi_metal3_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error)
{
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    [metal_copy_pass->blit_encoder endEncoding];
    free(metal_copy_pass);
}

static void lrhi_metal3_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    LRHITextureMetal3* metal_src_texture = (LRHITextureMetal3*)src_texture;
    LRHITextureMetal3* metal_dst_texture = (LRHITextureMetal3*)dst_texture;

    MTLOrigin src_origin = MTLOriginMake(src_region.x, src_region.y, src_region.z);
    MTLSize src_size = MTLSizeMake(src_region.width, src_region.height, src_region.depth);
    MTLOrigin dst_origin = MTLOriginMake(dst_region.x, dst_region.y, dst_region.z);

    [metal_copy_pass->blit_encoder copyFromTexture:metal_src_texture->texture sourceSlice:src_array_layer sourceLevel:src_mip_level sourceOrigin:src_origin sourceSize:src_size toTexture:metal_dst_texture->texture destinationSlice:dst_array_layer destinationLevel:dst_mip_level destinationOrigin:dst_origin];
}

static void lrhi_metal3_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error)
{
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    LRHIBufferMetal3* metal_src_buffer = (LRHIBufferMetal3*)src_buffer;
    LRHIBufferMetal3* metal_dst_buffer = (LRHIBufferMetal3*)dst_buffer;

    [metal_copy_pass->blit_encoder copyFromBuffer:metal_src_buffer->buffer sourceOffset:src_offset toBuffer:metal_dst_buffer->buffer destinationOffset:dst_offset size:size];
}

static void lrhi_metal3_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    LRHIBufferMetal3* metal_src_buffer = (LRHIBufferMetal3*)src_buffer;
    LRHITextureMetal3* metal_dst_texture = (LRHITextureMetal3*)dst_texture;

    MTLOrigin dst_origin = MTLOriginMake(dst_region.x, dst_region.y, dst_region.z);
    MTLSize dst_size = MTLSizeMake(dst_region.width, dst_region.height, dst_region.depth);

    [metal_copy_pass->blit_encoder copyFromBuffer:metal_src_buffer->buffer sourceOffset:src_offset sourceBytesPerRow:src_bytes_per_row sourceBytesPerImage:src_bytes_per_image sourceSize:dst_size toTexture:metal_dst_texture->texture destinationSlice:dst_array_layer destinationLevel:dst_mip_level destinationOrigin:dst_origin];
}

static void lrhi_metal3_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error)
{
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    LRHITextureMetal3* metal_src_texture = (LRHITextureMetal3*)src_texture;
    LRHIBufferMetal3* metal_dst_buffer = (LRHIBufferMetal3*)dst_buffer;

    MTLOrigin src_origin = MTLOriginMake(src_region.x, src_region.y, src_region.z);
    MTLSize src_size = MTLSizeMake(src_region.width, src_region.height, src_region.depth);

    [metal_copy_pass->blit_encoder copyFromTexture:metal_src_texture->texture sourceSlice:src_array_layer sourceLevel:src_mip_level sourceOrigin:src_origin sourceSize:src_size toBuffer:metal_dst_buffer->buffer destinationOffset:dst_offset destinationBytesPerRow:dst_bytes_per_row destinationBytesPerImage:dst_bytes_per_image];
}

// Residency set

static void lrhi_metal3_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

    MTLResidencySetDescriptor* descriptor = [[MTLResidencySetDescriptor alloc] init];
    descriptor.initialCapacity = 1024;

    NSError* ns_error = nil;
    id<MTLResidencySet> residency_set = [metal_device->device newResidencySetWithDescriptor:descriptor error:&ns_error];
    if (!residency_set || ns_error) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create residency set: %s", [[ns_error localizedDescription] UTF8String]);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_residency_set = NULL;
        return;
    }

    LRHIResidencySetMetal3* out = malloc(sizeof(LRHIResidencySetMetal3));
    out->base.vtable = &lrhi_metal3_residency_set_vtable;
    out->residency_set = residency_set;
    *out_residency_set = (LRHIResidencySet)out;
}

static void lrhi_metal3_destroy_residency_set(LRHIResidencySet residency_set)
{
    free(residency_set);
}

static void lrhi_metal3_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)texture;

    [metal_residency_set->residency_set addAllocation:metal_texture->texture];
}

static void lrhi_metal3_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;

    [metal_residency_set->residency_set addAllocation:metal_buffer->buffer];
}

static void lrhi_metal3_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)texture;

    [metal_residency_set->residency_set removeAllocation:metal_texture->texture];
}

static void lrhi_metal3_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;

    [metal_residency_set->residency_set removeAllocation:metal_buffer->buffer];
}

static void lrhi_metal3_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    [metal_residency_set->residency_set commit];
}

// Texture view

static void lrhi_metal3_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error)
{
    // If info is same as base texture, don't create a view.
    // Otherwise, create a new texture view with the same underlying texture but different descriptor.
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)info->texture;
    if (info->format == LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED) {
        info->format = metal_texture->info.format;
    }

    // Create the view
    LRHITextureViewMetal3* out = malloc(sizeof(LRHITextureViewMetal3));
    out->base.vtable = &lrhi_metal3_texture_view_vtable;
    out->info = *info;
    out->bindless_index = UINT32_MAX; // TODO: Implement bindless resource indexing

    // Validate
    uint8_t is_same_as_base_texture = (info->texture == (LRHITexture)metal_texture) &&
                                (info->format == metal_texture->info.format) &&
                                (info->base_mip_level == 0) &&
                                ((info->mip_level_count == metal_texture->info.mip_levels) ||
                                (info->mip_level_count == LUMINARY_TEXTURE_VIEW_ALL_MIPS)) &&
                                (info->base_array_layer == 0) &&
                                ((info->array_layer_count == metal_texture->info.array_layers) ||
                                (info->array_layer_count == LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS)) &&
                                (info->usage == metal_texture->info.usage);
    if (!is_same_as_base_texture) {
        MTLTextureViewDescriptor* descriptor = [[MTLTextureViewDescriptor alloc] init];
        descriptor.pixelFormat = lrhi_metal3_pixel_format(info->format);
        if (info->mip_level_count == LUMINARY_TEXTURE_VIEW_ALL_MIPS) {
            descriptor.levelRange = NSMakeRange(info->base_mip_level, metal_texture->info.mip_levels - info->base_mip_level);
        } else {
            descriptor.levelRange = NSMakeRange(info->base_mip_level, info->mip_level_count);
        }
        if (info->array_layer_count == LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS) {
            descriptor.sliceRange = NSMakeRange(info->base_array_layer, metal_texture->info.array_layers - info->base_array_layer);
        } else {
            descriptor.sliceRange = NSMakeRange(info->base_array_layer, info->array_layer_count);
        }
        descriptor.textureType = lrhi_metal3_texture_type(info->dimensions);

        out->texture_view = [metal_texture->texture newTextureViewWithDescriptor:descriptor];
    } else {
        out->texture_view = metal_texture->texture;
    }
    if (!out->texture_view) {
        free(out);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create texture view");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_texture_view = NULL;
        return;
    }
    *out_texture_view = (LRHITextureView)out;
}

static void lrhi_metal3_destroy_texture_view(LRHITextureView texture_view)
{
    free(texture_view);
}

static void lrhi_metal3_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info)
{
    LRHITextureViewMetal3* metal_texture_view = (LRHITextureViewMetal3*)texture_view;
    *out_info = metal_texture_view->info;
}

static uint32_t lrhi_metal3_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error)
{
    LRHITextureViewMetal3* metal_texture_view = (LRHITextureViewMetal3*)texture_view;
    LRHITextureViewInfo view_info = metal_texture_view->info;
    if (view_info.usage != LUMINARY_RHI_TEXTURE_USAGE_SAMPLED && view_info.usage != LUMINARY_RHI_TEXTURE_USAGE_STORAGE) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Only sampled and storage texture views can be used with bindless resources");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return UINT32_MAX;
    }

    return metal_texture_view->bindless_index;
}

// Render pass

static LRHIRenderPass lrhi_metal3_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error)
{
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;

    MTLRenderPassDescriptor* render_pass_descriptor = [[MTLRenderPassDescriptor alloc] init];
    render_pass_descriptor.renderTargetWidth = info->render_width;
    render_pass_descriptor.renderTargetHeight = info->render_height;
    for (int i = 0; i < info->color_attachment_count; i++) {
        LRHIRenderPassAttachmentInfo* attachment = &info->color_attachments[i];
        LRHITextureViewMetal3* metal_texture_view = (LRHITextureViewMetal3*)attachment->texture_view;
        render_pass_descriptor.colorAttachments[i].texture = metal_texture_view->texture_view;
        render_pass_descriptor.colorAttachments[i].loadAction = lrhi_metal3_load_action_to_mtl(attachment->load_action);
        render_pass_descriptor.colorAttachments[i].storeAction = lrhi_metal3_store_action_to_mtl(attachment->store_action);
        render_pass_descriptor.colorAttachments[i].clearColor = MTLClearColorMake(attachment->clear_color[0], attachment->clear_color[1], attachment->clear_color[2], attachment->clear_color[3]);
    }
    if (info->has_depth_stencil_attachment) {
        LRHIRenderPassAttachmentInfo* attachment = &info->depth_stencil_attachment;
        LRHITextureViewMetal3* metal_texture_view = (LRHITextureViewMetal3*)attachment->texture_view;
        render_pass_descriptor.depthAttachment.texture = metal_texture_view->texture_view;
        render_pass_descriptor.depthAttachment.loadAction = lrhi_metal3_load_action_to_mtl(attachment->load_action);
        render_pass_descriptor.depthAttachment.storeAction = lrhi_metal3_store_action_to_mtl(attachment->store_action);
        render_pass_descriptor.depthAttachment.clearDepth = attachment->clear_depth;

        render_pass_descriptor.stencilAttachment.texture = metal_texture_view->texture_view;
        render_pass_descriptor.stencilAttachment.loadAction = lrhi_metal3_load_action_to_mtl(attachment->load_action);
        render_pass_descriptor.stencilAttachment.storeAction = lrhi_metal3_store_action_to_mtl(attachment->store_action);
        render_pass_descriptor.stencilAttachment.clearStencil = attachment->clear_stencil;
    }

    id<MTLRenderCommandEncoder> render_encoder = [metal_cmd_list->command_buffer renderCommandEncoderWithDescriptor:render_pass_descriptor];
    if (!render_encoder) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create render command encoder for render pass");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    LRHIRenderPassMetal3* render_pass = malloc(sizeof(LRHIRenderPassMetal3));
    render_pass->base.vtable = &lrhi_metal3_render_pass_vtable;
    render_pass->render_encoder = render_encoder;
    return (LRHIRenderPass)render_pass;
}

static void lrhi_metal3_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder endEncoding];
    free(metal_render_pass);
}

static void lrhi_metal3_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder memoryBarrierWithScope:MTLBarrierScopeRenderTargets|MTLBarrierScopeBuffers|MTLBarrierScopeTextures afterStages:lrhi_metal3_render_stages_to_mtl(afterStage) beforeStages:lrhi_metal3_render_stages_to_mtl(beforeStage)];
}

static void lrhi_metal3_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder barrierAfterQueueStages:lrhi_metal3_render_stage_to_mtl(beforeStage) beforeStages:lrhi_metal3_render_stage_to_mtl(afterStage)];
}

// Utils

static void lrhi_metal3_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error)
{
    if (info->width == 0 || info->height == 0 || info->depth == 0) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Texture dimensions must be greater than 0");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->mip_levels == 0) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Texture mip levels must be greater than 0");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->array_layers == 0) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Texture array layers must be greater than 0");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->format == LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Texture format must be defined");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_1D && info->height != 1) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "1D textures must have a height of 1");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_2D && info->depth != 1) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "2D textures must have a depth of 1");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D && info->array_layers != 1) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "3D textures must have array layers of 1");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    if (info->dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE && (info->width != info->height || info->depth != 1)) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Cube textures must have equal width and height, and a depth of 1");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }
}

static MTLLoadAction lrhi_metal3_load_action_to_mtl(LRHIRenderPassAction load_op)
{
    if (load_op == LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR) {
        return MTLLoadActionClear;
    } else if (load_op == LUMINARY_RHI_RENDER_PASS_ACTION_LOAD) {
        return MTLLoadActionLoad;
    } else {
        return MTLLoadActionDontCare;
    }
    return MTLLoadActionDontCare;
}

static MTLStoreAction lrhi_metal3_store_action_to_mtl(LRHIRenderPassAction store_op)
{
    if (store_op == LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR) {
        return MTLStoreActionStore;
    }
    return MTLStoreActionDontCare;
}

static MTLStages lrhi_metal3_render_stage_to_mtl(LRHIRenderStage stage)
{
    MTLStages mtl_stages = 0;
    if (stage & LUMINARY_RHI_RENDER_STAGE_VERTEX)   mtl_stages |= MTLStageVertex;
    if (stage & LUMINARY_RHI_RENDER_STAGE_FRAGMENT) mtl_stages |= MTLStageFragment;
    if (stage & LUMINARY_RHI_RENDER_STAGE_COMPUTE)  mtl_stages |= MTLStageDispatch;
    if (stage & LUMINARY_RHI_RENDER_STAGE_COPY)     mtl_stages |= MTLStageBlit;
    if (stage & LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD) mtl_stages |= MTLStageAccelerationStructure;
    if (stage & LUMINARY_RHI_RENDER_STAGE_MESH)    mtl_stages |= MTLStageMesh;
    if (stage & LUMINARY_RHI_RENDER_STAGE_TASK)    mtl_stages |= MTLStageObject;
    return mtl_stages;
}

static MTLRenderStages lrhi_metal3_render_stages_to_mtl(LRHIRenderStage stages)
{
    MTLRenderStages mtl_stages = 0;
    if (stages & LUMINARY_RHI_RENDER_STAGE_VERTEX)   mtl_stages |= MTLRenderStageVertex;
    if (stages & LUMINARY_RHI_RENDER_STAGE_FRAGMENT) mtl_stages |= MTLRenderStageFragment;
    if (stages & LUMINARY_RHI_RENDER_STAGE_MESH)    mtl_stages |= MTLRenderStageMesh;
    if (stages & LUMINARY_RHI_RENDER_STAGE_TASK)    mtl_stages |= MTLRenderStageObject;
    return mtl_stages;
}

static MTLPixelFormat lrhi_metal3_pixel_format(LRHITextureFormat format)
{
    switch (format)
    {
        case LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED:            return MTLPixelFormatInvalid;
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM:      return MTLPixelFormatRGBA8Unorm;
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB:       return MTLPixelFormatRGBA8Unorm_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM:      return MTLPixelFormatBGRA8Unorm;
        case LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT:  return MTLPixelFormatRGBA16Float;
        case LUMINARY_RHI_TEXTURE_FORMAT_R32G32B32A32_FLOAT:  return MTLPixelFormatRGBA32Float;
        case LUMINARY_RHI_TEXTURE_FORMAT_D24_UNORM_S8_UINT:   return MTLPixelFormatDepth24Unorm_Stencil8;
        case LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT:   return MTLPixelFormatDepth32Float_Stencil8;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC1_UNORM:           return MTLPixelFormatBC1_RGBA;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC3_UNORM:           return MTLPixelFormatBC3_RGBA;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC7_UNORM:           return MTLPixelFormatBC7_RGBAUnorm;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC1_SRGB:            return MTLPixelFormatBC1_RGBA_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC3_SRGB:            return MTLPixelFormatBC3_RGBA_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC7_SRGB:            return MTLPixelFormatBC7_RGBAUnorm_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_UNORM:      return MTLPixelFormatASTC_4x4_LDR;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_UNORM:      return MTLPixelFormatASTC_6x6_LDR;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_UNORM:      return MTLPixelFormatASTC_8x8_LDR;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_SRGB:       return MTLPixelFormatASTC_4x4_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_SRGB:       return MTLPixelFormatASTC_6x6_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_SRGB:       return MTLPixelFormatASTC_8x8_sRGB;
        default:                                               return MTLPixelFormatInvalid;
    }
}

static MTLTextureUsage lrhi_metal3_texture_usage(LRHITextureUsage usage)
{
    MTLTextureUsage metal_usage = 0;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_SAMPLED)       metal_usage |= MTLTextureUsageShaderRead;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_STORAGE)       metal_usage |= MTLTextureUsageShaderWrite;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET) metal_usage |= MTLTextureUsageRenderTarget;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL) metal_usage |= MTLTextureUsageRenderTarget;
    return metal_usage;
}

static MTLTextureType lrhi_metal3_texture_type(LRHITextureDimensions type)
{
    switch (type)
    {
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_1D:       return MTLTextureType1D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_2D:       return MTLTextureType2D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY: return MTLTextureType2DArray;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_3D:       return MTLTextureType3D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE:     return MTLTextureTypeCube;
        default:                                       return MTLTextureType2D;
    }
}

// Swap chain

static void lrhi_metal3_create_swap_chain(LRHIDevice device, LRHICommandQueue queue,
                                           LRHISwapChainInfo* info,
                                           LRHISwapChain* out_swap_chain,
                                           LRHIError* out_error)
{
    (void)device;
    (void)queue;  // Metal3 does not use the queue for presentation

    if (info->handle_type != LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message),
                     "Metal3 swap chain only supports METAL_LAYER handle type");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_swap_chain = NULL;
        return;
    }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)info->handle.metal_layer;
    layer.pixelFormat     = lrhi_metal3_pixel_format(info->format);
    layer.drawableSize    = CGSizeMake(info->width, info->height);
    layer.framebufferOnly = NO;

    LRHISwapChainMetal3* sc = malloc(sizeof(LRHISwapChainMetal3));
    sc->base.vtable      = &lrhi_metal3_swap_chain_vtable;
    sc->layer            = layer;
    sc->current_drawable = nil;
    sc->info             = *info;

    sc->current_texture.base.vtable = &lrhi_metal3_swap_chain_texture_vtable;
    sc->current_texture.texture     = nil;
    sc->current_texture.info        = (LRHITextureInfo){
        .width        = info->width,
        .height       = info->height,
        .depth        = 1,
        .mip_levels   = 1,
        .array_layers = 1,
        .format       = info->format,
        .usage        = LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
        .dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
    };

    *out_swap_chain = (LRHISwapChain)sc;
}

static void lrhi_metal3_destroy_swap_chain(LRHISwapChain swap_chain)
{
    LRHISwapChainMetal3* sc = (LRHISwapChainMetal3*)swap_chain;
    sc->current_drawable = nil;
    free(sc);
}

static LRHITexture lrhi_metal3_swap_chain_get_current_texture(LRHISwapChain swap_chain,
                                                               LRHIError* out_error)
{
    LRHISwapChainMetal3* sc = (LRHISwapChainMetal3*)swap_chain;
    sc->current_drawable = nil;  // discard any un-presented drawable

    id<CAMetalDrawable> drawable = [sc->layer nextDrawable];
    if (!drawable) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message),
                     "Metal3: nextDrawable returned nil");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    sc->current_drawable        = drawable;
    sc->current_texture.texture = drawable.texture;
    return (LRHITexture)&sc->current_texture;
}

static void lrhi_metal3_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error)
{
    LRHISwapChainMetal3* sc = (LRHISwapChainMetal3*)swap_chain;
    if (!sc->current_drawable) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message),
                     "Metal3: present called with no current drawable");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
        }
        return;
    }
    [sc->current_drawable present];
    sc->current_drawable        = nil;
    sc->current_texture.texture = nil;
}
