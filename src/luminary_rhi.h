#ifndef LUMINARY_RHI_H
#define LUMINARY_RHI_H

#include "luminary_rhi_common.h"

/// The type of handle used for swap chain creation. This is used when creating a swap chain, and determines how the swap chain will be created and what kind of windowing system it will use.
typedef enum LRHISwapChainHandleType {
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_HWND,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_X11,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_WAYLAND,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_XCB,
} LRHISwapChainHandleType;

typedef enum LRHITextureFormat {
    LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED,
    LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB,
    LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT,
    LUMINARY_RHI_TEXTURE_FORMAT_R32G32B32A32_FLOAT,
    LUMINARY_RHI_TEXTURE_FORMAT_D24_UNORM_S8_UINT,
    LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT,
    LUMINARY_RHI_TEXTURE_FORMAT_BC1_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_BC3_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_BC7_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_BC1_SRGB,
    LUMINARY_RHI_TEXTURE_FORMAT_BC3_SRGB,
    LUMINARY_RHI_TEXTURE_FORMAT_BC7_SRGB,
    LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_UNORM,
    LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_SRGB,
    LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_SRGB,
    LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_SRGB,
} LRHITextureFormat;

typedef enum LRHITextureUsage {
    LUMINARY_RHI_TEXTURE_USAGE_SAMPLED = 1 << 0,
    LUMINARY_RHI_TEXTURE_USAGE_STORAGE = 1 << 1,
    LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET = 1 << 2,
    LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL = 1 << 3,
} LRHITextureUsage;

typedef enum LRHITextureDimensions {
    LUMINARY_RHI_TEXTURE_DIMENSIONS_1D, // Texture1D
    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D, // Texture2D
    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY, // Texture2DArray
    LUMINARY_RHI_TEXTURE_DIMENSIONS_3D, // Texture3D
    LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE, // TextureCube
} LRHITextureDimensions;

typedef enum LRHIBufferUsage {
    LUMINARY_RHI_BUFFER_USAGE_VERTEX = 1 << 0,
    LUMINARY_RHI_BUFFER_USAGE_INDEX = 1 << 1,
    LUMINARY_RHI_BUFFER_USAGE_CONSTANT = 1 << 2,
    LUMINARY_RHI_BUFFER_USAGE_SHADER_READ = 1 << 3,
    LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE = 1 << 4,
    LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS = 1 << 5,
    LUMINARY_RHI_BUFFER_USAGE_STAGING = 1 << 6
} LRHIBufferUsage;

typedef enum LRHIRenderPassAction {
    LUMINARY_RHI_RENDER_PASS_ACTION_LOAD,
    LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR,
    LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE
} LRHIRenderPassAction;

typedef enum LRHIRenderStage {
    LUMINARY_RHI_RENDER_STAGE_VERTEX = 1 << 0,
    LUMINARY_RHI_RENDER_STAGE_FRAGMENT = 1 << 1,
    LUMINARY_RHI_RENDER_STAGE_COMPUTE = 1 << 2,
    LUMINARY_RHI_RENDER_STAGE_MESH = 1 << 3,
    LUMINARY_RHI_RENDER_STAGE_TASK = 1 << 4,
    LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD = 1 << 5,
    LUMINARY_RHI_RENDER_STAGE_COPY = 1 << 6,
} LRHIRenderStage;

typedef enum LRHIPipelineTopology {
    LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
    LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST,
    LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST
} LRHIPipelineTopology;

typedef enum LRHIPipelineFillMode {
    LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
    LUMINARY_RHI_PIPELINE_FILL_MODE_WIREFRAME
} LRHIPipelineFillMode;

typedef enum LRHIPipelineCullMode {
    LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
    LUMINARY_RHI_PIPELINE_CULL_MODE_FRONT,
    LUMINARY_RHI_PIPELINE_CULL_MODE_BACK
} LRHIPipelineCullMode;

