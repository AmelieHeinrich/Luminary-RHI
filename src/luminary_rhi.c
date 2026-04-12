#include "luminary_rhi_internal.h"

#include <stdio.h>

void lrhi_create_device(LRHIBackend backend, LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    switch (backend) {
#ifdef LRHI_MACOS
        case LUMINARY_RHI_BACKEND_METAL3: lrhi_metal3_create_device(out_device, enable_debug, out_error); return;
        case LUMINARY_RHI_BACKEND_METAL4: lrhi_metal4_create_device(out_device, enable_debug, out_error); return;
#endif
        default:
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Unsupported backend");
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
    }
}

void lrhi_destroy_device(LRHIDevice device)
{
    ((LRHIDeviceBase*)device)->vtable->destroy_device(device);
}

LRHIDeviceInfo lrhi_get_device_info(LRHIDevice device)
{
    return ((LRHIDeviceBase*)device)->vtable->get_device_info(device);
}

void lrhi_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_texture(device, info, out_texture, out_error);
}

void lrhi_destroy_texture(LRHITexture texture)
{
    ((LRHITextureBase*)texture)->vtable->destroy_texture(texture);
}

void lrhi_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info)
{
    ((LRHITextureBase*)texture)->vtable->get_texture_info(texture, out_info);
}

void lrhi_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    ((LRHITextureBase*)texture)->vtable->texture_replace_region(texture, region, mip_level, array_layer, data, data_size, bytes_per_row, bytes_per_image, out_error);
}

void lrhi_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    ((LRHITextureBase*)texture)->vtable->texture_read_region(texture, region, mip_level, array_layer, out_data, data_size, bytes_per_row, bytes_per_image, out_error);
}

void lrhi_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->texture_readback(device, texture, region, mip_level, array_layer, out_data, data_size, bytes_per_row, bytes_per_image, out_error);
}

void lrhi_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_buffer(device, info, out_buffer, out_error);
}

void lrhi_destroy_buffer(LRHIBuffer buffer)
{
    ((LRHIBufferBase*)buffer)->vtable->destroy_buffer(buffer);
}

void lrhi_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info)
{
    ((LRHIBufferBase*)buffer)->vtable->get_buffer_info(buffer, out_info);
}

void* lrhi_buffer_map(LRHIBuffer buffer, LRHIError* out_error)
{
    return ((LRHIBufferBase*)buffer)->vtable->buffer_map(buffer, out_error);
}

void lrhi_buffer_unmap(LRHIBuffer buffer)
{
    ((LRHIBufferBase*)buffer)->vtable->buffer_unmap(buffer);
}

void lrhi_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->buffer_readback(device, buffer, out_data, data_size, out_error);
}

void lrhi_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_command_queue(device, out_queue, out_error);
}

void lrhi_destroy_command_queue(LRHICommandQueue queue)
{
    ((LRHICommandQueueBase*)queue)->vtable->destroy_command_queue(queue);
}

void lrhi_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    ((LRHICommandQueueBase*)queue)->vtable->signal_fence(queue, fence, value, out_error);
}

void lrhi_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    ((LRHICommandQueueBase*)queue)->vtable->wait_fence(queue, fence, value, timeout_ns, out_error);
}

void lrhi_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_fence(device, initial_value, out_fence, out_error);
}

void lrhi_destroy_fence(LRHIFence fence)
{
    ((LRHIFenceBase*)fence)->vtable->destroy_fence(fence);
}

uint64_t lrhi_fence_get_value(LRHIFence fence)
{
    return ((LRHIFenceBase*)fence)->vtable->get_value(fence);
}

void lrhi_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    ((LRHIFenceBase*)fence)->vtable->signal(fence, value, out_error);
}

void lrhi_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    ((LRHIFenceBase*)fence)->vtable->wait(fence, value, timeout_ns, out_error);
}
