#include "luminary_rhi.h"
#include "luminary_rhi_internal.h"

#include <stdio.h>
#include <string.h>

#include "ext/volk.h"
#include "ext/vk_mem_alloc.h"

// ─── Backend structs ────────────────────────────────────────────────────────

typedef struct LRHIDeviceVk {
    LRHIDeviceBase base;
    LRHIDeviceInfo info;
    uint8_t enable_debug;

    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VmaAllocator allocator;

    uint32_t graphics_queue_family_index;

    // TODO: global descriptor set layout/descriptor set
} LRHIDeviceVk;

typedef struct LRHITextureVk {
    LRHITextureBase base;
    LRHITextureInfo info;

    LRHIDeviceVk* device_ref;
    VkImage image;
    VmaAllocation allocation;
    uint8_t is_swapchain_image;
} LRHITextureVk;

typedef struct LRHIBufferVk {
    LRHIBufferBase base;
    LRHIBufferInfo info;

    LRHIDeviceVk* device_ref;
    LRHICommandType indirect_command_type;
    VkBuffer buffer;
    VmaAllocation allocation;
    void* mapped_ptr;
} LRHIBufferVk;

typedef struct LRHICommandQueueVk {
    LRHICommandQueueBase base;

    LRHIDeviceVk* device_ref;
    VkQueue queue;
    uint32_t queue_family_index;
    VkCommandPool command_pool;
} LRHICommandQueueVk;

typedef struct LRHIFenceVk {
    LRHIFenceBase base;

    LRHIDeviceVk* device_ref;
    uint64_t value;
    VkSemaphore timeline_semaphore;
} LRHIFenceVk;

typedef struct LRHICommandListVk {
    LRHICommandListBase base;

    LRHIDeviceVk* device_ref;
    LRHICommandQueueVk* queue_ref;
    VkCommandBuffer command_buffer;
    uint8_t push_constants[128];
} LRHICommandListVk;

typedef struct LRHICopyPassVk {
    LRHICopyPassBase base;

    VkCommandBuffer command_buffer; // reference to parent cmd list
} LRHICopyPassVk;

typedef struct LRHIResidencySetVk {
    LRHIResidencySetBase base;
} LRHIResidencySetVk;

typedef struct LRHISwapChainVk {
    LRHISwapChainBase base;
    LRHISwapChainInfo info;

    LRHIDeviceVk* device_ref;
    VkSurfaceKHR surface;
    VkSwapchainKHR swap_chain;
    uint32_t image_count;
    VkImage* images;
    LRHITextureVk* texture_wrappers;
    uint32_t current_image_index;
} LRHISwapChainVk;

typedef struct LRHITextureViewVk {
    LRHITextureViewBase base;
    LRHITextureViewInfo info;

    LRHIDeviceVk* device_ref;
    VkImageView image_view;
    uint32_t bindless_index;
} LRHITextureViewVk;

typedef struct LRHIRenderPassVk {
    LRHIRenderPassBase base;

    VkCommandBuffer command_buffer; // reference to parent cmd list
    uint8_t push_constants[128];
} LRHIRenderPassVk;

typedef struct LRHIShaderModuleVk {
    LRHIShaderModuleBase base;
    LRHIShaderModuleInfo info;

    VkShaderModule shader_module;
} LRHIShaderModuleVk;

typedef struct LRHIRenderPipelineVk {
    LRHIRenderPipelineBase base;
    LRHIRenderPipelineInfo info;

    VkPipeline pipeline;
} LRHIRenderPipelineVk;

typedef struct LRHIMeshPipelineVk {
    LRHIMeshPipelineBase base;
    LRHIMeshPipelineInfo info;

    VkPipeline pipeline;
} LRHIMeshPipelineVk;

typedef struct LRHIComputePipelineVk {
    LRHIComputePipelineBase base;
    LRHIComputePipelineInfo info;

    VkPipeline pipeline;
} LRHIComputePipelineVk;

typedef struct LRHIComputePassVk {
    LRHIComputePassBase base;

    VkCommandBuffer command_buffer; // reference to parent cmd list
    uint8_t push_constants[128];
} LRHIComputePassVk;

typedef struct LRHIBufferViewVk {
    LRHIBufferViewBase base;
    LRHIBufferViewInfo info;

    LRHIDeviceVk* device_ref;
    uint32_t bindless_index;
} LRHIBufferViewVk;

typedef struct LRHISamplerVk {
    LRHISamplerBase base;
    LRHISamplerInfo info;

    LRHIDeviceVk* device_ref;
    VkSampler sampler;
    uint32_t bindless_index;
} LRHISamplerVk;

typedef struct LRHIAccelerationStructurePassVk {
    LRHIAccelerationStructurePassBase base;

    VkCommandBuffer command_buffer; // reference to parent cmd list
} LRHIAccelerationStructurePassVk;

typedef struct LRHIBLASVk {
    LRHIBLASBase base;
    LRHIBLASInfo info;

    // TODO
} LRHIBLASVk;

typedef struct LRHITLASVk {
    LRHITLASBase base;
    LRHITLASInfo info;

    // TODO
} LRHITLASVk;

// ─── Helpers ───────────────────────────────────────────────────────────────
static int vk_result_to_lrhi(VkResult result, LRHIError* error)
{
    if (result != VK_SUCCESS) {
        sprintf(error->message, "VkResult: %d", result);
        error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        return 1;
    } else {
        error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
        return 0;
    }
}

// ─── Forward declarations ───────────────────────────────────────────────────

// Device
static void           lrhi_vk_destroy_device(LRHIDevice device);
static LRHIDeviceInfo lrhi_vk_get_device_info(LRHIDevice device);
static void           lrhi_vk_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
static void           lrhi_vk_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
static void           lrhi_vk_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void           lrhi_vk_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);
static void           lrhi_vk_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
static void           lrhi_vk_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
static void           lrhi_vk_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error);
static void           lrhi_vk_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error);
static void           lrhi_vk_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error);
static void           lrhi_vk_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error);
static void           lrhi_vk_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error);
static void           lrhi_vk_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error);
static void           lrhi_vk_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error);
static void           lrhi_vk_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error);
static void           lrhi_vk_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error);
static void           lrhi_vk_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error);
static void           lrhi_vk_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error);
static void           lrhi_vk_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error);

