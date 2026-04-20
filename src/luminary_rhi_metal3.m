#include "luminary_rhi.h"
#include "luminary_rhi_internal.h"

#include <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdatomic.h>

#define MAX_BINDLESS_RESOURCES 100000
#define MAX_BINDLESS_SAMPLERS 512

#ifndef IR_PRIVATE_IMPLEMENTATION
#define IR_PRIVATE_IMPLEMENTATION
#endif
#include "ext/metal_irconverter_runtime.h"
#include "ext/icb_shaders.h"

// #define LRHI_DEBUG_METAL_PROGRAMMATIC_CAPTURE 1

// ICB parameter argument structs
typedef struct Metal3ICBDrawParameters {
    MTLResourceID icb;
} Metal3ICBDrawParameters;

typedef struct Metal3ICBDrawIndexedParameters {
    MTLResourceID icb;
    MTLGPUAddress index_buffer;
} Metal3ICBDrawIndexedParameters;

typedef struct Metal3ICBDispatchParameters {
    MTLResourceID icb;

    uint32_t threads_per_group_x;
    uint32_t threads_per_group_y;
    uint32_t threads_per_group_z;
} Metal3ICBDispatchParameters;

typedef struct Metal3ICBDrawMeshTasksParameters {
    MTLResourceID icb;              // offset  0, 8 bytes
    uint32_t _pad0[2];              // offset  8, 8 bytes — align uint3 to 16

    // MSL uint3 in device address space has size+align = 16 (same as uint4)
    uint32_t threads_per_object_group_x;  // offset 16
    uint32_t threads_per_object_group_y;  // offset 20
    uint32_t threads_per_object_group_z;  // offset 24
    uint32_t _pad1;                       // offset 28 — pad uint3 to 16 bytes

    uint32_t threads_per_mesh_group_x;    // offset 32
    uint32_t threads_per_mesh_group_y;    // offset 36
    uint32_t threads_per_mesh_group_z;    // offset 40
    uint32_t _pad2;                       // offset 44 — pad uint3 to 16 bytes
} Metal3ICBDrawMeshTasksParameters;       // sizeof = 48

typedef struct Metal3BindlessManager {
    id<MTLBuffer> resource_heap_buffer;
    id<MTLBuffer> sampler_heap_buffer;

    IRDescriptorTableEntry* mapped_resource_heap;
    IRDescriptorTableEntry* mapped_sampler_heap;

    LRHIFreeList resource_heap_free_list;
    LRHIFreeList sampler_heap_free_list;
} Metal3BindlessManager;

typedef struct LRHIMetal3ArgumentBufferData {
    char push_constants[128];
    uint32_t draw_id;
    uint32_t _padding[31];
} LRHIMetal3ArgumentBufferData;

typedef struct LRHIMetal3LinearAllocator {
    LRHIBuffer buffer;
    uint64_t capacity;
    uint64_t offset;
} LRHIMetal3LinearAllocator;

typedef struct LRHIDeviceMetal3 {
    LRHIDeviceBase base;
    id<MTLDevice> device;
    uint8_t enable_debug;

    id<MTLComputePipelineState> draw_icb_pipe;
    id<MTLComputePipelineState> draw_indexed_icb_pipe;
    id<MTLComputePipelineState> dispatch_icb_pipe;
    id<MTLComputePipelineState> draw_mesh_tasks_icb_pipe;
    id<MTLResidencySet> internal_residency_set;

    Metal3BindlessManager bindless_manager;
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

    // All the indirect stuff
    id<MTLIndirectCommandBuffer> icb;
    id<MTLBuffer> icb_params;
    id<MTLBuffer> draw_id_atomic;
    id<MTLBuffer> per_draw_constants;
    id<MTLBuffer> primitive_type_buf;
    LRHICommandType icb_command_type;
} LRHIBufferMetal3;

typedef struct LRHICommandQueueMetal3 {
    LRHICommandQueueBase base;
    id<MTLCommandQueue>  queue;
    id<MTLDevice>        device;
    uint8_t              is_capture_enabled;

    LRHIDeviceMetal3* device_ref;
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
    id<MTLBuffer> push_constant_buffer;
    uint32_t push_constant_offset;

    LRHIDeviceMetal3* device;
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

    Metal3BindlessManager* bindless_manager; // Reference to the device's bindless manager for freeing the index on destruction
} LRHITextureViewMetal3;

typedef struct LRHIRenderPassMetal3 {
    LRHIRenderPassBase base;
    id<MTLRenderCommandEncoder> render_encoder;

    LRHIRenderPipeline current_render_pipeline; // Needed to track current pipeline state for dynamic state emulation
    LRHICommandListMetal3* command_list;
    char current_push_constants[128];
    uint32_t current_draw_id;
} LRHIRenderPassMetal3;

typedef struct LRHIShaderModuleMetal3 {
    LRHIShaderModuleBase base;
    LRHIShaderModuleInfo info;
    id<MTLLibrary> library;
    id<MTLFunction> function;
} LRHIShaderModuleMetal3;

typedef struct LRHIRenderPipelineMetal3 {
    LRHIRenderPipelineBase base;
    LRHIRenderPipelineInfo info;
    id<MTLRenderPipelineState> pipeline_state;
    id<MTLDepthStencilState> depth_stencil_state;

    Metal3BindlessManager* bindless_manager;
} LRHIRenderPipelineMetal3;

typedef struct LRHIMeshPipelineMetal3 {
    LRHIMeshPipelineBase base;
    LRHIMeshPipelineInfo info;
    id<MTLRenderPipelineState> pipeline_state;
    id<MTLDepthStencilState> depth_stencil_state;

    Metal3BindlessManager* bindless_manager;
} LRHIMeshPipelineMetal3;

typedef struct LRHIComputePipelineMetal3 {
    LRHIComputePipelineBase base;
    LRHIComputePipelineInfo info;
    id<MTLComputePipelineState> pipeline_state;

    Metal3BindlessManager* bindless_manager;
} LRHIComputePipelineMetal3;

typedef struct LRHIComputePassMetal3 {
    LRHIComputePassBase base;
    id<MTLComputeCommandEncoder> compute_encoder;

    LRHIComputePipeline current_compute_pipeline; // Needed to track current pipeline state for dynamic state emulation
    LRHICommandListMetal3* command_list;
    char current_push_constants[128];
} LRHIComputePassMetal3;

typedef struct LRHIBufferViewMetal3 {
    LRHIBufferViewBase base;
    LRHIBufferViewInfo info;
    uint32_t bindless_index;
    MTLGPUAddress gpu_address;

    Metal3BindlessManager* bindless_manager;
} LRHIBufferViewMetal3;

typedef struct LRHISamplerMetal3 {
    LRHISamplerBase base;
    LRHISamplerInfo info;
    id<MTLSamplerState> sampler_state;
    uint32_t bindless_index;

    Metal3BindlessManager* bindless_manager;
} LRHISamplerMetal3;

typedef struct LRHIAccelerationStructurePassMetal3 {
    LRHIAccelerationStructurePassBase base;
    id<MTLAccelerationStructureCommandEncoder> as_encoder;

    LRHICommandListMetal3* command_list;
} LRHIAccelerationStructurePassMetal3;

typedef struct LRHIBLASMetal3 {
    LRHIBLASBase base;
    LRHIBLASInfo info;
    LRHIDeviceMetal3* device;

    id<MTLAccelerationStructure> acceleration_structure;
    MTLAccelerationStructureSizes sizes;

    MTLPrimitiveAccelerationStructureDescriptor* mtl_descriptor;
} LRHIBLASMetal3;

typedef struct LRHITLASMetal3 {
    LRHITLASBase base;
    LRHITLASInfo info;
    LRHIDeviceMetal3* device; // Used because we need to add instance buffer and resource ID buffer to internal residency set

    id<MTLAccelerationStructure> acceleration_structure;
    MTLAccelerationStructureSizes sizes;

    id<MTLBuffer> instance_buffer;
    MTLIndirectAccelerationStructureInstanceDescriptor* mapped_instance_buffer;

    uint32_t instance_count;
    uint32_t bindless_index;

    id<MTLBuffer> resource_id_buffer; // Needed by Metal Shader Converter
    void* mapped_resource_id_buffer;

    MTLInstanceAccelerationStructureDescriptor* mtl_descriptor;
} LRHITLASMetal3;

// Forward declarations
static MTLPixelFormat            lrhi_metal3_pixel_format(LRHITextureFormat format);
static MTLTextureUsage           lrhi_metal3_texture_usage(LRHITextureUsage usage);
static MTLTextureType            lrhi_metal3_texture_type(LRHITextureDimensions type);
static void                      lrhi_metal3_validate_texture_info(LRHITextureInfo* info, LRHIError* out_error);
static MTLStages                 lrhi_metal3_render_stage_to_mtl(LRHIRenderStage stage);
static MTLRenderStages           lrhi_metal3_render_stages_to_mtl(LRHIRenderStage stages);
static MTLLoadAction             lrhi_metal3_load_action_to_mtl(LRHIRenderPassAction load_op);
static MTLStoreAction            lrhi_metal3_store_action_to_mtl(LRHIRenderPassAction store_op);
static MTLCullMode               lrhi_metal3_cull_mode_to_mtl(LRHIPipelineCullMode cull_mode);
static MTLTriangleFillMode       lrhi_metal3_fill_mode_to_mtl(LRHIPipelineFillMode fill_mode);
static MTLWinding                lrhi_metal3_front_face_to_mtl(LRHIPipelineFrontFace front_face);
static MTLPrimitiveType          lrhi_metal3_primitive_topology_to_mtl(LRHIPipelineTopology topology);
static MTLPrimitiveTopologyClass lrhi_metal3_primitive_topology_class_to_mtl(LRHIPipelineTopology topology);
static MTLBlendFactor            lrhi_metal3_blend_factor_to_mtl(LRHIBlendFactor factor);
static MTLBlendOperation         lrhi_metal3_blend_op_to_mtl(LRHIBlendOperation op);
static MTLCompareFunction        lrhi_metal3_compare_op_to_mtl(LRHICompareOperation op);
static MTLSamplerAddressMode     lrhi_metal3_address_mode_to_mtl(LRHISamplerAddressMode mode);
static MTLSamplerMinMagFilter    lrhi_metal3_filter_to_mtl(LRHISamplerFilter filter);
static MTLSamplerMipFilter       lrhi_metal3_mip_filter_to_mtl(LRHISamplerFilter filter);

static void            lrhi_metal3_destroy_device(LRHIDevice device);
static LRHIDeviceInfo  lrhi_metal3_get_device_info(LRHIDevice device);

static void            lrhi_metal3_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
static void            lrhi_metal3_destroy_texture(LRHITexture texture);
static void            lrhi_metal3_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
static void            lrhi_metal3_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal3_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal3_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void            lrhi_metal3_texture_set_name(LRHITexture texture, const char* name);

static void            lrhi_metal3_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
static void            lrhi_metal3_destroy_buffer(LRHIBuffer buffer);
static void            lrhi_metal3_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
static void*           lrhi_metal3_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal3_buffer_unmap(LRHIBuffer buffer);
static void            lrhi_metal3_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);
static void            lrhi_metal3_buffer_set_name(LRHIBuffer buffer, const char* name);
static void            lrhi_metal3_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error);

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
static void            lrhi_metal3_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error);

static LRHICopyPass   lrhi_metal3_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error);
static void           lrhi_metal3_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error);
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
static void            lrhi_metal3_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);
static void            lrhi_metal3_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void            lrhi_metal3_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void            lrhi_metal3_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void            lrhi_metal3_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);
static void            lrhi_metal3_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
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
static void            lrhi_metal3_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error);
static void            lrhi_metal3_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error);
static void            lrhi_metal3_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void            lrhi_metal3_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
static void            lrhi_metal3_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error);
static void            lrhi_metal3_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error);
static void            lrhi_metal3_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error);
static void            lrhi_metal3_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error);
static void            lrhi_metal3_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error);
static void            lrhi_metal3_flush_push_constants(LRHIRenderPassMetal3* render_pass, uint8_t is_mesh_pipeline, LRHIError* out_error);
static void            lrhi_metal3_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error);
static void            lrhi_metal3_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error);
static void            lrhi_metal3_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error);
static void            lrhi_metal3_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error);

