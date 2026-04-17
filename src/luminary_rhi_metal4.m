#include "luminary_rhi.h"
#include "luminary_rhi_internal.h"

#include <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <objc/objc.h>
#include <dispatch/dispatch.h>

#include "ext/metal_irconverter_runtime.h"
#include "ext/icb_shaders.h"

#define MAX_BINDLESS_RESOURCES 100000
#define MAX_BINDLESS_SAMPLERS 512

typedef struct Metal4BindlessManager {
    id<MTLBuffer> resource_heap_buffer;
    id<MTLBuffer> sampler_heap_buffer;

    IRDescriptorTableEntry* mapped_resource_heap;
    IRDescriptorTableEntry* mapped_sampler_heap;

    LRHIFreeList resource_heap_free_list;
    LRHIFreeList sampler_heap_free_list;
} Metal4BindlessManager;

typedef struct Metal4ICBDrawParameters {
    MTLResourceID icb;
} Metal4ICBDrawParameters;

typedef struct Metal4ICBDrawIndexedParameters {
    MTLResourceID    icb;
    MTLGPUAddress    index_buffer;
} Metal4ICBDrawIndexedParameters;

typedef struct Metal4ICBDispatchParameters {
    MTLResourceID icb;
    uint32_t      threads_per_group_x;
    uint32_t      threads_per_group_y;
    uint32_t      threads_per_group_z;
} Metal4ICBDispatchParameters;

typedef struct Metal4ICBDrawMeshTasksParameters {
    MTLResourceID icb;
    uint32_t _pad0[2];
    uint32_t threads_per_object_group_x;
    uint32_t threads_per_object_group_y;
    uint32_t threads_per_object_group_z;
    uint32_t _pad1;
    uint32_t threads_per_mesh_group_x;
    uint32_t threads_per_mesh_group_y;
    uint32_t threads_per_mesh_group_z;
    uint32_t _pad2;
} Metal4ICBDrawMeshTasksParameters;

typedef struct LRHIDeviceMetal4 {
    LRHIDeviceBase base;
    id<MTLDevice> device;
    uint8_t enable_debug;

    Metal4BindlessManager bindless_manager;
    id<MTLTextureViewPool> texture_view_pool;

    id<MTLComputePipelineState> draw_icb_pipe;
    id<MTLComputePipelineState> draw_indexed_icb_pipe;
    id<MTLComputePipelineState> dispatch_icb_pipe;
    id<MTLComputePipelineState> draw_mesh_tasks_icb_pipe;
    id<MTLComputePipelineState> reset_render_icb_pipe;
    id<MTLComputePipelineState> reset_compute_icb_pipe;
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

    // Indirect command buffer resources (populated by buffer_set_indirect_command_type)
    id<MTLIndirectCommandBuffer> icb;
    id<MTLBuffer> icb_params;
    id<MTLBuffer> draw_id_atomic;
    id<MTLBuffer> per_draw_constants;
    id<MTLBuffer> primitive_type_buf;
    id<MTLBuffer> icb_capacity_buf;
    LRHICommandType icb_command_type;
} LRHIBufferMetal4;

typedef struct LRHICommandQueueMetal4 {
    LRHICommandQueueBase base;
    id<MTL4CommandQueue> queue;
    id<MTLDevice> device;
    id<MTLResidencySet> internal_residency_set;
    LRHIDeviceMetal4* rhi_device;
} LRHICommandQueueMetal4;

typedef struct LRHIFenceMetal4 {
    LRHIFenceBase base;
    id<MTLSharedEvent> event;
} LRHIFenceMetal4;

typedef struct LRHICommandListMetal4 {
    LRHICommandListBase base;
    id<MTL4CommandBuffer> command_buffer;
    id<MTL4CommandAllocator> command_allocator;
    id<MTL4ArgumentTable> render_argument_table;
    id<MTL4ArgumentTable> compute_argument_table;
    id<MTLBuffer> push_constant_buffer;
    uint32_t push_constant_offset;
    LRHIDeviceMetal4* rhi_device;
} LRHICommandListMetal4;

typedef struct LRHICopyPassMetal4 {
    LRHICopyPassBase base;
    id<MTL4ComputeCommandEncoder> blit_encoder;
} LRHICopyPassMetal4;

typedef struct LRHIResidencySetMetal4 {
    LRHIResidencySetBase base;
    id<MTLResidencySet> residency_set;
} LRHIResidencySetMetal4;

typedef struct LRHISwapChainMetal4 {
    LRHISwapChainBase    base;
    CAMetalLayer*        layer;
    id<MTL4CommandQueue> queue;            // needed for waitForDrawable:/signalDrawable:
    id<CAMetalDrawable>  current_drawable;
    LRHITextureMetal4    current_texture;  // embedded — no heap allocation per frame
    LRHISwapChainInfo    info;
} LRHISwapChainMetal4;

typedef struct LRHITextureViewMetal4 {
    LRHITextureViewBase base;
    LRHITextureViewInfo info;
    id<MTLTexture> texture_view;

    uint32_t bindless_index;
    MTLResourceID bindless_resource_id;

    Metal4BindlessManager* bindless_manager;
} LRHITextureViewMetal4;

typedef struct LRHIArgumentBufferData {
    char push_constants[128];
    uint32_t draw_id;
} LRHIArgumentBufferData;

typedef struct LRHIRenderPassMetal4 {
    LRHIRenderPassBase base;
    id<MTL4RenderCommandEncoder> render_encoder;

    LRHIRenderPipeline current_render_pipeline;
    LRHICommandListMetal4* command_list;
    char current_push_constants[128];
    uint32_t current_draw_id;
} LRHIRenderPassMetal4;

typedef struct LRHIShaderModuleMetal4 {
    LRHIShaderModuleBase base;
    LRHIShaderModuleInfo info;
    id<MTLLibrary> library;
    id<MTLFunction> function;
} LRHIShaderModuleMetal4;

typedef struct LRHIRenderPipelineMetal4 {
    LRHIRenderPipelineBase base;
    LRHIRenderPipelineInfo info;
    id<MTLRenderPipelineState> pipeline_state;
    id<MTLDepthStencilState> depth_stencil_state;

    Metal4BindlessManager* bindless_manager;
} LRHIRenderPipelineMetal4;

typedef struct LRHIMeshPipelineMetal4 {
    LRHIMeshPipelineBase base;
    LRHIMeshPipelineInfo info;
    id<MTLRenderPipelineState> pipeline_state;
    id<MTLDepthStencilState> depth_stencil_state;

    Metal4BindlessManager* bindless_manager;
} LRHIMeshPipelineMetal4;

typedef struct LRHIComputePipelineMetal4 {
    LRHIComputePipelineBase base;
    LRHIComputePipelineInfo info;
    id<MTLComputePipelineState> pipeline_state;

    Metal4BindlessManager* bindless_manager;
} LRHIComputePipelineMetal4;

typedef struct LRHIComputePassMetal4 {
    LRHIComputePassBase base;
    id<MTL4ComputeCommandEncoder> compute_encoder;

    LRHIComputePipeline current_compute_pipeline;
    LRHICommandListMetal4* command_list;
    char current_push_constants[128];
} LRHIComputePassMetal4;

typedef struct LRHIBufferViewMetal4 {
    LRHIBufferViewBase base;
    LRHIBufferViewInfo info;
    uint32_t bindless_index;
    MTLGPUAddress gpu_address;

    Metal4BindlessManager* bindless_manager;
} LRHIBufferViewMetal4;

typedef struct LRHISamplerMetal4 {
    LRHISamplerBase base;
    LRHISamplerInfo info;
    uint32_t bindless_index;
    id<MTLSamplerState> sampler_state;

    Metal4BindlessManager* bindless_manager;
} LRHISamplerMetal4;

// Forward declarations
static MTLPixelFormat            lrhi_metal4_pixel_format(LRHITextureFormat format);
static MTLTextureUsage           lrhi_metal4_texture_usage(LRHITextureUsage usage);
static MTLTextureType            lrhi_metal4_texture_type(LRHITextureDimensions type);
static void                      lrhi_metal4_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error);
static MTLStages                 lrhi_metal4_render_stage_to_mtl(LRHIRenderStage stage);
static MTLLoadAction             lrhi_metal4_load_action_to_mtl(LRHIRenderPassAction load_op);
static MTLStoreAction            lrhi_metal4_store_action_to_mtl(LRHIRenderPassAction store_op);
static MTLCullMode               lrhi_metal4_cull_mode_to_mtl(LRHIPipelineCullMode cull_mode);
static MTLTriangleFillMode       lrhi_metal4_fill_mode_to_mtl(LRHIPipelineFillMode fill_mode);
static MTLWinding                lrhi_metal4_front_face_to_mtl(LRHIPipelineFrontFace front_face);
static MTLPrimitiveType          lrhi_metal4_primitive_topology_to_mtl(LRHIPipelineTopology topology);
static MTLPrimitiveTopologyClass lrhi_metal4_primitive_topology_class_to_mtl(LRHIPipelineTopology topology);
static MTLBlendFactor            lrhi_metal4_blend_factor_to_mtl(LRHIBlendFactor factor);
static MTLBlendOperation         lrhi_metal4_blend_op_to_mtl(LRHIBlendOperation op);
static MTLCompareFunction        lrhi_metal4_compare_op_to_mtl(LRHICompareOperation op);
static MTLSamplerAddressMode     lrhi_metal4_address_mode_to_mtl(LRHISamplerAddressMode mode);
static MTLSamplerMinMagFilter    lrhi_metal4_filter_to_mtl(LRHISamplerFilter filter);
static MTLSamplerMipFilter       lrhi_metal4_mip_filter_to_mtl(LRHISamplerFilter filter);

static void            lrhi_metal4_destroy_device(LRHIDevice device);
static LRHIDeviceInfo  lrhi_metal4_get_device_info(LRHIDevice device);

static void            lrhi_metal4_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
static void            lrhi_metal4_destroy_texture(LRHITexture texture);
static void            lrhi_metal4_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
static void            lrhi_metal4_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal4_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal4_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal4_texture_set_name(LRHITexture texture, const char* name);

static void            lrhi_metal4_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
static void            lrhi_metal4_destroy_buffer(LRHIBuffer buffer);
static void            lrhi_metal4_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
static void*           lrhi_metal4_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal4_buffer_unmap(LRHIBuffer buffer);
static void            lrhi_metal4_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);
static void            lrhi_metal4_buffer_set_name(LRHIBuffer buffer, const char* name);
static void            lrhi_metal4_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error);

static void            lrhi_metal4_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
static void            lrhi_metal4_destroy_command_queue(LRHICommandQueue queue);
static void            lrhi_metal4_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
static void            lrhi_metal4_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
static void            lrhi_metal4_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error);
static void            lrhi_metal4_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error);

static void            lrhi_metal4_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
static void            lrhi_metal4_destroy_fence(LRHIFence fence);
static uint64_t        lrhi_metal4_fence_get_value(LRHIFence fence);
static void            lrhi_metal4_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);
static void            lrhi_metal4_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

static void            lrhi_metal4_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error);
static void            lrhi_metal4_destroy_command_list(LRHICommandList command_list);
static void            lrhi_metal4_command_list_begin(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal4_command_list_end(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal4_command_list_reset(LRHICommandList command_list, LRHIError* out_error);
static LRHICopyPass    lrhi_metal4_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal4_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error);

static void            lrhi_metal4_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error);
static void            lrhi_metal4_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error);
static void            lrhi_metal4_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error);
static void            lrhi_metal4_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
static void            lrhi_metal4_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error);
static void            lrhi_metal4_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
static void            lrhi_metal4_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error);

static void            lrhi_metal4_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error);
static void            lrhi_metal4_destroy_residency_set(LRHIResidencySet residency_set);
static void            lrhi_metal4_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void            lrhi_metal4_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal4_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void            lrhi_metal4_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal4_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error);

static void            lrhi_metal4_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error);
static void            lrhi_metal4_destroy_swap_chain(LRHISwapChain swap_chain);
static LRHITexture     lrhi_metal4_swap_chain_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error);
static void            lrhi_metal4_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error);

static void            lrhi_metal4_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error);
static void            lrhi_metal4_destroy_texture_view(LRHITextureView texture_view);
static void            lrhi_metal4_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info);
static uint32_t        lrhi_metal4_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error);

