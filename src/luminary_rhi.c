#include "luminary_rhi_internal.h"

#include <stdio.h>

LRHIBackend lrhi_default_backend(void)
{
#ifdef LRHI_MACOS
    return LUMINARY_RHI_BACKEND_METAL4;
#elif defined(_WIN32)
    return LUMINARY_RHI_BACKEND_D3D12;
#else
    return LUMINARY_RHI_BACKEND_VULKAN;
#endif
}

void lrhi_create_device(LRHIBackend backend, LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    switch (backend) {
#if defined(LRHI_WINDOWS) || defined(LRHI_LINUX)
    case LUMINARY_RHI_BACKEND_VULKAN: lrhi_vulkan_create_device(out_device, enable_debug, out_error); return;
#endif
#ifdef LRHI_WINDOWS
    case LUMINARY_RHI_BACKEND_D3D12: lrhi_d3d12_create_device(out_device, enable_debug, out_error); return;
#endif
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

void lrhi_texture_set_name(LRHITexture texture, const char* name)
{
    ((LRHITextureBase*)texture)->vtable->texture_set_name(texture, name);
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
    if (!buffer) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Invalid buffer");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }
    return ((LRHIBufferBase*)buffer)->vtable->buffer_map(buffer, out_error);
}

void lrhi_buffer_unmap(LRHIBuffer buffer)
{
    if (!buffer) {
        return;
    }
    ((LRHIBufferBase*)buffer)->vtable->buffer_unmap(buffer);
}

void lrhi_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->buffer_readback(device, buffer, out_data, data_size, out_error);
}

void lrhi_buffer_set_name(LRHIBuffer buffer, const char* name)
{
    ((LRHIBufferBase*)buffer)->vtable->buffer_set_name(buffer, name);
}

void lrhi_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error)
{
    ((LRHIBufferBase*)buffer)->vtable->buffer_set_indirect_command_type(buffer, command_type, out_error);
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

void lrhi_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error)
{
    ((LRHICommandListBase*)command_list)->vtable->command_list_prepare_indirect_commands(command_list, indirect_command_buffer, count_buffer, maxCommandCount, parameters, pipeline, push_constants, push_constant_size, out_error);
}

LRHICopyPass lrhi_copy_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    return ((LRHICommandListBase*)command_list)->vtable->copy_pass_begin(command_list, out_error);;
}

void lrhi_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->copy_pass_end(copy_pass, out_error);
}

void lrhi_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->push_debug_group(copy_pass, label, out_error);
}

void lrhi_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error)
{
    ((LRHICopyPassBase*)copy_pass)->vtable->pop_debug_group(copy_pass, out_error);
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

void lrhi_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->add_blas(residency_set, blas, out_error);
}

void lrhi_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->add_tlas(residency_set, tlas, out_error);
}

void lrhi_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->remove_buffer(residency_set, buffer, out_error);
}

void lrhi_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->remove_blas(residency_set, blas, out_error);
}

void lrhi_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    ((LRHIResidencySetBase*)residency_set)->vtable->remove_tlas(residency_set, tlas, out_error);
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

void lrhi_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->push_debug_group(render_pass, label, out_error);
}

void lrhi_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->pop_debug_group(render_pass, out_error);
}

void lrhi_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->intra_barrier(render_pass, beforeStage, afterStage, out_error);
}

void lrhi_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->encoder_barrier(render_pass, beforeStage, afterStage, out_error);
}

void lrhi_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_shader_module(device, info, out_shader_module, out_error);
}

void lrhi_destroy_shader_module(LRHIShaderModule shader_module)
{
    ((LRHIShaderModuleBase*)shader_module)->vtable->destroy_shader_module(shader_module);
}

void lrhi_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info)
{
    ((LRHIShaderModuleBase*)shader_module)->vtable->get_shader_module_info(shader_module, out_info);
}

void lrhi_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_render_pipeline(device, info, out_pipeline, out_error);
}

void lrhi_destroy_render_pipeline(LRHIRenderPipeline pipeline)
{
    ((LRHIRenderPipelineBase*)pipeline)->vtable->destroy_render_pipeline(pipeline);
}

void lrhi_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info)
{
    ((LRHIRenderPipelineBase*)pipeline)->vtable->get_render_pipeline_info(pipeline, out_info);
}

