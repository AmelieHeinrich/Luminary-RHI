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

void lrhi_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error)
{
    ((LRHICommandQueueBase*)queue)->vtable->submit_command_lists(queue, command_lists, command_list_count, signal_fence, signal_value, wait_fence, wait_value, out_error);
}

void lrhi_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error)
{
    ((LRHICommandQueueBase*)queue)->vtable->add_residency_set(queue, residency_set, out_error);
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

void lrhi_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error)
{
    ((LRHICommandQueueBase*)queue)->vtable->create_command_list(queue, out_command_list, out_error);
}

void lrhi_destroy_command_list(LRHICommandList command_list)
{
    ((LRHICommandListBase*)command_list)->vtable->destroy_command_list(command_list);
}

void lrhi_command_list_begin(LRHICommandList command_list, LRHIError* out_error)
{
    ((LRHICommandListBase*)command_list)->vtable->command_list_begin(command_list, out_error);
}

void lrhi_command_list_end(LRHICommandList command_list, LRHIError* out_error)
{
    ((LRHICommandListBase*)command_list)->vtable->command_list_end(command_list, out_error);
}

void lrhi_command_list_reset(LRHICommandList command_list, LRHIError* out_error)
{
    ((LRHICommandListBase*)command_list)->vtable->command_list_reset(command_list, out_error);
}

LRHICopyPass lrhi_copy_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    return ((LRHICommandListBase*)command_list)->vtable->copy_pass_begin(command_list, out_error);;
}

void lrhi_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_pass_end(copy_pass, out_error);
}

void lrhi_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_pass_intra_barrier(copy_pass, out_error);
}

void lrhi_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_pass_encoder_barrier(copy_pass, afterStage, out_error);
}

void lrhi_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_buffer_to_buffer(copy_pass, src_buffer, src_offset, dst_buffer, dst_offset, size, out_error);
}

void lrhi_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_buffer_to_texture(copy_pass, src_buffer, src_offset, src_bytes_per_row, src_bytes_per_image, dst_texture, dst_region, dst_mip_level, dst_array_layer, out_error);
}

void lrhi_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_texture_to_buffer(copy_pass, src_texture, src_region, src_mip_level, src_array_layer, dst_buffer, dst_offset, dst_bytes_per_row, dst_bytes_per_image, out_error);
}

void lrhi_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_texture_to_texture(copy_pass, src_texture, src_region, src_mip_level, src_array_layer, dst_texture, dst_region, dst_mip_level, dst_array_layer, out_error);
}

void lrhi_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_residency_set(device, out_residency_set, out_error);
}

void lrhi_destroy_residency_set(LRHIResidencySet residency_set)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->destroy_residency_set(residency_set);
}

void lrhi_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->add_texture(residency_set, texture, out_error);
}

void lrhi_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->add_buffer(residency_set, buffer, out_error);
}

void lrhi_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->remove_texture(residency_set, texture, out_error);
}

void lrhi_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->remove_buffer(residency_set, buffer, out_error);
}

void lrhi_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->update(residency_set, out_error);
}

void lrhi_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_swap_chain(device, queue, info, out_swap_chain, out_error);
}

void lrhi_destroy_swap_chain(LRHISwapChain swap_chain)
{
    ((LRHISwapChainBase*)swap_chain)->vtable->destroy_swap_chain(swap_chain);
}

LRHITexture lrhi_swap_chain_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error)
{
    return ((LRHISwapChainBase*)swap_chain)->vtable->get_current_texture(swap_chain, out_error);
}

void lrhi_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error)
{
    ((LRHISwapChainBase*)swap_chain)->vtable->present(swap_chain, out_error);
}

void lrhi_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_texture_view(device, info, out_texture_view, out_error);
}

void lrhi_destroy_texture_view(LRHITextureView texture_view)
{
    ((LRHITextureViewBase*)texture_view)->vtable->destroy_texture_view(texture_view);
}

void lrhi_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info)
{
    ((LRHITextureViewBase*)texture_view)->vtable->get_texture_view_info(texture_view, out_info);
}

uint32_t lrhi_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error)
{
    return ((LRHITextureViewBase*)texture_view)->vtable->get_bindless_index(texture_view, out_error);
}

LRHIRenderPass lrhi_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error)
{
    return ((LRHICommandListBase*)command_list)->vtable->render_pass_begin(command_list, info, out_error);
}

void lrhi_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->end(render_pass, out_error);
}

void lrhi_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->intra_barrier(render_pass, beforeStage, afterStage, out_error);
}

void lrhi_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->encoder_barrier(render_pass, beforeStage, afterStage, out_error);
}