static void            lrhi_metal3_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error);
static void            lrhi_metal3_destroy_shader_module(LRHIShaderModule shader_module);
static void            lrhi_metal3_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info);

static void            lrhi_metal3_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error);
static void            lrhi_metal3_destroy_render_pipeline(LRHIRenderPipeline pipeline);
static void            lrhi_metal3_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info);
static uint64_t        lrhi_metal3_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error);

static void            lrhi_metal3_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error);
static void            lrhi_metal3_destroy_mesh_pipeline(LRHIMeshPipeline pipeline);
static void            lrhi_metal3_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info);
static uint64_t        lrhi_metal3_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error);

static void            lrhi_metal3_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error);
static void            lrhi_metal3_destroy_compute_pipeline(LRHIComputePipeline pipeline);
static void            lrhi_metal3_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info);
static uint64_t        lrhi_metal3_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error);

static LRHIComputePass lrhi_metal3_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error);
static void            lrhi_metal3_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error);

static void            lrhi_metal3_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error);
static void            lrhi_metal3_destroy_buffer_view(LRHIBufferView buffer_view);
static void            lrhi_metal3_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info);
static uint32_t        lrhi_metal3_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error);

static void     lrhi_metal3_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error);
static void     lrhi_metal3_destroy_sampler(LRHISampler sampler);
static void     lrhi_metal3_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info);
static uint32_t lrhi_metal3_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error);

static LRHIAccelerationStructurePass lrhi_metal3_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_end(LRHIAccelerationStructurePass as_pass, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass as_pass, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_encoder_barrier(LRHIAccelerationStructurePass as_pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error);
static void                          lrhi_metal3_acceleration_structure_pass_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error);

static void                                 lrhi_metal3_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error);
static void                                 lrhi_metal3_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error);
static void                                 lrhi_metal3_destroy_bottom_level_acceleration_structure(LRHIBottomLevelAccelerationStructure blas);
static void                                 lrhi_metal3_get_bottom_level_acceleration_structure_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info);
static LRHIAccelerationStructureBufferSizes lrhi_metal3_bottom_level_acceleration_structure_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);

static void                                 lrhi_metal3_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error);
static void                                 lrhi_metal3_destroy_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas);
static void                                 lrhi_metal3_get_top_level_acceleration_structure_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info);
static uint64_t                             lrhi_metal3_top_level_acceleration_structure_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static LRHIAccelerationStructureBufferSizes lrhi_metal3_top_level_acceleration_structure_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void                                 lrhi_metal3_reset_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void                                 lrhi_metal3_add_top_level_acceleration_structure_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error);

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
    .create_shader_module  = lrhi_metal3_create_shader_module,
    .create_render_pipeline  = lrhi_metal3_create_render_pipeline,
    .create_mesh_pipeline  = lrhi_metal3_create_mesh_pipeline,
    .create_compute_pipeline = lrhi_metal3_create_compute_pipeline,
    .create_buffer_view    = lrhi_metal3_create_buffer_view,
    .create_sampler        = lrhi_metal3_create_sampler,
    .create_bottom_level_acceleration_structure           = lrhi_metal3_create_bottom_level_acceleration_structure,
    .create_compacted_bottom_level_acceleration_structure = lrhi_metal3_create_compacted_bottom_level_acceleration_structure,
    .create_top_level_acceleration_structure              = lrhi_metal3_create_top_level_acceleration_structure
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
    .texture_set_name       = lrhi_metal3_texture_set_name,
};

static const LRHIBufferVTable lrhi_metal3_buffer_vtable = {
    .destroy_buffer                   = lrhi_metal3_destroy_buffer,
    .get_buffer_info                  = lrhi_metal3_get_buffer_info,
    .buffer_map                       = lrhi_metal3_buffer_map,
    .buffer_unmap                     = lrhi_metal3_buffer_unmap,
    .buffer_set_name                  = lrhi_metal3_buffer_set_name,
    .buffer_set_indirect_command_type = lrhi_metal3_buffer_set_indirect_command_type,
};

static const LRHICommandListVTable lrhi_metal3_command_list_vtable = {
    .destroy_command_list                   = lrhi_metal3_destroy_command_list,
    .command_list_begin                     = lrhi_metal3_command_list_begin,
    .command_list_end                       = lrhi_metal3_command_list_end,
    .command_list_reset                     = lrhi_metal3_command_list_reset,
    .copy_pass_begin                        = lrhi_metal3_command_list_begin_copy_pass,
    .render_pass_begin                      = lrhi_metal3_render_pass_begin,
    .compute_pass_begin                     = lrhi_metal3_compute_pass_begin,
    .acceleration_structure_pass_begin      = lrhi_metal3_acceleration_structure_pass_begin,
    .command_list_prepare_indirect_commands = lrhi_metal3_command_list_prepare_indirect_commands,
};

static const LRHICopyPassVTable lrhi_metal3_copy_pass_vtable = {
    .copy_pass_end                = lrhi_metal3_copy_pass_end,
    .push_debug_group             = lrhi_metal3_copy_pass_push_debug_group,
    .pop_debug_group              = lrhi_metal3_copy_pass_pop_debug_group,
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
    .add_blas = lrhi_metal3_residency_set_add_blas,
    .add_tlas = lrhi_metal3_residency_set_add_tlas,
    .remove_texture = lrhi_metal3_residency_set_remove_texture,
    .remove_buffer = lrhi_metal3_residency_set_remove_buffer,
    .remove_blas = lrhi_metal3_residency_set_remove_blas,
    .remove_tlas = lrhi_metal3_residency_set_remove_tlas,
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
    .push_debug_group = lrhi_metal3_render_pass_push_debug_group,
    .pop_debug_group  = lrhi_metal3_render_pass_pop_debug_group,
    .intra_barrier = lrhi_metal3_render_pass_intra_barrier,
    .encoder_barrier = lrhi_metal3_render_pass_encoder_barrier,
    .set_viewport = lrhi_metal3_render_pass_set_viewport,
    .set_scissor = lrhi_metal3_render_pass_set_scissor,
    .set_push_constants = lrhi_metal3_render_pass_set_push_constants,
    .set_render_pipeline = lrhi_metal3_render_pass_set_render_pipeline,
    .set_mesh_pipeline = lrhi_metal3_render_pass_set_mesh_pipeline,
    .draw = lrhi_metal3_render_pass_draw,
    .draw_indexed = lrhi_metal3_render_pass_draw_indexed,
    .draw_mesh_tasks = lrhi_metal3_render_pass_draw_mesh_tasks,
    .execute_indirect_commands = lrhi_metal3_render_pass_execute_indirect_commands,
};

static const LRHIShaderModuleVTable lrhi_metal3_shader_module_vtable = {
    .destroy_shader_module = lrhi_metal3_destroy_shader_module,
    .get_shader_module_info = lrhi_metal3_get_shader_module_info,
};

static const LRHIRenderPipelineVTable lrhi_metal3_render_pipeline_vtable = {
    .destroy_render_pipeline = lrhi_metal3_destroy_render_pipeline,
    .get_render_pipeline_info = lrhi_metal3_get_render_pipeline_info,
    .get_alloc_size = lrhi_metal3_render_pipeline_get_alloc_size,
};

static const LRHIMeshPipelineVTable lrhi_metal3_mesh_pipeline_vtable = {
    .destroy_mesh_pipeline = lrhi_metal3_destroy_mesh_pipeline,
    .get_mesh_pipeline_info = lrhi_metal3_get_mesh_pipeline_info,
    .get_alloc_size = lrhi_metal3_mesh_pipeline_get_alloc_size,
};

static const LRHIComputePipelineVTable lrhi_metal3_compute_pipeline_vtable = {
    .destroy_compute_pipeline = lrhi_metal3_destroy_compute_pipeline,
    .get_compute_pipeline_info = lrhi_metal3_get_compute_pipeline_info,
    .get_alloc_size = lrhi_metal3_compute_pipeline_get_alloc_size,
};

static const LRHIComputePassVTable lrhi_metal3_compute_pass_vtable = {
    .end = lrhi_metal3_compute_pass_end,
    .push_debug_group = lrhi_metal3_compute_pass_push_debug_group,
    .pop_debug_group  = lrhi_metal3_compute_pass_pop_debug_group,
    .barrier = lrhi_metal3_compute_pass_barrier,
    .encoder_barrier = lrhi_metal3_compute_pass_encoder_barrier,
    .set_pipeline = lrhi_metal3_compute_pass_set_pipeline,
    .set_push_constants = lrhi_metal3_compute_pass_set_push_constants,
    .dispatch = lrhi_metal3_compute_pass_dispatch,
    .dispatch_indirect = lrhi_metal3_compute_pass_dispatch_indirect,
};

static const LRHIBufferViewVTable lrhi_metal3_buffer_view_vtable = {
    .destroy_buffer_view = lrhi_metal3_destroy_buffer_view,
    .get_buffer_view_info = lrhi_metal3_get_buffer_view_info,
    .get_bindless_index = lrhi_metal3_buffer_view_get_bindless_index,
};

static const LRHISamplerVTable lrhi_metal3_sampler_vtable = {
    .destroy_sampler = lrhi_metal3_destroy_sampler,
    .get_sampler_info = lrhi_metal3_get_sampler_info,
    .get_bindless_index = lrhi_metal3_sampler_get_bindless_index,
};

static const LRHIBLASVTable lrhi_metal3_blas_vtable = {
    .destroy_bottom_level_acceleration_structure = lrhi_metal3_destroy_bottom_level_acceleration_structure,
    .get_bottom_level_acceleration_structure_info = lrhi_metal3_get_bottom_level_acceleration_structure_info,
    .get_build_scratch_size = lrhi_metal3_bottom_level_acceleration_structure_get_build_scratch_size,
};

static const LRHITLASVTable lrhi_metal3_tlas_vtable = {
    .destroy_top_level_acceleration_structure = lrhi_metal3_destroy_top_level_acceleration_structure,
    .get_top_level_acceleration_structure_info = lrhi_metal3_get_top_level_acceleration_structure_info,
    .get_build_scratch_size = lrhi_metal3_top_level_acceleration_structure_get_build_scratch_size,
    .get_bindless_index = lrhi_metal3_top_level_acceleration_structure_get_bindless_index,
    .reset = lrhi_metal3_reset_top_level_acceleration_structure,
    .add_instance = lrhi_metal3_add_top_level_acceleration_structure_instance,
};

static const LRHIAccelerationStructurePassVTable lrhi_metal3_acceleration_structure_pass_vtable = {
    .end                       = lrhi_metal3_acceleration_structure_pass_end,
    .push_debug_group          = lrhi_metal3_acceleration_structure_pass_push_debug_group,
    .pop_debug_group           = lrhi_metal3_acceleration_structure_pass_pop_debug_group,
    .barrier                   = lrhi_metal3_acceleration_structure_pass_barrier,
    .encoder_barrier           = lrhi_metal3_acceleration_structure_pass_encoder_barrier,
    .build_blas                = lrhi_metal3_acceleration_structure_pass_build_blas,
    .build_tlas                = lrhi_metal3_acceleration_structure_pass_build_tlas,
    .write_compacted_blas_size = lrhi_metal3_acceleration_structure_pass_write_compacted_blas_size,
    .compact_blas              = lrhi_metal3_acceleration_structure_pass_compact_blas,
    .refit_blas                = lrhi_metal3_acceleration_structure_pass_refit_blas,
    .refit_tlas                = lrhi_metal3_acceleration_structure_pass_refit_tlas,
    .copy_blas                 = lrhi_metal3_acceleration_structure_pass_copy_blas,
    .copy_tlas                 = lrhi_metal3_acceleration_structure_pass_copy_tlas,
};

