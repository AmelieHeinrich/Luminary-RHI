#include "luminary_rhi_internal.h"

#include <Metal/Metal.h>
#include <dispatch/dispatch.h>

typedef struct LRHIDeviceMetal4 {
    LRHIDeviceBase base;
    id<MTLDevice> device;
    uint8_t enable_debug;
} LRHIDeviceMetal4;

typedef struct LRHITextureMetal4 {
    LRHITextureBase base;
    id<MTLTexture> texture;
    LRHITextureInfo info;
} LRHITextureMetal4;

typedef struct LRHIBufferMetal4 {
    LRHIBufferBase base;
    id<MTLBuffer> buffer;
    LRHIBufferInfo info;
} LRHIBufferMetal4;

typedef struct LRHICommandQueueMetal4 {
    LRHICommandQueueBase base;
    id<MTL4CommandQueue> queue;
    id<MTLDevice> device;
} LRHICommandQueueMetal4;

typedef struct LRHIFenceMetal4 {
    LRHIFenceBase base;
    id<MTLSharedEvent> event;
} LRHIFenceMetal4;

// Forward declarations
static MTLPixelFormat  lrhi_metal4_pixel_format(LRHITextureFormat format);
static MTLTextureUsage lrhi_metal4_texture_usage(LRHITextureUsage usage);
static MTLTextureType  lrhi_metal4_texture_type(LRHITextureDimensions type);
static void            lrhi_metal4_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error);

static void            lrhi_metal4_destroy_device(LRHIDevice device);
static LRHIDeviceInfo  lrhi_metal4_get_device_info(LRHIDevice device);
static void            lrhi_metal4_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
static void            lrhi_metal4_destroy_texture(LRHITexture texture);
static void            lrhi_metal4_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
static void            lrhi_metal4_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal4_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal4_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal4_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
static void            lrhi_metal4_destroy_buffer(LRHIBuffer buffer);
static void            lrhi_metal4_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
static void*           lrhi_metal4_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal4_buffer_unmap(LRHIBuffer buffer);
static void            lrhi_metal4_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);

static void            lrhi_metal4_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
static void            lrhi_metal4_destroy_command_queue(LRHICommandQueue queue);
static void            lrhi_metal4_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
static void            lrhi_metal4_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

static void            lrhi_metal4_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
static void            lrhi_metal4_destroy_fence(LRHIFence fence);
static uint64_t        lrhi_metal4_fence_get_value(LRHIFence fence);
static void            lrhi_metal4_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);
static void            lrhi_metal4_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

// Vtable instances

static const LRHIDeviceVTable lrhi_metal4_device_vtable = {
    .destroy_device       = lrhi_metal4_destroy_device,
    .get_device_info      = lrhi_metal4_get_device_info,
    .create_texture       = lrhi_metal4_create_texture,
    .create_buffer        = lrhi_metal4_create_buffer,
    .texture_readback     = lrhi_metal4_texture_readback,
    .buffer_readback      = lrhi_metal4_buffer_readback,
    .create_command_queue = lrhi_metal4_create_command_queue,
    .create_fence         = lrhi_metal4_create_fence,
};

static const LRHICommandQueueVTable lrhi_metal4_command_queue_vtable = {
    .destroy_command_queue = lrhi_metal4_destroy_command_queue,
    .signal_fence          = lrhi_metal4_command_queue_signal,
    .wait_fence            = lrhi_metal4_command_queue_wait,
};

static const LRHIFenceVTable lrhi_metal4_fence_vtable = {
    .destroy_fence = lrhi_metal4_destroy_fence,
    .get_value     = lrhi_metal4_fence_get_value,
    .signal        = lrhi_metal4_fence_signal,
    .wait          = lrhi_metal4_fence_wait,
};