// Command queue
static void lrhi_vk_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error);
static void lrhi_vk_destroy_command_queue(LRHICommandQueue queue);
static void lrhi_vk_signal_fence(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
static void lrhi_vk_wait_fence(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
static void lrhi_vk_submit_command_lists(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error);
static void lrhi_vk_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error);

// Fence
static void     lrhi_vk_destroy_fence(LRHIFence fence);
static uint64_t lrhi_vk_fence_get_value(LRHIFence fence);
static void     lrhi_vk_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);
static void     lrhi_vk_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

// Texture
static void lrhi_vk_destroy_texture(LRHITexture texture);
static void lrhi_vk_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
static void lrhi_vk_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void lrhi_vk_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void lrhi_vk_texture_set_name(LRHITexture texture, const char* name);

// Buffer
static void  lrhi_vk_destroy_buffer(LRHIBuffer buffer);
static void  lrhi_vk_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
static void* lrhi_vk_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
static void  lrhi_vk_buffer_unmap(LRHIBuffer buffer);
static void  lrhi_vk_buffer_set_name(LRHIBuffer buffer, const char* name);
static void  lrhi_vk_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error);

// Command list
static void              lrhi_vk_destroy_command_list(LRHICommandList command_list);
static void              lrhi_vk_command_list_begin(LRHICommandList command_list, LRHIError* out_error);
static void              lrhi_vk_command_list_end(LRHICommandList command_list, LRHIError* out_error);
static void              lrhi_vk_command_list_reset(LRHICommandList command_list, LRHIError* out_error);
static void              lrhi_vk_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error);
static LRHICopyPass      lrhi_vk_copy_pass_begin(LRHICommandList command_list, LRHIError* out_error);
static LRHIRenderPass    lrhi_vk_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error);
static LRHIComputePass   lrhi_vk_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error);
static LRHIAccelerationStructurePass lrhi_vk_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error);

// Copy pass
static void lrhi_vk_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error);
static void lrhi_vk_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error);
static void lrhi_vk_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error);
static void lrhi_vk_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error);
static void lrhi_vk_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error);
static void lrhi_vk_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error);
static void lrhi_vk_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
static void lrhi_vk_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error);
static void lrhi_vk_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);

// Residency set
static void lrhi_vk_destroy_residency_set(LRHIResidencySet residency_set);
static void lrhi_vk_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void lrhi_vk_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void lrhi_vk_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);
static void lrhi_vk_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void lrhi_vk_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void lrhi_vk_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void lrhi_vk_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);
static void lrhi_vk_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void lrhi_vk_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error);

// Swap chain
static void        lrhi_vk_destroy_swap_chain(LRHISwapChain swap_chain);
static LRHITexture lrhi_vk_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error);
static void        lrhi_vk_present(LRHISwapChain swap_chain, LRHIError* out_error);

// Texture view
static void     lrhi_vk_destroy_texture_view(LRHITextureView texture_view);
static void     lrhi_vk_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info);
static uint32_t lrhi_vk_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error);

// Render pass
static void lrhi_vk_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error);
static void lrhi_vk_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error);
static void lrhi_vk_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error);
static void lrhi_vk_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void lrhi_vk_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void lrhi_vk_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error);
static void lrhi_vk_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error);
static void lrhi_vk_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error);
static void lrhi_vk_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error);
static void lrhi_vk_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error);
static void lrhi_vk_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error);
static void lrhi_vk_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error);
static void lrhi_vk_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error);
static void lrhi_vk_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error);

// Shader module
static void lrhi_vk_destroy_shader_module(LRHIShaderModule shader_module);
static void lrhi_vk_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info);

// Render pipeline
static void     lrhi_vk_destroy_render_pipeline(LRHIRenderPipeline pipeline);
static void     lrhi_vk_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info);
static uint64_t lrhi_vk_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error);

// Mesh pipeline
static void     lrhi_vk_destroy_mesh_pipeline(LRHIMeshPipeline pipeline);
static void     lrhi_vk_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info);
static uint64_t lrhi_vk_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error);

// Compute pipeline
static void     lrhi_vk_destroy_compute_pipeline(LRHIComputePipeline pipeline);
static void     lrhi_vk_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info);
static uint64_t lrhi_vk_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error);

// Compute pass
static void lrhi_vk_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error);
static void lrhi_vk_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error);
static void lrhi_vk_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error);
static void lrhi_vk_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error);
static void lrhi_vk_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_vk_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error);
static void lrhi_vk_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error);
static void lrhi_vk_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error);
static void lrhi_vk_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error);

// Buffer view
static void     lrhi_vk_destroy_buffer_view(LRHIBufferView buffer_view);
static void     lrhi_vk_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info);
static uint32_t lrhi_vk_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error);

// Sampler
static void     lrhi_vk_destroy_sampler(LRHISampler sampler);
static void     lrhi_vk_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info);
static uint32_t lrhi_vk_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error);

// Acceleration structure pass
static void lrhi_vk_acceleration_structure_pass_end(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void lrhi_vk_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error);
static void lrhi_vk_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void lrhi_vk_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void lrhi_vk_acceleration_structure_pass_encoder_barrier(LRHIAccelerationStructurePass pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_vk_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_vk_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_vk_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error);
static void lrhi_vk_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error);
static void lrhi_vk_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_vk_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_vk_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error);
static void lrhi_vk_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error);

// BLAS
static void                           lrhi_vk_destroy_blas(LRHIBottomLevelAccelerationStructure blas);
static void                           lrhi_vk_get_blas_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info);
static LRHIAccelerationStructureBufferSizes lrhi_vk_blas_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);