uint64_t lrhi_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    return ((LRHIRenderPipelineBase*)pipeline)->vtable->get_alloc_size(pipeline, out_error);
}

void lrhi_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_mesh_pipeline(device, info, out_pipeline, out_error);
}

void lrhi_destroy_mesh_pipeline(LRHIMeshPipeline pipeline)
{
    ((LRHIMeshPipelineBase*)pipeline)->vtable->destroy_mesh_pipeline(pipeline);
}

void lrhi_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info)
{
    ((LRHIMeshPipelineBase*)pipeline)->vtable->get_mesh_pipeline_info(pipeline, out_info);
}

uint64_t lrhi_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    return ((LRHIMeshPipelineBase*)pipeline)->vtable->get_alloc_size(pipeline, out_error);
}

void lrhi_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->set_render_pipeline(render_pass, pipeline, out_error);
}

void lrhi_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->set_mesh_pipeline(render_pass, pipeline, out_error);
}

void lrhi_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->set_viewport(render_pass, x, y, width, height, min_depth, max_depth, out_error);
}

void lrhi_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->set_scissor(render_pass, x, y, width, height, out_error);
}

void lrhi_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->set_push_constants(render_pass, data, size, out_error);
}

void lrhi_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->draw(render_pass, vertex_count, instance_count, first_vertex, first_instance, out_error);
}

void lrhi_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->draw_indexed(render_pass, index_count, instance_count, first_index, vertex_offset, first_instance, index_buffer, index_stride, out_error);
}

void lrhi_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->draw_mesh_tasks(render_pass, num_groups_x, num_groups_y, num_groups_z, threads_per_object_group_x, threads_per_object_group_y, threads_per_object_group_z, threads_per_mesh_group_x, threads_per_mesh_group_y, threads_per_mesh_group_z, out_error);
}

void lrhi_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error)
{
    ((LRHIRenderPassBase*)render_pass)->vtable->execute_indirect_commands(render_pass, indirect_command_buffer, count_buffer, max_command_count, out_error);
}

void lrhi_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_compute_pipeline(device, info, out_pipeline, out_error);
}

void lrhi_destroy_compute_pipeline(LRHIComputePipeline pipeline)
{
    ((LRHIComputePipelineBase*)pipeline)->vtable->destroy_compute_pipeline(pipeline);
}

void lrhi_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info)
{
    ((LRHIComputePipelineBase*)pipeline)->vtable->get_compute_pipeline_info(pipeline, out_info);
}

uint64_t lrhi_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error)
{
    return ((LRHIComputePipelineBase*)pipeline)->vtable->get_alloc_size(pipeline, out_error);
}

LRHIComputePass lrhi_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    return ((LRHICommandListBase*)command_list)->vtable->compute_pass_begin(command_list, out_error);
}

void lrhi_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->end(compute_pass, out_error);
}

void lrhi_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->push_debug_group(compute_pass, label, out_error);
}

void lrhi_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->pop_debug_group(compute_pass, out_error);
}

void lrhi_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->barrier(compute_pass, out_error);
}

void lrhi_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->encoder_barrier(compute_pass, after_stage, out_error);
}

void lrhi_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->set_pipeline(compute_pass, pipeline, out_error);
}

void lrhi_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->set_push_constants(compute_pass, data, size, out_error);
}

void lrhi_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->dispatch(compute_pass, num_groups_x, num_groups_y, num_groups_z, threads_per_group_x, threads_per_group_y, threads_per_group_z, out_error);
}

void lrhi_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error)
{
    ((LRHIComputePassBase*)compute_pass)->vtable->dispatch_indirect(compute_pass, indirect_command_buffer, out_error);
}

void lrhi_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_buffer_view(device, info, out_buffer_view, out_error);
}

void lrhi_destroy_buffer_view(LRHIBufferView buffer_view)
{
    ((LRHIBufferViewBase*)buffer_view)->vtable->destroy_buffer_view(buffer_view);
}

void lrhi_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info)
{
    ((LRHIBufferViewBase*)buffer_view)->vtable->get_buffer_view_info(buffer_view, out_info);
}

uint32_t lrhi_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error)
{
    return ((LRHIBufferViewBase*)buffer_view)->vtable->get_bindless_index(buffer_view, out_error);
}