// Bindless manager
static void lrhi_metal3_bindless_manager_init(Metal3BindlessManager* manager, LRHIDeviceMetal3* device, LRHIError* out_error)
{
    manager->resource_heap_buffer = [device->device newBufferWithLength:MAX_BINDLESS_RESOURCES * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
    manager->mapped_resource_heap = (IRDescriptorTableEntry*)manager->resource_heap_buffer.contents;

    manager->sampler_heap_buffer = [device->device newBufferWithLength:MAX_BINDLESS_SAMPLERS * sizeof(IRDescriptorTableEntry) options:MTLResourceStorageModeShared];
    manager->mapped_sampler_heap = (IRDescriptorTableEntry*)manager->sampler_heap_buffer.contents;

    // Create free list
    lrhi_freelist_init(&manager->resource_heap_free_list, MAX_BINDLESS_RESOURCES);
    lrhi_freelist_init(&manager->sampler_heap_free_list, MAX_BINDLESS_SAMPLERS);
}

static void lrhi_metal3_bindless_manager_destroy(Metal3BindlessManager* manager)
{
    lrhi_freelist_destroy(&manager->resource_heap_free_list);
    lrhi_freelist_destroy(&manager->sampler_heap_free_list);
}

static uint32_t lrhi_metal3_bindless_manager_write_texture_view(Metal3BindlessManager* manager, LRHITextureViewMetal3* texture_view, LRHIError* out_error)
{
    IRDescriptorTableEntry entry;
    IRDescriptorTableSetTexture(&entry, texture_view->texture_view, 0.0f, 0);

    uint32_t index = lrhi_freelist_allocate(&manager->resource_heap_free_list);
    memcpy(&manager->mapped_resource_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

static uint32_t lrhi_metal3_bindless_manager_write_buffer_view(Metal3BindlessManager* manager, LRHIBufferViewMetal3* buffer_view, LRHIError* out_error)
{
    IRDescriptorTableEntry entry;
    IRDescriptorTableSetBuffer(&entry, buffer_view->gpu_address, 0);

    uint32_t index = lrhi_freelist_allocate(&manager->resource_heap_free_list);
    memcpy(&manager->mapped_resource_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

static uint32_t lrhi_metal3_bindless_manager_write_sampler(Metal3BindlessManager* manager, LRHISamplerMetal3* sampler, LRHIError* out_error)
{
    IRDescriptorTableEntry entry;
    IRDescriptorTableSetSampler(&entry, sampler->sampler_state, 0);

    uint32_t index = lrhi_freelist_allocate(&manager->sampler_heap_free_list);
    memcpy(&manager->mapped_sampler_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

static uint32_t lrhi_metal3_bindless_manager_write_tlas(Metal3BindlessManager* manager, LRHITLASMetal3* tlas, LRHIError* out_error)
{
    IRDescriptorTableEntry entry;
    IRDescriptorTableSetAccelerationStructure(&entry, tlas->resource_id_buffer.gpuAddress);

    uint32_t index = lrhi_freelist_allocate(&manager->resource_heap_free_list);
    memcpy(&manager->mapped_resource_heap[index], &entry, sizeof(IRDescriptorTableEntry));
    return index;
}

static void lrhi_metal3_bindless_manager_free_resource_view(Metal3BindlessManager* manager, uint32_t index)
{
    lrhi_freelist_free(&manager->resource_heap_free_list, index);
}

static void lrhi_metal3_bindless_manager_free_sampler(Metal3BindlessManager* manager, uint32_t index)
{
    lrhi_freelist_free(&manager->sampler_heap_free_list, index);
}

// Device

void lrhi_metal3_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    LRHIDeviceMetal3* device = LRHI_MALLOC(sizeof(LRHIDeviceMetal3));
    device->base.vtable = &lrhi_metal3_device_vtable;
    device->device = MTLCreateSystemDefaultDevice();
    device->enable_debug = enable_debug;
    if ([device->device supportsFamily:MTLGPUFamilyMetal3] == NO) {
        LRHI_FREE(device);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Metal 3 is not supported on this device");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_device = NULL;
        return;
    }
    lrhi_metal3_bindless_manager_init(&device->bindless_manager, device, out_error);

    // Draw ICB
    {
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];

        // Need to do a copy of the static string because otherwise: Incompatible pointer types sending 'NSString *__strong' to parameter of type 'const char * _Nonnull'clang(-Wincompatible-pointer-types)
        char* draw_src = strdup((const char*)draw_icb_conversion_shader);
        NSString* draw_icb_conversion_shader = [NSString stringWithUTF8String:draw_src];
        NSError* metal_error = nil;
        id<MTLLibrary> lib = [device->device newLibraryWithSource:draw_icb_conversion_shader options:options error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to compile draw ICB conversion shader: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        id<MTLFunction> draw_func = [lib newFunctionWithName:@"encode_draws"];
        device->draw_icb_pipe = [device->device newComputePipelineStateWithFunction:draw_func error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to create draw ICB conversion pipeline: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        LRHI_FREE(draw_src);
    }

    // Draw indexed ICB
    {
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];

        // Need to do a copy of the static string because otherwise: Incompatible pointer types sending 'NSString *__strong' to parameter of type 'const char * _Nonnull'clang(-Wincompatible-pointer-types)
        char* draw_indexed_src = strdup((const char*)draw_indexed_icb_conversion_shader);
        NSString* draw_indexed_icb_conversion_shader = [NSString stringWithUTF8String:draw_indexed_src];
        NSError* metal_error = nil;
        id<MTLLibrary> lib = [device->device newLibraryWithSource:draw_indexed_icb_conversion_shader options:options error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to compile draw indexed ICB conversion shader: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        id<MTLFunction> draw_indexed_func = [lib newFunctionWithName:@"encode_draws"];
        device->draw_indexed_icb_pipe = [device->device newComputePipelineStateWithFunction:draw_indexed_func error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to create draw indexed ICB conversion pipeline: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        LRHI_FREE(draw_indexed_src);
    }

    // Dispatch
    {
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];

        // Need to do a copy of the static string because otherwise: Incompatible pointer types sending 'NSString *__strong' to parameter of type 'const char * _Nonnull'clang(-Wincompatible-pointer-types)
        char* dispatch_src = strdup((const char*)dispatch_icb_conversion_shader);
        NSString* dispatch_icb_conversion_shader = [NSString stringWithUTF8String:dispatch_src];
        NSError* metal_error = nil;
        id<MTLLibrary> lib = [device->device newLibraryWithSource:dispatch_icb_conversion_shader options:options error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to compile dispatch ICB conversion shader: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        id<MTLFunction> dispatch_func = [lib newFunctionWithName:@"encode_draws"];
        device->dispatch_icb_pipe = [device->device newComputePipelineStateWithFunction:dispatch_func error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to create dispatch ICB conversion pipeline: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        LRHI_FREE(dispatch_src);
    }

    // Draw mesh tasks
    {
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];

        // Need to do a copy of the static string because otherwise: Incompatible pointer types sending 'NSString *__strong' to parameter of type 'const char * _Nonnull'clang(-Wincompatible-pointer-types)
        char* draw_mesh_tasks_src = strdup((const char*)draw_mesh_icb_conversion_shader);
        NSString* draw_mesh_tasks_icb_conversion_shader = [NSString stringWithUTF8String:draw_mesh_tasks_src];
        NSError* metal_error = nil;
        id<MTLLibrary> lib = [device->device newLibraryWithSource:draw_mesh_tasks_icb_conversion_shader options:options error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to compile draw mesh tasks ICB conversion shader: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        id<MTLFunction> draw_mesh_tasks_func = [lib newFunctionWithName:@"encode_draws"];
        device->draw_mesh_tasks_icb_pipe = [device->device newComputePipelineStateWithFunction:draw_mesh_tasks_func error:&metal_error];
        if (metal_error) {
            LRHI_FREE(device);
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Failed to create draw mesh tasks ICB conversion pipeline: %s", metal_error.localizedDescription.UTF8String);
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            *out_device = NULL;
            return;
        }

        LRHI_FREE(draw_mesh_tasks_src);
    }

    // Create internal residency set
    MTLResidencySetDescriptor* residency_set_desc = [[MTLResidencySetDescriptor alloc] init];
    residency_set_desc.initialCapacity = 128;

    NSError* error = nil;
    device->internal_residency_set = [device->device newResidencySetWithDescriptor:residency_set_desc error:&error];
    if (error) {
        LRHI_FREE(device);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create internal residency set: %s", error.localizedDescription.UTF8String);
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_device = NULL;
        return;
    }

    *out_device = (LRHIDevice)device;
}

static void lrhi_metal3_destroy_device(LRHIDevice device)
{
    LRHIDeviceMetal3* d = (LRHIDeviceMetal3*)device;
    lrhi_metal3_bindless_manager_destroy(&d->bindless_manager);
    d->device                   = nil;
    d->draw_icb_pipe            = nil;
    d->draw_indexed_icb_pipe    = nil;
    d->dispatch_icb_pipe        = nil;
    d->draw_mesh_tasks_icb_pipe = nil;
    d->internal_residency_set   = nil;
    LRHI_FREE(device);
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

    if (info->name) { texture.label = [NSString stringWithUTF8String:info->name]; }

    LRHITextureMetal3* out = LRHI_MALLOC(sizeof(LRHITextureMetal3));
    out->base.vtable = &lrhi_metal3_texture_vtable;
    out->texture = texture;
    out->info = *info;
    *out_texture = (LRHITexture)out;
}

static void lrhi_metal3_destroy_texture(LRHITexture texture)
{
    LRHITextureMetal3* t = (LRHITextureMetal3*)texture;
    t->texture = nil;
    LRHI_FREE(texture);
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

static void lrhi_metal3_texture_set_name(LRHITexture texture, const char* name)
{
    LRHITextureMetal3* metal_texture = (LRHITextureMetal3*)texture;
    metal_texture->texture.label = [NSString stringWithUTF8String:name];
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

    if (info->name) { buffer.label = [NSString stringWithUTF8String:info->name]; }

    LRHIBufferMetal3* out = LRHI_MALLOC(sizeof(LRHIBufferMetal3));
    out->base.vtable = &lrhi_metal3_buffer_vtable;
    out->buffer = buffer;
    out->info = *info;
    *out_buffer = (LRHIBuffer)out;
}

static void lrhi_metal3_destroy_buffer(LRHIBuffer buffer)
{
    LRHIBufferMetal3* b = (LRHIBufferMetal3*)buffer;
    b->buffer           = nil;
    b->icb              = nil;
    b->icb_params       = nil;
    b->draw_id_atomic   = nil;
    b->per_draw_constants = nil;
    b->primitive_type_buf = nil;
    LRHI_FREE(buffer);
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

static void lrhi_metal3_buffer_set_name(LRHIBuffer buffer, const char* name)
{
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;
    metal_buffer->buffer.label = [NSString stringWithUTF8String:name];
}

static void lrhi_metal3_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error)
{
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)buffer;
    metal_buffer->icb_command_type = command_type;

    // Create ICB
    // Any command type that isn't dispatch, since we need to fill in push constants for every draw
    // to provide DrawID, we don't inherit buffers for these types of commands
    // that way we can fill in the per draw ID + push constants when converting from D3D12/Vulkan style
    // indirect buffer to ICB.

    uint32_t command_count = (uint32_t)(metal_buffer->info.size / metal_buffer->info.stride);

    MTLIndirectCommandBufferDescriptor* icb_descriptor = [[MTLIndirectCommandBufferDescriptor alloc] init];
    icb_descriptor.inheritTriangleFillMode = YES;
    icb_descriptor.inheritDepthBias = YES;
    icb_descriptor.inheritDepthClipMode = YES;
    icb_descriptor.inheritPipelineState = YES;
    icb_descriptor.inheritBuffers = YES;
    icb_descriptor.inheritDepthStencilState = YES;
    icb_descriptor.inheritFrontFacingWinding = YES;
    switch (command_type) {
        case LUMINARY_RHI_COMMAND_TYPE_DRAW:
            icb_descriptor.commandTypes = MTLIndirectCommandTypeDraw;
            icb_descriptor.inheritBuffers = NO;
            icb_descriptor.maxVertexBufferBindCount = 3;
            icb_descriptor.maxFragmentBufferBindCount = 3;
            metal_buffer->draw_id_atomic = [metal_buffer->buffer.device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->icb_params = [metal_buffer->buffer.device newBufferWithLength:sizeof(Metal3ICBDrawParameters) options:MTLResourceStorageModeShared];
            metal_buffer->per_draw_constants = [metal_buffer->buffer.device newBufferWithLength:(command_count * 256) options:MTLResourceStorageModeShared];
            metal_buffer->primitive_type_buf = [metal_buffer->buffer.device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            break;
        case LUMINARY_RHI_COMMAND_TYPE_DRAW_INDEXED:
            icb_descriptor.commandTypes = MTLIndirectCommandTypeDrawIndexed;
            icb_descriptor.inheritBuffers = NO;
            icb_descriptor.maxVertexBufferBindCount = 3;
            icb_descriptor.maxFragmentBufferBindCount = 3;
            metal_buffer->draw_id_atomic = [metal_buffer->buffer.device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->icb_params = [metal_buffer->buffer.device newBufferWithLength:sizeof(Metal3ICBDrawIndexedParameters) options:MTLResourceStorageModeShared];
            metal_buffer->per_draw_constants = [metal_buffer->buffer.device newBufferWithLength:(command_count * 256) options:MTLResourceStorageModeShared];
            metal_buffer->primitive_type_buf = [metal_buffer->buffer.device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            break;
        case LUMINARY_RHI_COMMAND_TYPE_DISPATCH:
            icb_descriptor.commandTypes = MTLIndirectCommandTypeConcurrentDispatchThreads;
            metal_buffer->icb_params = [metal_buffer->buffer.device newBufferWithLength:sizeof(Metal3ICBDispatchParameters) options:MTLResourceStorageModeShared];
            break;
        case LUMINARY_RHI_COMMAND_TYPE_DRAW_MESH_TASKS:
            icb_descriptor.commandTypes = MTLIndirectCommandTypeDrawMeshThreadgroups;
            icb_descriptor.inheritBuffers = NO;
            icb_descriptor.maxFragmentBufferBindCount = 3;
            icb_descriptor.maxObjectBufferBindCount = 3;
            icb_descriptor.maxMeshBufferBindCount = 3;
            metal_buffer->draw_id_atomic = [metal_buffer->buffer.device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            metal_buffer->icb_params = [metal_buffer->buffer.device newBufferWithLength:sizeof(Metal3ICBDrawMeshTasksParameters) options:MTLResourceStorageModeShared];
            metal_buffer->per_draw_constants = [metal_buffer->buffer.device newBufferWithLength:(command_count * 256) options:MTLResourceStorageModeShared];
            break;
        default:
            if (out_error) {
                snprintf(out_error->message, sizeof(out_error->message), "Invalid command type for indirect command buffer");
                out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
            }
            return;
    }

    metal_buffer->icb = [metal_buffer->buffer.device newIndirectCommandBufferWithDescriptor:icb_descriptor maxCommandCount:command_count options:MTLResourceStorageModeShared];
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
    [queue addResidencySet:metal_device->internal_residency_set];

    LRHICommandQueueMetal3* out = LRHI_MALLOC(sizeof(LRHICommandQueueMetal3));
    out->base.vtable = &lrhi_metal3_command_queue_vtable;
    out->queue = queue;
    out->device = metal_device->device;
    out->device_ref = metal_device;
    *out_queue = (LRHICommandQueue)out;

#ifdef LRHI_DEBUG_METAL_PROGRAMMATIC_CAPTURE
    MTLCaptureDescriptor* capture_desc = [[MTLCaptureDescriptor alloc] init];
    capture_desc.captureObject = queue;

    NSError* capture_error = nil;
    [[MTLCaptureManager sharedCaptureManager] startCaptureWithDescriptor:capture_desc error:&capture_error];
    if (capture_error) {
        NSLog(@"[LRHI] Metal3 programmatic capture failed to start: %@", capture_error);
    }
#endif
}

static void lrhi_metal3_destroy_command_queue(LRHICommandQueue queue)
{
    LRHICommandQueueMetal3* q = (LRHICommandQueueMetal3*)queue;
    q->queue   = nil;
    q->device  = nil;
    LRHI_FREE(q);
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

#ifdef LRHI_DEBUG_METAL_PROGRAMMATIC_CAPTURE
    [[MTLCaptureManager sharedCaptureManager] stopCapture];
#endif
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
    LRHIFenceMetal3* out = LRHI_MALLOC(sizeof(LRHIFenceMetal3));
    out->base.vtable = &lrhi_metal3_fence_vtable;
    atomic_init(&out->value, initial_value);
    out->waiters = NULL;

    int mutex_result = pthread_mutex_init(&out->waiters_mutex, NULL);
    if (mutex_result != 0) {
        LRHI_FREE(out);
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
    LRHI_FREE(metal_fence);
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

    LRHICommandListMetal3* out = LRHI_MALLOC(sizeof(LRHICommandListMetal3));
    out->base.vtable = &lrhi_metal3_command_list_vtable;
    out->command_buffer = cmd_buffer;
    out->push_constant_buffer = [metal_queue->device newBufferWithLength:1024 * 1024 options:MTLResourceStorageModeShared];
    out->push_constant_offset = 0;
    out->device = metal_queue->device_ref;
    *out_command_list = (LRHICommandList)out;
}

static void lrhi_metal3_destroy_command_list(LRHICommandList command_list)
{
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;
    metal_cmd_list->command_buffer    = nil;
    metal_cmd_list->push_constant_buffer = nil;
    LRHI_FREE(metal_cmd_list);
}

static void lrhi_metal3_command_list_begin(LRHICommandList command_list, LRHIError* out_error)
{
    // No explicit begin needed for Metal command buffers
    (void)command_list;
    (void)out_error;
}

static void lrhi_metal3_command_list_end(LRHICommandList command_list, LRHIError* out_error)
{
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
    metal_cmd_list->push_constant_offset = 0;
}

static void lrhi_metal3_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t maxCommandCount, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error)
{
    // Depending on command type, dispatch a different shader to prepare for command list. Either way, always start with a full reset of the ICB:
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)indirect_command_buffer;
    LRHIBufferMetal3* metal_count_buffer = (LRHIBufferMetal3*)count_buffer;
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;
    LRHIDeviceMetal3* device = (LRHIDeviceMetal3*)metal_cmd_list->device;

    // Write push constants into slot 0 of per_draw_constants so the ICB shader can propagate them
    if (metal_buffer->per_draw_constants && push_constants) {
        LRHIMetal3ArgumentBufferData* slot0 = (LRHIMetal3ArgumentBufferData*)metal_buffer->per_draw_constants.contents;
        uint32_t copy_size = push_constant_size < 128 ? push_constant_size : 128;
        memcpy(slot0->push_constants, push_constants, copy_size);
        slot0->draw_id = 0;
    }

    // Write primitive type for render pipelines
    if (metal_buffer->primitive_type_buf && pipeline) {
        LRHIRenderPipelineMetal3* metal_pipe = (LRHIRenderPipelineMetal3*)pipeline;
        LRHIPipelineTopology topo = metal_pipe->info.topology;
        uint32_t prim = (topo == LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST) ? 0 :
                        (topo == LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST)  ? 1 : 2;
        *(uint32_t*)metal_buffer->primitive_type_buf.contents = prim;
    }

    id<MTLBlitCommandEncoder> blit_encoder = [metal_cmd_list->command_buffer blitCommandEncoder];
    [blit_encoder resetCommandsInBuffer:metal_buffer->icb withRange:NSMakeRange(0, metal_buffer->icb.size)];
    if (metal_buffer->draw_id_atomic) {
        [blit_encoder fillBuffer:metal_buffer->draw_id_atomic range:NSMakeRange(0, metal_buffer->draw_id_atomic.length) value:0];
    }
    [blit_encoder endEncoding];

    id<MTLComputeCommandEncoder> compute_encoder = [metal_cmd_list->command_buffer computeCommandEncoder];
    [compute_encoder barrierAfterQueueStages:MTLStageBlit beforeStages:MTLStageDispatch];
    switch (metal_buffer->icb_command_type) {
        case LUMINARY_RHI_COMMAND_TYPE_DRAW: {
            Metal3ICBDrawParameters* params = (Metal3ICBDrawParameters*)metal_buffer->icb_params.contents;
            params->icb = metal_buffer->icb.gpuResourceID;

            [compute_encoder setComputePipelineState:device->draw_icb_pipe];
            [compute_encoder setBuffer:metal_buffer->buffer offset:0 atIndex:0];
            [compute_encoder setBuffer:metal_count_buffer->buffer offset:0 atIndex:1];
            [compute_encoder setBuffer:metal_buffer->primitive_type_buf offset:0 atIndex:2];
            [compute_encoder setBuffer:metal_buffer->icb_params offset:0 atIndex:3];
            [compute_encoder setBuffer:metal_buffer->per_draw_constants offset:0 atIndex:4];
            [compute_encoder setBuffer:device->bindless_manager.resource_heap_buffer offset:0 atIndex:5];
            [compute_encoder setBuffer:device->bindless_manager.sampler_heap_buffer offset:0 atIndex:6];
            [compute_encoder setBuffer:metal_buffer->draw_id_atomic offset:0 atIndex:7];

            uint32_t threadgroup_size = 64;
            [compute_encoder dispatchThreads:MTLSizeMake(maxCommandCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadgroup_size, 1, 1)];

            break;
        }
        case LUMINARY_RHI_COMMAND_TYPE_DRAW_INDEXED: {
            Metal3ICBDrawIndexedParameters* params = (Metal3ICBDrawIndexedParameters*)metal_buffer->icb_params.contents;
            params->icb = metal_buffer->icb.gpuResourceID;
            params->index_buffer = ((LRHIBufferMetal3*)parameters->index_buffer)->buffer.gpuAddress;

            [compute_encoder setComputePipelineState:device->draw_indexed_icb_pipe];
            [compute_encoder setBuffer:metal_buffer->buffer offset:0 atIndex:0];
            [compute_encoder setBuffer:metal_count_buffer->buffer offset:0 atIndex:1];
            [compute_encoder setBuffer:metal_buffer->primitive_type_buf offset:0 atIndex:2];
            [compute_encoder setBuffer:metal_buffer->icb_params offset:0 atIndex:3];
            [compute_encoder setBuffer:metal_buffer->per_draw_constants offset:0 atIndex:4];
            [compute_encoder setBuffer:device->bindless_manager.resource_heap_buffer offset:0 atIndex:5];
            [compute_encoder setBuffer:device->bindless_manager.sampler_heap_buffer offset:0 atIndex:6];
            [compute_encoder setBuffer:metal_buffer->draw_id_atomic offset:0 atIndex:7];

            uint32_t threadgroup_size = 64;
            [compute_encoder dispatchThreads:MTLSizeMake(maxCommandCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadgroup_size, 1, 1)];

            break;
        }
        case LUMINARY_RHI_COMMAND_TYPE_DISPATCH: {
            Metal3ICBDispatchParameters* params = (Metal3ICBDispatchParameters*)metal_buffer->icb_params.contents;
            params->icb = metal_buffer->icb.gpuResourceID;
            params->threads_per_group_x = parameters->threads_per_group_x;
            params->threads_per_group_y = parameters->threads_per_group_y;
            params->threads_per_group_z = parameters->threads_per_group_z;

            [compute_encoder setComputePipelineState:device->dispatch_icb_pipe];
            [compute_encoder setBuffer:metal_buffer->buffer offset:0 atIndex:0];
            [compute_encoder setBuffer:metal_buffer->icb_params offset:0 atIndex:1];
            [compute_encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
            break;
        }
        case LUMINARY_RHI_COMMAND_TYPE_DRAW_MESH_TASKS: {
            Metal3ICBDrawMeshTasksParameters* params = (Metal3ICBDrawMeshTasksParameters*)metal_buffer->icb_params.contents;
            params->icb = metal_buffer->icb.gpuResourceID;
            params->threads_per_object_group_x = parameters->threads_per_object_groups_x;
            params->threads_per_object_group_y = parameters->threads_per_object_groups_y;
            params->threads_per_object_group_z = parameters->threads_per_object_groups_z;
            params->threads_per_mesh_group_x = parameters->threads_per_mesh_groups_x;
            params->threads_per_mesh_group_y = parameters->threads_per_mesh_groups_y;
            params->threads_per_mesh_group_z = parameters->threads_per_mesh_groups_z;

            [compute_encoder setComputePipelineState:device->draw_mesh_tasks_icb_pipe];
            [compute_encoder setBuffer:metal_buffer->buffer offset:0 atIndex:0];
            [compute_encoder setBuffer:metal_count_buffer->buffer offset:0 atIndex:1];
            [compute_encoder setBuffer:metal_buffer->icb_params offset:0 atIndex:2];
            [compute_encoder setBuffer:metal_buffer->per_draw_constants offset:0 atIndex:3];
            [compute_encoder setBuffer:device->bindless_manager.resource_heap_buffer offset:0 atIndex:4];
            [compute_encoder setBuffer:device->bindless_manager.sampler_heap_buffer offset:0 atIndex:5];
            [compute_encoder setBuffer:metal_buffer->draw_id_atomic offset:0 atIndex:6];

            uint32_t threadgroup_size = 64;
            [compute_encoder dispatchThreads:MTLSizeMake(maxCommandCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(threadgroup_size, 1, 1)];

            break;
        }
    }
    [compute_encoder endEncoding];
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

    LRHICopyPassMetal3* out = LRHI_MALLOC(sizeof(LRHICopyPassMetal3));
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
    metal_copy_pass->blit_encoder = nil;
    LRHI_FREE(metal_copy_pass);
}

static void lrhi_metal3_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error)
{
    (void)out_error;
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    [metal_copy_pass->blit_encoder pushDebugGroup:[NSString stringWithUTF8String:label]];
}

static void lrhi_metal3_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHICopyPassMetal3* metal_copy_pass = (LRHICopyPassMetal3*)copy_pass;
    [metal_copy_pass->blit_encoder popDebugGroup];
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

    LRHIResidencySetMetal3* out = LRHI_MALLOC(sizeof(LRHIResidencySetMetal3));
    out->base.vtable = &lrhi_metal3_residency_set_vtable;
    out->residency_set = residency_set;
    *out_residency_set = (LRHIResidencySet)out;
}

static void lrhi_metal3_destroy_residency_set(LRHIResidencySet residency_set)
{
    LRHIResidencySetMetal3* rs = (LRHIResidencySetMetal3*)residency_set;
    rs->residency_set = nil;
    LRHI_FREE(rs);
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
    if (metal_buffer->icb) {
        [metal_residency_set->residency_set addAllocation:metal_buffer->icb];
    }
    if (metal_buffer->per_draw_constants) {
        [metal_residency_set->residency_set addAllocation:metal_buffer->per_draw_constants];
    }
    if (metal_buffer->primitive_type_buf) {
        [metal_residency_set->residency_set addAllocation:metal_buffer->primitive_type_buf];
    }
    if (metal_buffer->draw_id_atomic) {
        [metal_residency_set->residency_set addAllocation:metal_buffer->draw_id_atomic];
    }
}

static void lrhi_metal3_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;

    [metal_residency_set->residency_set addAllocation:metal_blas->acceleration_structure];
}

static void lrhi_metal3_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;

    [metal_residency_set->residency_set addAllocation:metal_tlas->acceleration_structure];
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
    if (metal_buffer->icb) {
        [metal_residency_set->residency_set removeAllocation:metal_buffer->icb];
    }
}

static void lrhi_metal3_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;

    [metal_residency_set->residency_set removeAllocation:metal_blas->acceleration_structure];
}

static void lrhi_metal3_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    LRHIResidencySetMetal3* metal_residency_set = (LRHIResidencySetMetal3*)residency_set;
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;

    [metal_residency_set->residency_set removeAllocation:metal_tlas->acceleration_structure];
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
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    if (info->format == LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED) {
        info->format = metal_texture->info.format;
    }

    // Create the view
    LRHITextureViewMetal3* out = LRHI_MALLOC(sizeof(LRHITextureViewMetal3));
    out->base.vtable = &lrhi_metal3_texture_view_vtable;
    out->info = *info;

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
        LRHI_FREE(out);
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create texture view");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_texture_view = NULL;
        return;
    }

    if (info->usage == LUMINARY_RHI_TEXTURE_USAGE_SAMPLED || info->usage & LUMINARY_RHI_TEXTURE_USAGE_STORAGE) {
        out->bindless_index = lrhi_metal3_bindless_manager_write_texture_view(&metal_device->bindless_manager, out, out_error);
    }
    out->bindless_manager = &metal_device->bindless_manager;
    *out_texture_view = (LRHITextureView)out;
}

static void lrhi_metal3_destroy_texture_view(LRHITextureView texture_view)
{
    LRHITextureViewMetal3* metal_texture_view = (LRHITextureViewMetal3*)texture_view;
    if (metal_texture_view->info.usage == LUMINARY_RHI_TEXTURE_USAGE_SAMPLED || metal_texture_view->info.usage == LUMINARY_RHI_TEXTURE_USAGE_STORAGE) {
        lrhi_metal3_bindless_manager_free_resource_view(metal_texture_view->bindless_manager, metal_texture_view->bindless_index);
    }
    // Nil the id<MTLTexture> field so ARC emits the release before LRHI_FREE()
    // (id fields in malloc'd C structs are __unsafe_unretained — ARC won't release them otherwise)
    metal_texture_view->texture_view = nil;
    LRHI_FREE(texture_view);
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

    LRHIRenderPassMetal3* render_pass = LRHI_CALLOC(1, sizeof(LRHIRenderPassMetal3));
    render_pass->base.vtable = &lrhi_metal3_render_pass_vtable;
    render_pass->render_encoder = render_encoder;
    render_pass->command_list = metal_cmd_list;
    return (LRHIRenderPass)render_pass;
}

static void lrhi_metal3_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder endEncoding];
    metal_render_pass->render_encoder = nil;
    LRHI_FREE(metal_render_pass);
}

static void lrhi_metal3_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder pushDebugGroup:[NSString stringWithUTF8String:label]];
}

static void lrhi_metal3_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder popDebugGroup];
}

static void lrhi_metal3_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder memoryBarrierWithScope:MTLBarrierScopeRenderTargets|MTLBarrierScopeBuffers|MTLBarrierScopeTextures afterStages:lrhi_metal3_render_stages_to_mtl(afterStage) beforeStages:lrhi_metal3_render_stages_to_mtl(beforeStage)];
}