// TLAS
static void                           lrhi_vk_destroy_tlas(LRHITopLevelAccelerationStructure tlas);
static void                           lrhi_vk_get_tlas_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info);
static uint64_t                       lrhi_vk_tlas_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static LRHIAccelerationStructureBufferSizes lrhi_vk_tlas_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void                           lrhi_vk_tlas_reset(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void                           lrhi_vk_tlas_add_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error);

// ─── VTables ────────────────────────────────────────────────────────────────

static const LRHIDeviceVTable s_device_vtable = {
    .destroy_device                                    = lrhi_vk_destroy_device,
    .get_device_info                                   = lrhi_vk_get_device_info,
    .create_texture                                    = lrhi_vk_create_texture,
    .create_buffer                                     = lrhi_vk_create_buffer,
    .texture_readback                                  = lrhi_vk_texture_readback,
    .buffer_readback                                   = lrhi_vk_buffer_readback,
    .create_command_queue                              = lrhi_vk_create_command_queue,
    .create_fence                                      = lrhi_vk_create_fence,
    .create_residency_set                              = lrhi_vk_create_residency_set,
    .create_swap_chain                                 = lrhi_vk_create_swap_chain,
    .create_texture_view                               = lrhi_vk_create_texture_view,
    .create_shader_module                              = lrhi_vk_create_shader_module,
    .create_render_pipeline                            = lrhi_vk_create_render_pipeline,
    .create_mesh_pipeline                              = lrhi_vk_create_mesh_pipeline,
    .create_compute_pipeline                           = lrhi_vk_create_compute_pipeline,
    .create_buffer_view                                = lrhi_vk_create_buffer_view,
    .create_sampler                                    = lrhi_vk_create_sampler,
    .create_bottom_level_acceleration_structure        = lrhi_vk_create_bottom_level_acceleration_structure,
    .create_compacted_bottom_level_acceleration_structure = lrhi_vk_create_compacted_bottom_level_acceleration_structure,
    .create_top_level_acceleration_structure           = lrhi_vk_create_top_level_acceleration_structure,
};

static const LRHICommandQueueVTable s_command_queue_vtable = {
    .create_command_list   = lrhi_vk_create_command_list,
    .destroy_command_queue = lrhi_vk_destroy_command_queue,
    .signal_fence          = lrhi_vk_signal_fence,
    .wait_fence            = lrhi_vk_wait_fence,
    .submit_command_lists  = lrhi_vk_submit_command_lists,
    .add_residency_set     = lrhi_vk_add_residency_set,
};

static const LRHIFenceVTable s_fence_vtable = {
    .destroy_fence = lrhi_vk_destroy_fence,
    .get_value     = lrhi_vk_fence_get_value,
    .signal        = lrhi_vk_fence_signal,
    .wait          = lrhi_vk_fence_wait,
};

static const LRHITextureVTable s_texture_vtable = {
    .destroy_texture       = lrhi_vk_destroy_texture,
    .get_texture_info      = lrhi_vk_get_texture_info,
    .texture_replace_region = lrhi_vk_texture_replace_region,
    .texture_read_region   = lrhi_vk_texture_read_region,
    .texture_set_name      = lrhi_vk_texture_set_name,
};

static const LRHIBufferVTable s_buffer_vtable = {
    .destroy_buffer                   = lrhi_vk_destroy_buffer,
    .get_buffer_info                  = lrhi_vk_get_buffer_info,
    .buffer_map                       = lrhi_vk_buffer_map,
    .buffer_unmap                     = lrhi_vk_buffer_unmap,
    .buffer_set_name                  = lrhi_vk_buffer_set_name,
    .buffer_set_indirect_command_type = lrhi_vk_buffer_set_indirect_command_type,
};

static const LRHICommandListVTable s_command_list_vtable = {
    .destroy_command_list                  = lrhi_vk_destroy_command_list,
    .command_list_begin                    = lrhi_vk_command_list_begin,
    .command_list_end                      = lrhi_vk_command_list_end,
    .command_list_reset                    = lrhi_vk_command_list_reset,
    .command_list_prepare_indirect_commands = lrhi_vk_command_list_prepare_indirect_commands,
    .copy_pass_begin                       = lrhi_vk_copy_pass_begin,
    .render_pass_begin                     = lrhi_vk_render_pass_begin,
    .compute_pass_begin                    = lrhi_vk_compute_pass_begin,
    .acceleration_structure_pass_begin     = lrhi_vk_acceleration_structure_pass_begin,
};

static const LRHICopyPassVTable s_copy_pass_vtable = {
    .copy_pass_end            = lrhi_vk_copy_pass_end,
    .push_debug_group         = lrhi_vk_copy_pass_push_debug_group,
    .pop_debug_group          = lrhi_vk_copy_pass_pop_debug_group,
    .copy_pass_intra_barrier  = lrhi_vk_copy_pass_intra_barrier,
    .copy_pass_encoder_barrier = lrhi_vk_copy_pass_encoder_barrier,
    .copy_buffer_to_buffer    = lrhi_vk_copy_buffer_to_buffer,
    .copy_buffer_to_texture   = lrhi_vk_copy_buffer_to_texture,
    .copy_texture_to_buffer   = lrhi_vk_copy_texture_to_buffer,
    .copy_texture_to_texture  = lrhi_vk_copy_texture_to_texture,
};

static const LRHIResidencySetVTable s_residency_set_vtable = {
    .destroy_residency_set = lrhi_vk_destroy_residency_set,
    .add_texture           = lrhi_vk_residency_set_add_texture,
    .add_buffer            = lrhi_vk_residency_set_add_buffer,
    .add_blas              = lrhi_vk_residency_set_add_blas,
    .add_tlas              = lrhi_vk_residency_set_add_tlas,
    .remove_texture        = lrhi_vk_residency_set_remove_texture,
    .remove_buffer         = lrhi_vk_residency_set_remove_buffer,
    .remove_blas           = lrhi_vk_residency_set_remove_blas,
    .remove_tlas           = lrhi_vk_residency_set_remove_tlas,
    .update                = lrhi_vk_residency_set_update,
};