void lrhi_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_sampler(device, info, out_sampler, out_error);
}

void lrhi_destroy_sampler(LRHISampler sampler)
{
    ((LRHISamplerBase*)sampler)->vtable->destroy_sampler(sampler);
}

void lrhi_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info)
{
    ((LRHISamplerBase*)sampler)->vtable->get_sampler_info(sampler, out_info);
}

uint32_t lrhi_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error)
{
    return ((LRHISamplerBase*)sampler)->vtable->get_bindless_index(sampler, out_error);
}

void lrhi_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_bottom_level_acceleration_structure(device, info, out_blas, out_error);
}

void lrhi_destroy_bottom_level_acceleration_structure(LRHIBottomLevelAccelerationStructure blas)
{
    ((LRHIBLASBase*)blas)->vtable->destroy_bottom_level_acceleration_structure(blas);
}

void lrhi_get_bottom_level_acceleration_structure_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info)
{
    ((LRHIBLASBase*)blas)->vtable->get_bottom_level_acceleration_structure_info(blas, out_info);
}

LRHIAccelerationStructureBufferSizes lrhi_bottom_level_acceleration_structure_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    return ((LRHIBLASBase*)blas)->vtable->get_build_scratch_size(blas, out_error);
}

LRHIAccelerationStructurePass lrhi_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    return ((LRHICommandListBase*)command_list)->vtable->acceleration_structure_pass_begin(command_list, out_error);
}

void lrhi_acceleration_structure_pass_end(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->end(pass, out_error);
}

void lrhi_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->push_debug_group(pass, label, out_error);
}

void lrhi_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->pop_debug_group(pass, out_error);
}

void lrhi_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->barrier(pass, out_error);
}

void lrhi_acceleration_structure_encoder_barrier(LRHIAccelerationStructurePass pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->encoder_barrier(pass, after_stage, out_error);
}

void lrhi_acceleration_structure_pass_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->build_blas(pass, blas, scratch_buffer, scratch_offset, out_error);   
}

void lrhi_acceleration_structure_pass_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->build_tlas(pass, tlas, scratch_buffer, scratch_offset, out_error);
}

void lrhi_acceleration_structure_pass_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->write_compacted_blas_size(pass, blas, dst_buffer, dst_offset, out_error);
}

void lrhi_acceleration_structure_pass_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->compact_blas(pass, src_blas, dst_blas, out_error);
}

void lrhi_acceleration_structure_pass_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->refit_blas(pass, blas, scratch_buffer, scratch_offset, out_error);
}

void lrhi_acceleration_structure_pass_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->refit_tlas(pass, tlas, scratch_buffer, scratch_offset, out_error);
}

void lrhi_acceleration_structure_pass_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->copy_blas(pass, src_blas, dst_blas, out_error);
}

void lrhi_acceleration_structure_pass_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error)
{
    ((LRHIAccelerationStructurePassBase*)pass)->vtable->copy_tlas(pass, src_tlas, dst_tlas, out_error);
}

void lrhi_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_compacted_bottom_level_acceleration_structure(device, compacted_size, out_blas, out_error);
}

void lrhi_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error)
{
    ((LRHIDeviceBase*)device)->vtable->create_top_level_acceleration_structure(device, info, out_tlas, out_error);
}

void lrhi_destroy_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas)
{
    ((LRHITLASBase*)tlas)->vtable->destroy_top_level_acceleration_structure(tlas);
}

void lrhi_get_top_level_acceleration_structure_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info)
{
    ((LRHITLASBase*)tlas)->vtable->get_top_level_acceleration_structure_info(tlas, out_info);
}

uint64_t lrhi_top_level_acceleration_structure_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    return ((LRHITLASBase*)tlas)->vtable->get_bindless_index(tlas, out_error);
}

LRHIAccelerationStructureBufferSizes lrhi_top_level_acceleration_structure_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    return ((LRHITLASBase*)tlas)->vtable->get_build_scratch_size(tlas, out_error);
}

void lrhi_reset_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    ((LRHITLASBase*)tlas)->vtable->reset(tlas, out_error);
}

void lrhi_add_top_level_acceleration_structure_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error)
{
    ((LRHITLASBase*)tlas)->vtable->add_instance(tlas, instance_info, out_error);
}