typedef enum LRHIPipelineFrontFace {
    LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
    LUMINARY_RHI_PIPELINE_FRONT_FACE_CLOCKWISE
} LRHIPipelineFrontFace;

typedef enum LRHICompareOperation {
    LUMINARY_RHI_COMPARE_OPERATION_NEVER,
    LUMINARY_RHI_COMPARE_OPERATION_LESS,
    LUMINARY_RHI_COMPARE_OPERATION_EQUAL,
    LUMINARY_RHI_COMPARE_OPERATION_LESS_EQUAL,
    LUMINARY_RHI_COMPARE_OPERATION_GREATER,
    LUMINARY_RHI_COMPARE_OPERATION_NOT_EQUAL,
    LUMINARY_RHI_COMPARE_OPERATION_GREATER_EQUAL,
    LUMINARY_RHI_COMPARE_OPERATION_ALWAYS
} LRHICompareOperation;

typedef enum LRHIBlendFactor {
    LUMINARY_RHI_BLEND_FACTOR_ZERO,
    LUMINARY_RHI_BLEND_FACTOR_ONE,
    LUMINARY_RHI_BLEND_FACTOR_SRC_COLOR,
    LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    LUMINARY_RHI_BLEND_FACTOR_DST_COLOR,
    LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    LUMINARY_RHI_BLEND_FACTOR_SRC_ALPHA,
    LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    LUMINARY_RHI_BLEND_FACTOR_DST_ALPHA,
    LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    LUMINARY_RHI_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    LUMINARY_RHI_BLEND_FACTOR_BLEND_COLOR
} LRHIBlendFactor;

typedef enum LRHIBlendOperation {
    LUMINARY_RHI_BLEND_OPERATION_ADD,
    LUMINARY_RHI_BLEND_OPERATION_SUBTRACT,
    LUMINARY_RHI_BLEND_OPERATION_REVERSE_SUBTRACT,
    LUMINARY_RHI_BLEND_OPERATION_MIN,
    LUMINARY_RHI_BLEND_OPERATION_MAX
} LRHIBlendOperation;

typedef enum LRHIBufferViewType {
    LUMINARY_RHI_BUFFER_VIEW_TYPE_CONSTANT, // ConstantBuffer<T>
    LUMINARY_RHI_BUFFER_VIEW_TYPE_STRUCTURED, // StructuredBuffer<T>
    LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_STRUCTURED, // RWStructuredBuffer<T>
    LUMINARY_RHI_BUFFER_VIEW_TYPE_RAW, // ByteAddressBuffer
    LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW // RWByteAddressBuffer
} LRHIBufferViewType;

typedef enum LRHISamplerFilter {
    LUMINARY_RHI_SAMPLER_FILTER_NEAREST,
    LUMINARY_RHI_SAMPLER_FILTER_LINEAR,
    LUMINARY_RHI_SAMPLER_FILTER_ANISOTROPIC
} LRHISamplerFilter;

typedef enum LRHISamplerAddressMode {
    LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT,
    LUMINARY_RHI_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
} LRHISamplerAddressMode;

#define LUMINARY_TEXTURE_VIEW_ALL_MIPS 0xFFFFFFFF
#define LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS 0xFFFFFFFF

// Types
LUMINARY_OPAQUE_TYPE(LRHIDevice);
LUMINARY_OPAQUE_TYPE(LRHICommandQueue);
LUMINARY_OPAQUE_TYPE(LRHICommandList);
LUMINARY_OPAQUE_TYPE(LRHISwapChain);
LUMINARY_OPAQUE_TYPE(LRHIFence);
LUMINARY_OPAQUE_TYPE(LRHIBuffer);
LUMINARY_OPAQUE_TYPE(LRHITexture);
LUMINARY_OPAQUE_TYPE(LRHISampler);
LUMINARY_OPAQUE_TYPE(LRHIAccelerationStructure);
LUMINARY_OPAQUE_TYPE(LRHIRenderPipeline);
LUMINARY_OPAQUE_TYPE(LRHIComputePipeline);
LUMINARY_OPAQUE_TYPE(LRHIMeshPipeline);
LUMINARY_OPAQUE_TYPE(LRHIRenderPass);
LUMINARY_OPAQUE_TYPE(LRHIComputePass);
LUMINARY_OPAQUE_TYPE(LRHICopyPass);
LUMINARY_OPAQUE_TYPE(LRHIAccelerationStructurePass);
LUMINARY_OPAQUE_TYPE(LRHIShaderModule);
LUMINARY_OPAQUE_TYPE(LRHIResidencySet);
LUMINARY_OPAQUE_TYPE(LRHITextureView);
LUMINARY_OPAQUE_TYPE(LRHIBufferView);