static const LRHISwapChainVTable s_swap_chain_vtable = {
    .destroy_swap_chain   = lrhi_vk_destroy_swap_chain,
    .get_current_texture  = lrhi_vk_get_current_texture,
    .present              = lrhi_vk_present,
};

static const LRHITextureViewVTable s_texture_view_vtable = {
    .destroy_texture_view  = lrhi_vk_destroy_texture_view,
    .get_texture_view_info = lrhi_vk_get_texture_view_info,
    .get_bindless_index    = lrhi_vk_texture_view_get_bindless_index,
};

static const LRHIRenderPassVTable s_render_pass_vtable = {
    .end                       = lrhi_vk_render_pass_end,
    .push_debug_group          = lrhi_vk_render_pass_push_debug_group,
    .pop_debug_group           = lrhi_vk_render_pass_pop_debug_group,
    .intra_barrier             = lrhi_vk_render_pass_intra_barrier,
    .encoder_barrier           = lrhi_vk_render_pass_encoder_barrier,
    .set_render_pipeline       = lrhi_vk_set_render_pipeline,
    .set_mesh_pipeline         = lrhi_vk_set_mesh_pipeline,
    .set_viewport              = lrhi_vk_set_viewport,
    .set_scissor               = lrhi_vk_set_scissor,
    .set_push_constants        = lrhi_vk_render_pass_set_push_constants,
    .draw                      = lrhi_vk_draw,
    .draw_indexed              = lrhi_vk_draw_indexed,
    .draw_mesh_tasks           = lrhi_vk_draw_mesh_tasks,
    .execute_indirect_commands = lrhi_vk_execute_indirect_commands,
};

static const LRHIShaderModuleVTable s_shader_module_vtable = {
    .destroy_shader_module  = lrhi_vk_destroy_shader_module,
    .get_shader_module_info = lrhi_vk_get_shader_module_info,
};

static const LRHIRenderPipelineVTable s_render_pipeline_vtable = {
    .destroy_render_pipeline  = lrhi_vk_destroy_render_pipeline,
    .get_render_pipeline_info = lrhi_vk_get_render_pipeline_info,
    .get_alloc_size           = lrhi_vk_render_pipeline_get_alloc_size,
};

static const LRHIMeshPipelineVTable s_mesh_pipeline_vtable = {
    .destroy_mesh_pipeline  = lrhi_vk_destroy_mesh_pipeline,
    .get_mesh_pipeline_info = lrhi_vk_get_mesh_pipeline_info,
    .get_alloc_size         = lrhi_vk_mesh_pipeline_get_alloc_size,
};

static const LRHIComputePipelineVTable s_compute_pipeline_vtable = {
    .destroy_compute_pipeline  = lrhi_vk_destroy_compute_pipeline,
    .get_compute_pipeline_info = lrhi_vk_get_compute_pipeline_info,
    .get_alloc_size            = lrhi_vk_compute_pipeline_get_alloc_size,
};

static const LRHIComputePassVTable s_compute_pass_vtable = {
    .end                  = lrhi_vk_compute_pass_end,
    .push_debug_group     = lrhi_vk_compute_pass_push_debug_group,
    .pop_debug_group      = lrhi_vk_compute_pass_pop_debug_group,
    .barrier              = lrhi_vk_compute_pass_barrier,
    .encoder_barrier      = lrhi_vk_compute_pass_encoder_barrier,
    .set_pipeline         = lrhi_vk_compute_pass_set_pipeline,
    .set_push_constants   = lrhi_vk_compute_pass_set_push_constants,
    .dispatch             = lrhi_vk_dispatch,
    .dispatch_indirect    = lrhi_vk_dispatch_indirect,
};

static const LRHIBufferViewVTable s_buffer_view_vtable = {
    .destroy_buffer_view  = lrhi_vk_destroy_buffer_view,
    .get_buffer_view_info = lrhi_vk_get_buffer_view_info,
    .get_bindless_index   = lrhi_vk_buffer_view_get_bindless_index,
};

static const LRHISamplerVTable s_sampler_vtable = {
    .destroy_sampler  = lrhi_vk_destroy_sampler,
    .get_sampler_info = lrhi_vk_get_sampler_info,
    .get_bindless_index = lrhi_vk_sampler_get_bindless_index,
};

static const LRHIAccelerationStructurePassVTable s_acceleration_structure_pass_vtable = {
    .end               = lrhi_vk_acceleration_structure_pass_end,
    .push_debug_group  = lrhi_vk_acceleration_structure_pass_push_debug_group,
    .pop_debug_group   = lrhi_vk_acceleration_structure_pass_pop_debug_group,
    .barrier           = lrhi_vk_acceleration_structure_pass_barrier,
    .encoder_barrier   = lrhi_vk_acceleration_structure_pass_encoder_barrier,
    .build_blas        = lrhi_vk_build_blas,
    .build_tlas        = lrhi_vk_build_tlas,
    .write_compacted_blas_size = lrhi_vk_write_compacted_blas_size,
    .compact_blas      = lrhi_vk_compact_blas,
    .refit_blas        = lrhi_vk_refit_blas,
    .refit_tlas        = lrhi_vk_refit_tlas,
    .copy_blas         = lrhi_vk_copy_blas,
    .copy_tlas         = lrhi_vk_copy_tlas,
};

static const LRHIBLASVTable s_blas_vtable = {
    .destroy_bottom_level_acceleration_structure     = lrhi_vk_destroy_blas,
    .get_bottom_level_acceleration_structure_info    = lrhi_vk_get_blas_info,
    .get_build_scratch_size                          = lrhi_vk_blas_get_build_scratch_size,
};

