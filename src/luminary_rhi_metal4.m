#include "luminary_rhi.h"

#include <Metal/Metal.h>

#define CAST_METAL4(type, name) (type##Metal4*)name

// Utils
MTLPixelFormat lrhi_to_metal4_pixel_format(LRHITextureFormat format);
MTLTextureUsage lrhi_to_metal4_texture_usage(LRHITextureUsage usage);
MTLTextureType lrhi_to_metal4_texture_type(LRHITextureDimensions type);

// Validation
void lrhi_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error);

typedef struct LRHIDeviceMetal4 {
    id<MTLDevice> device;
    uint8_t enable_debug;
} LRHIDeviceMetal4;

typedef struct LRHITextureMetal4 {
    id<MTLTexture> texture;
    LRHITextureInfo info;
} LRHITextureMetal4;

typedef struct LRHIBufferMetal4 {
    id<MTLBuffer> buffer;
    LRHIBufferInfo info;
} LRHIBufferMetal4;

// Device
void lrhi_create_device(LRHIBackend backend, LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    if (backend != LUMINARY_RHI_BACKEND_METAL4) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Unsupported backend");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_device = NULL;
        return;
    }

    LRHIDeviceMetal4* device = malloc(sizeof(LRHIDeviceMetal4));
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

void lrhi_destroy_device(LRHIDevice device)
{
    free(device);
}

LRHIDeviceInfo lrhi_get_device_info(LRHIDevice device)
{
    LRHIDeviceMetal4* metal_device = CAST_METAL4(LRHIDevice, device);

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

void lrhi_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = CAST_METAL4(LRHIDevice, device);
    lrhi_validate_texture_info(info, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        *out_texture = NULL;
        return;
    }

    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.pixelFormat = lrhi_to_metal4_pixel_format(info->format);
    descriptor.width = info->width;
    descriptor.height = info->height;
    descriptor.depth = info->depth;
    descriptor.mipmapLevelCount = info->mip_levels;
    descriptor.arrayLength = info->array_layers;
    descriptor.storageMode = MTLStorageModeShared; // Apple Silicon only. If you have an Intel GPU, just buy a new Macbook :p
    descriptor.textureType = lrhi_to_metal4_texture_type(info->dimensions);
    descriptor.usage = lrhi_to_metal4_texture_usage(info->usage);
    
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
    out->texture = texture;
    out->info = *info;
    *out_texture = (LRHITexture)out;
}

void lrhi_destroy_texture(LRHITexture texture)
{
    free(texture);
}

void lrhi_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info)
{
    LRHITextureMetal4* metal_texture = CAST_METAL4(LRHITexture, texture);
    *out_info = metal_texture->info;
}

void lrhi_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO: Validate region, mip level, array layer, and data size against texture info

    MTLRegion metal_region = MTLRegionMake3D(region->x, region->y, region->z, region->width, region->height, region->depth);
    LRHITextureMetal4* metal_texture = CAST_METAL4(LRHITexture, texture);
    [metal_texture->texture replaceRegion:metal_region mipmapLevel:mip_level slice:array_layer withBytes:data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image];
}

void lrhi_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO: Validate region, mip level, array layer, and data size against texture info

    MTLRegion metal_region = MTLRegionMake3D(region->x, region->y, region->z, region->width, region->height, region->depth);
    LRHITextureMetal4* metal_texture = CAST_METAL4(LRHITexture, texture);
    [metal_texture->texture getBytes:out_data bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image fromRegion:metal_region mipmapLevel:mip_level slice:array_layer];
}

void lrhi_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // No synchronization needed with Metal since we're using shared storage mode, so we can just read the texture data directly without needing a staging buffer
    lrhi_texture_read_region(texture, region, mip_level, array_layer, out_data, data_size, bytes_per_row, bytes_per_image, out_error);
}

void lrhi_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
    // No synchronization needed with Metal since we're using shared storage mode, so we can just read the buffer data directly without needing a staging buffer
    LRHIBufferMetal4* metal_buffer = CAST_METAL4(LRHIBuffer, buffer);
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

// Buffers

void lrhi_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = CAST_METAL4(LRHIDevice, device);

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
    out->buffer = buffer;
    out->info = *info;
    *out_buffer = (LRHIBuffer)out;
}

void lrhi_destroy_buffer(LRHIBuffer buffer)
{
    free(buffer);
}

void lrhi_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info)
{
    LRHIBufferMetal4* metal_buffer = CAST_METAL4(LRHIBuffer, buffer);
    *out_info = metal_buffer->info;
}

void* lrhi_buffer_map(LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIBufferMetal4* metal_buffer = CAST_METAL4(LRHIBuffer, buffer);
    return [metal_buffer->buffer contents];

}
void lrhi_buffer_unmap(LRHIBuffer buffer)
{
    (void)buffer; // No-op since we're using shared storage mode
}

// Utils

void lrhi_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error)
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

MTLPixelFormat lrhi_to_metal4_pixel_format(LRHITextureFormat format)
{
    switch (format)
    {
        case LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED: return MTLPixelFormatInvalid;
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM: return MTLPixelFormatRGBA8Unorm;
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM: return MTLPixelFormatBGRA8Unorm;
        case LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT: return MTLPixelFormatRGBA16Float;
        case LUMINARY_RHI_TEXTURE_FORMAT_R32G32B32A32_FLOAT: return MTLPixelFormatRGBA32Float;
        case LUMINARY_RHI_TEXTURE_FORMAT_D24_UNORM_S8_UINT: return MTLPixelFormatDepth24Unorm_Stencil8;
        case LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT: return MTLPixelFormatDepth32Float_Stencil8;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC1_UNORM: return MTLPixelFormatBC1_RGBA;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC3_UNORM: return MTLPixelFormatBC3_RGBA;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC7_UNORM: return MTLPixelFormatBC7_RGBAUnorm;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC1_SRGB: return MTLPixelFormatBC1_RGBA_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC3_SRGB: return MTLPixelFormatBC3_RGBA_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC7_SRGB: return MTLPixelFormatBC7_RGBAUnorm_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_UNORM: return MTLPixelFormatASTC_4x4_LDR;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_UNORM: return MTLPixelFormatASTC_6x6_LDR;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_UNORM: return MTLPixelFormatASTC_8x8_LDR;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_SRGB: return MTLPixelFormatASTC_4x4_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_SRGB: return MTLPixelFormatASTC_6x6_sRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_SRGB: return MTLPixelFormatASTC_8x8_sRGB;
        default: return MTLPixelFormatInvalid;
    }
    return MTLPixelFormatInvalid;
}

MTLTextureUsage lrhi_to_metal4_texture_usage(LRHITextureUsage usage)
{
    MTLTextureUsage metal_usage = 0;
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_SAMPLED) {
        metal_usage |= MTLTextureUsageShaderRead;
    }
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_STORAGE) {
        metal_usage |= MTLTextureUsageShaderWrite;
    }
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET) {
        metal_usage |= MTLTextureUsageRenderTarget;
    }
    if (usage & LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL) {
        metal_usage |= MTLTextureUsageRenderTarget;
    }
    return metal_usage;
}

MTLTextureType lrhi_to_metal4_texture_type(LRHITextureDimensions type)
{
    switch (type)
    {
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_1D: return MTLTextureType1D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_2D: return MTLTextureType2D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY: return MTLTextureType2DArray;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_3D: return MTLTextureType3D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE: return MTLTextureTypeCube;
        default: return MTLTextureType2D; // Default to 2D for invalid/undefined types
    }
}