// Structs

typedef struct LRHIDeviceFeatures {
    uint8_t ray_tracing : 1;
    uint8_t mesh_shading : 1;
    uint8_t bindless_resources : 1; // requires mutable descriptor for vulkan
    uint8_t multi_draw_indirect : 1;
} LRHIDeviceFeatures;

typedef struct LRHIDeviceLimits {
    uint32_t max_texture_dimension_2d;
    uint32_t max_texture_dimension_3d;
    uint32_t max_texture_array_layers;
    uint32_t max_buffer_size;
} LRHIDeviceLimits;

typedef struct LRHIDeviceInfo {
    LRHIBackend backend;
    LRHIDeviceFeatures features;
    LRHIDeviceLimits limits;
    char device_name[256];
} LRHIDeviceInfo;

typedef struct LRHITextureInfo {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mip_levels;
    uint32_t array_layers;
    LRHITextureFormat format;
    LRHITextureUsage usage;
    LRHITextureDimensions dimensions;
} LRHITextureInfo;

typedef struct LRHIRegion {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} LRHIRegion;

typedef struct LRHIBufferInfo {
    uint64_t size;
    uint64_t stride;
    LRHIBufferUsage usage;
} LRHIBufferInfo;

typedef struct LRHISwapChainInfo {
    LRHISwapChainHandleType handle_type;
    union {
        void* metal_layer;      // CAMetalLayer* cast to void*
        void* hwnd;             // HWND on Windows
        void* x11_window;       // X11 Window cast to void*
        void* xcb_window;       // xcb_window_t cast to void*
        void* wayland_surface;  // wl_surface* cast to void*
    } handle;
    uint32_t          width;
    uint32_t          height;
    LRHITextureFormat format;               // typically LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM
    uint8_t           max_frames_in_flight; // Metal4: controls frame-in-flight depth; Metal3: ignored
} LRHISwapChainInfo;

typedef struct LRHITextureViewInfo {
    LRHITexture texture;

    uint32_t base_mip_level;
    uint32_t mip_level_count;
    uint32_t base_array_layer;
    uint32_t array_layer_count;

    LRHITextureFormat format; // if the view should reinterpret the texture's format, otherwise LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED to use the texture's format
    LRHITextureUsage usage;
    LRHITextureDimensions dimensions;
} LRHITextureViewInfo;

typedef struct LRHIRenderPassAttachmentInfo {
    LRHITextureView texture_view;
    LRHIRenderPassAction load_action;
    LRHIRenderPassAction store_action;
    float clear_color[4]; // only used if load_action is LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR and the attachment is a color texture
    float clear_depth;    // only used if load_action is LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR and the attachment is a depth texture
    uint8_t clear_stencil; // only used if load_action is LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR and the attachment is a stencil texture
} LRHIRenderPassAttachmentInfo;

typedef struct LRHIRenderPassInfo {
    LRHIRenderPassAttachmentInfo color_attachments[8];
    uint32_t color_attachment_count;

    LRHIRenderPassAttachmentInfo depth_stencil_attachment;
    uint8_t has_depth_stencil_attachment;

    uint32_t render_area_x;
    uint32_t render_area_y;
    uint32_t render_width;
    uint32_t render_height;
} LRHIRenderPassInfo;

