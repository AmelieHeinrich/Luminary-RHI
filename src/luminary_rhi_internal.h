#ifndef LUMINARY_RHI_INTERNAL_H
#define LUMINARY_RHI_INTERNAL_H

#include "luminary_rhi.h"

typedef struct LRHIDeviceVTable {
    void           (*destroy_device)(LRHIDevice device);
    LRHIDeviceInfo (*get_device_info)(LRHIDevice device);
    void           (*create_texture)(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
    void           (*create_buffer)(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
    void           (*texture_readback)(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
    void           (*buffer_readback)(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);
    void           (*create_command_queue)(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
    void           (*create_fence)(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
    void           (*create_residency_set)(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error);
    void           (*create_swap_chain)(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error);
    void           (*create_texture_view)(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error);
} LRHIDeviceVTable;

typedef struct LRHICommandQueueVTable {
    void (*create_command_list)(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error);
    void (*destroy_command_queue)(LRHICommandQueue queue);
    void (*signal_fence)(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
    void (*wait_fence)(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
    void (*submit_command_lists)(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error);
    void (*add_residency_set)(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error);
} LRHICommandQueueVTable;

typedef struct LRHIFenceVTable {
    void     (*destroy_fence)(LRHIFence fence);
    uint64_t (*get_value)(LRHIFence fence);
    void     (*signal)(LRHIFence fence, uint64_t value, LRHIError* out_error);
    void     (*wait)(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
} LRHIFenceVTable;

typedef struct LRHITextureVTable {
    void (*destroy_texture)(LRHITexture texture);
    void (*get_texture_info)(LRHITexture texture, LRHITextureInfo* out_info);
    void (*texture_replace_region)(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
    void (*texture_read_region)(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
} LRHITextureVTable;

typedef struct LRHIBufferVTable {
    void  (*destroy_buffer)(LRHIBuffer buffer);
    void  (*get_buffer_info)(LRHIBuffer buffer, LRHIBufferInfo* out_info);
    void* (*buffer_map)(LRHIBuffer buffer, LRHIError* out_error);
    void  (*buffer_unmap)(LRHIBuffer buffer);
} LRHIBufferVTable;

typedef struct LRHICommandListVTable {
    void (*destroy_command_list)(LRHICommandList command_list);
    void (*command_list_begin)(LRHICommandList command_list, LRHIError* out_error);
    void (*command_list_end)(LRHICommandList command_list, LRHIError* out_error);
    void (*command_list_reset)(LRHICommandList command_list, LRHIError* out_error);
    LRHICopyPass (*copy_pass_begin)(LRHICommandList command_list, LRHIError* out_error);
    LRHIRenderPass (*render_pass_begin)(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error);
} LRHICommandListVTable;

typedef struct LRHICopyPassVTable {
    void (*copy_pass_end)(LRHICopyPass copy_pass, LRHIError* out_error);
    void (*copy_pass_intra_barrier)(LRHICopyPass copy_pass, LRHIError* out_error);
    void (*copy_pass_encoder_barrier)(LRHICopyPass copy_pass, LRHIRenderStage afterStage, LRHIError* out_error);
    void (*copy_buffer_to_buffer)(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error);
    void (*copy_buffer_to_texture)(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
    void (*copy_texture_to_buffer)(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error);
    void (*copy_texture_to_texture)(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
} LRHICopyPassVTable;

typedef struct LRHIResidencySetVTable {
    void (*destroy_residency_set)(LRHIResidencySet residency_set);
    void (*add_texture)(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
    void (*add_buffer)(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
    void (*remove_texture)(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
    void (*remove_buffer)(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
    void (*update)(LRHIResidencySet residency_set, LRHIError* out_error);
} LRHIResidencySetVTable;

typedef struct LRHISwapChainVTable {
    void        (*destroy_swap_chain)(LRHISwapChain swap_chain);
    LRHITexture (*get_current_texture)(LRHISwapChain swap_chain, LRHIError* out_error);
    void        (*present)(LRHISwapChain swap_chain, LRHIError* out_error);
} LRHISwapChainVTable;

typedef struct LRHITextureViewVTable {
    void (*destroy_texture_view)(LRHITextureView texture_view);
    void (*get_texture_view_info)(LRHITextureView texture_view, LRHITextureViewInfo* out_info);
    uint32_t (*get_bindless_index)(LRHITextureView texture_view, LRHIError* out_error);
} LRHITextureViewVTable;

typedef struct LRHIRenderPassVTable {
    void (*end)(LRHIRenderPass render_pass, LRHIError* out_error);
    void (*intra_barrier)(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
    void (*encoder_barrier)(LRHIRenderPass render_pass, LRHIRenderStage beforeStage, LRHIRenderStage afterStage, LRHIError* out_error);
} LRHIRenderPassVTable;

// Base structs — must be the first member of every backend struct.
typedef struct LRHIDeviceBase       { const LRHIDeviceVTable*         vtable; } LRHIDeviceBase;
typedef struct LRHICommandQueueBase { const LRHICommandQueueVTable*   vtable; } LRHICommandQueueBase;
typedef struct LRHIFenceBase        { const LRHIFenceVTable*          vtable; } LRHIFenceBase;
typedef struct LRHITextureBase      { const LRHITextureVTable*        vtable; } LRHITextureBase;
typedef struct LRHIBufferBase       { const LRHIBufferVTable*         vtable; } LRHIBufferBase;
typedef struct LRHICommandListBase  { const LRHICommandListVTable*    vtable; } LRHICommandListBase;
typedef struct LRHICopyPassBase     { const LRHICopyPassVTable*       vtable; } LRHICopyPassBase;
typedef struct LRHIResidencySetBase { const LRHIResidencySetVTable*   vtable; } LRHIResidencySetBase;
typedef struct LRHISwapChainBase    { const LRHISwapChainVTable*      vtable; } LRHISwapChainBase;
typedef struct LRHITextureViewBase  { const LRHITextureViewVTable*    vtable; } LRHITextureViewBase;
typedef struct LRHIRenderPassBase   { const LRHIRenderPassVTable*     vtable; } LRHIRenderPassBase;

#ifdef LRHI_MACOS
void lrhi_metal3_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error);
void lrhi_metal4_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error);
#endif

#endif // LUMINARY_RHI_INTERNAL_H
