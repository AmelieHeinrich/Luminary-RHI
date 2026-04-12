#ifndef LUMINARY_RHI_H
#define LUMINARY_RHI_H

#include <stdint.h>

#define LUMINARY_OPAQUE_TYPE(name) typedef struct name##_opaque* name

/// The selected RHI backend to use. This is used when creating a device, and determines which graphics API the device will use.
typedef enum LRHIBackend {
    LUMINARY_RHI_BACKEND_VULKAN,
    LUMINARY_RHI_BACKEND_D3D12,
    LUMINARY_RHI_BACKEND_METAL3,
    LUMINARY_RHI_BACKEND_METAL4,
    LUMINARY_RHI_BACKEND_SWITCH,
    LUMINARY_RHI_BACKEND_PLAYSTATION
} LRHIBackend;

typedef enum LRHIErrorSeverity {
    LUMINARY_RHI_ERROR_SEVERITY_SUCCESS,
    LUMINARY_RHI_ERROR_SEVERITY_WARNING,
    LUMINARY_RHI_ERROR_SEVERITY_ERROR
} LRHIErrorSeverity;

/// The type of handle used for swap chain creation. This is used when creating a swap chain, and determines how the swap chain will be created and what kind of windowing system it will use.
typedef enum LRHISwapChainHandleType {
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_HWND,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_X11,
    LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_WAYLAND
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
    LUMINARY_RHI_TEXTURE_DIMENSIONS_1D,
    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY,
    LUMINARY_RHI_TEXTURE_DIMENSIONS_3D,
    LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE,
} LRHITextureDimensions;

typedef enum LRHIBufferUsage {
    LUMINARY_RHI_BUFFER_USAGE_VERTEX = 1 << 0,
    LUMINARY_RHI_BUFFER_USAGE_INDEX = 1 << 1,
    LUMINARY_RHI_BUFFER_USAGE_CONSTANT = 1 << 2,
    LUMINARY_RHI_BUFFER_USAGE_SHADER_READ = 1 << 3,
    LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE = 1 << 4,
    LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS = 1 << 5,
} LRHIBufferUsage;

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
LUMINARY_OPAQUE_TYPE(LRHIGraphicsPipeline);
LUMINARY_OPAQUE_TYPE(LRHIComputePipeline);
LUMINARY_OPAQUE_TYPE(LRHIMeshPipeline);
LUMINARY_OPAQUE_TYPE(LRHIRenderPass);
LUMINARY_OPAQUE_TYPE(LRHIComputePass);
LUMINARY_OPAQUE_TYPE(LRHICopyPass);
LUMINARY_OPAQUE_TYPE(LRHIAccelerationStructurePass);
LUMINARY_OPAQUE_TYPE(LRHIShaderModule);

/// Structs
typedef struct LRHIError {
    char message[256];
    LRHIErrorSeverity severity;
} LRHIError;

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

// Only available on Metal thanks to UMA. On other platforms, create a staging buffer and use a copy command to upload texture data.
void lrhi_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
void lrhi_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);

// Buffer functions
void lrhi_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
void lrhi_destroy_buffer(LRHIBuffer buffer);
void lrhi_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
void* lrhi_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
void lrhi_buffer_unmap(LRHIBuffer buffer);

// Used by tests to force a texture readback operation without having to worry about synchronization or staging buffers. Not intended for general use.
void lrhi_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
void lrhi_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);

/*
    TODO:
        Command Queue:
            - create/destroy
            - submit command list
            - signal fence
            - wait for fence
        Fence:
            - create/destroy
            - get status
            - signal
            - wait
        Swap Chain:
            - create/destroy
            - get back buffer
            - present
        Sampler:
            - create/destroy
            - get info
        Graphics Pipeline:
            - create/destroy
            - get info
        Compute Pipeline:
            - create/destroy
            - get info
        Mesh Pipeline:
            - create/destroy
            - get info
        Render Pass:
            - create/destroy
            - get info
            - begin/end
            - set pipeline state
            - set vertex buffers
            - set index buffers
            - push constants
            - METAL ONLY: set buffers, textures
            - draw
            - draw indexed
            - execute indirect
            - draw mesh tasks
            - intrapass barriers
            - consumer barriers
        Compute Pass:
            - create/destroy
            - get info
            - begin/end
            - set pipeline state
            - push constants:
            - METAL ONLY: set buffers, textures
            - dispatch
            - dispatch indirect
        Shader Module:
            - create/destroy
            - get info
            - reflect shader resources (buffers, textures, samplers)
        Copy Pass:
            - create/destroy
            - get info
            - begin/end
            - copy buffer to buffer
            - copy buffer to texture
            - copy texture to buffer
            - copy texture to texture
        Acceleration structure:
            - create/destroy
            - get info
            - top level
            - bottom level
        Acceleration structure pass:
            - create/destroy
            - get info
            - begin/end
            - build (direct)
            - build (indirect), metal only
            - copy
            - compact
*/

#ifdef __cplusplus
} // extern "C"
#endif

#endif