static LRHIRenderPass  lrhi_metal4_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error);
static void            lrhi_metal4_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error);
static void            lrhi_metal4_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void            lrhi_metal4_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void            lrhi_metal4_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error);
static void            lrhi_metal4_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error);
static void            lrhi_metal4_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error);
static void            lrhi_metal4_flush_push_constants(LRHIRenderPassMetal4* render_pass, LRHIError* out_error);
static void            lrhi_metal4_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error);
static void            lrhi_metal4_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error);
static void            lrhi_metal4_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error);
static void            lrhi_metal4_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error);
static void            lrhi_metal4_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error);
static void            lrhi_metal4_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error);

static void            lrhi_metal4_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error);
static void            lrhi_metal4_destroy_shader_module(LRHIShaderModule shader_module);
static void            lrhi_metal4_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info);

static void            lrhi_metal4_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error);
static void            lrhi_metal4_destroy_render_pipeline(LRHIRenderPipeline pipeline);
static void            lrhi_metal4_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info);
static uint64_t        lrhi_metal4_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error);

static void            lrhi_metal4_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error);
static void            lrhi_metal4_destroy_mesh_pipeline(LRHIMeshPipeline pipeline);
static void            lrhi_metal4_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info);
static uint64_t        lrhi_metal4_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error);

static void            lrhi_metal4_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error);
static void            lrhi_metal4_destroy_compute_pipeline(LRHIComputePipeline pipeline);
static void            lrhi_metal4_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info);
static uint64_t        lrhi_metal4_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error);

static LRHIComputePass lrhi_metal4_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_set_compute_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error);
static void            lrhi_metal4_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error);

static void            lrhi_metal4_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error);
static void            lrhi_metal4_destroy_buffer_view(LRHIBufferView buffer_view);
static void            lrhi_metal4_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info);
static uint32_t        lrhi_metal4_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error);

static void            lrhi_metal4_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error);
static void            lrhi_metal4_destroy_sampler(LRHISampler sampler);
static void            lrhi_metal4_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info);
static uint32_t        lrhi_metal4_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error);

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
    .create_residency_set = lrhi_metal4_create_residency_set,
    .create_swap_chain    = lrhi_metal4_create_swap_chain,
    .create_texture_view  = lrhi_metal4_create_texture_view,
    .create_shader_module = lrhi_metal4_create_shader_module,
    .create_render_pipeline = lrhi_metal4_create_render_pipeline,
    .create_mesh_pipeline = lrhi_metal4_create_mesh_pipeline,
    .create_compute_pipeline = lrhi_metal4_create_compute_pipeline,
    .create_buffer_view = lrhi_metal4_create_buffer_view,
    .create_sampler = lrhi_metal4_create_sampler,
};

static const LRHICommandQueueVTable lrhi_metal4_command_queue_vtable = {
    .create_command_list   = lrhi_metal4_create_command_list,
    .destroy_command_queue = lrhi_metal4_destroy_command_queue,
    .signal_fence          = lrhi_metal4_command_queue_signal,
    .wait_fence            = lrhi_metal4_command_queue_wait,
    .submit_command_lists  = lrhi_metal4_command_queue_submit,
    .add_residency_set     = lrhi_metal4_command_queue_add_residency_set,
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
    .texture_set_name       = lrhi_metal4_texture_set_name,
};

static const LRHIBufferVTable lrhi_metal4_buffer_vtable = {
    .destroy_buffer                   = lrhi_metal4_destroy_buffer,
    .get_buffer_info                  = lrhi_metal4_get_buffer_info,
    .buffer_map                       = lrhi_metal4_buffer_map,
    .buffer_unmap                     = lrhi_metal4_buffer_unmap,
    .buffer_set_name                  = lrhi_metal4_buffer_set_name,
    .buffer_set_indirect_command_type = lrhi_metal4_buffer_set_indirect_command_type,
};

static const LRHICommandListVTable lrhi_metal4_command_list_vtable = {
    .destroy_command_list              = lrhi_metal4_destroy_command_list,
    .command_list_begin                = lrhi_metal4_command_list_begin,
    .command_list_end                  = lrhi_metal4_command_list_end,
    .command_list_reset                = lrhi_metal4_command_list_reset,
    .command_list_prepare_indirect_commands = lrhi_metal4_command_list_prepare_indirect_commands,
    .copy_pass_begin                   = lrhi_metal4_command_list_begin_copy_pass,
    .render_pass_begin                 = lrhi_metal4_render_pass_begin,
    .compute_pass_begin                = lrhi_metal4_compute_pass_begin,
};

static const LRHICopyPassVTable lrhi_metal4_copy_pass_vtable = {
    .copy_pass_end = lrhi_metal4_copy_pass_end,
    .copy_pass_intra_barrier = lrhi_metal4_copy_pass_intra_barrier,
    .copy_pass_encoder_barrier = lrhi_metal4_copy_pass_encoder_barrier,
    .copy_texture_to_texture = lrhi_metal4_copy_pass_copy_texture_to_texture,
    .copy_buffer_to_buffer = lrhi_metal4_copy_pass_copy_buffer_to_buffer,
    .copy_buffer_to_texture = lrhi_metal4_copy_pass_copy_buffer_to_texture,
    .copy_texture_to_buffer = lrhi_metal4_copy_pass_copy_texture_to_buffer,
};

static const LRHIResidencySetVTable lrhi_metal4_residency_set_vtable = {
    .destroy_residency_set = lrhi_metal4_destroy_residency_set,
    .add_texture = lrhi_metal4_residency_set_add_texture,
    .add_buffer = lrhi_metal4_residency_set_add_buffer,
    .remove_texture = lrhi_metal4_residency_set_remove_texture,
    .remove_buffer = lrhi_metal4_residency_set_remove_buffer,
    .update = lrhi_metal4_residency_set_update,
};

static const LRHITextureViewVTable lrhi_metal4_texture_view_vtable = {
    .destroy_texture_view = lrhi_metal4_destroy_texture_view,
    .get_texture_view_info = lrhi_metal4_get_texture_view_info,
    .get_bindless_index = lrhi_metal4_texture_view_get_bindless_index,
};

static void lrhi_metal4_swap_chain_texture_destroy_noop(LRHITexture texture) { (void)texture; }

static const LRHITextureVTable lrhi_metal4_swap_chain_texture_vtable = {
    .destroy_texture        = lrhi_metal4_swap_chain_texture_destroy_noop,
    .get_texture_info       = lrhi_metal4_get_texture_info,
    .texture_replace_region = lrhi_metal4_texture_replace_region,
    .texture_read_region    = lrhi_metal4_texture_read_region,
};

static const LRHISwapChainVTable lrhi_metal4_swap_chain_vtable = {
    .destroy_swap_chain  = lrhi_metal4_destroy_swap_chain,
    .get_current_texture = lrhi_metal4_swap_chain_get_current_texture,
    .present             = lrhi_metal4_swap_chain_present,
};

static const LRHIRenderPassVTable lrhi_metal4_render_pass_vtable = {
    .end = lrhi_metal4_render_pass_end,
    .intra_barrier = lrhi_metal4_render_pass_intra_barrier,
    .encoder_barrier = lrhi_metal4_render_pass_encoder_barrier,
    .set_viewport = lrhi_metal4_render_pass_set_viewport,
    .set_scissor = lrhi_metal4_render_pass_set_scissor,
    .set_push_constants = lrhi_metal4_render_pass_set_push_constants,
    .set_render_pipeline = lrhi_metal4_render_pass_set_render_pipeline,
    .set_mesh_pipeline = lrhi_metal4_render_pass_set_mesh_pipeline,
    .draw = lrhi_metal4_render_pass_draw,
    .draw_indexed = lrhi_metal4_render_pass_draw_indexed,
    .draw_mesh_tasks = lrhi_metal4_render_pass_draw_mesh_tasks,
    .execute_indirect_commands = lrhi_metal4_render_pass_execute_indirect_commands,
};

static const LRHIShaderModuleVTable lrhi_metal4_shader_module_vtable = {
    .destroy_shader_module = lrhi_metal4_destroy_shader_module,
    .get_shader_module_info = lrhi_metal4_get_shader_module_info,
};

static const LRHIRenderPipelineVTable lrhi_metal4_render_pipeline_vtable = {
    .destroy_render_pipeline = lrhi_metal4_destroy_render_pipeline,
    .get_render_pipeline_info = lrhi_metal4_get_render_pipeline_info,
    .get_alloc_size = lrhi_metal4_render_pipeline_get_alloc_size,
};

static const LRHIMeshPipelineVTable lrhi_metal4_mesh_pipeline_vtable = {
    .destroy_mesh_pipeline = lrhi_metal4_destroy_mesh_pipeline,
    .get_mesh_pipeline_info = lrhi_metal4_get_mesh_pipeline_info,
    .get_alloc_size = lrhi_metal4_mesh_pipeline_get_alloc_size,
};

static const LRHIComputePipelineVTable lrhi_metal4_compute_pipeline_vtable = {
    .destroy_compute_pipeline = lrhi_metal4_destroy_compute_pipeline,
    .get_compute_pipeline_info = lrhi_metal4_get_compute_pipeline_info,
    .get_alloc_size = lrhi_metal4_compute_pipeline_get_alloc_size,
};

static const LRHIComputePassVTable lrhi_metal4_compute_pass_vtable = {
    .end = lrhi_metal4_compute_pass_end,
    .barrier = lrhi_metal4_compute_pass_barrier,
    .encoder_barrier = lrhi_metal4_compute_pass_encoder_barrier,
    .set_push_constants = lrhi_metal4_compute_pass_set_push_constants,
    .set_pipeline = lrhi_metal4_compute_pass_set_compute_pipeline,
    .dispatch = lrhi_metal4_compute_pass_dispatch,
    .dispatch_indirect = lrhi_metal4_compute_pass_dispatch_indirect,
};

static const LRHIBufferViewVTable lrhi_metal4_buffer_view_vtable = {
    .destroy_buffer_view = lrhi_metal4_destroy_buffer_view,
    .get_buffer_view_info = lrhi_metal4_get_buffer_view_info,
    .get_bindless_index = lrhi_metal4_buffer_view_get_bindless_index,
};

static const LRHISamplerVTable lrhi_metal4_sampler_vtable = {
    .destroy_sampler = lrhi_metal4_destroy_sampler,
    .get_sampler_info = lrhi_metal4_get_sampler_info,
    .get_bindless_index = lrhi_metal4_sampler_get_bindless_index,
};