static void lrhi_metal3_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    [metal_render_pass->render_encoder barrierAfterQueueStages:lrhi_metal3_render_stage_to_mtl(afterStage) beforeStages:lrhi_metal3_render_stage_to_mtl(beforeStage)];
}

static void lrhi_metal3_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    (void)out_error;
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    uint32_t copy_size = size < 128 ? size : 128;
    memcpy(metal_render_pass->current_push_constants, data, copy_size);
}

static void lrhi_metal3_flush_push_constants(LRHIRenderPassMetal3* render_pass, uint8_t is_mesh_pipeline, LRHIError* out_error)
{
    LRHICommandListMetal3* cmd = render_pass->command_list;

    if (cmd->push_constant_offset + 256 > (uint32_t)cmd->push_constant_buffer.length) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Push constant linear allocator exhausted");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    uint32_t offset = cmd->push_constant_offset;
    LRHIMetal3ArgumentBufferData* slot = (LRHIMetal3ArgumentBufferData*)((uint8_t*)cmd->push_constant_buffer.contents + offset);
    memcpy(slot->push_constants, render_pass->current_push_constants, 128);
    slot->draw_id = render_pass->current_draw_id++;
    cmd->push_constant_offset += 256;

    [render_pass->render_encoder setVertexBuffer:cmd->push_constant_buffer offset:offset atIndex:kIRArgumentBufferBindPoint];
    [render_pass->render_encoder setFragmentBuffer:cmd->push_constant_buffer offset:offset atIndex:kIRArgumentBufferBindPoint];

    if (is_mesh_pipeline) {
        [render_pass->render_encoder setObjectBuffer:cmd->push_constant_buffer offset:offset atIndex:kIRArgumentBufferBindPoint];
        [render_pass->render_encoder setMeshBuffer:cmd->push_constant_buffer offset:offset atIndex:kIRArgumentBufferBindPoint];
    }
}