typedef struct LRHIShaderModuleInfo {
    LRHIShaderStage stage;
    const char* entry_point;

    const uint32_t* code;
    uint32_t code_size;
} LRHIShaderModuleInfo;

typedef struct LRHIRenderPipelineInfo {
    // General info
    uint8_t supports_indirect_commands;

    // Rasterizer
    LRHIPipelineFillMode fill_mode;
    LRHIPipelineCullMode cull_mode;
    LRHIPipelineFrontFace front_face;
    LRHIPipelineTopology topology;

    // Depth-stencil
    uint8_t depth_test_enable;
    uint8_t depth_write_enable;
    uint8_t depth_clamp_enable;
    LRHICompareOperation depth_compare_op;
    uint8_t stencil_test_enable;
    uint8_t stencil_write_enable;
    LRHICompareOperation stencil_compare_op;
    LRHITextureFormat depth_stencil_format;

    // Blending and color output
    uint8_t blend_enable[8];
    LRHIBlendFactor blend_src_rgb_factor[8];
    LRHIBlendFactor blend_dst_rgb_factor[8];
    LRHIBlendOperation blend_rgb_op[8];
    LRHIBlendFactor blend_src_alpha_factor[8];
    LRHIBlendFactor blend_dst_alpha_factor[8];
    LRHIBlendOperation blend_alpha_op[8];
    LRHITextureFormat render_target_formats[8];
    uint32_t render_target_count;

    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;
    LRHIShaderModule mesh_shader;
    LRHIShaderModule task_shader;
} LRHIRenderPipelineInfo;

typedef struct LRHIRenderPipelineInfo LRHIMeshPipelineInfo;

typedef struct LRHIComputePipelineInfo {
    LRHIShaderModule compute_shader;
    uint8_t supports_indirect_commands;
} LRHIComputePipelineInfo;

typedef struct LRHIBufferViewInfo {
    LRHIBuffer buffer;
    uint64_t offset;
    
    LRHIBufferViewType view_type;
} LRHIBufferViewInfo;

typedef struct LRHISamplerInfo {
    LRHISamplerFilter min_filter;
    LRHISamplerFilter mag_filter;
    LRHISamplerFilter mipmap_filter;
    LRHISamplerAddressMode address_mode_u;
    LRHISamplerAddressMode address_mode_v;
    LRHISamplerAddressMode address_mode_w;
    float mip_lod_bias;
    uint8_t anisotropy_enable;
    uint8_t compare_enable;
    LRHICompareOperation compare_op;
    float min_lod;
    float max_lod;
} LRHISamplerInfo;

#ifdef __cplusplus
extern "C" {
#endif

/// Device functions
void lrhi_create_device(LRHIBackend backend, LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error);
void lrhi_destroy_device(LRHIDevice device);
LRHIDeviceInfo lrhi_get_device_info(LRHIDevice device);

/// Texture functions
void lrhi_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
void lrhi_destroy_texture(LRHITexture texture);
void lrhi_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
void lrhi_texture_set_name(LRHITexture texture, const char* name);

// Only available on Metal thanks to UMA. On other platforms, create a staging buffer and use a copy command to upload texture data.
void lrhi_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
void lrhi_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);

// Buffer functions
void lrhi_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
void lrhi_destroy_buffer(LRHIBuffer buffer);
void lrhi_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
void* lrhi_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
void lrhi_buffer_unmap(LRHIBuffer buffer);
void lrhi_buffer_set_name(LRHIBuffer buffer, const char* name);

// Used by tests to force a texture readback operation without having to worry about synchronization or staging buffers. Not intended for general use.
void lrhi_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
void lrhi_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);

// Command Queue functions
void lrhi_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
void lrhi_destroy_command_queue(LRHICommandQueue queue);
void lrhi_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
void lrhi_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
void lrhi_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error);
void lrhi_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error);

// Fence functions
void lrhi_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
void lrhi_destroy_fence(LRHIFence fence);
uint64_t lrhi_fence_get_value(LRHIFence fence);
void lrhi_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);
void lrhi_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