static const LRHITLASVTable s_tlas_vtable = {
    .destroy_top_level_acceleration_structure    = lrhi_vk_destroy_tlas,
    .get_top_level_acceleration_structure_info   = lrhi_vk_get_tlas_info,
    .get_bindless_index                          = lrhi_vk_tlas_get_bindless_index,
    .get_build_scratch_size                      = lrhi_vk_tlas_get_build_scratch_size,
    .reset                                       = lrhi_vk_tlas_reset,
    .add_instance                                = lrhi_vk_tlas_add_instance,
};

// ─── Device ─────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_device(LRHIDevice device)
{
    // TODO
    LRHIDeviceVk* d = (LRHIDeviceVk*)device;
    LRHI_FREE(d);
}

static LRHIDeviceInfo lrhi_vk_get_device_info(LRHIDevice device)
{
    LRHIDeviceVk* d = (LRHIDeviceVk*)device;
    return d->info;
}

static void lrhi_vk_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_texture; (void)out_error;
}

static void lrhi_vk_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_buffer; (void)out_error;
}

static void lrhi_vk_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO
    (void)device; (void)texture; (void)region; (void)mip_level; (void)array_layer;
    (void)out_data; (void)data_size; (void)bytes_per_row; (void)bytes_per_image; (void)out_error;
}

static void lrhi_vk_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
    // TODO
    (void)device; (void)buffer; (void)out_data; (void)data_size; (void)out_error;
}

static void lrhi_vk_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error)
{
    // TODO
    (void)device; (void)out_queue; (void)out_error;
}

static void lrhi_vk_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error)
{
    // TODO
    (void)device; (void)initial_value; (void)out_fence; (void)out_error;
}

static void lrhi_vk_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error)
{
    // TODO
    (void)device; (void)out_residency_set; (void)out_error;
}

static void lrhi_vk_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error)
{
    // TODO
    (void)device; (void)queue; (void)info; (void)out_swap_chain; (void)out_error;
}

static void lrhi_vk_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_texture_view; (void)out_error;
}

static void lrhi_vk_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_shader_module; (void)out_error;
}

static void lrhi_vk_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_pipeline; (void)out_error;
}

static void lrhi_vk_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_pipeline; (void)out_error;
}

static void lrhi_vk_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_pipeline; (void)out_error;
}

static void lrhi_vk_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_buffer_view; (void)out_error;
}

static void lrhi_vk_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_sampler; (void)out_error;
}

static void lrhi_vk_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_blas; (void)out_error;
}

static void lrhi_vk_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
    // TODO
    (void)device; (void)compacted_size; (void)out_blas; (void)out_error;
}

static void lrhi_vk_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error)
{
    // TODO
    (void)device; (void)info; (void)out_tlas; (void)out_error;
}

// ─── Command queue ───────────────────────────────────────────────────────────

static void lrhi_vk_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error)
{
    // TODO
    (void)queue; (void)out_command_list; (void)out_error;
}

static void lrhi_vk_destroy_command_queue(LRHICommandQueue queue)
{
    // TODO
    LRHICommandQueueVk* q = (LRHICommandQueueVk*)queue;
    LRHI_FREE(q);
}

static void lrhi_vk_signal_fence(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    // TODO
    (void)queue; (void)fence; (void)value; (void)out_error;
}

static void lrhi_vk_wait_fence(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    // TODO
    (void)queue; (void)fence; (void)value; (void)timeout_ns; (void)out_error;
}

static void lrhi_vk_submit_command_lists(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error)
{
    // TODO
    (void)queue; (void)command_lists; (void)command_list_count;
    (void)signal_fence; (void)signal_value; (void)wait_fence; (void)wait_value; (void)out_error;
}

static void lrhi_vk_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error)
{
    // TODO
    (void)queue; (void)residency_set; (void)out_error;
}

// ─── Fence ───────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_fence(LRHIFence fence)
{
    // TODO
    LRHIFenceVk* f = (LRHIFenceVk*)fence;
    LRHI_FREE(f);
}

static uint64_t lrhi_vk_fence_get_value(LRHIFence fence)
{
    // TODO
    (void)fence;
    return 0;
}

static void lrhi_vk_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error)
{
    // TODO
    (void)fence; (void)value; (void)out_error;
}

static void lrhi_vk_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    // TODO
    (void)fence; (void)value; (void)timeout_ns; (void)out_error;
}

// ─── Texture ─────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_texture(LRHITexture texture)
{
    // TODO
    LRHITextureVk* t = (LRHITextureVk*)texture;
    if (!t->is_swapchain_image) {
        LRHI_FREE(t);
    }
}

static void lrhi_vk_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info)
{
    LRHITextureVk* t = (LRHITextureVk*)texture;
    *out_info = t->info;
}

static void lrhi_vk_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO
    (void)texture; (void)region; (void)mip_level; (void)array_layer;
    (void)data; (void)data_size; (void)bytes_per_row; (void)bytes_per_image; (void)out_error;
}

static void lrhi_vk_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    // TODO
    (void)texture; (void)region; (void)mip_level; (void)array_layer;
    (void)out_data; (void)data_size; (void)bytes_per_row; (void)bytes_per_image; (void)out_error;
}

static void lrhi_vk_texture_set_name(LRHITexture texture, const char* name)
{
    // TODO
    (void)texture; (void)name;
}

// ─── Buffer ──────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_buffer(LRHIBuffer buffer)
{
    // TODO
    LRHIBufferVk* b = (LRHIBufferVk*)buffer;
    LRHI_FREE(b);
}

static void lrhi_vk_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info)
{
    LRHIBufferVk* b = (LRHIBufferVk*)buffer;
    *out_info = b->info;
}

static void* lrhi_vk_buffer_map(LRHIBuffer buffer, LRHIError* out_error)
{
    // TODO
    (void)buffer; (void)out_error;
    return NULL;
}

static void lrhi_vk_buffer_unmap(LRHIBuffer buffer)
{
    // TODO
    (void)buffer;
}