static void lrhi_metal3_flush_push_constants_compute(LRHIComputePassMetal3* render_pass, LRHIError* out_error)
{
    LRHICommandListMetal3* cmd = render_pass->command_list;

    if (cmd->push_constant_offset + 256 > (uint32_t)cmd->push_constant_buffer.length) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Push constant linear allocator exhausted");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    uint32_t offset = cmd->push_constant_offset;
    LRHIMetal3ArgumentBufferData* slot = (LRHIMetal3ArgumentBufferData*)((uint8_t*)cmd->push_constant_buffer.contents + offset);
    memcpy(slot->push_constants, render_pass->current_push_constants, 128);
    cmd->push_constant_offset += 256;

    [render_pass->compute_encoder setBuffer:cmd->push_constant_buffer offset:offset atIndex:kIRArgumentBufferBindPoint];
}

static void lrhi_metal3_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
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

static void lrhi_metal3_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    MTLScissorRect scissor_rect = {
        .x = x,
        .y = y,
        .width = width,
        .height = height
    };
    [metal_render_pass->render_encoder setScissorRect:scissor_rect];
}

static void lrhi_metal3_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    LRHIRenderPipelineMetal3* metal_pipeline = (LRHIRenderPipelineMetal3*)pipeline;
    [metal_render_pass->render_encoder setRenderPipelineState:metal_pipeline->pipeline_state];
    if (metal_pipeline->info.depth_test_enable) {
        [metal_render_pass->render_encoder setDepthStencilState:metal_pipeline->depth_stencil_state];
    }
    [metal_render_pass->render_encoder setTriangleFillMode:lrhi_metal3_fill_mode_to_mtl(metal_pipeline->info.fill_mode)];
    [metal_render_pass->render_encoder setCullMode:lrhi_metal3_cull_mode_to_mtl(metal_pipeline->info.cull_mode)];
    [metal_render_pass->render_encoder setFrontFacingWinding:lrhi_metal3_front_face_to_mtl(metal_pipeline->info.front_face)];

    // Bind resource heaps
    [metal_render_pass->render_encoder setVertexBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->render_encoder setFragmentBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->render_encoder setVertexBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];
    [metal_render_pass->render_encoder setFragmentBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];

    metal_render_pass->current_render_pipeline = pipeline;
}

static void lrhi_metal3_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    LRHIMeshPipelineMetal3* metal_pipeline = (LRHIMeshPipelineMetal3*)pipeline;
    [metal_render_pass->render_encoder setRenderPipelineState:metal_pipeline->pipeline_state];
    if (metal_pipeline->info.depth_test_enable) {
        [metal_render_pass->render_encoder setDepthStencilState:metal_pipeline->depth_stencil_state];
    }
    [metal_render_pass->render_encoder setTriangleFillMode:lrhi_metal3_fill_mode_to_mtl(metal_pipeline->info.fill_mode)];
    [metal_render_pass->render_encoder setCullMode:lrhi_metal3_cull_mode_to_mtl(metal_pipeline->info.cull_mode)];
    [metal_render_pass->render_encoder setFrontFacingWinding:lrhi_metal3_front_face_to_mtl(metal_pipeline->info.front_face)];

    // Bind resource heaps
    [metal_render_pass->render_encoder setVertexBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->render_encoder setFragmentBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->render_encoder setObjectBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->render_encoder setMeshBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_render_pass->render_encoder setVertexBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];
    [metal_render_pass->render_encoder setFragmentBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];
    [metal_render_pass->render_encoder setObjectBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];
    [metal_render_pass->render_encoder setMeshBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];
}

static void lrhi_metal3_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    MTLPrimitiveType primitive_type = lrhi_metal3_primitive_topology_to_mtl(metal_render_pass->current_render_pipeline ? ((LRHIRenderPipelineMetal3*)metal_render_pass->current_render_pipeline)->info.topology : LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST);

    lrhi_metal3_flush_push_constants(metal_render_pass, 0, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    IRRuntimeDrawPrimitives(metal_render_pass->render_encoder, primitive_type, first_vertex, vertex_count, instance_count, first_instance);
}

static void lrhi_metal3_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    LRHIBufferMetal3* metal_index_buffer = (LRHIBufferMetal3*)index_buffer;

    MTLPrimitiveType primitive_type = lrhi_metal3_primitive_topology_to_mtl(metal_render_pass->current_render_pipeline ? ((LRHIRenderPipelineMetal3*)metal_render_pass->current_render_pipeline)->info.topology : LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST);
    MTLIndexType mtl_index_type = (index_stride == 4) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;

    lrhi_metal3_flush_push_constants(metal_render_pass, 0, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    IRRuntimeDrawIndexedPrimitives(metal_render_pass->render_encoder, primitive_type, index_count, mtl_index_type, metal_index_buffer->buffer, first_index * index_stride, instance_count, vertex_offset, first_instance);
}