// Bindless manager
static void lrhi_metal4_bindless_manager_init(Metal4BindlessManager* manager, LRHIDeviceMetal4* device, LRHIError* out_error)
{
    manager->resource_heap_buffer = [device->device newBufferWithLength:MAX_BINDLESS_RESOURCES * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
    manager->mapped_resource_heap = (IRDescriptorTableEntry*)manager->resource_heap_buffer.contents;

    manager->sampler_heap_buffer = [device->device newBufferWithLength:MAX_BINDLESS_SAMPLERS * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
    manager->mapped_sampler_heap = (IRDescriptorTableEntry*)manager->sampler_heap_buffer.contents;

    // Create free list
    lrhi_freelist_init(&manager->resource_heap_free_list, MAX_BINDLESS_RESOURCES);
    lrhi_freelist_init(&manager->sampler_heap_free_list, MAX_BINDLESS_SAMPLERS);
}

static void lrhi_metal4_bindless_manager_destroy(Metal4BindlessManager* manager)
{
    lrhi_freelist_destroy(&manager->resource_heap_free_list);
    lrhi_freelist_destroy(&manager->sampler_heap_free_list);
}

static uint32_t lrhi_metal4_bindless_manager_find_free_resource(Metal4BindlessManager* manager)
{
    uint32_t index = lrhi_freelist_allocate(&manager->resource_heap_free_list);
    return index;
}

static uint32_t lrhi_metal4_bindless_manager_find_free_sampler(Metal4BindlessManager* manager)
{
    uint32_t index = lrhi_freelist_allocate(&manager->sampler_heap_free_list);
    return index;
}

static uint32_t lrhi_metal4_bindless_manager_write_texture_view(Metal4BindlessManager* manager, LRHITextureViewMetal4* texture_view, uint32_t index)
{
    IRDescriptorTableEntry entry;
    entry.textureViewID = texture_view->bindless_resource_id._impl;
    entry.gpuVA = 0;

    memcpy(&manager->mapped_resource_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

static uint32_t lrhi_metal4_bindless_manager_write_buffer_view(Metal4BindlessManager* manager, LRHIBufferViewMetal4* buffer_view, uint32_t index)
{
    IRDescriptorTableEntry entry;
    entry.gpuVA = buffer_view->gpu_address;

    memcpy(&manager->mapped_resource_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

static uint32_t lrhi_metal4_bindless_manager_write_sampler(Metal4BindlessManager* manager, LRHISamplerMetal4* sampler, uint32_t index)
{
    IRDescriptorTableEntry entry;
    IRDescriptorTableSetSampler(&entry, sampler->sampler_state, 0.0f);

    memcpy(&manager->mapped_sampler_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

// TODO: Write acceleration structure
static void lrhi_metal4_bindless_manager_free_resource_view(Metal4BindlessManager* manager, uint32_t index)
{
    lrhi_freelist_free(&manager->resource_heap_free_list, index);
}

static void lrhi_metal4_bindless_manager_free_sampler(Metal4BindlessManager* manager, uint32_t index)
{
    lrhi_freelist_free(&manager->sampler_heap_free_list, index);
}

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

    MTLResourceViewPoolDescriptor* resource_view_pool_desc = [[MTLResourceViewPoolDescriptor alloc] init];
    resource_view_pool_desc.resourceViewCount = MAX_BINDLESS_RESOURCES;

    NSError* pool_error = nil;
    device->texture_view_pool = [device->device newTextureViewPoolWithDescriptor:resource_view_pool_desc error:&pool_error];
    if (pool_error) {
        free(device);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create texture view pool: %s", pool_error.localizedDescription.UTF8String);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_device = NULL;
        return;
    }

    lrhi_metal4_bindless_manager_init(&device->bindless_manager, device, out_error);

    // Compile ICB conversion and reset pipelines
#define LRHI_METAL4_COMPILE_ICB_PIPE(src_str, fn_name, dst_field) \
    do { \
        NSError* _icb_err = nil; \
        id<MTLLibrary> _lib = [device->device \
            newLibraryWithSource:[NSString stringWithUTF8String:(src_str)] \
            options:nil error:&_icb_err]; \
        if (_icb_err || !_lib) { \
            if (out_error) { \
                snprintf(out_error->message, sizeof(out_error->message), \
                         "Failed to compile ICB shader '" fn_name "': %s", \
                         _icb_err ? _icb_err.localizedDescription.UTF8String : "unknown"); \
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR; \
            } \
            free(device); *out_device = NULL; return; \
        } \
        id<MTLFunction> _fn = [_lib newFunctionWithName:@fn_name]; \
        device->dst_field = [device->device newComputePipelineStateWithFunction:_fn error:&_icb_err]; \
        if (_icb_err || !device->dst_field) { \
            if (out_error) { \
                snprintf(out_error->message, sizeof(out_error->message), \
                         "Failed to create ICB pipeline '" fn_name "': %s", \
                         _icb_err ? _icb_err.localizedDescription.UTF8String : "unknown"); \
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR; \
            } \
            free(device); *out_device = NULL; return; \
        } \
    } while(0)

    LRHI_METAL4_COMPILE_ICB_PIPE(draw_icb_conversion_shader,         "encode_draws",   draw_icb_pipe);
    LRHI_METAL4_COMPILE_ICB_PIPE(draw_indexed_icb_conversion_shader, "encode_draws",   draw_indexed_icb_pipe);
    LRHI_METAL4_COMPILE_ICB_PIPE(dispatch_icb_conversion_shader,     "encode_draws",   dispatch_icb_pipe);
    LRHI_METAL4_COMPILE_ICB_PIPE(draw_mesh_icb_conversion_shader,    "encode_draws",   draw_mesh_tasks_icb_pipe);
    LRHI_METAL4_COMPILE_ICB_PIPE(reset_render_icb_commands_shader,   "reset_commands", reset_render_icb_pipe);
    LRHI_METAL4_COMPILE_ICB_PIPE(reset_compute_icb_commands_shader,  "reset_commands", reset_compute_icb_pipe);

#undef LRHI_METAL4_COMPILE_ICB_PIPE

    *out_device = (LRHIDevice)device;
}

static void lrhi_metal4_destroy_device(LRHIDevice device)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    lrhi_metal4_bindless_manager_destroy(&metal_device->bindless_manager);
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

static void lrhi_metal4_texture_set_name(LRHITexture texture, const char* name)
{
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)texture;
    metal_texture->texture.label = [NSString stringWithUTF8String:name];
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
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;
    metal_buffer->icb               = nil;
    metal_buffer->icb_params        = nil;
    metal_buffer->draw_id_atomic    = nil;
    metal_buffer->per_draw_constants = nil;
    metal_buffer->primitive_type_buf = nil;
    metal_buffer->icb_capacity_buf  = nil;
    free(buffer);
}

static void lrhi_metal4_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error)
{
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;
    metal_buffer->icb_command_type = command_type;

    uint32_t command_count = (uint32_t)(metal_buffer->info.size / metal_buffer->info.stride);

    MTLIndirectCommandBufferDescriptor* desc = [[MTLIndirectCommandBufferDescriptor alloc] init];
    desc.inheritTriangleFillMode   = YES;
    desc.inheritDepthBias          = YES;
    desc.inheritDepthClipMode      = YES;
    desc.inheritPipelineState      = YES;
    desc.inheritBuffers            = YES;
    desc.inheritDepthStencilState  = YES;
    desc.inheritFrontFacingWinding = YES;

    id<MTLDevice> dev = metal_buffer->buffer.device;

    switch (command_type) {
        case LUMINARY_RHI_COMMAND_TYPE_DRAW:
            desc.commandTypes             = MTLIndirectCommandTypeDraw;
            desc.inheritBuffers           = NO;
            desc.maxVertexBufferBindCount = 3;
            desc.maxFragmentBufferBindCount = 3;
            desc.maxObjectBufferBindCount = 3;
            desc.maxMeshBufferBindCount = 3;
            metal_buffer->draw_id_atomic    = [dev newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->per_draw_constants = [dev newBufferWithLength:(command_count * 256) options:MTLResourceStorageModeShared];
            metal_buffer->primitive_type_buf = [dev newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->icb_params        = [dev newBufferWithLength:sizeof(Metal4ICBDrawParameters) options:MTLResourceStorageModeShared];
            break;
        case LUMINARY_RHI_COMMAND_TYPE_DRAW_INDEXED:
            desc.commandTypes             = MTLIndirectCommandTypeDrawIndexed;
            desc.inheritBuffers           = NO;
            desc.maxVertexBufferBindCount = 3;
            desc.maxFragmentBufferBindCount = 3;
            desc.maxObjectBufferBindCount = 3;
            desc.maxMeshBufferBindCount = 3;
            metal_buffer->draw_id_atomic    = [dev newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->per_draw_constants = [dev newBufferWithLength:(command_count * 256) options:MTLResourceStorageModeShared];
            metal_buffer->primitive_type_buf = [dev newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->icb_params        = [dev newBufferWithLength:sizeof(Metal4ICBDrawIndexedParameters) options:MTLResourceStorageModeShared];
            break;
        case LUMINARY_RHI_COMMAND_TYPE_DISPATCH:
            desc.commandTypes  = MTLIndirectCommandTypeConcurrentDispatchThreads;
            metal_buffer->icb_params = [dev newBufferWithLength:sizeof(Metal4ICBDispatchParameters) options:MTLResourceStorageModeShared];
            break;
        case LUMINARY_RHI_COMMAND_TYPE_DRAW_MESH_TASKS:
            desc.commandTypes               = MTLIndirectCommandTypeDrawMeshThreadgroups;
            desc.inheritBuffers             = NO;
            desc.maxVertexBufferBindCount = 3;
            desc.maxFragmentBufferBindCount = 3;
            desc.maxObjectBufferBindCount = 3;
            desc.maxMeshBufferBindCount = 3;
            metal_buffer->draw_id_atomic    = [dev newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->per_draw_constants = [dev newBufferWithLength:(command_count * 256) options:MTLResourceStorageModeShared];
            metal_buffer->icb_params        = [dev newBufferWithLength:sizeof(Metal4ICBDrawMeshTasksParameters) options:MTLResourceStorageModeShared];
            break;
        default:
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Invalid indirect command type");
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            return;
    }

    metal_buffer->icb = [dev newIndirectCommandBufferWithDescriptor:desc maxCommandCount:command_count options:MTLResourceStorageModeShared];

    // Write the ICB resource ID into icb_params (it starts with MTLResourceID at offset 0 for all param types)
    MTLResourceID icb_rid = metal_buffer->icb.gpuResourceID;
    memcpy(metal_buffer->icb_params.contents, &icb_rid, sizeof(MTLResourceID));

    // Capacity buffer: used by the reset kernel to know how many commands to reset
    metal_buffer->icb_capacity_buf = [dev newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
    *(uint32_t*)metal_buffer->icb_capacity_buf.contents = command_count;
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

static void lrhi_metal4_buffer_set_name(LRHIBuffer buffer, const char* name)
{
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;
    metal_buffer->buffer.label = [NSString stringWithUTF8String:name];
}

// Command queue and fence

static void lrhi_metal4_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    id<MTL4CommandQueue> base_queue = [metal_device->device newMTL4CommandQueue];
    if (!base_queue) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command queue");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_queue = NULL;
        return;
    }

    MTLResidencySetDescriptor* internal_rs_desc = [[MTLResidencySetDescriptor alloc] init];
    internal_rs_desc.label = @"LRHIInternalResidencySet";
    NSError* rs_error = nil;
    id<MTLResidencySet> internal_residency_set = [metal_device->device newResidencySetWithDescriptor:internal_rs_desc error:&rs_error];
    if (!internal_residency_set || rs_error) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create internal residency set");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_queue = NULL;
        return;
    }
    [internal_residency_set addAllocation:metal_device->bindless_manager.resource_heap_buffer];
    [internal_residency_set addAllocation:metal_device->bindless_manager.sampler_heap_buffer];
    [base_queue addResidencySet:internal_residency_set];

    LRHICommandQueueMetal4* out = malloc(sizeof(LRHICommandQueueMetal4));
    out->base.vtable = &lrhi_metal4_command_queue_vtable;
    out->queue = base_queue;
    out->device = metal_device->device;
    out->internal_residency_set = internal_residency_set;
    out->rhi_device = metal_device;
    *out_queue = (LRHICommandQueue)out;

#ifdef LRHI_DEBUG_METAL_PROGRAMMATIC_CAPTURE
    MTLCaptureDescriptor* capture_desc = [[MTLCaptureDescriptor alloc] init];
    capture_desc.captureObject = base_queue;

    NSError* capture_error = nil;
    [[MTLCaptureManager sharedCaptureManager] startCaptureWithDescriptor:capture_desc error:&capture_error];
    if (capture_error) {
        NSLog(@"[LRHI] Metal4 programmatic capture failed to start: %@", capture_error);
    }
#endif
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
    [metal_queue->queue signalEvent:metal_fence->event value:(uint64_t)value];
}

static void lrhi_metal4_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    (void)queue;
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;

    [metal_queue->queue waitForEvent:metal_fence->event value:(uint64_t)value];
}

static void lrhi_metal4_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error)
{
    (void)out_error;
    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;

    // Wait on the fence before executing command lists
    if (wait_fence) {
        LRHIFenceMetal4* metal_wait_fence = (LRHIFenceMetal4*)wait_fence;
        [metal_queue->queue waitForEvent:metal_wait_fence->event value:wait_value];
    }

    // Execute command lists
    for (uint32_t i = 0; i < command_list_count; i++) {
        LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_lists[i];
        [metal_queue->queue commit:&metal_cmd_list->command_buffer count:1];
    }

    // Signal the fence after executing command lists
    if (signal_fence) {
        LRHIFenceMetal4* metal_signal_fence = (LRHIFenceMetal4*)signal_fence;
        [metal_queue->queue signalEvent:metal_signal_fence->event value:signal_value];
    }

#ifdef LRHI_DEBUG_METAL_PROGRAMMATIC_CAPTURE
    [[MTLCaptureManager sharedCaptureManager] stopCapture];
#endif
}

static void lrhi_metal4_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error)
{
    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;
    LRHIResidencySetMetal4* metal_residency_set = (LRHIResidencySetMetal4*)residency_set;
    [metal_queue->queue addResidencySet:metal_residency_set->residency_set];
}

// Fence

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
    [metal_fence->event setSignaledValue:value];
}

static void lrhi_metal4_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
    LRHIFenceMetal4* metal_fence = (LRHIFenceMetal4*)fence;
    uint32_t timeout_ms = (timeout_ns == UINT64_MAX) ? UINT32_MAX : (uint32_t)(timeout_ns / 1000000);
    BOOL signaled = [metal_fence->event waitUntilSignaledValue:value timeoutMS:timeout_ms];
    if (!signaled && out_error) {
        snprintf(out_error->message, sizeof(out_error->message), "Fence wait timed out");
        out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
    }
}

// Command lists

static void lrhi_metal4_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error)
{
    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;
    id<MTL4CommandAllocator> command_allocator = [metal_queue->device newCommandAllocator];
    if (!command_allocator) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command allocator");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_command_list = NULL;
        return;
    }
    [command_allocator reset];

    id<MTL4CommandBuffer> command_buffer = [metal_queue->device newCommandBuffer];
    if (!command_buffer) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create command buffer");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_command_list = NULL;
        return;
    }

    LRHICommandListMetal4* out = malloc(sizeof(LRHICommandListMetal4));
    out->base.vtable = &lrhi_metal4_command_list_vtable;
    out->command_allocator = command_allocator;
    out->command_buffer = command_buffer;
    
    MTL4ArgumentTableDescriptor* argument_table_descriptor = [[MTL4ArgumentTableDescriptor alloc] init];
    argument_table_descriptor.maxBufferBindCount = 31;
    argument_table_descriptor.maxTextureBindCount = 31;

    NSError* argument_error = nil;
    out->render_argument_table = [metal_queue->device newArgumentTableWithDescriptor:argument_table_descriptor error:&argument_error];
    out->compute_argument_table = [metal_queue->device newArgumentTableWithDescriptor:argument_table_descriptor error:&argument_error];
    out->push_constant_buffer = [metal_queue->device newBufferWithLength:1024 * 1024 options:MTLResourceStorageModeShared];
    out->push_constant_offset = 0;
    out->rhi_device = metal_queue->rhi_device;

    [metal_queue->internal_residency_set addAllocation:out->push_constant_buffer];
    [metal_queue->internal_residency_set commit];

    *out_command_list = (LRHICommandList)out;
}

static void lrhi_metal4_destroy_command_list(LRHICommandList command_list)
{
    free(command_list);
}

static void lrhi_metal4_command_list_begin(LRHICommandList command_list, LRHIError* out_error)
{
    (void)out_error;
    LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_list;
    
    [metal_cmd_list->command_buffer beginCommandBufferWithAllocator:metal_cmd_list->command_allocator];
}

static void lrhi_metal4_command_list_end(LRHICommandList command_list, LRHIError* out_error)
{
    (void)out_error;
    LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_list;
    [metal_cmd_list->command_buffer endCommandBuffer];
}

static void lrhi_metal4_command_list_reset(LRHICommandList command_list, LRHIError* out_error)
{
    (void)out_error;
    LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_list;
    [metal_cmd_list->command_allocator reset];
    metal_cmd_list->push_constant_offset = 0;
}

static LRHICopyPass lrhi_metal4_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error)
{
    (void)out_error;
    LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_list;
    id<MTL4ComputeCommandEncoder> blit_encoder = [metal_cmd_list->command_buffer computeCommandEncoder];
    if (!blit_encoder) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create blit command encoder for copy pass");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    LRHICopyPassMetal4* copy_pass = malloc(sizeof(LRHICopyPassMetal4));
    copy_pass->base.vtable = &lrhi_metal4_copy_pass_vtable;
    copy_pass->blit_encoder = blit_encoder;
    return (LRHICopyPass)copy_pass;
}