static void lrhi_vk_buffer_set_name(LRHIBuffer buffer, const char* name)
{
    // TODO
    (void)buffer; (void)name;
}

static void lrhi_vk_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error)
{
    // TODO
    (void)buffer; (void)command_type; (void)out_error;
}

// ─── Command list ────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_command_list(LRHICommandList command_list)
{
    // TODO
    LRHICommandListVk* cl = (LRHICommandListVk*)command_list;
    LRHI_FREE(cl);
}

static void lrhi_vk_command_list_begin(LRHICommandList command_list, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)out_error;
}

static void lrhi_vk_command_list_end(LRHICommandList command_list, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)out_error;
}

static void lrhi_vk_command_list_reset(LRHICommandList command_list, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)out_error;
}

static void lrhi_vk_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)indirect_command_buffer; (void)count_buffer; (void)maxCommandCount;
    (void)parameters; (void)pipeline; (void)push_constants; (void)push_constant_size; (void)out_error;
}

static LRHICopyPass lrhi_vk_copy_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)out_error;
    return NULL;
}

static LRHIRenderPass lrhi_vk_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)info; (void)out_error;
    return NULL;
}

static LRHIComputePass lrhi_vk_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)out_error;
    return NULL;
}

static LRHIAccelerationStructurePass lrhi_vk_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    // TODO
    (void)command_list; (void)out_error;
    return NULL;
}

// ─── Copy pass ───────────────────────────────────────────────────────────────

static void lrhi_vk_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)out_error;
}

static void lrhi_vk_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)label; (void)out_error;
}

static void lrhi_vk_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)out_error;
}

static void lrhi_vk_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)out_error;
}

static void lrhi_vk_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)afterStage; (void)out_error;
}

static void lrhi_vk_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)src_buffer; (void)src_offset; (void)dst_buffer; (void)dst_offset; (void)size; (void)out_error;
}

static void lrhi_vk_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)src_buffer; (void)src_offset; (void)src_bytes_per_row; (void)src_bytes_per_image;
    (void)dst_texture; (void)dst_region; (void)dst_mip_level; (void)dst_array_layer; (void)out_error;
}

static void lrhi_vk_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)src_texture; (void)src_region; (void)src_mip_level; (void)src_array_layer;
    (void)dst_buffer; (void)dst_offset; (void)dst_bytes_per_row; (void)dst_bytes_per_image; (void)out_error;
}

static void lrhi_vk_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    // TODO
    (void)copy_pass; (void)src_texture; (void)src_region; (void)src_mip_level; (void)src_array_layer;
    (void)dst_texture; (void)dst_region; (void)dst_mip_level; (void)dst_array_layer; (void)out_error;
}

// ─── Residency set ───────────────────────────────────────────────────────────

static void lrhi_vk_destroy_residency_set(LRHIResidencySet residency_set)
{
    // TODO
    LRHIResidencySetVk* rs = (LRHIResidencySetVk*)residency_set;
    LRHI_FREE(rs);
}

static void lrhi_vk_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    (void)residency_set; (void)texture; (void)out_error;
}

static void lrhi_vk_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    (void)residency_set; (void)buffer; (void)out_error;
}

static void lrhi_vk_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    (void)residency_set; (void)blas; (void)out_error;
}

static void lrhi_vk_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    (void)residency_set; (void)tlas; (void)out_error;
}

static void lrhi_vk_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    (void)residency_set; (void)texture; (void)out_error;
}

static void lrhi_vk_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    (void)residency_set; (void)buffer; (void)out_error;
}

static void lrhi_vk_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    (void)residency_set; (void)blas; (void)out_error;
}

static void lrhi_vk_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    (void)residency_set; (void)tlas; (void)out_error;
}

static void lrhi_vk_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error)
{
    (void)residency_set; (void)out_error;
}

// ─── Swap chain ──────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_swap_chain(LRHISwapChain swap_chain)
{
    // TODO
    LRHISwapChainVk* sc = (LRHISwapChainVk*)swap_chain;
    LRHI_FREE(sc->images);
    LRHI_FREE(sc->texture_wrappers);
    LRHI_FREE(sc);
}

static LRHITexture lrhi_vk_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error)
{
    // TODO
    (void)swap_chain; (void)out_error;
    return NULL;
}

static void lrhi_vk_present(LRHISwapChain swap_chain, LRHIError* out_error)
{
    // TODO
    (void)swap_chain; (void)out_error;
}

// ─── Texture view ────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_texture_view(LRHITextureView texture_view)
{
    // TODO
    LRHITextureViewVk* tv = (LRHITextureViewVk*)texture_view;
    LRHI_FREE(tv);
}

static void lrhi_vk_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info)
{
    LRHITextureViewVk* tv = (LRHITextureViewVk*)texture_view;
    *out_info = tv->info;
}

static uint32_t lrhi_vk_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error)
{
    // TODO
    (void)texture_view; (void)out_error;
    return 0;
}

// ─── Render pass ─────────────────────────────────────────────────────────────

static void lrhi_vk_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)out_error;
}

static void lrhi_vk_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)label; (void)out_error;
}

static void lrhi_vk_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)out_error;
}

static void lrhi_vk_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)beforeStage; (void)afterStage; (void)out_error;
}

static void lrhi_vk_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)beforeStage; (void)afterStage; (void)out_error;
}

static void lrhi_vk_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)pipeline; (void)out_error;
}

static void lrhi_vk_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)pipeline; (void)out_error;
}

static void lrhi_vk_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)x; (void)y; (void)width; (void)height; (void)min_depth; (void)max_depth; (void)out_error;
}

static void lrhi_vk_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)x; (void)y; (void)width; (void)height; (void)out_error;
}

static void lrhi_vk_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)data; (void)size; (void)out_error;
}

static void lrhi_vk_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)vertex_count; (void)instance_count; (void)first_vertex; (void)first_instance; (void)out_error;
}