static const LRHITextureVTable lrhi_metal4_texture_vtable = {
    .destroy_texture        = lrhi_metal4_destroy_texture,
    .get_texture_info       = lrhi_metal4_get_texture_info,
    .texture_replace_region = lrhi_metal4_texture_replace_region,
    .texture_read_region    = lrhi_metal4_texture_read_region,
};

static const LRHIBufferVTable lrhi_metal4_buffer_vtable = {
    .destroy_buffer  = lrhi_metal4_destroy_buffer,
    .get_buffer_info = lrhi_metal4_get_buffer_info,
    .buffer_map      = lrhi_metal4_buffer_map,
    .buffer_unmap    = lrhi_metal4_buffer_unmap,
};

// Device

void lrhi_metal4_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    LRHIDeviceMetal4* device = malloc(sizeof(LRHIDeviceMetal4));
    device->base.vtable = &lrhi_metal4_device_vtable;
    device->device = MTLCreateSystemDefaultDevice();
    device->enable_debug = enable_debug;
    if ([device->device supportsFamily:MTLGPUFamilyMetal4] == NO) {
        free(device);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Metal 4 is not supported on this device");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_device = NULL;
        return;
    }
    *out_device = (LRHIDevice)device;
}

static void lrhi_metal4_destroy_device(LRHIDevice device)
{
    free(device);
}

static LRHIDeviceInfo lrhi_metal4_get_device_info(LRHIDevice device)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

    LRHIDeviceInfo info = {0};
    info.backend = LUMINARY_RHI_BACKEND_METAL4;
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

static void lrhi_metal4_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    lrhi_metal4_validate_texture_info(info, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        *out_texture = NULL;
        return;
    }

    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.pixelFormat = lrhi_metal4_pixel_format(info->format);
    descriptor.width = info->width;
    descriptor.height = info->height;
    descriptor.depth = info->depth;
    descriptor.mipmapLevelCount = info->mip_levels;
    descriptor.arrayLength = info->array_layers;
    descriptor.storageMode = MTLStorageModeShared; // Apple Silicon only. If you have an Intel GPU, just buy a new Macbook :p
    descriptor.textureType = lrhi_metal4_texture_type(info->dimensions);
    descriptor.usage = lrhi_metal4_texture_usage(info->usage);

    id<MTLTexture> texture = [metal_device->device newTextureWithDescriptor:descriptor];
    if (!texture) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create texture");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_texture = NULL;
        return;
    }

    LRHITextureMetal4* out = malloc(sizeof(LRHITextureMetal4));
    out->base.vtable = &lrhi_metal4_texture_vtable;
    out->texture = texture;
    out->info = *info;
    *out_texture = (LRHITexture)out;
}

static void lrhi_metal4_destroy_texture(LRHITexture texture)
{
    free(texture);
}

static void lrhi_metal4_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info)
{
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)texture;
    *out_info = metal_texture->info;
}

static void lrhi_metal4_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO: Validate region, mip level, array layer, and data size against texture info
    MTLRegion metal_region = MTLRegionMake3D(region->x, region->y, region->z, region->width, region->height, region->depth);
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)texture;
    [metal_texture->texture replaceRegion:metal_region mipmapLevel:mip_level slice:array_layer withBytes:data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image];
}

static void lrhi_metal4_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO: Validate region, mip level, array layer, and data size against texture info
    MTLRegion metal_region = MTLRegionMake3D(region->x, region->y, region->z, region->width, region->height, region->depth);
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)texture;
    [metal_texture->texture getBytes:out_data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image fromRegion:metal_region mipmapLevel:mip_level slice:array_layer];
}

static void lrhi_metal4_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // No synchronization needed with Metal since we're using shared storage mode
    lrhi_metal4_texture_read_region(texture, region, mip_level, array_layer, out_data, data_size, bytes_per_row, bytes_per_image, out_error);
}

// Buffers

static void lrhi_metal4_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

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

    LRHIBufferMetal4* out = malloc(sizeof(LRHIBufferMetal4));
    out->base.vtable = &lrhi_metal4_buffer_vtable;
    out->buffer = buffer;
    out->info = *info;
    *out_buffer = (LRHIBuffer)out;
}

