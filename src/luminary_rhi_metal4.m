#include "luminary_rhi.h"

#include <Metal/Metal.h>

#define CAST_METAL4(type, name) (type##Metal4*)name

typedef struct LRHIDeviceMetal4 {
    id<MTLDevice> device;
    uint8_t enable_debug;
} LRHIDeviceMetal4;

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