static void lrhi_vk_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)index_count; (void)instance_count; (void)first_index;
    (void)vertex_offset; (void)first_instance; (void)index_buffer; (void)index_stride; (void)out_error;
}

static void lrhi_vk_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)num_groups_x; (void)num_groups_y; (void)num_groups_z;
    (void)threads_per_object_group_x; (void)threads_per_object_group_y; (void)threads_per_object_group_z;
    (void)threads_per_mesh_group_x; (void)threads_per_mesh_group_y; (void)threads_per_mesh_group_z; (void)out_error;
}

static void lrhi_vk_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error)
{
    // TODO
    (void)render_pass; (void)indirect_command_buffer; (void)count_buffer; (void)max_command_count; (void)out_error;
}

// ─── Shader module ───────────────────────────────────────────────────────────

static void lrhi_vk_destroy_shader_module(LRHIShaderModule shader_module)
{
    // TODO
    LRHIShaderModuleVk* sm = (LRHIShaderModuleVk*)shader_module;
    LRHI_FREE(sm);
}

static void lrhi_vk_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info)
{
    LRHIShaderModuleVk* sm = (LRHIShaderModuleVk*)shader_module;
    *out_info = sm->info;
}

// ─── Render pipeline ─────────────────────────────────────────────────────────

static void lrhi_vk_destroy_render_pipeline(LRHIRenderPipeline pipeline)
{
    // TODO
    LRHIRenderPipelineVk* p = (LRHIRenderPipelineVk*)pipeline;
    LRHI_FREE(p);
}

static void lrhi_vk_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info)
{
    LRHIRenderPipelineVk* p = (LRHIRenderPipelineVk*)pipeline;
    *out_info = p->info;
}

static uint64_t lrhi_vk_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    // TODO
    (void)pipeline; (void)out_error;
    return 0;
}

// ─── Mesh pipeline ───────────────────────────────────────────────────────────

static void lrhi_vk_destroy_mesh_pipeline(LRHIMeshPipeline pipeline)
{
    // TODO
    LRHIMeshPipelineVk* p = (LRHIMeshPipelineVk*)pipeline;
    LRHI_FREE(p);
}

static void lrhi_vk_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info)
{
    LRHIMeshPipelineVk* p = (LRHIMeshPipelineVk*)pipeline;
    *out_info = p->info;
}

static uint64_t lrhi_vk_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    // TODO
    (void)pipeline; (void)out_error;
    return 0;
}

// ─── Compute pipeline ────────────────────────────────────────────────────────

static void lrhi_vk_destroy_compute_pipeline(LRHIComputePipeline pipeline)
{
    // TODO
    LRHIComputePipelineVk* p = (LRHIComputePipelineVk*)pipeline;
    LRHI_FREE(p);
}

static void lrhi_vk_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info)
{
    LRHIComputePipelineVk* p = (LRHIComputePipelineVk*)pipeline;
    *out_info = p->info;
}

static uint64_t lrhi_vk_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error)
{
    // TODO
    (void)pipeline; (void)out_error;
    return 0;
}

// ─── Compute pass ────────────────────────────────────────────────────────────

static void lrhi_vk_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)out_error;
}

static void lrhi_vk_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)label; (void)out_error;
}

static void lrhi_vk_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)out_error;
}

static void lrhi_vk_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)out_error;
}

static void lrhi_vk_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)after_stage; (void)out_error;
}

static void lrhi_vk_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)pipeline; (void)out_error;
}

static void lrhi_vk_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)data; (void)size; (void)out_error;
}

static void lrhi_vk_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)num_groups_x; (void)num_groups_y; (void)num_groups_z;
    (void)threads_per_group_x; (void)threads_per_group_y; (void)threads_per_group_z; (void)out_error;
}

static void lrhi_vk_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error)
{
    // TODO
    (void)compute_pass; (void)indirect_command_buffer; (void)out_error;
}

// ─── Buffer view ─────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_buffer_view(LRHIBufferView buffer_view)
{
    // TODO
    LRHIBufferViewVk* bv = (LRHIBufferViewVk*)buffer_view;
    LRHI_FREE(bv);
}

static void lrhi_vk_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info)
{
    LRHIBufferViewVk* bv = (LRHIBufferViewVk*)buffer_view;
    *out_info = bv->info;
}

static uint32_t lrhi_vk_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error)
{
    // TODO
    (void)buffer_view; (void)out_error;
    return 0;
}

// ─── Sampler ─────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_sampler(LRHISampler sampler)
{
    // TODO
    LRHISamplerVk* s = (LRHISamplerVk*)sampler;
    LRHI_FREE(s);
}

static void lrhi_vk_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info)
{
    LRHISamplerVk* s = (LRHISamplerVk*)sampler;
    *out_info = s->info;
}

static uint32_t lrhi_vk_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error)
{
    // TODO
    (void)sampler; (void)out_error;
    return 0;
}

// ─── Acceleration structure pass ─────────────────────────────────────────────

static void lrhi_vk_acceleration_structure_pass_end(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)out_error;
}

static void lrhi_vk_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)label; (void)out_error;
}

static void lrhi_vk_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)out_error;
}

static void lrhi_vk_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)out_error;
}

static void lrhi_vk_acceleration_structure_pass_encoder_barrier(LRHIAccelerationStructurePass pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)after_stage; (void)out_error;
}

static void lrhi_vk_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)blas; (void)scratch_buffer; (void)scratch_offset; (void)out_error;
}

static void lrhi_vk_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)tlas; (void)scratch_buffer; (void)scratch_offset; (void)out_error;
}

static void lrhi_vk_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)blas; (void)dst_buffer; (void)dst_offset; (void)out_error;
}

static void lrhi_vk_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)src_blas; (void)dst_blas; (void)out_error;
}

static void lrhi_vk_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)blas; (void)scratch_buffer; (void)scratch_offset; (void)out_error;
}

static void lrhi_vk_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)tlas; (void)scratch_buffer; (void)scratch_offset; (void)out_error;
}