static void lrhi_metal3_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;

    lrhi_metal3_flush_push_constants(metal_render_pass, 1, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    [metal_render_pass->render_encoder drawMeshThreadgroups:MTLSizeMake(num_groups_x, num_groups_y, num_groups_z) threadsPerObjectThreadgroup:MTLSizeMake(threads_per_object_group_x, threads_per_object_group_y, threads_per_object_group_z) threadsPerMeshThreadgroup:MTLSizeMake(threads_per_mesh_group_x, threads_per_mesh_group_y, threads_per_mesh_group_z)];
}

static void lrhi_metal3_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error)
{
    LRHIRenderPassMetal3* metal_render_pass = (LRHIRenderPassMetal3*)render_pass;
    LRHIBufferMetal3* metal_icb = (LRHIBufferMetal3*)indirect_command_buffer;
    (void)count_buffer;
    [metal_render_pass->render_encoder executeCommandsInBuffer:metal_icb->icb withRange:NSMakeRange(0, max_command_count)];
}

// Shader module

static void lrhi_metal3_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

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

    if (info->name) {
        library.label  = [NSString stringWithUTF8String:info->name];
        function.label = [NSString stringWithUTF8String:info->name];
    }

    LRHIShaderModuleMetal3* out = LRHI_MALLOC(sizeof(LRHIShaderModuleMetal3));
    out->base.vtable = &lrhi_metal3_shader_module_vtable;
    out->library = library;
    out->function = function;
    out->info = *info;
    *out_shader_module = (LRHIShaderModule)out;
}

static void lrhi_metal3_destroy_shader_module(LRHIShaderModule shader_module)
{
    LRHIShaderModuleMetal3* m = (LRHIShaderModuleMetal3*)shader_module;
    m->library  = nil;
    m->function = nil;
    LRHI_FREE(shader_module);
}

static void lrhi_metal3_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info)
{
    LRHIShaderModuleMetal3* metal_shader_module = (LRHIShaderModuleMetal3*)shader_module;
    *out_info = metal_shader_module->info;
}

// Render pipeline

static void lrhi_metal3_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = ((LRHIShaderModuleMetal3*)info->vertex_shader)->function;
    if (info->fragment_shader) {
        descriptor.fragmentFunction = ((LRHIShaderModuleMetal3*)info->fragment_shader)->function;
    }
    for (uint32_t i = 0; i < info->render_target_count; i++) {
        descriptor.colorAttachments[i].pixelFormat = lrhi_metal3_pixel_format(info->render_target_formats[i]);
    }
    if (info->depth_test_enable || info->stencil_test_enable) {
        descriptor.depthAttachmentPixelFormat = lrhi_metal3_pixel_format(info->depth_stencil_format);
        descriptor.stencilAttachmentPixelFormat = lrhi_metal3_pixel_format(info->depth_stencil_format);
    }
    descriptor.inputPrimitiveTopology = lrhi_metal3_primitive_topology_class_to_mtl(info->topology);
    descriptor.rasterizationEnabled = YES;
    if (info->supports_indirect_commands) {
        descriptor.supportIndirectCommandBuffers = YES;
    }

    // Blending
    for (uint32_t i = 0; i < info->render_target_count; i++) {
        if (info->blend_enable[i]) {
            descriptor.colorAttachments[i].blendingEnabled = YES;
            descriptor.colorAttachments[i].sourceRGBBlendFactor = lrhi_metal3_blend_factor_to_mtl(info->blend_src_rgb_factor[i]);
            descriptor.colorAttachments[i].destinationRGBBlendFactor = lrhi_metal3_blend_factor_to_mtl(info->blend_dst_rgb_factor[i]);
            descriptor.colorAttachments[i].rgbBlendOperation = lrhi_metal3_blend_op_to_mtl(info->blend_rgb_op[i]);
            descriptor.colorAttachments[i].sourceAlphaBlendFactor = lrhi_metal3_blend_factor_to_mtl(info->blend_src_alpha_factor[i]);
            descriptor.colorAttachments[i].destinationAlphaBlendFactor = lrhi_metal3_blend_factor_to_mtl(info->blend_dst_alpha_factor[i]);
            descriptor.colorAttachments[i].alphaBlendOperation = lrhi_metal3_blend_op_to_mtl(info->blend_alpha_op[i]);
        }
    }

    // Depth stencil state
    id<MTLDepthStencilState> depth_stencil_state = nil;
    if (info->depth_test_enable || info->stencil_test_enable) {
        MTLDepthStencilDescriptor* depth_stencil_descriptor = [[MTLDepthStencilDescriptor alloc] init];
        depth_stencil_descriptor.depthCompareFunction = lrhi_metal3_compare_op_to_mtl(info->depth_compare_op);
        depth_stencil_descriptor.depthWriteEnabled = info->depth_write_enable;
        // TODO: Stencil state

        depth_stencil_state = [metal_device->device newDepthStencilStateWithDescriptor:depth_stencil_descriptor];
    }

    if (info->name) { descriptor.label = [NSString stringWithUTF8String:info->name]; }

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

    LRHIRenderPipelineMetal3* out = LRHI_MALLOC(sizeof(LRHIRenderPipelineMetal3));
    out->base.vtable = &lrhi_metal3_render_pipeline_vtable;
    out->pipeline_state = pipeline_state;
    out->depth_stencil_state = depth_stencil_state;
    out->info = *info;
    out->bindless_manager = &metal_device->bindless_manager;
    *out_pipeline = (LRHIRenderPipeline)out;
}

static void lrhi_metal3_destroy_render_pipeline(LRHIRenderPipeline pipeline)
{
    LRHIRenderPipelineMetal3* p = (LRHIRenderPipelineMetal3*)pipeline;
    p->pipeline_state      = nil;
    p->depth_stencil_state = nil;
    LRHI_FREE(pipeline);
}

static void lrhi_metal3_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info)
{
    LRHIRenderPipelineMetal3* metal_pipeline = (LRHIRenderPipelineMetal3*)pipeline;
    *out_info = metal_pipeline->info;
}

static uint64_t lrhi_metal3_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error)
{
    LRHIRenderPipelineMetal3* metal_pipeline = (LRHIRenderPipelineMetal3*)pipeline;
    return (uint64_t)metal_pipeline->pipeline_state.allocatedSize;
}

// Mesh pipeline

static void lrhi_metal3_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    
    MTLMeshRenderPipelineDescriptor* descriptor = [[MTLMeshRenderPipelineDescriptor alloc] init];
    if (info->task_shader) {
        descriptor.objectFunction = ((LRHIShaderModuleMetal3*)info->task_shader)->function;
    }
    descriptor.meshFunction = ((LRHIShaderModuleMetal3*)info->mesh_shader)->function;
    if (info->fragment_shader) {
        descriptor.fragmentFunction = ((LRHIShaderModuleMetal3*)info->fragment_shader)->function;
    }
    for (uint32_t i = 0; i < info->render_target_count; i++) {
        descriptor.colorAttachments[i].pixelFormat = lrhi_metal3_pixel_format(info->render_target_formats[i]);
    }
    if (info->depth_test_enable || info->stencil_test_enable) {
        descriptor.depthAttachmentPixelFormat = lrhi_metal3_pixel_format(info->depth_stencil_format);
        descriptor.stencilAttachmentPixelFormat = lrhi_metal3_pixel_format(info->depth_stencil_format);
    }
    if (info->supports_indirect_commands) {
        descriptor.supportIndirectCommandBuffers = YES;
    }

    // Depth stencil
    id<MTLDepthStencilState> depth_stencil_state = nil;
    if (info->depth_test_enable || info->stencil_test_enable) {
        MTLDepthStencilDescriptor* depth_stencil_descriptor = [[MTLDepthStencilDescriptor alloc] init];
        depth_stencil_descriptor.depthCompareFunction = lrhi_metal3_compare_op_to_mtl(info->depth_compare_op);
        depth_stencil_descriptor.depthWriteEnabled = info->depth_write_enable;
        // TODO: Stencil state

        depth_stencil_state = [metal_device->device newDepthStencilStateWithDescriptor:depth_stencil_descriptor];
    }

    if (info->name) { descriptor.label = [NSString stringWithUTF8String:info->name]; }

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

    LRHIMeshPipelineMetal3* out = LRHI_MALLOC(sizeof(LRHIMeshPipelineMetal3));
    out->base.vtable = &lrhi_metal3_mesh_pipeline_vtable;
    out->pipeline_state = pipeline_state;
    out->depth_stencil_state = depth_stencil_state;
    out->info = *info;
    out->bindless_manager = &metal_device->bindless_manager;
    *out_pipeline = (LRHIMeshPipeline)out;
}

static void lrhi_metal3_destroy_mesh_pipeline(LRHIMeshPipeline pipeline)
{
    LRHIMeshPipelineMetal3* p = (LRHIMeshPipelineMetal3*)pipeline;
    p->pipeline_state      = nil;
    p->depth_stencil_state = nil;
    LRHI_FREE(pipeline);
}

static void lrhi_metal3_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info)
{
    LRHIMeshPipelineMetal3* metal_pipeline = (LRHIMeshPipelineMetal3*)pipeline;
    *out_info = metal_pipeline->info;
}

static uint64_t lrhi_metal3_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error)
{
    LRHIMeshPipelineMetal3* metal_pipeline = (LRHIMeshPipelineMetal3*)pipeline;
    return (uint64_t)metal_pipeline->pipeline_state.allocatedSize;
}

// Compute pipeline

static void lrhi_metal3_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

    MTLComputePipelineDescriptor* descriptor = [[MTLComputePipelineDescriptor alloc] init];
    descriptor.computeFunction = ((LRHIShaderModuleMetal3*)info->compute_shader)->function;
    if (info->supports_indirect_commands) {
        descriptor.supportIndirectCommandBuffers = YES;
    }

    if (info->name) { descriptor.label = [NSString stringWithUTF8String:info->name]; }

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

    LRHIComputePipelineMetal3* out = LRHI_MALLOC(sizeof(LRHIComputePipelineMetal3));
    out->base.vtable = &lrhi_metal3_compute_pipeline_vtable;
    out->pipeline_state = pipeline_state;
    out->info = *info;
    out->bindless_manager = &metal_device->bindless_manager;
    *out_pipeline = (LRHIComputePipeline)out;
}

static void lrhi_metal3_destroy_compute_pipeline(LRHIComputePipeline pipeline)
{
    LRHIComputePipelineMetal3* p = (LRHIComputePipelineMetal3*)pipeline;
    p->pipeline_state = nil;
    LRHI_FREE(pipeline);
}

static void lrhi_metal3_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info)
{
    LRHIComputePipelineMetal3* metal_pipeline = (LRHIComputePipelineMetal3*)pipeline;
    *out_info = metal_pipeline->info;
}

static uint64_t lrhi_metal3_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error)
{
    LRHIComputePipelineMetal3* metal_pipeline = (LRHIComputePipelineMetal3*)pipeline;
    return (uint64_t)metal_pipeline->pipeline_state.allocatedSize;
}

// Compute pass

static LRHIComputePass lrhi_metal3_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;

    id<MTLComputeCommandEncoder> compute_encoder = [metal_cmd_list->command_buffer computeCommandEncoder];
    if (!compute_encoder) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Failed to create compute command encoder for compute pass");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return NULL;
    }

    LRHIComputePassMetal3* compute_pass = LRHI_CALLOC(1, sizeof(LRHIComputePassMetal3));
    compute_pass->base.vtable = &lrhi_metal3_compute_pass_vtable;
    compute_pass->compute_encoder = compute_encoder;
    compute_pass->command_list = metal_cmd_list;
    return (LRHIComputePass)compute_pass;
}

static void lrhi_metal3_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    [metal_compute_pass->compute_encoder endEncoding];
    metal_compute_pass->compute_encoder = nil;
    LRHI_FREE(metal_compute_pass);
}

static void lrhi_metal3_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    [metal_compute_pass->compute_encoder pushDebugGroup:[NSString stringWithUTF8String:label]];
}

static void lrhi_metal3_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    [metal_compute_pass->compute_encoder popDebugGroup];
}

static void lrhi_metal3_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    [metal_compute_pass->compute_encoder memoryBarrierWithScope:MTLBarrierScopeBuffers|MTLBarrierScopeTextures];
}

static void lrhi_metal3_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    [metal_compute_pass->compute_encoder barrierAfterQueueStages:lrhi_metal3_render_stage_to_mtl(after_stage) beforeStages:MTLStageDispatch];
}