static void lrhi_metal4_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error)
{
    LRHICommandListMetal4* metal_cmd_list  = (LRHICommandListMetal4*)command_list;
    LRHIBufferMetal4*      metal_buffer    = (LRHIBufferMetal4*)indirect_command_buffer;
    LRHIBufferMetal4*      metal_count_buf = (LRHIBufferMetal4*)count_buffer;
    LRHIDeviceMetal4*      device          = metal_cmd_list->rhi_device;

    // Write push constants into slot 0 of per_draw_constants
    if (metal_buffer->per_draw_constants && push_constants && push_constant_size > 0) {
        LRHIArgumentBufferData* slot0 = (LRHIArgumentBufferData*)metal_buffer->per_draw_constants.contents;
        uint32_t copy_size = push_constant_size < 128 ? push_constant_size : 128;
        memcpy(slot0->push_constants, push_constants, copy_size);
        slot0->draw_id = 0;
    }

    // Write primitive type for render pipelines
    if (metal_buffer->primitive_type_buf && pipeline) {
        LRHIRenderPipelineMetal4* metal_pipe = (LRHIRenderPipelineMetal4*)pipeline;
        LRHIPipelineTopology topo = metal_pipe->info.topology;
        uint32_t prim = (topo == LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST) ? 0 :
                        (topo == LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST)  ? 1 : 2;
        *(uint32_t*)metal_buffer->primitive_type_buf.contents = prim;
    }

    // Reset draw_id_atomic from CPU (shared storage)
    if (metal_buffer->draw_id_atomic)
        *(uint32_t*)metal_buffer->draw_id_atomic.contents = 0;

    // Phase A: Reset the ICB via compute kernel
    {
        id<MTLComputePipelineState> reset_pipe =
            (metal_buffer->icb_command_type == LUMINARY_RHI_COMMAND_TYPE_DISPATCH)
            ? device->reset_compute_icb_pipe
            : device->reset_render_icb_pipe;

        id<MTL4ComputeCommandEncoder> reset_enc = [metal_cmd_list->command_buffer computeCommandEncoder];
        [reset_enc setComputePipelineState:reset_pipe];
        [metal_cmd_list->compute_argument_table setAddress:metal_buffer->icb_params.gpuAddress        atIndex:0];
        [metal_cmd_list->compute_argument_table setAddress:metal_buffer->icb_capacity_buf.gpuAddress  atIndex:1];
        [reset_enc setArgumentTable:metal_cmd_list->compute_argument_table];
        uint32_t cap = *(uint32_t*)metal_buffer->icb_capacity_buf.contents;
        [reset_enc dispatchThreads:MTLSizeMake(cap, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [reset_enc endEncoding];
    }

    // Phase B: ICB conversion kernel
    {
        id<MTL4ComputeCommandEncoder> convert_enc = [metal_cmd_list->command_buffer computeCommandEncoder];
        [convert_enc barrierAfterQueueStages:MTLStageDispatch beforeStages:MTLStageDispatch
                           visibilityOptions:MTL4VisibilityOptionDevice];

        switch (metal_buffer->icb_command_type) {
            case LUMINARY_RHI_COMMAND_TYPE_DRAW: {
                [convert_enc setComputePipelineState:device->draw_icb_pipe];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->buffer.gpuAddress               atIndex:0];
                [metal_cmd_list->compute_argument_table setAddress:metal_count_buf->buffer.gpuAddress             atIndex:1];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->primitive_type_buf.gpuAddress   atIndex:2];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->icb_params.gpuAddress           atIndex:3];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->per_draw_constants.gpuAddress   atIndex:4];
                [metal_cmd_list->compute_argument_table setAddress:device->bindless_manager.resource_heap_buffer.gpuAddress atIndex:5];
                [metal_cmd_list->compute_argument_table setAddress:device->bindless_manager.sampler_heap_buffer.gpuAddress  atIndex:6];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->draw_id_atomic.gpuAddress       atIndex:7];
                [convert_enc setArgumentTable:metal_cmd_list->compute_argument_table];
                [convert_enc dispatchThreads:MTLSizeMake(maxCommandCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                break;
            }
            case LUMINARY_RHI_COMMAND_TYPE_DRAW_INDEXED: {
                Metal4ICBDrawIndexedParameters* params = (Metal4ICBDrawIndexedParameters*)metal_buffer->icb_params.contents;
                params->index_buffer = ((LRHIBufferMetal4*)parameters->index_buffer)->buffer.gpuAddress;

                [convert_enc setComputePipelineState:device->draw_indexed_icb_pipe];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->buffer.gpuAddress               atIndex:0];
                [metal_cmd_list->compute_argument_table setAddress:metal_count_buf->buffer.gpuAddress             atIndex:1];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->primitive_type_buf.gpuAddress   atIndex:2];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->icb_params.gpuAddress           atIndex:3];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->per_draw_constants.gpuAddress   atIndex:4];
                [metal_cmd_list->compute_argument_table setAddress:device->bindless_manager.resource_heap_buffer.gpuAddress atIndex:5];
                [metal_cmd_list->compute_argument_table setAddress:device->bindless_manager.sampler_heap_buffer.gpuAddress  atIndex:6];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->draw_id_atomic.gpuAddress       atIndex:7];
                [convert_enc setArgumentTable:metal_cmd_list->compute_argument_table];
                [convert_enc dispatchThreads:MTLSizeMake(maxCommandCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                break;
            }
            case LUMINARY_RHI_COMMAND_TYPE_DISPATCH: {
                Metal4ICBDispatchParameters* params = (Metal4ICBDispatchParameters*)metal_buffer->icb_params.contents;
                params->threads_per_group_x = parameters->threads_per_group_x;
                params->threads_per_group_y = parameters->threads_per_group_y;
                params->threads_per_group_z = parameters->threads_per_group_z;

                [convert_enc setComputePipelineState:device->dispatch_icb_pipe];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->buffer.gpuAddress   atIndex:0];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->icb_params.gpuAddress atIndex:1];
                [convert_enc setArgumentTable:metal_cmd_list->compute_argument_table];
                [convert_enc dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
                break;
            }
            case LUMINARY_RHI_COMMAND_TYPE_DRAW_MESH_TASKS: {
                Metal4ICBDrawMeshTasksParameters* params = (Metal4ICBDrawMeshTasksParameters*)metal_buffer->icb_params.contents;
                params->threads_per_object_group_x = parameters->threads_per_object_groups_x;
                params->threads_per_object_group_y = parameters->threads_per_object_groups_y;
                params->threads_per_object_group_z = parameters->threads_per_object_groups_z;
                params->threads_per_mesh_group_x   = parameters->threads_per_mesh_groups_x;
                params->threads_per_mesh_group_y   = parameters->threads_per_mesh_groups_y;
                params->threads_per_mesh_group_z   = parameters->threads_per_mesh_groups_z;

                [convert_enc setComputePipelineState:device->draw_mesh_tasks_icb_pipe];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->buffer.gpuAddress               atIndex:0];
                [metal_cmd_list->compute_argument_table setAddress:metal_count_buf->buffer.gpuAddress             atIndex:1];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->icb_params.gpuAddress           atIndex:2];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->per_draw_constants.gpuAddress   atIndex:3];
                [metal_cmd_list->compute_argument_table setAddress:device->bindless_manager.resource_heap_buffer.gpuAddress atIndex:4];
                [metal_cmd_list->compute_argument_table setAddress:device->bindless_manager.sampler_heap_buffer.gpuAddress  atIndex:5];
                [metal_cmd_list->compute_argument_table setAddress:metal_buffer->draw_id_atomic.gpuAddress       atIndex:6];
                [convert_enc setArgumentTable:metal_cmd_list->compute_argument_table];
                [convert_enc dispatchThreads:MTLSizeMake(maxCommandCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                break;
            }
        }

        [convert_enc endEncoding];
    }
}

// Copy pass

static void lrhi_metal4_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    [metal_copy_pass->blit_encoder endEncoding];
    free(metal_copy_pass);
}

static void lrhi_metal4_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error)
{
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    [metal_copy_pass->blit_encoder barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
}

static void lrhi_metal4_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    [metal_copy_pass->blit_encoder barrierAfterQueueStages:lrhi_metal4_render_stage_to_mtl(afterStage) beforeStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];
}

static void lrhi_metal4_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    LRHITextureMetal4* metal_src_texture = (LRHITextureMetal4*)src_texture;
    LRHITextureMetal4* metal_dst_texture = (LRHITextureMetal4*)dst_texture;

    MTLOrigin src_origin = MTLOriginMake(src_region.x, src_region.y, src_region.z);
    MTLSize src_size = MTLSizeMake(src_region.width, src_region.height, src_region.depth);
    MTLOrigin dst_origin = MTLOriginMake(dst_region.x, dst_region.y, dst_region.z);

    [metal_copy_pass->blit_encoder copyFromTexture:metal_src_texture->texture sourceSlice:src_array_layer sourceLevel:src_mip_level sourceOrigin:src_origin sourceSize:src_size toTexture:metal_dst_texture->texture destinationSlice:dst_array_layer destinationLevel:dst_mip_level destinationOrigin:dst_origin];
}

