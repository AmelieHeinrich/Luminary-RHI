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
} LRHIDeviceVTable;

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

// Base structs — must be the first member of every backend device/texture/buffer struct.
typedef struct LRHIDeviceBase  { const LRHIDeviceVTable*  vtable; } LRHIDeviceBase;
typedef struct LRHITextureBase { const LRHITextureVTable* vtable; } LRHITextureBase;
typedef struct LRHIBufferBase  { const LRHIBufferVTable*  vtable; } LRHIBufferBase;

#ifdef LRHI_MACOS
void lrhi_metal3_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error);
void lrhi_metal4_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error);
#endif

#endif // LUMINARY_RHI_INTERNAL_H