static void lrhi_metal4_destroy_buffer(LRHIBuffer buffer)
{
    free(buffer);
}

static void lrhi_metal4_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info)
{
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;
    *out_info = metal_buffer->info;
}

static void* lrhi_metal4_buffer_map(LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;
    return [metal_buffer->buffer contents];
}

static void lrhi_metal4_buffer_unmap(LRHIBuffer buffer)
{
    (void)buffer; // No-op since we're using shared storage mode
}

static void lrhi_metal4_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
    // No synchronization needed with Metal since we're using shared storage mode
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;
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

static void lrhi_metal4_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    id<MTLCommandQueue> base_queue = [metal_device->device newCommandQueue];
    if (!base_queue) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command queue");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_queue = NULL;
        return;
    }

    LRHICommandQueueMetal4* out = malloc(sizeof(LRHICommandQueueMetal4));
    out->base.vtable = &lrhi_metal4_command_queue_vtable;
    out->queue = (id<MTL4CommandQueue>)base_queue;
    out->device = metal_device->device;
    *out_queue = (LRHICommandQueue)out;
}

static void lrhi_metal4_destroy_command_queue(LRHICommandQueue queue)
{
    free(queue);
}

static void lrhi_metal4_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    (void)out_error;
    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    [metal_queue->queue signalEvent:metal_fence->event value:value];
}

static void lrhi_metal4_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    (void)timeout_ns;
    (void)out_error;
    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    [metal_queue->queue waitForEvent:metal_fence->event value:value];
}

static void lrhi_metal4_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    id<MTLSharedEvent> event = [metal_device->device newSharedEvent];
    if (!event) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create shared event for fence");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_fence = NULL;
        return;
    }

    event.signaledValue = initial_value;

    LRHIFenceMetal4* out = malloc(sizeof(LRHIFenceMetal4));
    out->base.vtable = &lrhi_metal4_fence_vtable;
    out->event = event;
    *out_fence = (LRHIFence)out;
}

static void lrhi_metal4_destroy_fence(LRHIFence fence)
{
    free(fence);
}

static uint64_t lrhi_metal4_fence_get_value(LRHIFence fence)
{
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    return metal_fence->event.signaledValue;
}

static void lrhi_metal4_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    (void)out_error;
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    metal_fence->event.signaledValue = value;
}

static void lrhi_metal4_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    if (metal_fence->event.signaledValue >= value)
        return;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    MTLSharedEventListener* listener = [[MTLSharedEventListener alloc] init];
    [metal_fence->event notifyListener:listener atValue:value block:^(id<MTLSharedEvent> e, uint64_t v) {
        (void)e;
        (void)v;
        dispatch_semaphore_signal(sem);
    }];

    dispatch_time_t deadline = (timeout_ns == UINT64_MAX)
        ? DISPATCH_TIME_FOREVER
        : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ns);

    if (dispatch_semaphore_wait(sem, deadline) != 0 && out_error) {
        snprintf(out_error->message, sizeof(out_error->message), "Fence wait timed out");
        out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
    }
}

// Utils

static void lrhi_metal4_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error)
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

static MTLPixelFormat lrhi_metal4_pixel_format(LRHITextureFormat format)
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

static MTLTextureUsage lrhi_metal4_texture_usage(LRHITextureUsage usage)
{
    MTLTextureUsage metal_usage = 0;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_SAMPLED)       metal_usage |= MTLTextureUsageShaderRead;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_STORAGE)       metal_usage |= MTLTextureUsageShaderWrite;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET) metal_usage |= MTLTextureUsageRenderTarget;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL) metal_usage |= MTLTextureUsageRenderTarget;
    return metal_usage;
}

static MTLTextureType lrhi_metal4_texture_type(LRHITextureDimensions type)
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