static void lrhi_metal4_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error)
{
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    LRHIBufferMetal4* metal_src_buffer = (LRHIBufferMetal4*)src_buffer;
    LRHIBufferMetal4* metal_dst_buffer = (LRHIBufferMetal4*)dst_buffer;

    [metal_copy_pass->blit_encoder copyFromBuffer:metal_src_buffer->buffer sourceOffset:src_offset toBuffer:metal_dst_buffer->buffer destinationOffset:dst_offset size:size];
}

static void lrhi_metal4_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    LRHIBufferMetal4* metal_src_buffer = (LRHIBufferMetal4*)src_buffer;
    LRHITextureMetal4* metal_dst_texture = (LRHITextureMetal4*)dst_texture;

    MTLOrigin dst_origin = MTLOriginMake(dst_region.x, dst_region.y, dst_region.z);
    MTLSize dst_size = MTLSizeMake(dst_region.width, dst_region.height, dst_region.depth);

    [metal_copy_pass->blit_encoder copyFromBuffer:metal_src_buffer->buffer sourceOffset:src_offset sourceBytesPerRow:src_bytes_per_row sourceBytesPerImage:src_bytes_per_image sourceSize:dst_size toTexture:metal_dst_texture->texture destinationSlice:dst_array_layer destinationLevel:dst_mip_level destinationOrigin:dst_origin];
}

static void lrhi_metal4_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error)
{
    LRHICopyPassMetal4* metal_copy_pass = (LRHICopyPassMetal4*)copy_pass;
    LRHITextureMetal4* metal_src_texture = (LRHITextureMetal4*)src_texture;
    LRHIBufferMetal4* metal_dst_buffer = (LRHIBufferMetal4*)dst_buffer;

    MTLOrigin src_origin = MTLOriginMake(src_region.x, src_region.y, src_region.z);
    MTLSize src_size = MTLSizeMake(src_region.width, src_region.height, src_region.depth);

    [metal_copy_pass->blit_encoder copyFromTexture:metal_src_texture->texture sourceSlice:src_array_layer sourceLevel:src_mip_level sourceOrigin:src_origin sourceSize:src_size toBuffer:metal_dst_buffer->buffer destinationOffset:dst_offset destinationBytesPerRow:dst_bytes_per_row destinationBytesPerImage:dst_bytes_per_image];
}

// Residency set

static void lrhi_metal4_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

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

    LRHIResidencySetMetal4* out = malloc(sizeof(LRHIResidencySetMetal4));
    out->base.vtable = &lrhi_metal4_residency_set_vtable;
    out->residency_set = residency_set;
    *out_residency_set = (LRHIResidencySet)out;
}

static void lrhi_metal4_destroy_residency_set(LRHIResidencySet residency_set)
{
    free(residency_set);
}

static void lrhi_metal4_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    LRHIResidencySetMetal4* metal_residency_set = (LRHIResidencySetMetal4*)residency_set;
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)texture;

    [metal_residency_set->residency_set addAllocation:metal_texture->texture];    
}

static void lrhi_metal4_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIResidencySetMetal4* metal_residency_set = (LRHIResidencySetMetal4*)residency_set;
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;

    [metal_residency_set->residency_set addAllocation:metal_buffer->buffer];

    // Also make ICB sub-buffers and the ICB itself resident when the user adds this buffer
    if (metal_buffer->icb)               [metal_residency_set->residency_set addAllocation:metal_buffer->icb];
    if (metal_buffer->icb_params)        [metal_residency_set->residency_set addAllocation:metal_buffer->icb_params];
    if (metal_buffer->icb_capacity_buf)  [metal_residency_set->residency_set addAllocation:metal_buffer->icb_capacity_buf];
    if (metal_buffer->draw_id_atomic)    [metal_residency_set->residency_set addAllocation:metal_buffer->draw_id_atomic];
    if (metal_buffer->per_draw_constants)[metal_residency_set->residency_set addAllocation:metal_buffer->per_draw_constants];
    if (metal_buffer->primitive_type_buf)[metal_residency_set->residency_set addAllocation:metal_buffer->primitive_type_buf];
}

static void lrhi_metal4_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
    LRHIResidencySetMetal4* metal_residency_set = (LRHIResidencySetMetal4*)residency_set;
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)texture;

    [metal_residency_set->residency_set removeAllocation:metal_texture->texture];
}

static void lrhi_metal4_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
    LRHIResidencySetMetal4* metal_residency_set = (LRHIResidencySetMetal4*)residency_set;
    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)buffer;

    [metal_residency_set->residency_set removeAllocation:metal_buffer->buffer];
}

static void lrhi_metal4_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error)
{
    LRHIResidencySetMetal4* metal_residency_set = (LRHIResidencySetMetal4*)residency_set;
    [metal_residency_set->residency_set commit];
}

// Texture view

static void lrhi_metal4_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error)
{
    // If info is same as base texture, don't create a view.
    // Otherwise, create a new texture view with the same underlying texture but different descriptor.
    LRHITextureMetal4* metal_texture = (LRHITextureMetal4*)info->texture;
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    if (info->format == LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED) {
        info->format = metal_texture->info.format;
    }

    // Create the view
    LRHITextureViewMetal4* out = malloc(sizeof(LRHITextureViewMetal4));
    out->base.vtable = &lrhi_metal4_texture_view_vtable;
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
    if (info->usage == LUMINARY_RHI_TEXTURE_USAGE_SAMPLED || info->usage == LUMINARY_RHI_TEXTURE_USAGE_STORAGE) {
        // Bindless views always need a descriptor heap slot, even when matching the base texture.
        MTLTextureViewDescriptor* descriptor = [[MTLTextureViewDescriptor alloc] init];
        descriptor.pixelFormat = lrhi_metal4_pixel_format(info->format);
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
        descriptor.textureType = lrhi_metal4_texture_type(info->dimensions);

        out->bindless_index = lrhi_metal4_bindless_manager_find_free_resource(&metal_device->bindless_manager);
        out->bindless_resource_id = [metal_device->texture_view_pool setTextureView:metal_texture->texture descriptor:descriptor atIndex:out->bindless_index];
        lrhi_metal4_bindless_manager_write_texture_view(&metal_device->bindless_manager, out, out->bindless_index);
    } else if (!is_same_as_base_texture) {
        MTLTextureViewDescriptor* descriptor = [[MTLTextureViewDescriptor alloc] init];
        descriptor.pixelFormat = lrhi_metal4_pixel_format(info->format);
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
        descriptor.textureType = lrhi_metal4_texture_type(info->dimensions);

        out->texture_view = [metal_texture->texture newTextureViewWithDescriptor:descriptor];
    } else {
        out->texture_view = metal_texture->texture;
    }
    if (!out->texture_view && out->bindless_index == UINT32_MAX) {
        free(out);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create texture view");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_texture_view = NULL;
        return;
    }
    out->bindless_manager = &metal_device->bindless_manager;
    *out_texture_view = (LRHITextureView)out;
}

static void lrhi_metal4_destroy_texture_view(LRHITextureView texture_view)
{
    LRHITextureViewMetal4* metal_texture_view = (LRHITextureViewMetal4*)texture_view;
    if (metal_texture_view->bindless_index != UINT32_MAX) {
        lrhi_metal4_bindless_manager_free_resource_view(metal_texture_view->bindless_manager, metal_texture_view->bindless_index);
    }
    free(texture_view);
}

static void lrhi_metal4_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info)
{
    LRHITextureViewMetal4* metal_texture_view = (LRHITextureViewMetal4*)texture_view;
    *out_info = metal_texture_view->info;
}

static uint32_t lrhi_metal4_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error)
{
    LRHITextureViewMetal4* metal_texture_view = (LRHITextureViewMetal4*)texture_view;
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

static LRHIRenderPass lrhi_metal4_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error)
{
    LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_list;

    MTL4RenderPassDescriptor* render_pass_descriptor = [[MTL4RenderPassDescriptor alloc] init];
    render_pass_descriptor.renderTargetWidth = info->render_width;
    render_pass_descriptor.renderTargetHeight = info->render_height;
    for (int i = 0; i < info->color_attachment_count; i++) {
        LRHIRenderPassAttachmentInfo* attachment = &info->color_attachments[i];
        LRHITextureViewMetal4* metal_texture_view = (LRHITextureViewMetal4*)attachment->texture_view;
        render_pass_descriptor.colorAttachments[i].texture = metal_texture_view->texture_view;
        render_pass_descriptor.colorAttachments[i].loadAction = lrhi_metal4_load_action_to_mtl(attachment->load_action);
        render_pass_descriptor.colorAttachments[i].storeAction = lrhi_metal4_store_action_to_mtl(attachment->store_action);
        render_pass_descriptor.colorAttachments[i].clearColor = MTLClearColorMake(attachment->clear_color[0], attachment->clear_color[1], attachment->clear_color[2], attachment->clear_color[3]);
    }
    if (info->has_depth_stencil_attachment) {
        LRHIRenderPassAttachmentInfo* attachment = &info->depth_stencil_attachment;
        LRHITextureViewMetal4* metal_texture_view = (LRHITextureViewMetal4*)attachment->texture_view;
        render_pass_descriptor.depthAttachment.texture = metal_texture_view->texture_view;
        render_pass_descriptor.depthAttachment.loadAction = lrhi_metal4_load_action_to_mtl(attachment->load_action);
        render_pass_descriptor.depthAttachment.storeAction = lrhi_metal4_store_action_to_mtl(attachment->store_action);
        render_pass_descriptor.depthAttachment.clearDepth = attachment->clear_depth;

        render_pass_descriptor.stencilAttachment.texture = metal_texture_view->texture_view;
        render_pass_descriptor.stencilAttachment.loadAction = lrhi_metal4_load_action_to_mtl(attachment->load_action);
        render_pass_descriptor.stencilAttachment.storeAction = lrhi_metal4_store_action_to_mtl(attachment->store_action);
        render_pass_descriptor.stencilAttachment.clearStencil = attachment->clear_stencil;
    }

    id<MTL4RenderCommandEncoder> render_encoder = [metal_cmd_list->command_buffer renderCommandEncoderWithDescriptor:render_pass_descriptor];
    if (!render_encoder) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create render command encoder for render pass");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    LRHIRenderPassMetal4* render_pass = calloc(1, sizeof(LRHIRenderPassMetal4));
    render_pass->base.vtable = &lrhi_metal4_render_pass_vtable;
    render_pass->render_encoder = render_encoder;
    render_pass->command_list = metal_cmd_list;
    
    [render_encoder setArgumentTable:metal_cmd_list->render_argument_table atStages:MTLRenderStageVertex | MTLRenderStageFragment];
    
    return (LRHIRenderPass)render_pass;
}

static void lrhi_metal4_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    [metal_render_pass->render_encoder endEncoding];
    free(metal_render_pass);
}

static void lrhi_metal4_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    [metal_render_pass->render_encoder barrierAfterEncoderStages:lrhi_metal4_render_stage_to_mtl(afterStage) beforeEncoderStages:lrhi_metal4_render_stage_to_mtl(beforeStage) visibilityOptions:MTL4VisibilityOptionDevice];
}

static void lrhi_metal4_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    [metal_render_pass->render_encoder barrierAfterQueueStages:lrhi_metal4_render_stage_to_mtl(afterStage) beforeStages:lrhi_metal4_render_stage_to_mtl(beforeStage) visibilityOptions:MTL4VisibilityOptionDevice];
}

static void lrhi_metal4_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    uint32_t copy_size = size < 128 ? size : 128;
    memcpy(metal_render_pass->current_push_constants, data, copy_size);
}

static void lrhi_metal4_flush_push_constants(LRHIRenderPassMetal4* render_pass, LRHIError* out_error)
{
    LRHICommandListMetal4* cmd = render_pass->command_list;

    if (cmd->push_constant_offset + 256 > (uint32_t)cmd->push_constant_buffer.length) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Push constant linear allocator exhausted");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    uint32_t offset = cmd->push_constant_offset;
    LRHIArgumentBufferData* slot = (LRHIArgumentBufferData*)((uint8_t*)cmd->push_constant_buffer.contents + offset);
    memcpy(slot->push_constants, render_pass->current_push_constants, 128);
    slot->draw_id = render_pass->current_draw_id++;
    cmd->push_constant_offset += 256;

    [cmd->render_argument_table setAddress:cmd->push_constant_buffer.gpuAddress + offset atIndex:kIRArgumentBufferBindPoint];
}