static void lrhi_metal3_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error)
{
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    LRHIComputePipelineMetal3* metal_pipeline = (LRHIComputePipelineMetal3*)pipeline;
    [metal_compute_pass->compute_encoder setComputePipelineState:metal_pipeline->pipeline_state];

    // Bind resource heaps
    [metal_compute_pass->compute_encoder setBuffer:metal_pipeline->bindless_manager->resource_heap_buffer offset:0 atIndex:kIRDescriptorHeapBindPoint];
    [metal_compute_pass->compute_encoder setBuffer:metal_pipeline->bindless_manager->sampler_heap_buffer offset:0 atIndex:kIRSamplerHeapBindPoint];
}

static void lrhi_metal3_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error)
{
    (void)out_error;
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    uint32_t copy_size = size < 128 ? size : 128;
    memcpy(metal_compute_pass->current_push_constants, data, copy_size);
}

static void lrhi_metal3_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error)
{
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;

    lrhi_metal3_flush_push_constants_compute(metal_compute_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    [metal_compute_pass->compute_encoder dispatchThreadgroups:MTLSizeMake(num_groups_x, num_groups_y, num_groups_z) threadsPerThreadgroup:MTLSizeMake(threads_per_group_x, threads_per_group_y, threads_per_group_z)];
}

static void lrhi_metal3_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error)
{
    LRHIComputePassMetal3* metal_compute_pass = (LRHIComputePassMetal3*)compute_pass;
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)indirect_command_buffer;

    lrhi_metal3_flush_push_constants_compute(metal_compute_pass, out_error);
    if (out_error && out_error->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

    [metal_compute_pass->compute_encoder executeCommandsInBuffer:metal_buffer->icb withRange:NSMakeRange(0, 1)];
}

// Buffer view

static void lrhi_metal3_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    LRHIBufferMetal3* metal_buffer = (LRHIBufferMetal3*)info->buffer;

    if (info->offset > metal_buffer->info.size) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Buffer view range exceeds buffer size");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        *out_buffer_view = NULL;
        return;
    }

    LRHIBufferViewMetal3* out = LRHI_MALLOC(sizeof(LRHIBufferViewMetal3));
    out->base.vtable = &lrhi_metal3_buffer_view_vtable;
    out->gpu_address = metal_buffer->buffer.gpuAddress + info->offset;
    out->bindless_index = lrhi_metal3_bindless_manager_write_buffer_view(&metal_device->bindless_manager, out, out_error);
    out->bindless_manager = &metal_device->bindless_manager;
    *out_buffer_view = (LRHIBufferView)out;
}

static void lrhi_metal3_destroy_buffer_view(LRHIBufferView buffer_view)
{
    LRHIBufferViewMetal3* metal_buffer_view = (LRHIBufferViewMetal3*)buffer_view;
    lrhi_metal3_bindless_manager_free_resource_view(metal_buffer_view->bindless_manager, metal_buffer_view->bindless_index);
    LRHI_FREE(buffer_view);
}

static void lrhi_metal3_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info)
{
    LRHIBufferViewMetal3* metal_buffer_view = (LRHIBufferViewMetal3*)buffer_view;
    *out_info = metal_buffer_view->info;
}

static uint32_t lrhi_metal3_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error)
{
    LRHIBufferViewMetal3* metal_buffer_view = (LRHIBufferViewMetal3*)buffer_view;
    return metal_buffer_view->bindless_index;
}

// Sampler

static void lrhi_metal3_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

    MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];
    descriptor.minFilter = lrhi_metal3_filter_to_mtl(info->min_filter);
    descriptor.magFilter = lrhi_metal3_filter_to_mtl(info->mag_filter);
    descriptor.mipFilter = lrhi_metal3_mip_filter_to_mtl(info->mipmap_filter);
    descriptor.sAddressMode = lrhi_metal3_address_mode_to_mtl(info->address_mode_u);
    descriptor.tAddressMode = lrhi_metal3_address_mode_to_mtl(info->address_mode_v);
    descriptor.rAddressMode = lrhi_metal3_address_mode_to_mtl(info->address_mode_w);
    descriptor.lodMinClamp = info->min_lod;
    descriptor.lodMaxClamp = info->max_lod;
    descriptor.maxAnisotropy = info->anisotropy_enable ? 16.0f : 1.0f;
    if (info->compare_enable) {
        descriptor.compareFunction = lrhi_metal3_compare_op_to_mtl(info->compare_op);
    }
    descriptor.supportArgumentBuffers = YES;
    if (info->name) { descriptor.label = [NSString stringWithUTF8String:info->name]; }

    LRHISamplerMetal3* out = LRHI_MALLOC(sizeof(LRHISamplerMetal3));
    out->base.vtable = &lrhi_metal3_sampler_vtable;
    out->info = *info;
    out->sampler_state = [metal_device->device newSamplerStateWithDescriptor:descriptor];
    out->bindless_index = lrhi_metal3_bindless_manager_write_sampler(&metal_device->bindless_manager, out, out_error);
    out->bindless_manager = &metal_device->bindless_manager;
    *out_sampler = (LRHISampler)out;
}

static void lrhi_metal3_destroy_sampler(LRHISampler sampler)
{
    LRHISamplerMetal3* metal_sampler = (LRHISamplerMetal3*)sampler;
    lrhi_metal3_bindless_manager_free_sampler(metal_sampler->bindless_manager, metal_sampler->bindless_index);
    LRHI_FREE(sampler);
}

static void lrhi_metal3_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info)
{
    LRHISamplerMetal3* metal_sampler = (LRHISamplerMetal3*)sampler;
    *out_info = metal_sampler->info;
}

static uint32_t lrhi_metal3_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error)
{
    LRHISamplerMetal3* metal_sampler = (LRHISamplerMetal3*)sampler;
    return metal_sampler->bindless_index;
}

// Acceleration structure pass

static LRHIAccelerationStructurePass lrhi_metal3_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
    LRHICommandListMetal3* metal_cmd_list = (LRHICommandListMetal3*)command_list;
    
    LRHIAccelerationStructurePassMetal3* as_pass = LRHI_CALLOC(1, sizeof(LRHIAccelerationStructurePassMetal3));
    as_pass->base .vtable = &lrhi_metal3_acceleration_structure_pass_vtable;
    as_pass->command_list = metal_cmd_list;
    as_pass->as_encoder = [metal_cmd_list->command_buffer accelerationStructureCommandEncoder];
    return (LRHIAccelerationStructurePass)as_pass;
}

static void lrhi_metal3_acceleration_structure_pass_end(LRHIAccelerationStructurePass as_pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)as_pass;
    [metal_as_pass->as_encoder endEncoding];
    metal_as_pass->as_encoder = nil;
    LRHI_FREE(metal_as_pass);
}

static void lrhi_metal3_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error)
{
    (void)out_error;
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    [metal_as_pass->as_encoder pushDebugGroup:[NSString stringWithUTF8String:label]];
}

static void lrhi_metal3_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
    (void)out_error;
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    [metal_as_pass->as_encoder popDebugGroup];
}

static void lrhi_metal3_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass as_pass, LRHIError* out_error)
{
    // no-op since Metal automatically handles synchronization for acceleration structure builds
}

static void lrhi_metal3_acceleration_structure_pass_encoder_barrier(LRHIAccelerationStructurePass as_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
    (void)out_error;
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)as_pass;
    [metal_as_pass->as_encoder barrierAfterQueueStages:lrhi_metal3_render_stage_to_mtl(after_stage) beforeStages:MTLStageAccelerationStructure];
}

static void lrhi_metal3_acceleration_structure_pass_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;
    LRHIBufferMetal3* metal_scratch_buffer = (LRHIBufferMetal3*)scratch_buffer;

    [metal_as_pass->as_encoder buildAccelerationStructure:metal_blas->acceleration_structure descriptor:metal_blas->mtl_descriptor scratchBuffer:metal_scratch_buffer->buffer scratchBufferOffset:scratch_offset];
}

static void lrhi_metal3_acceleration_structure_pass_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;
    LRHIBufferMetal3* metal_scratch_buffer = (LRHIBufferMetal3*)scratch_buffer;

    metal_tlas->mtl_descriptor.instanceDescriptorBuffer = metal_tlas->instance_buffer;
    metal_tlas->mtl_descriptor.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
    metal_tlas->mtl_descriptor.instanceCount = metal_tlas->instance_count;
    metal_tlas->mtl_descriptor.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);

    [metal_as_pass->as_encoder buildAccelerationStructure:metal_tlas->acceleration_structure descriptor:metal_tlas->mtl_descriptor scratchBuffer:metal_scratch_buffer->buffer scratchBufferOffset:scratch_offset];
}

// BLAS

static void lrhi_metal3_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
    // Convert geometries to Metal's format
    uint32_t geometry_count = info->geometry_count;
    NSMutableArray<MTLAccelerationStructureGeometryDescriptor*>* mtl_geometries = [NSMutableArray arrayWithCapacity:geometry_count];

    if (info->geometry_type == LUMINARY_RHI_BOTTOM_LEVEL_GEOMETRY_TYPE_TRIANGLES) {
        for (uint32_t i = 0; i < geometry_count; i++) {
            LRHIBLASGeometryInfo* geometry = &info->geometries[i];
            LRHIBufferMetal3* metal_vertex_buffer = (LRHIBufferMetal3*)geometry->triangles.vertex_buffer;
            LRHIBufferMetal3* metal_index_buffer  = (LRHIBufferMetal3*)geometry->triangles.index_buffer;

            MTLAccelerationStructureTriangleGeometryDescriptor* mtl_geometry = [[MTLAccelerationStructureTriangleGeometryDescriptor alloc] init];
            mtl_geometry.vertexBuffer       = metal_vertex_buffer->buffer;
            mtl_geometry.vertexStride       = metal_vertex_buffer->info.stride;
            mtl_geometry.vertexFormat       = MTLAttributeFormatFloat3;
            mtl_geometry.vertexBufferOffset = geometry->triangles.vertex_offset;
            mtl_geometry.indexBuffer        = metal_index_buffer->buffer;
            mtl_geometry.indexType          = metal_index_buffer->info.stride == 4 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
            mtl_geometry.indexBufferOffset  = geometry->triangles.index_offset;
            mtl_geometry.transformationMatrixLayout = MTLMatrixLayoutRowMajor;
            mtl_geometry.triangleCount      = geometry->triangles.index_count / 3;
            mtl_geometry.opaque             = geometry->opaque ? YES : NO;
            [mtl_geometries addObject:mtl_geometry];
        }
    } else {
        for (uint32_t i = 0; i < geometry_count; i++) {
            LRHIBLASGeometryInfo* geometry = &info->geometries[i];
            LRHIBufferMetal3* metal_aabb_buffer = (LRHIBufferMetal3*)geometry->aabbs.aabb_buffer;

            MTLAccelerationStructureBoundingBoxGeometryDescriptor* mtl_geometry = [[MTLAccelerationStructureBoundingBoxGeometryDescriptor alloc] init];
            mtl_geometry.boundingBoxBuffer       = metal_aabb_buffer->buffer;
            mtl_geometry.boundingBoxBufferOffset = geometry->aabbs.aabb_offset;
            mtl_geometry.boundingBoxCount        = geometry->aabbs.aabb_count;
            mtl_geometry.boundingBoxStride       = geometry->aabbs.aabb_stride;
            mtl_geometry.opaque                  = geometry->opaque ? YES : NO;
            [mtl_geometries addObject:mtl_geometry];
        }
    }

    MTLPrimitiveAccelerationStructureDescriptor* as_desc = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
    as_desc.usage = MTLAccelerationStructureUsagePreferFastBuild;
    if (info->allow_update) {
        as_desc.usage |= MTLAccelerationStructureUsageRefit;
    }
    as_desc.geometryDescriptors = mtl_geometries;
    
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;

    LRHIBLASMetal3* out = LRHI_MALLOC(sizeof(LRHIBLASMetal3));
    out->base.vtable = &lrhi_metal3_blas_vtable;
    out->info = *info;
    out->device = metal_device;
    out->sizes = [metal_device->device accelerationStructureSizesWithDescriptor:as_desc];
    out->acceleration_structure = [metal_device->device newAccelerationStructureWithSize:out->sizes.accelerationStructureSize];
    if (info->name) { out->acceleration_structure.label = [NSString stringWithUTF8String:info->name]; }
    out->mtl_descriptor = as_desc;
    *out_blas = (LRHIBottomLevelAccelerationStructure)out;
}