static void lrhi_vk_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)src_blas; (void)dst_blas; (void)out_error;
}

static void lrhi_vk_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error)
{
    // TODO
    (void)pass; (void)src_tlas; (void)dst_tlas; (void)out_error;
}

// ─── BLAS ────────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_blas(LRHIBottomLevelAccelerationStructure blas)
{
    // TODO
    LRHIBLASVk* b = (LRHIBLASVk*)blas;
    LRHI_FREE(b);
}

static void lrhi_vk_get_blas_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info)
{
    LRHIBLASVk* b = (LRHIBLASVk*)blas;
    *out_info = b->info;
}

static LRHIAccelerationStructureBufferSizes lrhi_vk_blas_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    // TODO
    (void)blas; (void)out_error;
    LRHIAccelerationStructureBufferSizes sizes;
    memset(&sizes, 0, sizeof(sizes));
    return sizes;
}

// ─── TLAS ────────────────────────────────────────────────────────────────────

static void lrhi_vk_destroy_tlas(LRHITopLevelAccelerationStructure tlas)
{
    // TODO
    LRHITLASVk* t = (LRHITLASVk*)tlas;
    LRHI_FREE(t);
}

static void lrhi_vk_get_tlas_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info)
{
    LRHITLASVk* t = (LRHITLASVk*)tlas;
    *out_info = t->info;
}

static uint64_t lrhi_vk_tlas_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    // TODO
    (void)tlas; (void)out_error;
    return 0;
}

static LRHIAccelerationStructureBufferSizes lrhi_vk_tlas_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    // TODO
    (void)tlas; (void)out_error;
    LRHIAccelerationStructureBufferSizes sizes;
    memset(&sizes, 0, sizeof(sizes));
    return sizes;
}

static void lrhi_vk_tlas_reset(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    // TODO
    (void)tlas; (void)out_error;
}

static void lrhi_vk_tlas_add_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error)
{
    // TODO
    (void)tlas; (void)instance_info; (void)out_error;
}

// ─── Entry point ─────────────────────────────────────────────────────────────

void lrhi_vulkan_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    LRHIDeviceVk* device = (LRHIDeviceVk*)LRHI_CALLOC(1, sizeof(LRHIDeviceVk));
    if (!device) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to allocate Vulkan device");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    device->base.vtable  = &s_device_vtable;
    device->enable_debug = enable_debug;

    VkResult result = volkInitialize();
    if (vk_result_to_lrhi(result, out_error)) return;

    // Create instance with optional validation layers
    const char* layer = "VK_LAYER_khronos_validation";
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME  
    };

    VkApplicationInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.apiVersion = VK_API_VERSION_1_4;
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "Luminary RHI Engine";
    app_info.pApplicationName = "Luminary RHI App";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);

    VkInstanceCreateInfo instance_info;
    memset(&instance_info, 0, sizeof(VkInstanceCreateInfo));
    instance_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    if (enable_debug) instance_info.enabledExtensionCount = 2;
    else instance_info.enabledExtensionCount = 1;
    instance_info.ppEnabledExtensionNames = extensions;
    if (enable_debug) {
        instance_info.enabledLayerCount = 1;
        instance_info.ppEnabledLayerNames = &layer;
    }
    instance_info.pApplicationInfo = &app_info;

    result = vkCreateInstance(&instance_info, NULL, &device->instance);
    if (vk_result_to_lrhi(result, out_error)) return;

    volkLoadInstance(device->instance);

    // Now choose physical device
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(device->instance, &physical_device_count, NULL);
    VkPhysicalDevice* physical_devices = LRHI_CALLOC(physical_device_count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(device->instance, &physical_device_count, physical_devices);

    for (uint32_t i = 0; i < physical_device_count; i++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_devices[i], &properties);

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            device->physical_device = physical_devices[i];
            break;
        }
    }
    // Fallback if none found
    if (device->physical_device == VK_NULL_HANDLE) device->physical_device = physical_devices[0];

    VkPhysicalDeviceProperties propeties;
    VkPhysicalDeviceLimits limits;
    vkGetPhysicalDeviceProperties(device->physical_device, &propeties);
    limits = propeties.limits;

    // Get support for raytracing, mesh shaders, mutable descriptor, descriptor indexing, unified image layouts, BDA
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &extension_count, NULL);
    VkExtensionProperties* extensions_properties = LRHI_CALLOC(extension_count, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device->physical_device, NULL, &extension_count, extensions_properties);

    uint8_t has_unified_image_layouts = 1;
    for (uint32_t i = 0; i < extension_count; i++) {
        if (strcmp(extensions_properties[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0) {
            device->info.features.ray_tracing = 1;
        }
        if (strcmp(extensions_properties[i].extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
            device->info.features.mesh_shading = 1;
        }
        if (strcmp(extensions_properties[i].extensionName, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME) == 0) {
            device->info.features.bindless_resources = 1;
        }
        if (strcmp(extensions_properties[i].extensionName, VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME) == 0) {
            has_unified_image_layouts = 1;
        }
    }
    if (!device->info.features.bindless_resources || !has_unified_image_layouts) {
        if (out_error) {
            if (!device->info.features.bindless_resources) {
                snprintf(out_error->message, sizeof(out_error->message), "Vulkan device does not support required feature: bindless resources");
            } else {
                snprintf(out_error->message, sizeof(out_error->message), "Vulkan device does not support required feature: unified image layouts");
            }
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    device->info.limits.max_buffer_size = limits.maxStorageBufferRange;
    device->info.limits.max_texture_array_layers = limits.maxImageArrayLayers;
    device->info.limits.max_texture_dimension_2d = limits.maxImageDimension2D;
    device->info.limits.max_texture_dimension_3d = limits.maxImageDimension3D;
    sprintf(device->info.device_name, "%s", propeties.deviceName);
    device->info.backend = LUMINARY_RHI_BACKEND_VULKAN;

    // Create device

    *out_device = (LRHIDevice)device;
}