static void lrhi_metal4_compute_pass_flush_push_constants(LRHIComputePassMetal4* compute_pass, LRHIError* out_error)
{
    LRHICommandListMetal4* cmd = compute_pass->command_list;

    if (cmd->push_constant_offset + 256 > (uint32_t)cmd->push_constant_buffer.length) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Push constant linear allocator exhausted");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    uint32_t offset = cmd->push_constant_offset;
    LRHIArgumentBufferData* slot = (LRHIArgumentBufferData*)((uint8_t*)cmd->push_constant_buffer.contents + offset);
    memcpy(slot->push_constants, compute_pass->current_push_constants, 128);
    cmd->push_constant_offset += 256;

    [cmd->compute_argument_table setAddress:cmd->push_constant_buffer.gpuAddress + offset atIndex:kIRArgumentBufferBindPoint];
}

static void lrhi_metal4_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    MTLViewport viewport = {
        .originX = x,
        .originY = y,
        .width = width,
        .height = height,
        .znear = min_depth,
        .zfar = max_depth
    };
    [metal_render_pass->render_encoder setViewport:viewport];
}

static void lrhi_metal4_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    MTLScissorRect scissor_rect = {
        .x = x,
        .y = y,
        .width = width,
        .height = height
    };
    [metal_render_pass->render_encoder setScissorRect:scissor_rect];
}

static void lrhi_metal4_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    LRHIRenderPipelineMetal4* metal_pipeline = (LRHIRenderPipelineMetal4*)pipeline;
    [metal_render_pass->render_encoder setRenderPipelineState:metal_pipeline->pipeline_state];
    if (metal_pipeline->info.depth_test_enable) {
        [metal_render_pass->render_encoder setDepthStencilState:metal_pipeline->depth_stencil_state];
    }
    [metal_render_pass->render_encoder setTriangleFillMode:lrhi_metal4_fill_mode_to_mtl(metal_pipeline->info.fill_mode)];
    [metal_render_pass->render_encoder setCullMode:lrhi_metal4_cull_mode_to_mtl(metal_pipeline->info.cull_mode)];
    [metal_render_pass->render_encoder setFrontFacingWinding:lrhi_metal4_front_face_to_mtl(metal_pipeline->info.front_face)];

    Metal4BindlessManager* bindless_manager = metal_pipeline->bindless_manager;
    [metal_render_pass->command_list->render_argument_table setAddress:bindless_manager->resource_heap_buffer.gpuAddress atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->command_list->render_argument_table setAddress:bindless_manager->sampler_heap_buffer.gpuAddress atIndex:kIRSamplerHeapBindPoint];

    metal_render_pass->current_render_pipeline = pipeline;
}

static void lrhi_metal4_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    LRHIMeshPipelineMetal4* metal_pipeline = (LRHIMeshPipelineMetal4*)pipeline;
    [metal_render_pass->render_encoder setRenderPipelineState:metal_pipeline->pipeline_state];
    if (metal_pipeline->info.depth_test_enable) {
        [metal_render_pass->render_encoder setDepthStencilState:metal_pipeline->depth_stencil_state];
    }
    [metal_render_pass->render_encoder setTriangleFillMode:lrhi_metal4_fill_mode_to_mtl(metal_pipeline->info.fill_mode)];
    [metal_render_pass->render_encoder setCullMode:lrhi_metal4_cull_mode_to_mtl(metal_pipeline->info.cull_mode)];
    [metal_render_pass->render_encoder setFrontFacingWinding:lrhi_metal4_front_face_to_mtl(metal_pipeline->info.front_face)];

    Metal4BindlessManager* bindless_manager = metal_pipeline->bindless_manager;
    [metal_render_pass->command_list->render_argument_table setAddress:bindless_manager->resource_heap_buffer.gpuAddress atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->command_list->render_argument_table setAddress:bindless_manager->sampler_heap_buffer.gpuAddress atIndex:kIRSamplerHeapBindPoint];
}

static void lrhi_metal4_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    LRHICommandListMetal4* metal_cmd_list = metal_render_pass->command_list;
    MTLPrimitiveType primitive_type = lrhi_metal4_primitive_topology_to_mtl(metal_render_pass->current_render_pipeline ? ((LRHIRenderPipelineMetal4*)metal_render_pass->current_render_pipeline)->info.topology : LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST);

    lrhi_metal4_flush_push_constants(metal_render_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    // TODO: Check for if we're using a Metal Shader Converter pipeline or not.
    uint32_t required_size = sizeof(IRRuntimeDrawParams);
    required_size = (required_size + 255) & ~255;
    
    if (metal_cmd_list->push_constant_offset + required_size <= metal_cmd_list->push_constant_buffer.length) {
        IRRuntimeDrawParams* draw_arg = (IRRuntimeDrawParams*)((uint8_t*)metal_cmd_list->push_constant_buffer.contents + metal_cmd_list->push_constant_offset);
        draw_arg->draw.vertexCountPerInstance = vertex_count;
        draw_arg->draw.instanceCount = instance_count;
        draw_arg->draw.startVertexLocation = first_vertex;
        draw_arg->draw.startInstanceLocation = first_instance;
        
        [metal_cmd_list->render_argument_table setAddress:metal_cmd_list->push_constant_buffer.gpuAddress + metal_cmd_list->push_constant_offset atIndex:kIRArgumentBufferDrawArgumentsBindPoint];
        metal_cmd_list->push_constant_offset += required_size;
    }
    
    uint32_t draw_type_size = sizeof(uint16_t);
    draw_type_size = (draw_type_size + 255) & ~255;
    
    if (metal_cmd_list->push_constant_offset + draw_type_size <= metal_cmd_list->push_constant_buffer.length) {
        uint16_t* draw_type_ptr = (uint16_t*)((uint8_t*)metal_cmd_list->push_constant_buffer.contents + metal_cmd_list->push_constant_offset);
        *draw_type_ptr = kIRNonIndexedDraw;
        
        [metal_cmd_list->render_argument_table setAddress:metal_cmd_list->push_constant_buffer.gpuAddress + metal_cmd_list->push_constant_offset atIndex:kIRArgumentBufferUniformsBindPoint];
        metal_cmd_list->push_constant_offset += draw_type_size;
    }

    [metal_render_pass->render_encoder drawPrimitives:primitive_type vertexStart:first_vertex vertexCount:vertex_count instanceCount:instance_count baseInstance:first_instance];
}

static void lrhi_metal4_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    LRHIBufferMetal4* metal_index_buffer = (LRHIBufferMetal4*)index_buffer;
    LRHICommandListMetal4* metal_cmd_list = metal_render_pass->command_list;

    MTLPrimitiveType primitive_type = lrhi_metal4_primitive_topology_to_mtl(metal_render_pass->current_render_pipeline ? ((LRHIRenderPipelineMetal4*)metal_render_pass->current_render_pipeline)->info.topology : LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST);
    MTLIndexType mtl_index_type = (index_stride == 4) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;

    lrhi_metal4_flush_push_constants(metal_render_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    // TODO: Check for if we're using a Metal Shader Converter pipeline or not.
    uint32_t required_size = sizeof(IRRuntimeDrawParams);
    required_size = (required_size + 255) & ~255;
    
    if (metal_cmd_list->push_constant_offset + required_size <= metal_cmd_list->push_constant_buffer.length) {
        IRRuntimeDrawParams* draw_arg = (IRRuntimeDrawParams*)((uint8_t*)metal_cmd_list->push_constant_buffer.contents + metal_cmd_list->push_constant_offset);
        draw_arg->drawIndexed.indexCountPerInstance = index_count;
        draw_arg->drawIndexed.instanceCount = instance_count;
        draw_arg->drawIndexed.baseVertexLocation = vertex_offset;
        draw_arg->drawIndexed.startInstanceLocation = first_instance;
        draw_arg->drawIndexed.startIndexLocation = first_index;
        
        [metal_cmd_list->render_argument_table setAddress:metal_cmd_list->push_constant_buffer.gpuAddress + metal_cmd_list->push_constant_offset atIndex:kIRArgumentBufferDrawArgumentsBindPoint];
        metal_cmd_list->push_constant_offset += required_size;
    }
    
    uint32_t draw_type_size = sizeof(uint16_t);
    draw_type_size = (draw_type_size + 255) & ~255;
    
    if (metal_cmd_list->push_constant_offset + draw_type_size <= metal_cmd_list->push_constant_buffer.length) {
        uint16_t* draw_type_ptr = (uint16_t*)((uint8_t*)metal_cmd_list->push_constant_buffer.contents + metal_cmd_list->push_constant_offset);
        *draw_type_ptr = mtl_index_type + 1;
        
        [metal_cmd_list->render_argument_table setAddress:metal_cmd_list->push_constant_buffer.gpuAddress + metal_cmd_list->push_constant_offset atIndex:kIRArgumentBufferUniformsBindPoint];
        metal_cmd_list->push_constant_offset += draw_type_size;
    }

    [metal_render_pass->render_encoder drawIndexedPrimitives:primitive_type indexCount:index_count indexType:mtl_index_type indexBuffer:metal_index_buffer->buffer.gpuAddress + (first_index * index_stride) indexBufferLength:metal_index_buffer->buffer.allocatedSize - (first_index * index_stride) instanceCount:instance_count baseVertex:vertex_offset baseInstance:first_instance];
}

static void lrhi_metal4_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error)
{
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;

    lrhi_metal4_flush_push_constants(metal_render_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    [metal_render_pass->render_encoder drawMeshThreadgroups:MTLSizeMake(num_groups_x, num_groups_y, num_groups_z) threadsPerObjectThreadgroup:MTLSizeMake(threads_per_object_group_x, threads_per_object_group_y, threads_per_object_group_z) threadsPerMeshThreadgroup:MTLSizeMake(threads_per_mesh_group_x, threads_per_mesh_group_y, threads_per_mesh_group_z)];
}

static void lrhi_metal4_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error)
{
    (void)count_buffer;
    (void)out_error;
    LRHIRenderPassMetal4* metal_render_pass = (LRHIRenderPassMetal4*)render_pass;
    LRHIBufferMetal4*     metal_icb         = (LRHIBufferMetal4*)indirect_command_buffer;
    [metal_render_pass->render_encoder executeCommandsInBuffer:metal_icb->icb withRange:NSMakeRange(0, max_command_count)];
}

// Shader module

static void lrhi_metal4_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

    NSError* error = nil;
    dispatch_data_t shader_data = dispatch_data_create(info->code, info->code_size, dispatch_get_main_queue(), NULL);
    id<MTLLibrary> library = [metal_device->device newLibraryWithData:shader_data error:&error];
    id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:info->entry_point]];

    if (!library || !function || error) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create shader module: %s", [[error localizedDescription] UTF8String]);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_shader_module = NULL;
        return;
    }

    LRHIShaderModuleMetal4* out = malloc(sizeof(LRHIShaderModuleMetal4));
    out->base.vtable = &lrhi_metal4_shader_module_vtable;
    out->library = library;
    out->function = function;
    out->info = *info;
    *out_shader_module = (LRHIShaderModule)out;
}

static void lrhi_metal4_destroy_shader_module(LRHIShaderModule shader_module)
{
    free(shader_module);
}

static void lrhi_metal4_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info)
{
    LRHIShaderModuleMetal4* metal_shader_module = (LRHIShaderModuleMetal4*)shader_module;
    *out_info = metal_shader_module->info;
}

// Render pipeline