// Command list functions
void lrhi_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error);
void lrhi_destroy_command_list(LRHICommandList command_list);
void lrhi_command_list_begin(LRHICommandList command_list, LRHIError* out_error);
void lrhi_command_list_end(LRHICommandList command_list, LRHIError* out_error);
void lrhi_command_list_reset(LRHICommandList command_list, LRHIError* out_error);

// Copy pass functions
LRHICopyPass lrhi_copy_pass_begin(LRHICommandList command_list, LRHIError* out_error);
void lrhi_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error);
void lrhi_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error); // Blit-Blit barrier
void lrhi_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error); // afterStage-Blit barrier
void lrhi_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error);
void lrhi_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
void lrhi_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error);
void lrhi_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);

// Residency set functions
void lrhi_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error);
void lrhi_destroy_residency_set(LRHIResidencySet residency_set);
void lrhi_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
void lrhi_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
void lrhi_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
void lrhi_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
void lrhi_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error);

// SwapChain functions
// NOTE: the texture returned by lrhi_swap_chain_get_current_texture is borrowed — valid until the next lrhi_swap_chain_present on the same swapchain.
void lrhi_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error);
void lrhi_destroy_swap_chain(LRHISwapChain swap_chain);
LRHITexture lrhi_swap_chain_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error);
void lrhi_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error);

// Texture view functions
void lrhi_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error);
void lrhi_destroy_texture_view(LRHITextureView texture_view);
void lrhi_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info);
uint32_t lrhi_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error);

// Shader module
void lrhi_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error);
void lrhi_destroy_shader_module(LRHIShaderModule shader_module);
void lrhi_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info);

// Render pipeline functions
void lrhi_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error);
void lrhi_destroy_render_pipeline(LRHIRenderPipeline pipeline);
void lrhi_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info);
uint64_t lrhi_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error);

// Mesh pipeline functions
void lrhi_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error);
void lrhi_destroy_mesh_pipeline(LRHIMeshPipeline pipeline);
void lrhi_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info);
uint64_t lrhi_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error);

// Compute pipeline functions
void lrhi_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error);
void lrhi_destroy_compute_pipeline(LRHIComputePipeline pipeline);
void lrhi_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info);
uint64_t lrhi_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error);

// Render pass functions
LRHIRenderPass lrhi_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error);
void lrhi_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error);
void lrhi_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage before_stage, LRHIRenderStage after_stage, LRHIError* out_error);
void lrhi_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage before_stage, LRHIRenderStage after_stage, LRHIError* out_error);
void lrhi_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error);
void lrhi_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error);
void lrhi_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error);
void lrhi_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error);
void lrhi_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error);
void lrhi_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error);
void lrhi_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error);
void lrhi_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error);

// Compute pass functions
LRHIComputePass lrhi_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error);
void lrhi_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error);
void lrhi_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error);
void lrhi_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error);
void lrhi_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error);
void lrhi_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error);
void lrhi_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error);

// Buffer view
void lrhi_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error);
void lrhi_destroy_buffer_view(LRHIBufferView buffer_view);
void lrhi_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info);
uint32_t lrhi_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error);

// Sampler
void lrhi_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error);
void lrhi_destroy_sampler(LRHISampler sampler);
void lrhi_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info);
uint32_t lrhi_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error);

/*
    TODO:
        Render Pass:
            - execute indirect
        Compute Pass:
            - dispatch indirect
        Acceleration structure:
            - create/destroy
            - get info
            - top level
            - bottom level
            - get bindless index
        Acceleration structure pass:
            - create/destroy
            - get info
            - begin/end
            - build (direct)
            - copy
            - compact

        API SPECIFIC:
            - Vulkan: do the entire backend lmao
            - D3D12: same
            - Switch & Playstation: get them devkits >.>
*/

#ifdef __cplusplus
} // extern "C"
#endif

#endif
