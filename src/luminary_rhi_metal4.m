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

}

void lrhi_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    
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