static void lrhi_metal4_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = ((LRHIShaderModuleMetal4*)info->vertex_shader)->function;
    if (info->fragment_shader) {
        descriptor.fragmentFunction = ((LRHIShaderModuleMetal4*)info->fragment_shader)->function;
    }
    for (uint32_t i = 0; i < info->render_target_count; i++) {
        descriptor.colorAttachments[i].pixelFormat = lrhi_metal4_pixel_format(info->render_target_formats[i]);
    }
    if (info->depth_test_enable || info->stencil_test_enable) {
        descriptor.depthAttachmentPixelFormat = lrhi_metal4_pixel_format(info->depth_stencil_format);
        descriptor.stencilAttachmentPixelFormat = lrhi_metal4_pixel_format(info->depth_stencil_format);
    }
    descriptor.inputPrimitiveTopology = lrhi_metal4_primitive_topology_class_to_mtl(info->topology);
    descriptor.rasterizationEnabled = YES;
    if (info->supports_indirect_commands) {
        descriptor.supportIndirectCommandBuffers = YES;
    }

    // Blending
    for (uint32_t i = 0; i < info->render_target_count; i++) {
        if (info->blend_enable[i]) {
            descriptor.colorAttachments[i].blendingEnabled = YES;
            descriptor.colorAttachments[i].sourceRGBBlendFactor = lrhi_metal4_blend_factor_to_mtl(info->blend_src_rgb_factor[i]);
            descriptor.colorAttachments[i].destinationRGBBlendFactor = lrhi_metal4_blend_factor_to_mtl(info->blend_dst_rgb_factor[i]);
            descriptor.colorAttachments[i].rgbBlendOperation = lrhi_metal4_blend_op_to_mtl(info->blend_rgb_op[i]);
            descriptor.colorAttachments[i].sourceAlphaBlendFactor = lrhi_metal4_blend_factor_to_mtl(info->blend_src_alpha_factor[i]);
            descriptor.colorAttachments[i].destinationAlphaBlendFactor = lrhi_metal4_blend_factor_to_mtl(info->blend_dst_alpha_factor[i]);
            descriptor.colorAttachments[i].alphaBlendOperation = lrhi_metal4_blend_op_to_mtl(info->blend_alpha_op[i]);
        }
    }

    // Depth stencil state
    id<MTLDepthStencilState> depth_stencil_state = nil;
    if (info->depth_test_enable || info->stencil_test_enable) {
        MTLDepthStencilDescriptor* depth_stencil_descriptor = [[MTLDepthStencilDescriptor alloc] init];
        depth_stencil_descriptor.depthCompareFunction = lrhi_metal4_compare_op_to_mtl(info->depth_compare_op);
        depth_stencil_descriptor.depthWriteEnabled = info->depth_write_enable;
        // TODO: Stencil state

        depth_stencil_state = [metal_device->device newDepthStencilStateWithDescriptor:depth_stencil_descriptor];
    }

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline_state = [metal_device->device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!pipeline_state || error) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create render pipeline: %s", [[error localizedDescription] UTF8String]);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_pipeline = NULL;
        return;
    }

    LRHIRenderPipelineMetal4* out = malloc(sizeof(LRHIRenderPipelineMetal4));
    out->base.vtable = &lrhi_metal4_render_pipeline_vtable;
    out->pipeline_state = pipeline_state;
    out->depth_stencil_state = depth_stencil_state;
    out->info = *info;
    out->bindless_manager = &metal_device->bindless_manager;
    *out_pipeline = (LRHIRenderPipeline)out;
}

static void lrhi_metal4_destroy_render_pipeline(LRHIRenderPipeline pipeline)
{
    free(pipeline);
}

static void lrhi_metal4_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info)
{
    LRHIRenderPipelineMetal4* metal_pipeline = (LRHIRenderPipelineMetal4*)pipeline;
    *out_info = metal_pipeline->info;
}

static uint64_t lrhi_metal4_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    LRHIRenderPipelineMetal4* metal_pipeline = (LRHIRenderPipelineMetal4*)pipeline;
    return (uint64_t)metal_pipeline->pipeline_state.allocatedSize;
}

// Mesh pipeline

static void lrhi_metal4_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;
    
    MTLMeshRenderPipelineDescriptor* descriptor = [[MTLMeshRenderPipelineDescriptor alloc] init];
    if (info->task_shader) {
        descriptor.objectFunction = ((LRHIShaderModuleMetal4*)info->task_shader)->function;
    }
    descriptor.meshFunction = ((LRHIShaderModuleMetal4*)info->mesh_shader)->function;
    if (info->fragment_shader) {
        descriptor.fragmentFunction = ((LRHIShaderModuleMetal4*)info->fragment_shader)->function;
    }
    for (uint32_t i = 0; i < info->render_target_count; i++) {
        descriptor.colorAttachments[i].pixelFormat = lrhi_metal4_pixel_format(info->render_target_formats[i]);
    }
    if (info->depth_test_enable || info->stencil_test_enable) {
        descriptor.depthAttachmentPixelFormat = lrhi_metal4_pixel_format(info->depth_stencil_format);
        descriptor.stencilAttachmentPixelFormat = lrhi_metal4_pixel_format(info->depth_stencil_format);
    }
    if (info->supports_indirect_commands) {
        descriptor.supportIndirectCommandBuffers = YES;
    }

    // Depth stencil
    id<MTLDepthStencilState> depth_stencil_state = nil;
    if (info->depth_test_enable || info->stencil_test_enable) {
        MTLDepthStencilDescriptor* depth_stencil_descriptor = [[MTLDepthStencilDescriptor alloc] init];
        depth_stencil_descriptor.depthCompareFunction = lrhi_metal4_compare_op_to_mtl(info->depth_compare_op);
        depth_stencil_descriptor.depthWriteEnabled = info->depth_write_enable;
        // TODO: Stencil state

        depth_stencil_state = [metal_device->device newDepthStencilStateWithDescriptor:depth_stencil_descriptor];
    }

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline_state = [metal_device->device newRenderPipelineStateWithMeshDescriptor:descriptor options:MTLPipelineOptionNone reflection:nil error:&error];
    if (!pipeline_state || error) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create mesh pipeline: %s", [[error localizedDescription] UTF8String]);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_pipeline = NULL;
        return;
    }

    LRHIMeshPipelineMetal4* out = malloc(sizeof(LRHIMeshPipelineMetal4));
    out->base.vtable = &lrhi_metal4_mesh_pipeline_vtable;
    out->pipeline_state = pipeline_state;
    out->depth_stencil_state = depth_stencil_state;
    out->info = *info;
    out->bindless_manager = &metal_device->bindless_manager;
    *out_pipeline = (LRHIMeshPipeline)out;
}

static void lrhi_metal4_destroy_mesh_pipeline(LRHIMeshPipeline pipeline)
{
    free(pipeline);
}

static void lrhi_metal4_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info)
{
    LRHIMeshPipelineMetal4* metal_pipeline = (LRHIMeshPipelineMetal4*)pipeline;
    *out_info = metal_pipeline->info;
}

static uint64_t lrhi_metal4_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    LRHIMeshPipelineMetal4* metal_pipeline = (LRHIMeshPipelineMetal4*)pipeline;
    return (uint64_t)metal_pipeline->pipeline_state.allocatedSize;
}

// Compute pipeline

static void lrhi_metal4_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

    MTLComputePipelineDescriptor* descriptor = [[MTLComputePipelineDescriptor alloc] init];
    descriptor.computeFunction = ((LRHIShaderModuleMetal4*)info->compute_shader)->function;
    if (info->supports_indirect_commands) {
        descriptor.supportIndirectCommandBuffers = YES;
    }

    NSError* error = nil;
    id<MTLComputePipelineState> pipeline_state = [metal_device->device newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionNone reflection:nil error:&error];
    if (!pipeline_state || error) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create compute pipeline: %s", [[error localizedDescription] UTF8String]);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_pipeline = NULL;
        return;
    }

    LRHIComputePipelineMetal4* out = malloc(sizeof(LRHIComputePipelineMetal4));
    out->base.vtable = &lrhi_metal4_compute_pipeline_vtable;
    out->pipeline_state = pipeline_state;
    out->info = *info;
    out->bindless_manager = &metal_device->bindless_manager;
    *out_pipeline = (LRHIComputePipeline)out;
}

static void lrhi_metal4_destroy_compute_pipeline(LRHIComputePipeline pipeline)
{
    free(pipeline);
}

static void lrhi_metal4_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info)
{
    LRHIComputePipelineMetal4* metal_pipeline = (LRHIComputePipelineMetal4*)pipeline;
    *out_info = metal_pipeline->info;
}

static uint64_t lrhi_metal4_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error)
{
    LRHIComputePipelineMetal4* metal_pipeline = (LRHIComputePipelineMetal4*)pipeline;
    return (uint64_t)metal_pipeline->pipeline_state.allocatedSize;
}

// Compute pass

static LRHIComputePass lrhi_metal4_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    LRHICommandListMetal4* metal_cmd_list = (LRHICommandListMetal4*)command_list;

    id<MTL4ComputeCommandEncoder> compute_encoder = [metal_cmd_list->command_buffer computeCommandEncoder];
    if (!compute_encoder) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create compute command encoder for compute pass");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    LRHIComputePassMetal4* compute_pass = calloc(1, sizeof(LRHIComputePassMetal4));
    compute_pass->base.vtable = &lrhi_metal4_compute_pass_vtable;
    compute_pass->compute_encoder = compute_encoder;
    compute_pass->command_list = metal_cmd_list;

    [compute_encoder setArgumentTable:metal_cmd_list->compute_argument_table];

    return (LRHIComputePass)compute_pass;
}

static void lrhi_metal4_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;
    [metal_compute_pass->compute_encoder endEncoding];
    free(metal_compute_pass);
}

static void lrhi_metal4_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;
    [metal_compute_pass->compute_encoder barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
}

static void lrhi_metal4_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;
    [metal_compute_pass->compute_encoder barrierAfterQueueStages:lrhi_metal4_render_stage_to_mtl(after_stage) beforeStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
}

static void lrhi_metal4_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;
    uint32_t copy_size = size < 128 ? size : 128;
    memcpy(metal_compute_pass->current_push_constants, data, copy_size);
}

static void lrhi_metal4_compute_pass_set_compute_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error)
{
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;
    LRHIComputePipelineMetal4* metal_pipeline = (LRHIComputePipelineMetal4*)pipeline;
    [metal_compute_pass->compute_encoder setComputePipelineState:metal_pipeline->pipeline_state];
    metal_compute_pass->current_compute_pipeline = pipeline;

    Metal4BindlessManager* bindless_manager = metal_pipeline->bindless_manager;
    [metal_compute_pass->command_list->compute_argument_table setAddress:bindless_manager->resource_heap_buffer.gpuAddress atIndex:kIRDescriptorHeapBindPoint];
    [metal_compute_pass->command_list->compute_argument_table setAddress:bindless_manager->sampler_heap_buffer.gpuAddress atIndex:kIRSamplerHeapBindPoint];
}

static void lrhi_metal4_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error)
{
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;

    lrhi_metal4_compute_pass_flush_push_constants(metal_compute_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    [metal_compute_pass->compute_encoder dispatchThreadgroups:MTLSizeMake(group_count_x, group_count_y, group_count_z) threadsPerThreadgroup:MTLSizeMake(threads_per_group_x, threads_per_group_y, threads_per_group_z)];
}

static void lrhi_metal4_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error)
{
    LRHIComputePassMetal4* metal_compute_pass = (LRHIComputePassMetal4*)compute_pass;
    LRHIBufferMetal4*      metal_buffer       = (LRHIBufferMetal4*)indirect_command_buffer;

    lrhi_metal4_compute_pass_flush_push_constants(metal_compute_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    [metal_compute_pass->compute_encoder executeCommandsInBuffer:metal_buffer->icb withRange:NSMakeRange(0, 1)];
}

// Swap chain

static void lrhi_metal4_create_swap_chain(LRHIDevice device, LRHICommandQueue queue,
                                           LRHISwapChainInfo* info,
                                           LRHISwapChain* out_swap_chain,
                                           LRHIError* out_error)
{
    (void)device;

    if (info->handle_type != LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message),
                     "Metal4 swap chain only supports METAL_LAYER handle type");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_swap_chain = NULL;
        return;
    }

    LRHICommandQueueMetal4* metal_queue = (LRHICommandQueueMetal4*)queue;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)info->handle.metal_layer;

    uint8_t max_fif = info->max_frames_in_flight > 0 ? info->max_frames_in_flight : 2;
    layer.pixelFormat          = lrhi_metal4_pixel_format(info->format);
    layer.drawableSize         = CGSizeMake(info->width, info->height);
    layer.framebufferOnly      = NO;
    layer.maximumDrawableCount = (max_fif <= 3) ? max_fif : 3;  // CAMetalLayer caps at 3

    LRHISwapChainMetal4* sc = malloc(sizeof(LRHISwapChainMetal4));
    sc->base.vtable      = &lrhi_metal4_swap_chain_vtable;
    sc->layer            = layer;
    sc->queue            = metal_queue->queue;
    sc->current_drawable = nil;
    sc->info             = *info;

    sc->current_texture.base.vtable = &lrhi_metal4_swap_chain_texture_vtable;
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

static void lrhi_metal4_destroy_swap_chain(LRHISwapChain swap_chain)
{
    LRHISwapChainMetal4* sc = (LRHISwapChainMetal4*)swap_chain;
    sc->current_drawable = nil;
    free(sc);
}