static void lrhi_metal3_destroy_bottom_level_acceleration_structure(LRHIBottomLevelAccelerationStructure blas)
{
    LRHI_FREE(blas);
}

static void lrhi_metal3_get_bottom_level_acceleration_structure_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info)
{
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;
    *out_info = metal_blas->info;
}

static LRHIAccelerationStructureBufferSizes lrhi_metal3_bottom_level_acceleration_structure_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;

    LRHIAccelerationStructureBufferSizes out_sizes;
    out_sizes.build_scratch_size = metal_blas->sizes.buildScratchBufferSize;
    out_sizes.update_scratch_size = metal_blas->sizes.refitScratchBufferSize;
    return out_sizes;
}

// TLAS

static void lrhi_metal3_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error)
{
    MTLInstanceAccelerationStructureDescriptor* as_desc = [MTLInstanceAccelerationStructureDescriptor descriptor];
    as_desc.instanceCount = info->max_instance_count;
    as_desc.usage = MTLAccelerationStructureUsagePreferFastIntersection;
    
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    LRHITLASMetal3* out = LRHI_MALLOC(sizeof(LRHITLASMetal3));

    // Create buffers
    out->instance_buffer = [metal_device->device newBufferWithLength:sizeof(MTLIndirectAccelerationStructureInstanceDescriptor) * info->max_instance_count options:MTLResourceStorageModeShared];
    out->mapped_instance_buffer = (MTLIndirectAccelerationStructureInstanceDescriptor*)out->instance_buffer.contents;
    [metal_device->internal_residency_set addAllocation:out->instance_buffer];

    out->resource_id_buffer = [metal_device->device newBufferWithLength:sizeof(uint64_t) options:MTLResourceStorageModeShared];
    out->mapped_resource_id_buffer = (uint64_t*)out->resource_id_buffer.contents;
    [metal_device->internal_residency_set addAllocation:out->resource_id_buffer];
    [metal_device->internal_residency_set commit];

    out->base.vtable = &lrhi_metal3_tlas_vtable;
    out->info = *info;
    out->device = metal_device;
    out->sizes = [metal_device->device accelerationStructureSizesWithDescriptor:as_desc];
    out->acceleration_structure = [metal_device->device newAccelerationStructureWithSize:out->sizes.accelerationStructureSize];
    if (info->name) { out->acceleration_structure.label = [NSString stringWithUTF8String:info->name]; }
    MTLResourceID resource_id = out->acceleration_structure.gpuResourceID;
    memcpy(out->mapped_resource_id_buffer, &resource_id, sizeof(uint64_t));
    out->bindless_index = lrhi_metal3_bindless_manager_write_tlas(&metal_device->bindless_manager, out, out_error);

    out->mtl_descriptor = as_desc;
    *out_tlas = (LRHITopLevelAccelerationStructure)out;
}

static void lrhi_metal3_destroy_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas)
{
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;
    LRHIDeviceMetal3* metal_device = metal_tlas->device;
    lrhi_metal3_bindless_manager_free_resource_view(&metal_device->bindless_manager, metal_tlas->bindless_index);
    [metal_device->internal_residency_set removeAllocation:metal_tlas->instance_buffer];
    [metal_device->internal_residency_set removeAllocation:metal_tlas->resource_id_buffer];
    [metal_device->internal_residency_set commit];
    LRHI_FREE(tlas);
}

static void lrhi_metal3_get_top_level_acceleration_structure_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info)
{
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;
    *out_info = metal_tlas->info;
}

static uint64_t lrhi_metal3_top_level_acceleration_structure_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;
    return metal_tlas->bindless_index;
}

static LRHIAccelerationStructureBufferSizes lrhi_metal3_top_level_acceleration_structure_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;

    LRHIAccelerationStructureBufferSizes out_sizes;
    out_sizes.build_scratch_size = metal_tlas->sizes.buildScratchBufferSize;
    out_sizes.update_scratch_size = metal_tlas->sizes.refitScratchBufferSize;
    return out_sizes;
}

static void lrhi_metal3_reset_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;
    metal_tlas->instance_count = 0;
    // Optionally, we could also clear the instance buffer here, but it's not strictly necessary since we'll overwrite it when adding instances
}

static void lrhi_metal3_add_top_level_acceleration_structure_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error)
{
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;

    if (metal_tlas->instance_count >= metal_tlas->info.max_instance_count) {
        if (out_error) {
            snprintf(out_error->message, sizeof(out_error->message), "Exceeded maximum instance count for TLAS");
            out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
        }
        return;
    }

    // Write instance data to the mapped instance buffer
    MTLIndirectAccelerationStructureInstanceDescriptor* instance_desc = &metal_tlas->mapped_instance_buffer[metal_tlas->instance_count];
    instance_desc->accelerationStructureID = ((LRHIBLASMetal3*)instance_info->blas)->acceleration_structure.gpuResourceID;
    instance_desc->options = instance_info->opaque ? MTLAccelerationStructureInstanceOptionOpaque :  MTLAccelerationStructureInstanceOptionNonOpaque;
    instance_desc->mask = 0xFF;
    instance_desc->userID = instance_info->user_id;
    memcpy(instance_desc->transformationMatrix.columns, instance_info->transform, sizeof(float) * 12);

    metal_tlas->instance_count++;
}

static void lrhi_metal3_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
    LRHIBLASMetal3* out = LRHI_MALLOC(sizeof(LRHIBLASMetal3));
    out->base.vtable           = &lrhi_metal3_blas_vtable;
    out->device                = metal_device;
    out->acceleration_structure = [metal_device->device newAccelerationStructureWithSize:compacted_size];
    out->mtl_descriptor        = nil;
    out->sizes                 = (MTLAccelerationStructureSizes){0};
    memset(&out->info, 0, sizeof(LRHIBLASInfo));
    *out_blas = (LRHIBottomLevelAccelerationStructure)out;
}

static void lrhi_metal3_acceleration_structure_pass_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;
    LRHIBufferMetal3* metal_dst_buffer = (LRHIBufferMetal3*)dst_buffer;
    [metal_as_pass->as_encoder writeCompactedAccelerationStructureSize:metal_blas->acceleration_structure
                                                              toBuffer:metal_dst_buffer->buffer
                                                                offset:dst_offset
                                                          sizeDataType:MTLDataTypeULong];
}

static void lrhi_metal3_acceleration_structure_pass_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHIBLASMetal3* metal_src = (LRHIBLASMetal3*)src_blas;
    LRHIBLASMetal3* metal_dst = (LRHIBLASMetal3*)dst_blas;
    [metal_as_pass->as_encoder copyAndCompactAccelerationStructure:metal_src->acceleration_structure
                                           toAccelerationStructure:metal_dst->acceleration_structure];
}

static void lrhi_metal3_acceleration_structure_pass_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHIBLASMetal3* metal_blas = (LRHIBLASMetal3*)blas;
    LRHIBufferMetal3* metal_scratch = (LRHIBufferMetal3*)scratch_buffer;
    [metal_as_pass->as_encoder refitAccelerationStructure:metal_blas->acceleration_structure
                                               descriptor:metal_blas->mtl_descriptor
                                              destination:nil
                                            scratchBuffer:metal_scratch->buffer
                                      scratchBufferOffset:scratch_offset];
}

static void lrhi_metal3_acceleration_structure_pass_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHITLASMetal3* metal_tlas = (LRHITLASMetal3*)tlas;
    LRHIBufferMetal3* metal_scratch = (LRHIBufferMetal3*)scratch_buffer;
    metal_tlas->mtl_descriptor.instanceDescriptorBuffer = metal_tlas->instance_buffer;
    metal_tlas->mtl_descriptor.instanceDescriptorType   = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
    metal_tlas->mtl_descriptor.instanceCount            = metal_tlas->instance_count;
    metal_tlas->mtl_descriptor.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    [metal_as_pass->as_encoder refitAccelerationStructure:metal_tlas->acceleration_structure
                                               descriptor:metal_tlas->mtl_descriptor
                                              destination:nil
                                            scratchBuffer:metal_scratch->buffer
                                      scratchBufferOffset:scratch_offset];
}

static void lrhi_metal3_acceleration_structure_pass_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHIBLASMetal3* metal_src = (LRHIBLASMetal3*)src_blas;
    LRHIBLASMetal3* metal_dst = (LRHIBLASMetal3*)dst_blas;
    [metal_as_pass->as_encoder copyAccelerationStructure:metal_src->acceleration_structure
                                 toAccelerationStructure:metal_dst->acceleration_structure];
}

static void lrhi_metal3_acceleration_structure_pass_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error)
{
    LRHIAccelerationStructurePassMetal3* metal_as_pass = (LRHIAccelerationStructurePassMetal3*)pass;
    LRHITLASMetal3* metal_src = (LRHITLASMetal3*)src_tlas;
    LRHITLASMetal3* metal_dst = (LRHITLASMetal3*)dst_tlas;
    [metal_as_pass->as_encoder copyAccelerationStructure:metal_src->acceleration_structure
                                 toAccelerationStructure:metal_dst->acceleration_structure];
    metal_dst->instance_count = metal_src->instance_count;
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

static MTLCullMode lrhi_metal3_cull_mode_to_mtl(LRHIPipelineCullMode cull_mode)
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

static MTLTriangleFillMode lrhi_metal3_fill_mode_to_mtl(LRHIPipelineFillMode fill_mode)
{
    if (fill_mode == LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID) {
        return MTLTriangleFillModeFill;
    } else if (fill_mode == LUMINARY_RHI_PIPELINE_FILL_MODE_WIREFRAME) {
        return MTLTriangleFillModeLines;
    }
    return MTLTriangleFillModeFill;
}

static MTLWinding lrhi_metal3_front_face_to_mtl(LRHIPipelineFrontFace front_face)
{
    if (front_face == LUMINARY_RHI_PIPELINE_FRONT_FACE_CLOCKWISE) {
        return MTLWindingClockwise;
    } else if (front_face == LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE) {
        return MTLWindingCounterClockwise;
    }
    return MTLWindingClockwise;
}

static MTLPrimitiveType lrhi_metal3_primitive_topology_to_mtl(LRHIPipelineTopology topology)
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

static MTLPrimitiveTopologyClass lrhi_metal3_primitive_topology_class_to_mtl(LRHIPipelineTopology topology)
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

static MTLBlendFactor lrhi_metal3_blend_factor_to_mtl(LRHIBlendFactor factor)
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

static MTLBlendOperation lrhi_metal3_blend_op_to_mtl(LRHIBlendOperation op)
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

static MTLCompareFunction lrhi_metal3_compare_op_to_mtl(LRHICompareOperation op)
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

static MTLSamplerAddressMode lrhi_metal3_address_mode_to_mtl(LRHISamplerAddressMode mode)
{
    switch (mode) {
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT: return MTLSamplerAddressModeRepeat;
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
        case LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToBorderColor;
        default: return MTLSamplerAddressModeRepeat;
    }
}

static MTLSamplerMinMagFilter lrhi_metal3_filter_to_mtl(LRHISamplerFilter filter)
{
    switch (filter) {
        case LUMINARY_RHI_SAMPLER_FILTER_NEAREST: return MTLSamplerMinMagFilterNearest;
        case LUMINARY_RHI_SAMPLER_FILTER_LINEAR: return MTLSamplerMinMagFilterLinear;
        default: return MTLSamplerMinMagFilterNearest;
    }
}

static MTLSamplerMipFilter lrhi_metal3_mip_filter_to_mtl(LRHISamplerFilter filter)
{
    switch (filter) {
        case LUMINARY_RHI_SAMPLER_FILTER_NEAREST: return MTLSamplerMipFilterNotMipmapped;
        case LUMINARY_RHI_SAMPLER_FILTER_LINEAR: return MTLSamplerMipFilterLinear;
        default: return MTLSamplerMipFilterNotMipmapped;
    }
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
    LRHIDeviceMetal3* metal_device = (LRHIDeviceMetal3*)device;
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
    layer.device          = metal_device->device;
    layer.pixelFormat     = lrhi_metal3_pixel_format(info->format);
    layer.drawableSize    = CGSizeMake(info->width, info->height);
    layer.framebufferOnly = NO;

    LRHISwapChainMetal3* sc = LRHI_MALLOC(sizeof(LRHISwapChainMetal3));
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
    LRHI_FREE(sc);
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