static LRHITexture lrhi_metal4_swap_chain_get_current_texture(LRHISwapChain swap_chain,
                                                               LRHIError* out_error)
{
    LRHISwapChainMetal4* sc = (LRHISwapChainMetal4*)swap_chain;
    sc->current_drawable = nil;  // discard any un-presented drawable

    id<CAMetalDrawable> drawable = [sc->layer nextDrawable];
    if (!drawable) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message),
                     "Metal4: nextDrawable returned nil");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    sc->current_drawable        = drawable;
    sc->current_texture.texture = drawable.texture;
    return (LRHITexture)&sc->current_texture;
}

static void lrhi_metal4_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error)
{
    LRHISwapChainMetal4* sc = (LRHISwapChainMetal4*)swap_chain;
    if (!sc->current_drawable) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message),
                     "Metal4: present called with no current drawable");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
        }
        return;
    }
    // Both are GPU-timeline events enqueued on the command queue after all rendering commands:
    // waitForDrawable: ensures the display has finished reading this drawable before the GPU re-uses it
    // signalDrawable:  signals the display system that rendering into this drawable is complete
    [sc->queue waitForDrawable:sc->current_drawable];
    [sc->queue signalDrawable:sc->current_drawable];
    [sc->current_drawable present];
    sc->current_drawable        = nil;
    sc->current_texture.texture = nil;
}

// Buffer view

static void lrhi_metal4_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

    if (info->buffer == NULL) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Buffer view creation failed: buffer is NULL");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_buffer_view = NULL;
        return;
    }

    LRHIBufferMetal4* metal_buffer = (LRHIBufferMetal4*)info->buffer;
    if (info->offset > metal_buffer->buffer.allocatedSize) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Buffer view creation failed: offset + size exceeds buffer bounds");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_buffer_view = NULL;
        return;
    }

    LRHIBufferViewMetal4* out = malloc(sizeof(LRHIBufferViewMetal4));
    out->base.vtable = &lrhi_metal4_buffer_view_vtable;
    out->gpu_address = metal_buffer->buffer.gpuAddress + info->offset;
    out->info = *info;
    out->bindless_index = lrhi_metal4_bindless_manager_find_free_resource(&metal_device->bindless_manager);
    lrhi_metal4_bindless_manager_write_buffer_view(&metal_device->bindless_manager, out, out->bindless_index);
    out->bindless_manager = &metal_device->bindless_manager;
    *out_buffer_view = (LRHIBufferView)out;
}

static void lrhi_metal4_destroy_buffer_view(LRHIBufferView buffer_view)
{
    LRHIBufferViewMetal4* metal_buffer_view = (LRHIBufferViewMetal4*)buffer_view;
    if (metal_buffer_view->bindless_index != UINT32_MAX) {
        lrhi_metal4_bindless_manager_free_resource_view(metal_buffer_view->bindless_manager, metal_buffer_view->bindless_index);
    }
    free(buffer_view);
}

static void lrhi_metal4_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info)
{
    LRHIBufferViewMetal4* metal_buffer_view = (LRHIBufferViewMetal4*)buffer_view;
    *out_info = metal_buffer_view->info;
}

static uint32_t lrhi_metal4_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error)
{
    LRHIBufferViewMetal4* metal_buffer_view = (LRHIBufferViewMetal4*)buffer_view;
    if (metal_buffer_view->bindless_index == UINT32_MAX) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Buffer view does not have a valid bindless index");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return UINT32_MAX;
    }
    return metal_buffer_view->bindless_index;
}

// Sampler

static void lrhi_metal4_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error)
{
    LRHIDeviceMetal4* metal_device = (LRHIDeviceMetal4*)device;

    MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];
    descriptor.minFilter = lrhi_metal4_filter_to_mtl(info->min_filter);
    descriptor.magFilter = lrhi_metal4_filter_to_mtl(info->mag_filter);
    descriptor.mipFilter = lrhi_metal4_mip_filter_to_mtl(info->mipmap_filter);
    descriptor.sAddressMode = lrhi_metal4_address_mode_to_mtl(info->address_mode_u);
    descriptor.tAddressMode = lrhi_metal4_address_mode_to_mtl(info->address_mode_v);
    descriptor.rAddressMode = lrhi_metal4_address_mode_to_mtl(info->address_mode_w);
    descriptor.lodMinClamp = info->min_lod;
    descriptor.lodMaxClamp = info->max_lod;
    descriptor.maxAnisotropy = info->anisotropy_enable ? 16.0f : 1.0f;
    if (info->compare_enable) {
        descriptor.compareFunction = lrhi_metal4_compare_op_to_mtl(info->compare_op);
    }
    descriptor.supportArgumentBuffers = YES;

    LRHISamplerMetal4* out = malloc(sizeof(LRHISamplerMetal4));
    out->base.vtable = &lrhi_metal4_sampler_vtable;
    out->info = *info;
    out->sampler_state = [metal_device->device newSamplerStateWithDescriptor:descriptor];
    out->bindless_index = lrhi_metal4_bindless_manager_find_free_sampler(&metal_device->bindless_manager);
    lrhi_metal4_bindless_manager_write_sampler(&metal_device->bindless_manager, out, out->bindless_index);
    out->bindless_manager = &metal_device->bindless_manager;
    *out_sampler = (LRHISampler)out;
}

static void lrhi_metal4_destroy_sampler(LRHISampler sampler)
{
    LRHISamplerMetal4* metal_sampler = (LRHISamplerMetal4*)sampler;
    lrhi_metal4_bindless_manager_free_sampler(metal_sampler->bindless_manager, metal_sampler->bindless_index);
    free(sampler);
}

static void lrhi_metal4_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info)
{
    LRHISamplerMetal4* metal_sampler = (LRHISamplerMetal4*)sampler;
    *out_info = metal_sampler->info;
}

static uint32_t lrhi_metal4_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error)
{
    LRHISamplerMetal4* metal_sampler = (LRHISamplerMetal4*)sampler;
    if (metal_sampler->bindless_index == UINT32_MAX) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Sampler does not have a valid bindless index");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return UINT32_MAX;
    }
    return metal_sampler->bindless_index;
}

// Utils

static MTLStages lrhi_metal4_render_stage_to_mtl(LRHIRenderStage stage)
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

static MTLLoadAction lrhi_metal4_load_action_to_mtl(LRHIRenderPassAction load_op)
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

static MTLStoreAction lrhi_metal4_store_action_to_mtl(LRHIRenderPassAction store_op)
{
    if (store_op == LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR) {
        return MTLStoreActionStore;
    }
    return MTLStoreActionDontCare;
}

static MTLCullMode lrhi_metal4_cull_mode_to_mtl(LRHIPipelineCullMode cull_mode)
{
    if (cull_mode == LUMINARY_RHI_PIPELINE_CULL_MODE_NONE) {
        return MTLCullModeNone;
    } else if (cull_mode == LUMINARY_RHI_PIPELINE_CULL_MODE_FRONT) {
        return MTLCullModeFront;
    } else if (cull_mode == LUMINARY_RHI_PIPELINE_CULL_MODE_BACK) {
        return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

static MTLTriangleFillMode lrhi_metal4_fill_mode_to_mtl(LRHIPipelineFillMode fill_mode)
{
    if (fill_mode == LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID) {
        return MTLTriangleFillModeFill;
    } else if (fill_mode == LUMINARY_RHI_PIPELINE_FILL_MODE_WIREFRAME) {
        return MTLTriangleFillModeLines;
    }
    return MTLTriangleFillModeFill;
}

static MTLWinding lrhi_metal4_front_face_to_mtl(LRHIPipelineFrontFace front_face)
{
    if (front_face == LUMINARY_RHI_PIPELINE_FRONT_FACE_CLOCKWISE) {
        return MTLWindingClockwise;
    } else if (front_face == LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE) {
        return MTLWindingCounterClockwise;
    }
    return MTLWindingClockwise;
}

static MTLPrimitiveType lrhi_metal4_primitive_topology_to_mtl(LRHIPipelineTopology topology)
{
    if (topology == LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST) {
        return MTLPrimitiveTypePoint;
    } else if (topology == LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST) {
        return MTLPrimitiveTypeLine;
    } else if (topology == LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST) {
        return MTLPrimitiveTypeTriangle;
    }
    return MTLPrimitiveTypeTriangle;
}

static MTLPrimitiveTopologyClass lrhi_metal4_primitive_topology_class_to_mtl(LRHIPipelineTopology topology)
{
    if (topology == LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST) {
        return MTLPrimitiveTopologyClassPoint;
    } else if (topology == LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST) {
        return MTLPrimitiveTopologyClassLine;
    } else if (topology == LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST) {
        return MTLPrimitiveTopologyClassTriangle;
    }
    return MTLPrimitiveTopologyClassTriangle;
}

static MTLBlendFactor lrhi_metal4_blend_factor_to_mtl(LRHIBlendFactor factor)
{
    switch (factor) {
        case LUMINARY_RHI_BLEND_FACTOR_ZERO: return MTLBlendFactorZero;
        case LUMINARY_RHI_BLEND_FACTOR_ONE: return MTLBlendFactorOne;
        case LUMINARY_RHI_BLEND_FACTOR_SRC_COLOR: return MTLBlendFactorSourceColor;
        case LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
        case LUMINARY_RHI_BLEND_FACTOR_DST_COLOR: return MTLBlendFactorDestinationColor;
        case LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
        case LUMINARY_RHI_BLEND_FACTOR_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
        case LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
        case LUMINARY_RHI_BLEND_FACTOR_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
        case LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
        case LUMINARY_RHI_BLEND_FACTOR_SRC_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
        case LUMINARY_RHI_BLEND_FACTOR_BLEND_COLOR: return MTLBlendFactorBlendColor;
        default: return MTLBlendFactorZero;
    }
}

static MTLBlendOperation lrhi_metal4_blend_op_to_mtl(LRHIBlendOperation op)
{
    switch (op) {
        case LUMINARY_RHI_BLEND_OPERATION_ADD: return MTLBlendOperationAdd;
        case LUMINARY_RHI_BLEND_OPERATION_SUBTRACT: return MTLBlendOperationSubtract;
        case LUMINARY_RHI_BLEND_OPERATION_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
        case LUMINARY_RHI_BLEND_OPERATION_MIN: return MTLBlendOperationMin;
        case LUMINARY_RHI_BLEND_OPERATION_MAX: return MTLBlendOperationMax;
        default: return MTLBlendOperationAdd;
    }
}

static MTLCompareFunction lrhi_metal4_compare_op_to_mtl(LRHICompareOperation op)
{
    switch (op) {
        case LUMINARY_RHI_COMPARE_OPERATION_NEVER: return MTLCompareFunctionNever;
        case LUMINARY_RHI_COMPARE_OPERATION_LESS: return MTLCompareFunctionLess;
        case LUMINARY_RHI_COMPARE_OPERATION_EQUAL: return MTLCompareFunctionEqual;
        case LUMINARY_RHI_COMPARE_OPERATION_LESS_EQUAL: return MTLCompareFunctionLessEqual;
        case LUMINARY_RHI_COMPARE_OPERATION_GREATER: return MTLCompareFunctionGreater;
        case LUMINARY_RHI_COMPARE_OPERATION_NOT_EQUAL: return MTLCompareFunctionNotEqual;
        case LUMINARY_RHI_COMPARE_OPERATION_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
        case LUMINARY_RHI_COMPARE_OPERATION_ALWAYS: return MTLCompareFunctionAlways;
        default: return MTLCompareFunctionAlways;
    }
}

static MTLSamplerAddressMode lrhi_metal4_address_mode_to_mtl(LRHISamplerAddressMode mode)
{
    switch (mode) {
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT: return MTLSamplerAddressModeRepeat;
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToBorderColor;
        default: return MTLSamplerAddressModeRepeat;
    }
}

static MTLSamplerMinMagFilter lrhi_metal4_filter_to_mtl(LRHISamplerFilter filter)
{
    switch (filter) {
        case LUMINARY_RHI_SAMPLER_FILTER_NEAREST: return MTLSamplerMinMagFilterNearest;
        case LUMINARY_RHI_SAMPLER_FILTER_LINEAR: return MTLSamplerMinMagFilterLinear;
        default: return MTLSamplerMinMagFilterNearest;
    }
}

static MTLSamplerMipFilter lrhi_metal4_mip_filter_to_mtl(LRHISamplerFilter filter)
{
    switch (filter) {
        case LUMINARY_RHI_SAMPLER_FILTER_NEAREST: return MTLSamplerMipFilterNotMipmapped;
        case LUMINARY_RHI_SAMPLER_FILTER_LINEAR: return MTLSamplerMipFilterLinear;
        default: return MTLSamplerMipFilterNotMipmapped;
    }
}

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
