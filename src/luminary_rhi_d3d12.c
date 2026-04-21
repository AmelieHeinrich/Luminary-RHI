#include "luminary_rhi.h"
#include "luminary_rhi_internal.h"

#include <stdio.h>
#include <string.h>

#include <Windows.h>
#include "ext/d3d12.h"
#include <dxgi1_6.h>

#define MAX_RESOURCE_HEAP_SIZE 1'000'000
#define MAX_SAMPLER_HEAP_SIZE 2048
#define MAX_RTV_HEAP_SIZE 2048
#define MAX_DSV_HEAP_SIZE 2048

__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) extern const uint32_t D3D12SDKVersion = 614;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\.";

typedef struct LRHIBindlessManagerD3D12 {
	ID3D12DescriptorHeap* cbv_srv_uav_heap;
	ID3D12DescriptorHeap* sampler_heap;
	ID3D12DescriptorHeap* rtv_heap;
	ID3D12DescriptorHeap* dsv_heap;

	uint32_t cbv_srv_uav_descriptor_size;
	uint32_t sampler_descriptor_size;
	uint32_t rtv_descriptor_size;
	uint32_t dsv_descriptor_size;

	LRHIFreeList cbv_srv_uav_free_list;
	LRHIFreeList sampler_free_list;
	LRHIFreeList rtv_free_list;
	LRHIFreeList dsv_free_list;
} LRHIBindlessManagerD3D12;

typedef struct LRHIDescriptorD3D12 {
	uint32_t descriptor_index;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
	D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
} LRHIDescriptorD3D12;

typedef struct LRHIDeviceD3D12 {
	LRHIDeviceBase base;
	LRHIDeviceInfo info;
	uint8_t enable_debug;

	ID3D12Device* device;
	IDXGIFactory1* factory;
    IDXGIAdapter1* adapter;
    ID3D12Debug1* debug_controller;

	LRHIBindlessManagerD3D12 bindless_manager;

    ID3D12RootSignature* global_root_signature;

    ID3D12CommandSignature* draw_indirect_command_signature;
    ID3D12CommandSignature* draw_indexed_indirect_command_signature;
    ID3D12CommandSignature* dispatch_indirect_command_signature;
    ID3D12CommandSignature* mesh_draw_indirect_command_signature;
} LRHIDeviceD3D12;

typedef struct LRHITextureD3D12 {
	LRHITextureBase base;
	LRHITextureInfo info;

    LRHIDeviceD3D12* device_ref;
    ID3D12Resource* resource;
} LRHITextureD3D12;

typedef struct LRHIBufferD3D12 {
	LRHIBufferBase base;
	LRHIBufferInfo info;

    LRHIDeviceD3D12* device_ref;
    LRHICommandType indirect_command_type;
    ID3D12Resource* resource;
} LRHIBufferD3D12;

typedef struct LRHICommandQueueD3D12 {
	LRHICommandQueueBase base;

    LRHIDeviceD3D12* device_ref;
    ID3D12CommandQueue* queue;
} LRHICommandQueueD3D12;

typedef struct LRHIFenceD3D12 {
	LRHIFenceBase base;

    ID3D12Fence* fence;
    HANDLE event;
} LRHIFenceD3D12;

typedef struct LRHICommandListD3D12 {
	LRHICommandListBase base;

	LRHIDeviceD3D12* device_ref;
    ID3D12GraphicsCommandList10* command_list;
    ID3D12CommandAllocator* command_allocator;
    uint8_t push_constants[128]; // Stored for execute indirect
} LRHICommandListD3D12;

typedef struct LRHICopyPassD3D12 {
	LRHICopyPassBase base;

    ID3D12GraphicsCommandList10* command_list; // Just a reference to parent cmd list
} LRHICopyPassD3D12;

typedef struct LRHIResidencySetD3D12 {
	LRHIResidencySetBase base;
} LRHIResidencySetD3D12;

typedef struct LRHISwapChainD3D12 {
	LRHISwapChainBase base;
	LRHISwapChainInfo info;

    IDXGISwapChain4* swap_chain;
} LRHISwapChainD3D12;

typedef struct LRHITextureViewD3D12 {
	LRHITextureViewBase base;
	LRHITextureViewInfo info;

	LRHIDeviceD3D12* device_ref;
	LRHIDescriptorD3D12 descriptor;
} LRHITextureViewD3D12;

typedef struct LRHIRenderPassD3D12 {
	LRHIRenderPassBase base;

    ID3D12GraphicsCommandList10* command_list; // Just a reference to parent cmd list
    uint8_t push_constants[128];
} LRHIRenderPassD3D12;

typedef struct LRHIShaderModuleD3D12 {
	LRHIShaderModuleBase base;
	LRHIShaderModuleInfo info;
} LRHIShaderModuleD3D12;

typedef struct LRHIRenderPipelineD3D12 {
	LRHIRenderPipelineBase base;
	LRHIRenderPipelineInfo info;

    ID3D12PipelineState* pipeline_state;
} LRHIRenderPipelineD3D12;

typedef struct LRHIMeshPipelineD3D12 {
	LRHIMeshPipelineBase base;
	LRHIMeshPipelineInfo info;

    ID3D12PipelineState* pipeline_state;
} LRHIMeshPipelineD3D12;

typedef struct LRHIComputePipelineD3D12 {
	LRHIComputePipelineBase base;
	LRHIComputePipelineInfo info;
    
    ID3D12PipelineState* pipeline_state;
} LRHIComputePipelineD3D12;

typedef struct LRHIComputePassD3D12 {
	LRHIComputePassBase base;

    ID3D12GraphicsCommandList10* command_list; // Just a reference to parent cmd list
    uint8_t push_constants[128];
} LRHIComputePassD3D12;

typedef struct LRHIBufferViewD3D12 {
	LRHIBufferViewBase base;
	LRHIBufferViewInfo info;

	LRHIDeviceD3D12* device_ref;
	LRHIDescriptorD3D12 descriptor;
} LRHIBufferViewD3D12;

typedef struct LRHISamplerD3D12 {
	LRHISamplerBase base;
	LRHISamplerInfo info;
} LRHISamplerD3D12;

typedef struct LRHIAccelerationStructurePassD3D12 {
	LRHIAccelerationStructurePassBase base;

    ID3D12CommandList* command_list; // Just a reference to parent cmd list
} LRHIAccelerationStructurePassD3D12;

typedef struct LRHIBLASD3D12 {
	LRHIBLASBase base;
	LRHIBLASInfo info;

    // TODO
} LRHIBLASD3D12;

typedef struct LRHITLASD3D12 {
	LRHITLASBase base;
	LRHITLASInfo info;

    // TODO
} LRHITLASD3D12;

// Bindless manager
static void lrhi_d3d12_bindless_manager_init(LRHIDeviceD3D12* device, LRHIError* out_error)
{
	D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc;
	memset(&cbv_srv_uav_heap_desc, 0, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));
	cbv_srv_uav_heap_desc.NumDescriptors = MAX_RESOURCE_HEAP_SIZE;
	cbv_srv_uav_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbv_srv_uav_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc;
	memset(&sampler_heap_desc, 0, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));
	sampler_heap_desc.NumDescriptors = MAX_SAMPLER_HEAP_SIZE;
	sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
	memset(&rtv_heap_desc, 0, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));
	rtv_heap_desc.NumDescriptors = MAX_RTV_HEAP_SIZE;
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
	memset(&dsv_heap_desc, 0, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));
	dsv_heap_desc.NumDescriptors = MAX_DSV_HEAP_SIZE;
	dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr;
	hr = device->device->lpVtbl->CreateDescriptorHeap(device->device, &cbv_srv_uav_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&device->bindless_manager.cbv_srv_uav_heap);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create resource heap!")) {
		return;
	}
	hr = device->device->lpVtbl->CreateDescriptorHeap(device->device, &sampler_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&device->bindless_manager.sampler_heap);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create sampler heap!")) {
		return;
	}
	hr = device->device->lpVtbl->CreateDescriptorHeap(device->device, &rtv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&device->bindless_manager.rtv_heap);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create RTV heap!")) {
		return;
	}
	hr = device->device->lpVtbl->CreateDescriptorHeap(device->device, &dsv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&device->bindless_manager.dsv_heap);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create DSV heap!")) {
		return;
	}
}

static void lrhi_d3d12_bindless_manager_destroy(LRHIDeviceD3D12* device)
{
	ID3D12DescriptorHeap_Release(device->bindless_manager.cbv_srv_uav_heap);
	ID3D12DescriptorHeap_Release(device->bindless_manager.sampler_heap);
	ID3D12DescriptorHeap_Release(device->bindless_manager.rtv_heap);
	ID3D12DescriptorHeap_Release(device->bindless_manager.dsv_heap);
}

// Enum translation
static DXGI_FORMAT lrhi_format_to_dxgi_format(LRHITextureFormat format);
static D3D12_RESOURCE_DIMENSION lrhi_texture_dimension_to_d3d12_resource_dimension(LRHITextureDimensions dimensions);
static D3D12_BARRIER_SYNC lrhi_pipeline_stage_to_d3d12_barrier_sync(LRHIRenderStage stage);
static D3D12_BARRIER_ACCESS lrhi_pipeline_usage_to_d3d12_barrier_access(LRHIRenderStage usage);

// Forward declarations
static void lrhi_d3d12_destroy_device(LRHIDevice device);
static LRHIDeviceInfo lrhi_d3d12_get_device_info(LRHIDevice device);
static void lrhi_d3d12_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error);
static void lrhi_d3d12_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error);
static void lrhi_d3d12_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void lrhi_d3d12_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error);
static void lrhi_d3d12_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error);
static void lrhi_d3d12_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error);
static void lrhi_d3d12_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error);
static void lrhi_d3d12_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error);
static void lrhi_d3d12_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error);
static void lrhi_d3d12_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error);
static void lrhi_d3d12_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error);
static void lrhi_d3d12_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error);
static void lrhi_d3d12_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error);
static void lrhi_d3d12_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error);
static void lrhi_d3d12_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error);
static void lrhi_d3d12_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error);
static void lrhi_d3d12_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error);
static void lrhi_d3d12_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error);

static void lrhi_d3d12_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error);
static void lrhi_d3d12_destroy_command_queue(LRHICommandQueue queue);
static void lrhi_d3d12_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error);
static void lrhi_d3d12_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);
static void lrhi_d3d12_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error);
static void lrhi_d3d12_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error);

static void lrhi_d3d12_destroy_fence(LRHIFence fence);
static uint64_t lrhi_d3d12_fence_get_value(LRHIFence fence);
static void lrhi_d3d12_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error);
static void lrhi_d3d12_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error);

static void lrhi_d3d12_destroy_texture(LRHITexture texture);
static void lrhi_d3d12_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info);
static void lrhi_d3d12_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void lrhi_d3d12_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error);
static void lrhi_d3d12_texture_set_name(LRHITexture texture, const char* name);

static void lrhi_d3d12_destroy_buffer(LRHIBuffer buffer);
static void lrhi_d3d12_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info);
static void* lrhi_d3d12_buffer_map(LRHIBuffer buffer, LRHIError* out_error);
static void lrhi_d3d12_buffer_unmap(LRHIBuffer buffer);
static void lrhi_d3d12_buffer_set_name(LRHIBuffer buffer, const char* name);
static void lrhi_d3d12_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error);

static void lrhi_d3d12_destroy_command_list(LRHICommandList command_list);
static void lrhi_d3d12_command_list_begin(LRHICommandList command_list, LRHIError* out_error);
static void lrhi_d3d12_command_list_end(LRHICommandList command_list, LRHIError* out_error);
static void lrhi_d3d12_command_list_reset(LRHICommandList command_list, LRHIError* out_error);
static void lrhi_d3d12_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error);
static LRHICopyPass lrhi_d3d12_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error);
static LRHIRenderPass lrhi_d3d12_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error);
static LRHIComputePass lrhi_d3d12_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error);
static LRHIAccelerationStructurePass lrhi_d3d12_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error);

static void lrhi_d3d12_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error);
static void lrhi_d3d12_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error);

static void lrhi_d3d12_destroy_residency_set(LRHIResidencySet residency_set);
static void lrhi_d3d12_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void lrhi_d3d12_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void lrhi_d3d12_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);
static void lrhi_d3d12_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void lrhi_d3d12_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error);
static void lrhi_d3d12_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error);
static void lrhi_d3d12_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);
static void lrhi_d3d12_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void lrhi_d3d12_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error);

static void lrhi_d3d12_destroy_swap_chain(LRHISwapChain swap_chain);
static LRHITexture lrhi_d3d12_swap_chain_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error);
static void lrhi_d3d12_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error);

static void lrhi_d3d12_destroy_texture_view(LRHITextureView texture_view);
static void lrhi_d3d12_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info);
static uint32_t lrhi_d3d12_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error);

static void lrhi_d3d12_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error);
static void lrhi_d3d12_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error);
static void lrhi_d3d12_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error);
static void lrhi_d3d12_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage before_stage, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_d3d12_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage before_stage, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_d3d12_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error);
static void lrhi_d3d12_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error);
static void lrhi_d3d12_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error);
static void lrhi_d3d12_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error);
static void lrhi_d3d12_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error);
static void lrhi_d3d12_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error);
static void lrhi_d3d12_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error);
static void lrhi_d3d12_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error);
static void lrhi_d3d12_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error);

static void lrhi_d3d12_destroy_shader_module(LRHIShaderModule shader_module);
static void lrhi_d3d12_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info);

static void lrhi_d3d12_destroy_render_pipeline(LRHIRenderPipeline pipeline);
static void lrhi_d3d12_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info);
static uint64_t lrhi_d3d12_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error);

static void lrhi_d3d12_destroy_mesh_pipeline(LRHIMeshPipeline pipeline);
static void lrhi_d3d12_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info);
static uint64_t lrhi_d3d12_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error);

static void lrhi_d3d12_destroy_compute_pipeline(LRHIComputePipeline pipeline);
static void lrhi_d3d12_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info);
static uint64_t lrhi_d3d12_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error);

static void lrhi_d3d12_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error);
static void lrhi_d3d12_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error);

static void lrhi_d3d12_destroy_buffer_view(LRHIBufferView buffer_view);
static void lrhi_d3d12_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info);
static uint32_t lrhi_d3d12_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error);

static void lrhi_d3d12_destroy_sampler(LRHISampler sampler);
static void lrhi_d3d12_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info);
static uint32_t lrhi_d3d12_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error);

static void lrhi_d3d12_acceleration_structure_pass_end(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass pass, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_encoder_barrier(LRHIAccelerationStructurePass pass, LRHIRenderStage after_stage, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error);
static void lrhi_d3d12_acceleration_structure_pass_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error);

static void lrhi_d3d12_destroy_bottom_level_acceleration_structure(LRHIBottomLevelAccelerationStructure blas);
static void lrhi_d3d12_get_bottom_level_acceleration_structure_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info);
static LRHIAccelerationStructureBufferSizes lrhi_d3d12_bottom_level_acceleration_structure_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error);

static void lrhi_d3d12_destroy_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas);
static void lrhi_d3d12_get_top_level_acceleration_structure_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info);
static uint64_t lrhi_d3d12_top_level_acceleration_structure_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static LRHIAccelerationStructureBufferSizes lrhi_d3d12_top_level_acceleration_structure_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void lrhi_d3d12_reset_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error);
static void lrhi_d3d12_add_top_level_acceleration_structure_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error);

// HRESULT handling
static int hr_to_lrhi_error(HRESULT hr, LRHIError* out_error, const char* context) {
	if (!out_error) {
		return FAILED(hr);
	}

    if (SUCCEEDED(hr)) {
        out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
        out_error->message[0] = '\0';
        return 0;
    } else {
        out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
		if (context && context[0] != '\0') {
			snprintf(out_error->message, sizeof(out_error->message), "%s (HRESULT: 0x%08X)", context, hr);
		} else {
			snprintf(out_error->message, sizeof(out_error->message), "HRESULT: 0x%08X", hr);
		}
        return 1;    
    }
}

// Wstring to cstring (UTF-16 to UTF-8)
static void wstring_to_cstring(const wchar_t* wstr, char* cstr, size_t cstr_size) {
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, cstr, (int)cstr_size, NULL, NULL);
}

// cstring to wstrng (UTF-8 to UTF-16)
static void cstring_to_wstring(const char* cstr, wchar_t* wstr, size_t wstr_size) {
    MultiByteToWideChar(CP_UTF8, 0, cstr, -1, wstr, (int)wstr_size);
}

// Vtable instances
static const LRHIDeviceVTable lrhi_d3d12_device_vtable = {
	lrhi_d3d12_destroy_device,
	lrhi_d3d12_get_device_info,
	lrhi_d3d12_create_texture,
	lrhi_d3d12_create_buffer,
	lrhi_d3d12_texture_readback,
	lrhi_d3d12_buffer_readback,
	lrhi_d3d12_create_command_queue,
	lrhi_d3d12_create_fence,
	lrhi_d3d12_create_residency_set,
	lrhi_d3d12_create_swap_chain,
	lrhi_d3d12_create_texture_view,
	lrhi_d3d12_create_shader_module,
	lrhi_d3d12_create_render_pipeline,
	lrhi_d3d12_create_mesh_pipeline,
	lrhi_d3d12_create_compute_pipeline,
	lrhi_d3d12_create_buffer_view,
	lrhi_d3d12_create_sampler,
	lrhi_d3d12_create_bottom_level_acceleration_structure,
	lrhi_d3d12_create_compacted_bottom_level_acceleration_structure,
	lrhi_d3d12_create_top_level_acceleration_structure,
};

static const LRHICommandQueueVTable lrhi_d3d12_command_queue_vtable = {
	lrhi_d3d12_create_command_list,
	lrhi_d3d12_destroy_command_queue,
	lrhi_d3d12_command_queue_signal,
	lrhi_d3d12_command_queue_wait,
	lrhi_d3d12_command_queue_submit,
	lrhi_d3d12_command_queue_add_residency_set,
};

static const LRHIFenceVTable lrhi_d3d12_fence_vtable = {
	lrhi_d3d12_destroy_fence,
	lrhi_d3d12_fence_get_value,
	lrhi_d3d12_fence_signal,
	lrhi_d3d12_fence_wait,
};

static const LRHITextureVTable lrhi_d3d12_texture_vtable = {
	lrhi_d3d12_destroy_texture,
	lrhi_d3d12_get_texture_info,
	lrhi_d3d12_texture_replace_region,
	lrhi_d3d12_texture_read_region,
	lrhi_d3d12_texture_set_name,
};

static const LRHIBufferVTable lrhi_d3d12_buffer_vtable = {
	lrhi_d3d12_destroy_buffer,
	lrhi_d3d12_get_buffer_info,
	lrhi_d3d12_buffer_map,
	lrhi_d3d12_buffer_unmap,
	lrhi_d3d12_buffer_set_name,
	lrhi_d3d12_buffer_set_indirect_command_type,
};

static const LRHICommandListVTable lrhi_d3d12_command_list_vtable = {
	lrhi_d3d12_destroy_command_list,
	lrhi_d3d12_command_list_begin,
	lrhi_d3d12_command_list_end,
	lrhi_d3d12_command_list_reset,
	lrhi_d3d12_command_list_prepare_indirect_commands,
	lrhi_d3d12_command_list_begin_copy_pass,
	lrhi_d3d12_render_pass_begin,
	lrhi_d3d12_compute_pass_begin,
	lrhi_d3d12_acceleration_structure_pass_begin,
};

static const LRHICopyPassVTable lrhi_d3d12_copy_pass_vtable = {
	lrhi_d3d12_copy_pass_end,
	lrhi_d3d12_copy_pass_push_debug_group,
	lrhi_d3d12_copy_pass_pop_debug_group,
	lrhi_d3d12_copy_pass_intra_barrier,
	lrhi_d3d12_copy_pass_encoder_barrier,
	lrhi_d3d12_copy_pass_copy_buffer_to_buffer,
	lrhi_d3d12_copy_pass_copy_buffer_to_texture,
	lrhi_d3d12_copy_pass_copy_texture_to_buffer,
	lrhi_d3d12_copy_pass_copy_texture_to_texture,
};

static const LRHIResidencySetVTable lrhi_d3d12_residency_set_vtable = {
	lrhi_d3d12_destroy_residency_set,
	lrhi_d3d12_residency_set_add_texture,
	lrhi_d3d12_residency_set_add_buffer,
	lrhi_d3d12_residency_set_add_blas,
	lrhi_d3d12_residency_set_add_tlas,
	lrhi_d3d12_residency_set_remove_texture,
	lrhi_d3d12_residency_set_remove_buffer,
	lrhi_d3d12_residency_set_remove_blas,
	lrhi_d3d12_residency_set_remove_tlas,
	lrhi_d3d12_residency_set_update,
};

static const LRHISwapChainVTable lrhi_d3d12_swap_chain_vtable = {
	lrhi_d3d12_destroy_swap_chain,
	lrhi_d3d12_swap_chain_get_current_texture,
	lrhi_d3d12_swap_chain_present,
};

static const LRHITextureViewVTable lrhi_d3d12_texture_view_vtable = {
	lrhi_d3d12_destroy_texture_view,
	lrhi_d3d12_get_texture_view_info,
	lrhi_d3d12_texture_view_get_bindless_index,
};

static const LRHIRenderPassVTable lrhi_d3d12_render_pass_vtable = {
	lrhi_d3d12_render_pass_end,
	lrhi_d3d12_render_pass_push_debug_group,
	lrhi_d3d12_render_pass_pop_debug_group,
	lrhi_d3d12_render_pass_intra_barrier,
	lrhi_d3d12_render_pass_encoder_barrier,
	lrhi_d3d12_render_pass_set_render_pipeline,
	lrhi_d3d12_render_pass_set_mesh_pipeline,
	lrhi_d3d12_render_pass_set_viewport,
	lrhi_d3d12_render_pass_set_scissor,
	lrhi_d3d12_render_pass_set_push_constants,
	lrhi_d3d12_render_pass_draw,
	lrhi_d3d12_render_pass_draw_indexed,
	lrhi_d3d12_render_pass_draw_mesh_tasks,
	lrhi_d3d12_render_pass_execute_indirect_commands,
};

static const LRHIShaderModuleVTable lrhi_d3d12_shader_module_vtable = {
	lrhi_d3d12_destroy_shader_module,
	lrhi_d3d12_get_shader_module_info,
};

static const LRHIRenderPipelineVTable lrhi_d3d12_render_pipeline_vtable = {
	lrhi_d3d12_destroy_render_pipeline,
	lrhi_d3d12_get_render_pipeline_info,
	lrhi_d3d12_render_pipeline_get_alloc_size,
};

static const LRHIMeshPipelineVTable lrhi_d3d12_mesh_pipeline_vtable = {
	lrhi_d3d12_destroy_mesh_pipeline,
	lrhi_d3d12_get_mesh_pipeline_info,
	lrhi_d3d12_mesh_pipeline_get_alloc_size,
};

static const LRHIComputePipelineVTable lrhi_d3d12_compute_pipeline_vtable = {
	lrhi_d3d12_destroy_compute_pipeline,
	lrhi_d3d12_get_compute_pipeline_info,
	lrhi_d3d12_compute_pipeline_get_alloc_size,
};

static const LRHIComputePassVTable lrhi_d3d12_compute_pass_vtable = {
	lrhi_d3d12_compute_pass_end,
	lrhi_d3d12_compute_pass_push_debug_group,
	lrhi_d3d12_compute_pass_pop_debug_group,
	lrhi_d3d12_compute_pass_barrier,
	lrhi_d3d12_compute_pass_encoder_barrier,
	lrhi_d3d12_compute_pass_set_pipeline,
	lrhi_d3d12_compute_pass_set_push_constants,
	lrhi_d3d12_compute_pass_dispatch,
	lrhi_d3d12_compute_pass_dispatch_indirect,
};

static const LRHIBufferViewVTable lrhi_d3d12_buffer_view_vtable = {
	lrhi_d3d12_destroy_buffer_view,
	lrhi_d3d12_get_buffer_view_info,
	lrhi_d3d12_buffer_view_get_bindless_index,
};

static const LRHISamplerVTable lrhi_d3d12_sampler_vtable = {
	lrhi_d3d12_destroy_sampler,
	lrhi_d3d12_get_sampler_info,
	lrhi_d3d12_sampler_get_bindless_index,
};

static const LRHIAccelerationStructurePassVTable lrhi_d3d12_acceleration_structure_pass_vtable = {
	lrhi_d3d12_acceleration_structure_pass_end,
	lrhi_d3d12_acceleration_structure_pass_push_debug_group,
	lrhi_d3d12_acceleration_structure_pass_pop_debug_group,
	lrhi_d3d12_acceleration_structure_pass_barrier,
	lrhi_d3d12_acceleration_structure_pass_encoder_barrier,
	lrhi_d3d12_acceleration_structure_pass_build_blas,
	lrhi_d3d12_acceleration_structure_pass_build_tlas,
	lrhi_d3d12_acceleration_structure_pass_write_compacted_blas_size,
	lrhi_d3d12_acceleration_structure_pass_compact_blas,
	lrhi_d3d12_acceleration_structure_pass_refit_blas,
	lrhi_d3d12_acceleration_structure_pass_refit_tlas,
	lrhi_d3d12_acceleration_structure_pass_copy_blas,
	lrhi_d3d12_acceleration_structure_pass_copy_tlas,
};

static const LRHIBLASVTable lrhi_d3d12_blas_vtable = {
	lrhi_d3d12_destroy_bottom_level_acceleration_structure,
	lrhi_d3d12_get_bottom_level_acceleration_structure_info,
	lrhi_d3d12_bottom_level_acceleration_structure_get_build_scratch_size,
};

static const LRHITLASVTable lrhi_d3d12_tlas_vtable = {
	lrhi_d3d12_destroy_top_level_acceleration_structure,
	lrhi_d3d12_get_top_level_acceleration_structure_info,
	lrhi_d3d12_top_level_acceleration_structure_get_bindless_index,
	lrhi_d3d12_top_level_acceleration_structure_get_build_scratch_size,
	lrhi_d3d12_reset_top_level_acceleration_structure,
	lrhi_d3d12_add_top_level_acceleration_structure_instance,
};

static void lrhi_d3d12_set_not_implemented(LRHIError* out_error, const char* function_name)
{
	if (!out_error) {
		return;
	}
	snprintf(out_error->message, sizeof(out_error->message), "%s is not implemented for D3D12 backend stub", function_name);
	out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_WARNING;
}

static void lrhi_d3d12_set_error(LRHIError* out_error, const char* message)
{
	if (!out_error) {
		return;
	}
	snprintf(out_error->message, sizeof(out_error->message), "%s", message);
	out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
}

static void lrhi_d3d12_destroy_device(LRHIDevice device)
{
	LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

	lrhi_d3d12_bindless_manager_destroy(device_d3d12);
	ID3D12RootSignature_Release(device_d3d12->global_root_signature);
	if (device_d3d12->debug_controller) {
		ID3D12Debug_Release(device_d3d12->debug_controller);
	}
	IDXGIFactory6_Release(device_d3d12->factory);
	IDXGIAdapter_Release(device_d3d12->adapter);
	ID3D12CommandSignature_Release(device_d3d12->draw_indirect_command_signature);
	ID3D12CommandSignature_Release(device_d3d12->dispatch_indirect_command_signature);
	ID3D12CommandSignature_Release(device_d3d12->mesh_draw_indirect_command_signature);
	ID3D12CommandSignature_Release(device_d3d12->mesh_draw_indirect_command_signature);
	ID3D12Device_Release(device_d3d12->device);

	LRHI_FREE(device);
}

static LRHIDeviceInfo lrhi_d3d12_get_device_info(LRHIDevice device)
{
	LRHIDeviceInfo info;
	memset(&info, 0, sizeof(info));
	if (device) {
		info = ((LRHIDeviceD3D12*)device)->info;
	}
	return info;
}

static void lrhi_d3d12_create_texture(LRHIDevice device, LRHITextureInfo* info, LRHITexture* out_texture, LRHIError* out_error)
{
	D3D12_RESOURCE_DESC resource_desc;
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Width = info->width;
    resource_desc.Height = info->height;
    resource_desc.DepthOrArraySize = info->dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY ? info->array_layers : info->depth;
    resource_desc.MipLevels = info->mip_levels;
    resource_desc.Dimension = lrhi_texture_dimension_to_d3d12_resource_dimension(info->dimensions);
    resource_desc.Format = lrhi_format_to_dxgi_format(info->format);
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    if (info->usage & LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (info->usage & LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (info->usage & LUMINARY_RHI_TEXTURE_USAGE_STORAGE) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES heap_properties;
    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

    LRHITextureD3D12* texture_d3d12 = LRHI_MALLOC(sizeof(LRHITextureD3D12));
    memset(texture_d3d12, 0, sizeof(LRHITextureD3D12));
	texture_d3d12->base.vtable = &lrhi_d3d12_texture_vtable;
    HRESULT hr = ID3D12Device_CreateCommittedResource(device_d3d12->device, &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&texture_d3d12->resource);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create texture resource")) {
        LRHI_FREE(texture_d3d12);
        return;
    }
	texture_d3d12->device_ref = device_d3d12;
    texture_d3d12->info = *info;

	*out_texture = (LRHITexture)texture_d3d12;
}

static void lrhi_d3d12_destroy_texture(LRHITexture texture)
{
    LRHITextureD3D12* texture_d3d12 = (LRHITextureD3D12*)texture;
    if (texture_d3d12->resource) {
        ID3D12Resource_Release(texture_d3d12->resource);
    }
	LRHI_FREE(texture);
}

static void lrhi_d3d12_get_texture_info(LRHITexture texture, LRHITextureInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (texture) {
		*out_info = ((LRHITextureD3D12*)texture)->info;
	}
}

static void lrhi_d3d12_texture_replace_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
	(void)texture;
	(void)region;
	(void)mip_level;
	(void)array_layer;
	(void)data;
	(void)data_size;
	(void)bytes_per_row;
	(void)bytes_per_image;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_texture_replace_region");
}

static void lrhi_d3d12_texture_read_region(LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
	(void)texture;
	(void)region;
	(void)mip_level;
	(void)array_layer;
	(void)out_data;
	(void)data_size;
	(void)bytes_per_row;
	(void)bytes_per_image;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_texture_read_region");
}

static void lrhi_d3d12_texture_readback(LRHIDevice device, LRHITexture texture, LRHIRegion* region, uint32_t mip_level, uint32_t array_layer, void* out_data, uint32_t data_size, uint32_t bytes_per_row, uint32_t bytes_per_image, LRHIError* out_error)
{
    LRHITextureD3D12* texture_d3d12 = (LRHITextureD3D12*)texture;
    LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

    if (!device_d3d12 || !texture_d3d12 || !texture_d3d12->resource || !region || !out_data) {
		lrhi_d3d12_set_error(out_error, "Invalid arguments for texture readback");
		return;
	}

	if (mip_level >= texture_d3d12->info.mip_levels) {
		lrhi_d3d12_set_error(out_error, "Requested mip level is out of range for texture readback");
		return;
	}

	if (texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D) {
		if (array_layer != 0) {
			lrhi_d3d12_set_error(out_error, "3D texture readback does not use array layers");
			return;
		}
		if (region->depth == 0) {
			lrhi_d3d12_set_error(out_error, "Texture readback region depth must be non-zero");
			return;
		}
	} else {
		if (array_layer >= texture_d3d12->info.array_layers) {
			lrhi_d3d12_set_error(out_error, "Requested array layer is out of range for texture readback");
			return;
		}
		if (region->depth != 1) {
			lrhi_d3d12_set_error(out_error, "2D texture readback regions must have depth 1");
			return;
		}
	}

	if (region->width == 0 || region->height == 0) {
		lrhi_d3d12_set_error(out_error, "Texture readback region must have non-zero width and height");
		return;
	}

	D3D12_RESOURCE_DESC readback_texture_desc;
	memset(&readback_texture_desc, 0, sizeof(readback_texture_desc));
	readback_texture_desc.Dimension = lrhi_texture_dimension_to_d3d12_resource_dimension(texture_d3d12->info.dimensions);
	readback_texture_desc.Alignment = 0;
	readback_texture_desc.Width = region->width;
	readback_texture_desc.Height = texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_1D ? 1 : region->height;
	readback_texture_desc.DepthOrArraySize = texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? region->depth : 1;
	readback_texture_desc.MipLevels = 1;
	readback_texture_desc.Format = lrhi_format_to_dxgi_format(texture_d3d12->info.format);
	readback_texture_desc.SampleDesc.Count = 1;
	readback_texture_desc.SampleDesc.Quality = 0;
	readback_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	readback_texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
	UINT row_count = 0;
	UINT64 row_size_in_bytes = 0;
	UINT64 total_bytes = 0;
	ID3D12Device_GetCopyableFootprints(device_d3d12->device, &readback_texture_desc, 0, 1, 0, &layout, &row_count, &row_size_in_bytes, &total_bytes);

	uint32_t effective_bytes_per_row = bytes_per_row != 0 ? bytes_per_row : (uint32_t)row_size_in_bytes;
	if ((uint64_t)effective_bytes_per_row < row_size_in_bytes) {
		lrhi_d3d12_set_error(out_error, "bytes_per_row is smaller than the texture row size");
		return;
	}

	uint32_t slice_count = texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? region->depth : 1;
	uint64_t effective_bytes_per_image = bytes_per_image != 0 ? bytes_per_image : (uint64_t)effective_bytes_per_row * region->height;
	uint64_t required_size = 0;
	if (slice_count > 0) {
		required_size = effective_bytes_per_image * (slice_count - 1) + (uint64_t)effective_bytes_per_row * (region->height - 1) + row_size_in_bytes;
	}
	if ((uint64_t)data_size < required_size) {
		lrhi_d3d12_set_error(out_error, "Output buffer is too small for texture readback");
		return;
	}

	D3D12_HEAP_PROPERTIES readback_heap_properties;
	memset(&readback_heap_properties, 0, sizeof(readback_heap_properties));
	readback_heap_properties.Type = D3D12_HEAP_TYPE_READBACK;

	D3D12_RESOURCE_DESC readback_buffer_desc;
	memset(&readback_buffer_desc, 0, sizeof(readback_buffer_desc));
	readback_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	readback_buffer_desc.Alignment = 0;
	readback_buffer_desc.Width = total_bytes;
	readback_buffer_desc.Height = 1;
	readback_buffer_desc.DepthOrArraySize = 1;
	readback_buffer_desc.MipLevels = 1;
	readback_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
	readback_buffer_desc.SampleDesc.Count = 1;
	readback_buffer_desc.SampleDesc.Quality = 0;
	readback_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	readback_buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ID3D12Resource* readback_buffer = NULL;
	HRESULT hr = ID3D12Device_CreateCommittedResource(device_d3d12->device, &readback_heap_properties, D3D12_HEAP_FLAG_NONE, &readback_buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&readback_buffer);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary readback buffer")) {
		return;
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc;
	memset(&queue_desc, 0, sizeof(queue_desc));
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 0;

	ID3D12CommandQueue* command_queue = NULL;
	hr = ID3D12Device_CreateCommandQueue(device_d3d12->device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&command_queue);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary command queue")) {
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	ID3D12CommandAllocator* command_allocator = NULL;
	hr = ID3D12Device_CreateCommandAllocator(device_d3d12->device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&command_allocator);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary command allocator")) {
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	ID3D12GraphicsCommandList* command_list = NULL;
	hr = ID3D12Device_CreateCommandList(device_d3d12->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void**)&command_list);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary command list")) {
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	D3D12_RESOURCE_BARRIER barrier;
	memset(&barrier, 0, sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture_d3d12->resource;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);

	D3D12_TEXTURE_COPY_LOCATION src_location;
	memset(&src_location, 0, sizeof(src_location));
	src_location.pResource = texture_d3d12->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src_location.SubresourceIndex = texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? mip_level : array_layer * texture_d3d12->info.mip_levels + mip_level;

	D3D12_TEXTURE_COPY_LOCATION dst_location;
	memset(&dst_location, 0, sizeof(dst_location));
	dst_location.pResource = readback_buffer;
	dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst_location.PlacedFootprint = layout;

	D3D12_BOX src_box;
	src_box.left = region->x;
	src_box.top = region->y;
	src_box.front = region->z;
	src_box.right = region->x + region->width;
	src_box.bottom = region->y + region->height;
	src_box.back = region->z + region->depth;
	ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, &src_box);

	hr = ID3D12GraphicsCommandList_Close(command_list);
	if (hr_to_lrhi_error(hr, out_error, "Failed to close temporary command list")) {
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	ID3D12CommandList* command_lists[] = {(ID3D12CommandList*)command_list};
	ID3D12CommandQueue_ExecuteCommandLists(command_queue, 1, command_lists);

	ID3D12Fence* fence = NULL;
	hr = ID3D12Device_CreateFence(device_d3d12->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary fence")) {
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	const uint64_t fence_value = 1;
	hr = ID3D12CommandQueue_Signal(command_queue, fence, fence_value);
	if (hr_to_lrhi_error(hr, out_error, "Failed to signal temporary fence")) {
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	HANDLE fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!fence_event) {
		lrhi_d3d12_set_error(out_error, "Failed to create fence wait event");
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	hr = ID3D12Fence_SetEventOnCompletion(fence, fence_value, fence_event);
	if (hr_to_lrhi_error(hr, out_error, "Failed to wait for temporary fence")) {
		CloseHandle(fence_event);
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	WaitForSingleObject(fence_event, INFINITE);
	CloseHandle(fence_event);

	void* mapped_data = NULL;
	D3D12_RANGE read_range;
	read_range.Begin = 0;
	read_range.End = (SIZE_T)total_bytes;
	hr = ID3D12Resource_Map(readback_buffer, 0, &read_range, &mapped_data);
	if (hr_to_lrhi_error(hr, out_error, "Failed to map temporary readback buffer")) {
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	const uint8_t* src_bytes = (const uint8_t*)mapped_data + layout.Offset;
	uint8_t* dst_bytes = (uint8_t*)out_data;
	uint64_t source_slice_stride = (uint64_t)layout.Footprint.RowPitch * row_count;
	for (uint32_t slice = 0; slice < slice_count; ++slice) {
		const uint8_t* src_slice = src_bytes + source_slice_stride * slice;
		uint8_t* dst_slice = dst_bytes + effective_bytes_per_image * slice;
		for (uint32_t row = 0; row < region->height; ++row) {
			memcpy(dst_slice + (uint64_t)effective_bytes_per_row * row, src_slice + (uint64_t)layout.Footprint.RowPitch * row, (size_t)row_size_in_bytes);
		}
	}

	D3D12_RANGE written_range;
	written_range.Begin = 0;
	written_range.End = 0;
	ID3D12Resource_Unmap(readback_buffer, 0, &written_range);

	ID3D12Fence_Release(fence);
	ID3D12GraphicsCommandList_Release(command_list);
	ID3D12CommandAllocator_Release(command_allocator);
	ID3D12CommandQueue_Release(command_queue);
	ID3D12Resource_Release(readback_buffer);

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_texture_set_name(LRHITexture texture, const char* name)
{
	LRHITextureD3D12* texture_d3d12 = (LRHITextureD3D12*)texture;

    // Convert to wchar
    wchar_t wname[256];
    int wname_length = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, (int)(sizeof(wname) / sizeof(wchar_t)));
    if (wname_length > 0) {
        ID3D12Object_SetName((ID3D12Object*)texture_d3d12->resource, wname);
    }
}

static void lrhi_d3d12_create_buffer(LRHIDevice device, LRHIBufferInfo* info, LRHIBuffer* out_buffer, LRHIError* out_error)
{
    D3D12_RESOURCE_DESC resource_desc;
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = info->size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (info->usage & LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES allocation_desc;
    memset(&allocation_desc, 0, sizeof(allocation_desc));
    allocation_desc.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (info->usage & LUMINARY_RHI_BUFFER_USAGE_READBACK) allocation_desc.Type = D3D12_HEAP_TYPE_READBACK;
    if (info->usage & LUMINARY_RHI_BUFFER_USAGE_STAGING) allocation_desc.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (info->usage & LUMINARY_RHI_BUFFER_USAGE_CONSTANT) allocation_desc.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
	if (allocation_desc.Type == D3D12_HEAP_TYPE_READBACK) {
		initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
	} else if (allocation_desc.Type == D3D12_HEAP_TYPE_UPLOAD) {
		initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

    LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

    LRHIBufferD3D12* buffer_d3d12 = LRHI_MALLOC(sizeof(LRHIBufferD3D12));
    memset(buffer_d3d12, 0, sizeof(LRHIBufferD3D12));
	buffer_d3d12->base.vtable = &lrhi_d3d12_buffer_vtable;
	HRESULT hr = ID3D12Device_CreateCommittedResource(device_d3d12->device, &allocation_desc, D3D12_HEAP_FLAG_NONE, &resource_desc, initial_state, NULL, &IID_ID3D12Resource, (void**)&buffer_d3d12->resource);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create buffer resource")) {
        LRHI_FREE(buffer_d3d12);
        return;
    }
    buffer_d3d12->info = *info;
    buffer_d3d12->device_ref = device_d3d12;

	*out_buffer = (LRHIBuffer)buffer_d3d12;

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_destroy_buffer(LRHIBuffer buffer)
{
    LRHIBufferD3D12* buffer_d3d12 = (LRHIBufferD3D12*)buffer;
    if (buffer_d3d12->resource) {
        ID3D12Resource_Release(buffer_d3d12->resource);
    }
	LRHI_FREE(buffer);
}

static void lrhi_d3d12_get_buffer_info(LRHIBuffer buffer, LRHIBufferInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (buffer) {
		*out_info = ((LRHIBufferD3D12*)buffer)->info;
	}
}

static void* lrhi_d3d12_buffer_map(LRHIBuffer buffer, LRHIError* out_error)
{
	if (!buffer) {
		lrhi_d3d12_set_error(out_error, "Invalid buffer for map");
		return NULL;
	}

	LRHIBufferD3D12* buffer_d3d12 = (LRHIBufferD3D12*)buffer;

    D3D12_RANGE range;
    range.Begin = 0;
    range.End = buffer_d3d12->info.size;

	void* ptr = NULL;
	HRESULT hr = ID3D12Resource_Map(buffer_d3d12->resource, 0, &range, &ptr);
	if (hr_to_lrhi_error(hr, out_error, "Failed to map buffer")) {
		return NULL;
	}

	return ptr;
}

static void lrhi_d3d12_buffer_unmap(LRHIBuffer buffer)
{
	LRHIBufferD3D12* buffer_d3d12 = (LRHIBufferD3D12*)buffer;
    
    D3D12_RANGE range;
    range.Begin = 0;
    range.End = buffer_d3d12->info.size;

    ID3D12Resource_Unmap(buffer_d3d12->resource, 0, NULL);
}

static void lrhi_d3d12_buffer_readback(LRHIDevice device, LRHIBuffer buffer, void* out_data, uint32_t data_size, LRHIError* out_error)
{
	LRHIBufferD3D12* buffer_d3d12 = (LRHIBufferD3D12*)buffer;
	LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

	if (!device_d3d12 || !buffer_d3d12 || !buffer_d3d12->resource || !out_data) {
		lrhi_d3d12_set_error(out_error, "Invalid arguments for buffer readback");
		return;
	}

	if (buffer_d3d12->info.size == 0) {
		if (out_error) {
			out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
			out_error->message[0] = '\0';
		}
		return;
	}

	if ((uint64_t)data_size < buffer_d3d12->info.size) {
		lrhi_d3d12_set_error(out_error, "Output buffer is too small for buffer readback");
		return;
	}

	D3D12_HEAP_PROPERTIES readback_heap_properties;
	memset(&readback_heap_properties, 0, sizeof(readback_heap_properties));
	readback_heap_properties.Type = D3D12_HEAP_TYPE_READBACK;

	D3D12_RESOURCE_DESC readback_buffer_desc;
	memset(&readback_buffer_desc, 0, sizeof(readback_buffer_desc));
	readback_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	readback_buffer_desc.Alignment = 0;
	readback_buffer_desc.Width = buffer_d3d12->info.size;
	readback_buffer_desc.Height = 1;
	readback_buffer_desc.DepthOrArraySize = 1;
	readback_buffer_desc.MipLevels = 1;
	readback_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
	readback_buffer_desc.SampleDesc.Count = 1;
	readback_buffer_desc.SampleDesc.Quality = 0;
	readback_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	readback_buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ID3D12Resource* readback_buffer = NULL;
	HRESULT hr = ID3D12Device_CreateCommittedResource(device_d3d12->device, &readback_heap_properties, D3D12_HEAP_FLAG_NONE, &readback_buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&readback_buffer);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary readback buffer")) {
		return;
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc;
	memset(&queue_desc, 0, sizeof(queue_desc));
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.NodeMask = 0;

	ID3D12CommandQueue* command_queue = NULL;
	hr = ID3D12Device_CreateCommandQueue(device_d3d12->device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&command_queue);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary command queue")) {
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	ID3D12CommandAllocator* command_allocator = NULL;
	hr = ID3D12Device_CreateCommandAllocator(device_d3d12->device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&command_allocator);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary command allocator")) {
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	ID3D12GraphicsCommandList* command_list = NULL;
	hr = ID3D12Device_CreateCommandList(device_d3d12->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void**)&command_list);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary command list")) {
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	D3D12_RESOURCE_BARRIER barrier;
	memset(&barrier, 0, sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = buffer_d3d12->resource;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);

	ID3D12GraphicsCommandList_CopyBufferRegion(command_list, readback_buffer, 0, buffer_d3d12->resource, 0, buffer_d3d12->info.size);

	hr = ID3D12GraphicsCommandList_Close(command_list);
	if (hr_to_lrhi_error(hr, out_error, "Failed to close temporary command list")) {
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	ID3D12CommandList* command_lists[] = {(ID3D12CommandList*)command_list};
	ID3D12CommandQueue_ExecuteCommandLists(command_queue, 1, command_lists);

	ID3D12Fence* fence = NULL;
	hr = ID3D12Device_CreateFence(device_d3d12->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create temporary fence")) {
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	const uint64_t fence_value = 1;
	hr = ID3D12CommandQueue_Signal(command_queue, fence, fence_value);
	if (hr_to_lrhi_error(hr, out_error, "Failed to signal temporary fence")) {
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	HANDLE fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!fence_event) {
		lrhi_d3d12_set_error(out_error, "Failed to create fence wait event");
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	hr = ID3D12Fence_SetEventOnCompletion(fence, fence_value, fence_event);
	if (hr_to_lrhi_error(hr, out_error, "Failed to wait for temporary fence")) {
		CloseHandle(fence_event);
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	WaitForSingleObject(fence_event, INFINITE);
	CloseHandle(fence_event);

	void* mapped_data = NULL;
	D3D12_RANGE read_range;
	read_range.Begin = 0;
	read_range.End = (SIZE_T)buffer_d3d12->info.size;
	hr = ID3D12Resource_Map(readback_buffer, 0, &read_range, &mapped_data);
	if (hr_to_lrhi_error(hr, out_error, "Failed to map temporary readback buffer")) {
		ID3D12Fence_Release(fence);
		ID3D12GraphicsCommandList_Release(command_list);
		ID3D12CommandAllocator_Release(command_allocator);
		ID3D12CommandQueue_Release(command_queue);
		ID3D12Resource_Release(readback_buffer);
		return;
	}

	memcpy(out_data, mapped_data, (size_t)buffer_d3d12->info.size);

	D3D12_RANGE written_range;
	written_range.Begin = 0;
	written_range.End = 0;
	ID3D12Resource_Unmap(readback_buffer, 0, &written_range);

	ID3D12Fence_Release(fence);
	ID3D12GraphicsCommandList_Release(command_list);
	ID3D12CommandAllocator_Release(command_allocator);
	ID3D12CommandQueue_Release(command_queue);
	ID3D12Resource_Release(readback_buffer);

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_buffer_set_name(LRHIBuffer buffer, const char* name)
{
	LRHIBufferD3D12* buffer_d3d12 = (LRHIBufferD3D12*)buffer;

    // Convert to wchar
    wchar_t wname[256];
    int wname_length = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, (int)(sizeof(wname) / sizeof(wchar_t)));
    if (wname_length > 0) {
        ID3D12Object_SetName((ID3D12Object*)buffer_d3d12->resource, wname);
    }
}

static void lrhi_d3d12_buffer_set_indirect_command_type(LRHIBuffer buffer, LRHICommandType command_type, LRHIError* out_error)
{
	LRHIBufferD3D12* buffer_d3d12 = (LRHIBufferD3D12*)buffer;
    buffer_d3d12->indirect_command_type = command_type;
}

static void lrhi_d3d12_create_command_queue(LRHIDevice device, LRHICommandQueue* out_queue, LRHIError* out_error)
{
	LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

    D3D12_COMMAND_QUEUE_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    LRHICommandQueueD3D12* queue_d3d12 = LRHI_MALLOC(sizeof(LRHICommandQueueD3D12));
    memset(queue_d3d12, 0, sizeof(LRHICommandQueueD3D12));
	queue_d3d12->base.vtable = &lrhi_d3d12_command_queue_vtable;
    HRESULT hr = ID3D12Device_CreateCommandQueue(device_d3d12->device, &desc, &IID_ID3D12CommandQueue, (void**)&queue_d3d12->queue);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create command queue")) {
        LRHI_FREE(queue_d3d12);
        return;
    }
    queue_d3d12->device_ref = device_d3d12;
	*out_queue = (LRHICommandQueue)queue_d3d12;
}

static void lrhi_d3d12_destroy_command_queue(LRHICommandQueue queue)
{
    LRHICommandQueueD3D12* queue_d3d12 = (LRHICommandQueueD3D12*)queue;
    ID3D12CommandQueue_Release(queue_d3d12->queue);
	LRHI_FREE(queue);
}

static void lrhi_d3d12_command_queue_signal(LRHICommandQueue queue, LRHIFence fence, uint64_t value, LRHIError* out_error)
{
	LRHICommandQueueD3D12* queue_d3d12 = (LRHICommandQueueD3D12*)queue;
    LRHIFenceD3D12* fence_d3d12 = (LRHIFenceD3D12*)fence;
    HRESULT hr = ID3D12CommandQueue_Signal(queue_d3d12->queue, fence_d3d12->fence, value);
    hr_to_lrhi_error(hr, out_error, "Failed to signal fence");
}

static void lrhi_d3d12_command_queue_wait(LRHICommandQueue queue, LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
	LRHICommandQueueD3D12* queue_d3d12 = (LRHICommandQueueD3D12*)queue;
    LRHIFenceD3D12* fence_d3d12 = (LRHIFenceD3D12*)fence;

    // D3D12 doesn't support waiting with a timeout on the GPU, so we ignore the timeout and wait indefinitely.

    HRESULT hr = ID3D12CommandQueue_Wait(queue_d3d12->queue, fence_d3d12->fence, value);
    hr_to_lrhi_error(hr, out_error, "Failed to wait for fence");
}

static void lrhi_d3d12_command_queue_submit(LRHICommandQueue queue, LRHICommandList* command_lists, uint32_t command_list_count, LRHIFence signal_fence, uint64_t signal_value, LRHIFence wait_fence, uint64_t wait_value, LRHIError* out_error)
{
	LRHICommandQueueD3D12* queue_d3d12 = (LRHICommandQueueD3D12*)queue;

    // D3D12 doesn't support waiting on the GPU, so we ignore the wait_fence and wait_value.

    ID3D12CommandList* d3d12_command_lists[16];
    for (uint32_t i = 0; i < command_list_count; ++i) {
        LRHICommandListD3D12* command_list_d3d12 = (LRHICommandListD3D12*)command_lists[i];
        d3d12_command_lists[i] = (ID3D12CommandList*)command_list_d3d12->command_list;
    }

    if (wait_fence) {
        lrhi_d3d12_command_queue_wait(queue, wait_fence, wait_value, UINT64_MAX, out_error);
    }

    ID3D12CommandQueue_ExecuteCommandLists(queue_d3d12->queue, command_list_count, d3d12_command_lists);

    if (signal_fence) {
        lrhi_d3d12_command_queue_signal(queue, signal_fence, signal_value, out_error);
    }
}

static void lrhi_d3d12_command_queue_add_residency_set(LRHICommandQueue queue, LRHIResidencySet residency_set, LRHIError* out_error)
{
    // No-op
	(void)queue;
	(void)residency_set;
	(void)out_error;
}

static void lrhi_d3d12_create_fence(LRHIDevice device, uint64_t initial_value, LRHIFence* out_fence, LRHIError* out_error)
{
	LRHIDeviceD3D12* device_d3d12 = (LRHIDeviceD3D12*)device;

    LRHIFenceD3D12* fence_d3d12 = LRHI_MALLOC(sizeof(LRHIFenceD3D12));
    memset(fence_d3d12, 0, sizeof(LRHIFenceD3D12));
	fence_d3d12->base.vtable = &lrhi_d3d12_fence_vtable;
    HRESULT hr = ID3D12Device_CreateFence(device_d3d12->device, initial_value, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence_d3d12->fence);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create fence")) {
        LRHI_FREE(fence_d3d12);
        return;
    }
	fence_d3d12->event = CreateEventA(NULL, FALSE, FALSE, NULL);
	if (!fence_d3d12->event) {
		ID3D12Fence_Release(fence_d3d12->fence);
		lrhi_d3d12_set_error(out_error, "Failed to create fence event");
		LRHI_FREE(fence_d3d12);
		return;
	}
    *out_fence = (LRHIFence)fence_d3d12;
}

static void lrhi_d3d12_destroy_fence(LRHIFence fence)
{
	LRHIFenceD3D12* fence_d3d12 = (LRHIFenceD3D12*)fence;
	if (fence_d3d12->event) {
		CloseHandle(fence_d3d12->event);
	}
    ID3D12Fence_Release(fence_d3d12->fence);
    LRHI_FREE(fence_d3d12);
}

static uint64_t lrhi_d3d12_fence_get_value(LRHIFence fence)
{
	LRHIFenceD3D12* fence_d3d12 = (LRHIFenceD3D12*)fence;
	return ID3D12Fence_GetCompletedValue(fence_d3d12->fence);
}

static void lrhi_d3d12_fence_signal(LRHIFence fence, uint64_t value, LRHIError* out_error)
{
	LRHIFenceD3D12* fence_d3d12 = (LRHIFenceD3D12*)fence;
    HRESULT hr = ID3D12Fence_Signal(fence_d3d12->fence, value);
    hr_to_lrhi_error(hr, out_error, "Failed to signal fence");
}

static void lrhi_d3d12_fence_wait(LRHIFence fence, uint64_t value, uint64_t timeout_ns, LRHIError* out_error)
{
	LRHIFenceD3D12* fence_d3d12 = (LRHIFenceD3D12*)fence;

	HRESULT hr = ID3D12Fence_SetEventOnCompletion(fence_d3d12->fence, value, fence_d3d12->event);
	if (hr_to_lrhi_error(hr, out_error, "Failed to wait for fence")) {
		return;
	}

    WaitForSingleObjectEx(fence_d3d12->event, (DWORD)(timeout_ns / 1000000), FALSE);
}

static void lrhi_d3d12_create_command_list(LRHICommandQueue queue, LRHICommandList* out_command_list, LRHIError* out_error)
{
	LRHICommandQueueD3D12* queue_d3d12 = (LRHICommandQueueD3D12*)queue;

    LRHICommandListD3D12* command_list_d3d12 = LRHI_MALLOC(sizeof(LRHICommandListD3D12));
    memset(command_list_d3d12, 0, sizeof(LRHICommandListD3D12));
	command_list_d3d12->base.vtable = &lrhi_d3d12_command_list_vtable;
    HRESULT hr = ID3D12Device_CreateCommandAllocator(queue_d3d12->device_ref->device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&command_list_d3d12->command_allocator);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create command allocator for command list")) {
        ID3D12GraphicsCommandList_Release(command_list_d3d12->command_list);
        LRHI_FREE(command_list_d3d12);
        return;
    }
	hr = ID3D12Device_CreateCommandList(queue_d3d12->device_ref->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_list_d3d12->command_allocator, NULL, &IID_ID3D12GraphicsCommandList, (void**)&command_list_d3d12->command_list);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create command list")) {
        LRHI_FREE(command_list_d3d12);
        return;
    }
    ID3D12GraphicsCommandList_Close(command_list_d3d12->command_list);
	command_list_d3d12->device_ref = queue_d3d12->device_ref;
    *out_command_list = (LRHICommandList)command_list_d3d12;
}

static void lrhi_d3d12_destroy_command_list(LRHICommandList command_list)
{
	LRHICommandListD3D12* command_list_d3d12 = (LRHICommandListD3D12*)command_list;
    ID3D12CommandAllocator_Release(command_list_d3d12->command_allocator);
    ID3D12GraphicsCommandList_Release(command_list_d3d12->command_list);
    LRHI_FREE(command_list_d3d12);
}

static void lrhi_d3d12_command_list_begin(LRHICommandList command_list, LRHIError* out_error)
{
	LRHICommandListD3D12* command_list_d3d12 = (LRHICommandListD3D12*)command_list;
    HRESULT hr = ID3D12CommandAllocator_Reset(command_list_d3d12->command_allocator);
    if (hr_to_lrhi_error(hr, out_error, "Failed to reset command allocator for command list")) {
        return;
    }
    hr = ID3D12GraphicsCommandList_Reset(command_list_d3d12->command_list, command_list_d3d12->command_allocator, NULL);
    if (hr_to_lrhi_error(hr, out_error, "Failed to reset command list")) {
        return;
    }

	// Set descriptor heaps
	ID3D12DescriptorHeap* descriptor_heaps[] = {
		command_list_d3d12->device_ref->bindless_manager.cbv_srv_uav_heap,
		command_list_d3d12->device_ref->bindless_manager.sampler_heap
	};
	ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list_d3d12->command_list, 2, descriptor_heaps);
}

static void lrhi_d3d12_command_list_end(LRHICommandList command_list, LRHIError* out_error)
{
	LRHICommandListD3D12* command_list_d3d12 = (LRHICommandListD3D12*)command_list;
    HRESULT hr = ID3D12GraphicsCommandList_Close(command_list_d3d12->command_list);
    hr_to_lrhi_error(hr, out_error, "Failed to close command list");
}

static void lrhi_d3d12_command_list_reset(LRHICommandList command_list, LRHIError* out_error)
{
	// No-op, done in begin instead
	(void)command_list;
	(void)out_error;
}

static void lrhi_d3d12_command_list_prepare_indirect_commands(LRHICommandList command_list, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIDrawIndirectParameters* parameters, LRHIRenderPipeline pipeline, const void* push_constants, uint32_t push_constant_size, LRHIError* out_error)
{
	(void)command_list;
	(void)indirect_command_buffer;
	(void)count_buffer;
	(void)max_command_count;
	(void)parameters;
	(void)pipeline;
	
    LRHICommandListD3D12* command_list_d3d12 = (LRHICommandListD3D12*)command_list;
    memcpy(command_list_d3d12->push_constants, push_constants, push_constant_size);
}

static LRHICopyPass lrhi_d3d12_command_list_begin_copy_pass(LRHICommandList command_list, LRHIError* out_error)
{
	LRHICommandListD3D12* command_list_d3d12 = (LRHICommandListD3D12*)command_list;
    
    LRHICopyPassD3D12* copy_pass = LRHI_MALLOC(sizeof(LRHICopyPassD3D12));
    memset(copy_pass, 0, sizeof(LRHICopyPassD3D12));
    copy_pass->base.vtable = &lrhi_d3d12_copy_pass_vtable;
    copy_pass->command_list = command_list_d3d12->command_list;
	return (LRHICopyPass)copy_pass;
}

static void lrhi_d3d12_copy_pass_end(LRHICopyPass copy_pass, LRHIError* out_error)
{
	LRHI_FREE(copy_pass);
}

static void lrhi_d3d12_copy_pass_push_debug_group(LRHICopyPass copy_pass, const char* label, LRHIError* out_error)
{
    (void)copy_pass;
    (void)label;
    (void)out_error;
}

static void lrhi_d3d12_copy_pass_pop_debug_group(LRHICopyPass copy_pass, LRHIError* out_error)
{
	(void)copy_pass;
    (void)out_error;
}

static void lrhi_d3d12_copy_pass_intra_barrier(LRHICopyPass copy_pass, LRHIError* out_error)
{
	// Global barrier
    LRHICopyPassD3D12* copy_pass_d3d12 = (LRHICopyPassD3D12*)copy_pass;

    D3D12_GLOBAL_BARRIER global_barrier;
    memset(&global_barrier, 0, sizeof(global_barrier));
    global_barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
    global_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    global_barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE;
    global_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE;

    D3D12_BARRIER_GROUP barrier_group;
    memset(&barrier_group, 0, sizeof(barrier_group));
    barrier_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
    barrier_group.NumBarriers = 1;
    barrier_group.pGlobalBarriers = &global_barrier;

    ID3D12GraphicsCommandList10_Barrier(copy_pass_d3d12->command_list, 1, &barrier_group);
}

static void lrhi_d3d12_copy_pass_encoder_barrier(LRHICopyPass copy_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
	LRHICopyPassD3D12* copy_pass_d3d12 = (LRHICopyPassD3D12*)copy_pass;

    D3D12_GLOBAL_BARRIER global_barrier;
    memset(&global_barrier, 0, sizeof(global_barrier));
    global_barrier.SyncBefore = lrhi_pipeline_stage_to_d3d12_barrier_sync(after_stage);
    global_barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
    global_barrier.AccessBefore = lrhi_pipeline_usage_to_d3d12_barrier_access(after_stage);
    global_barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST | D3D12_BARRIER_ACCESS_COPY_SOURCE;

    D3D12_BARRIER_GROUP barrier_group;
    memset(&barrier_group, 0, sizeof(barrier_group));
    barrier_group.Type = D3D12_BARRIER_TYPE_GLOBAL;
    barrier_group.NumBarriers = 1;
    barrier_group.pGlobalBarriers = &global_barrier;

    ID3D12GraphicsCommandList10_Barrier(copy_pass_d3d12->command_list, 1, &barrier_group);
}

static void lrhi_d3d12_copy_pass_copy_texture_to_texture(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
	LRHICopyPassD3D12* copy_pass_d3d12 = (LRHICopyPassD3D12*)copy_pass;
	LRHITextureD3D12* src_texture_d3d12 = (LRHITextureD3D12*)src_texture;
	LRHITextureD3D12* dst_texture_d3d12 = (LRHITextureD3D12*)dst_texture;
	ID3D12GraphicsCommandList* command_list = (ID3D12GraphicsCommandList*)copy_pass_d3d12->command_list;

	if (!copy_pass_d3d12 || !src_texture_d3d12 || !dst_texture_d3d12 || !src_texture_d3d12->resource || !dst_texture_d3d12->resource) {
		lrhi_d3d12_set_error(out_error, "Invalid arguments for copy texture to texture");
		return;
	}

	D3D12_TEXTURE_COPY_LOCATION src_location;
	memset(&src_location, 0, sizeof(src_location));
	src_location.pResource = src_texture_d3d12->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src_location.SubresourceIndex = src_texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? src_mip_level : src_array_layer * src_texture_d3d12->info.mip_levels + src_mip_level;

	D3D12_TEXTURE_COPY_LOCATION dst_location;
	memset(&dst_location, 0, sizeof(dst_location));
	dst_location.pResource = dst_texture_d3d12->resource;
	dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst_location.SubresourceIndex = dst_texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? dst_mip_level : dst_array_layer * dst_texture_d3d12->info.mip_levels + dst_mip_level;

	D3D12_BOX src_box;
	src_box.left = src_region.x;
	src_box.top = src_region.y;
	src_box.front = src_region.z;
	src_box.right = src_region.x + src_region.width;
	src_box.bottom = src_region.y + src_region.height;
	src_box.back = src_region.z + src_region.depth;

	ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, dst_region.x, dst_region.y, dst_region.z, &src_location, &src_box);

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_copy_pass_copy_buffer_to_buffer(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, LRHIBuffer dst_buffer, uint64_t dst_offset, uint64_t size, LRHIError* out_error)
{
	LRHICopyPassD3D12* copy_pass_d3d12 = (LRHICopyPassD3D12*)copy_pass;
	LRHIBufferD3D12* src_buffer_d3d12 = (LRHIBufferD3D12*)src_buffer;
	LRHIBufferD3D12* dst_buffer_d3d12 = (LRHIBufferD3D12*)dst_buffer;
	ID3D12GraphicsCommandList* command_list = (ID3D12GraphicsCommandList*)copy_pass_d3d12->command_list;

	if (!copy_pass_d3d12 || !src_buffer_d3d12 || !dst_buffer_d3d12 || !src_buffer_d3d12->resource || !dst_buffer_d3d12->resource) {
		lrhi_d3d12_set_error(out_error, "Invalid arguments for copy buffer to buffer");
		return;
	}

	ID3D12GraphicsCommandList_CopyBufferRegion(command_list, dst_buffer_d3d12->resource, dst_offset, src_buffer_d3d12->resource, src_offset, size);

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_copy_pass_copy_buffer_to_texture(LRHICopyPass copy_pass, LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bytes_per_row, uint32_t src_bytes_per_image, LRHITexture dst_texture, LRHIRegion dst_region, uint32_t dst_mip_level, uint32_t dst_array_layer, LRHIError* out_error)
{
	LRHICopyPassD3D12* copy_pass_d3d12 = (LRHICopyPassD3D12*)copy_pass;
	LRHIBufferD3D12* src_buffer_d3d12 = (LRHIBufferD3D12*)src_buffer;
	LRHITextureD3D12* dst_texture_d3d12 = (LRHITextureD3D12*)dst_texture;
	ID3D12GraphicsCommandList* command_list = (ID3D12GraphicsCommandList*)copy_pass_d3d12->command_list;

	if (!copy_pass_d3d12 || !src_buffer_d3d12 || !dst_texture_d3d12 || !src_buffer_d3d12->resource || !dst_texture_d3d12->resource) {
		lrhi_d3d12_set_error(out_error, "Invalid arguments for copy buffer to texture");
		return;
	}

	if (src_bytes_per_row == 0) {
		lrhi_d3d12_set_error(out_error, "src_bytes_per_row must be non-zero for copy buffer to texture");
		return;
	}

	(void)src_bytes_per_image;

	D3D12_TEXTURE_COPY_LOCATION src_location;
	memset(&src_location, 0, sizeof(src_location));
	src_location.pResource = src_buffer_d3d12->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src_location.PlacedFootprint.Offset = src_offset;
	src_location.PlacedFootprint.Footprint.Format = lrhi_format_to_dxgi_format(dst_texture_d3d12->info.format);
	src_location.PlacedFootprint.Footprint.Width = dst_region.width;
	src_location.PlacedFootprint.Footprint.Height = dst_region.height;
	src_location.PlacedFootprint.Footprint.Depth = dst_region.depth;
	src_location.PlacedFootprint.Footprint.RowPitch = src_bytes_per_row;

	D3D12_TEXTURE_COPY_LOCATION dst_location;
	memset(&dst_location, 0, sizeof(dst_location));
	dst_location.pResource = dst_texture_d3d12->resource;
	dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst_location.SubresourceIndex = dst_texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? dst_mip_level : dst_array_layer * dst_texture_d3d12->info.mip_levels + dst_mip_level;

	ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, dst_region.x, dst_region.y, dst_region.z, &src_location, NULL);

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_copy_pass_copy_texture_to_buffer(LRHICopyPass copy_pass, LRHITexture src_texture, LRHIRegion src_region, uint32_t src_mip_level, uint32_t src_array_layer, LRHIBuffer dst_buffer, uint64_t dst_offset, uint32_t dst_bytes_per_row, uint32_t dst_bytes_per_image, LRHIError* out_error)
{
	LRHICopyPassD3D12* copy_pass_d3d12 = (LRHICopyPassD3D12*)copy_pass;
	LRHITextureD3D12* src_texture_d3d12 = (LRHITextureD3D12*)src_texture;
	LRHIBufferD3D12* dst_buffer_d3d12 = (LRHIBufferD3D12*)dst_buffer;
	ID3D12GraphicsCommandList* command_list = (ID3D12GraphicsCommandList*)copy_pass_d3d12->command_list;

	if (!copy_pass_d3d12 || !src_texture_d3d12 || !dst_buffer_d3d12 || !src_texture_d3d12->resource || !dst_buffer_d3d12->resource) {
		lrhi_d3d12_set_error(out_error, "Invalid arguments for copy texture to buffer");
		return;
	}

	if (dst_bytes_per_row == 0) {
		lrhi_d3d12_set_error(out_error, "dst_bytes_per_row must be non-zero for copy texture to buffer");
		return;
	}

	(void)dst_bytes_per_image;

	D3D12_TEXTURE_COPY_LOCATION src_location;
	memset(&src_location, 0, sizeof(src_location));
	src_location.pResource = src_texture_d3d12->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src_location.SubresourceIndex = src_texture_d3d12->info.dimensions == LUMINARY_RHI_TEXTURE_DIMENSIONS_3D ? src_mip_level : src_array_layer * src_texture_d3d12->info.mip_levels + src_mip_level;

	D3D12_TEXTURE_COPY_LOCATION dst_location;
	memset(&dst_location, 0, sizeof(dst_location));
	dst_location.pResource = dst_buffer_d3d12->resource;
	dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst_location.PlacedFootprint.Offset = dst_offset;
	dst_location.PlacedFootprint.Footprint.Format = lrhi_format_to_dxgi_format(src_texture_d3d12->info.format);
	dst_location.PlacedFootprint.Footprint.Width = src_region.width;
	dst_location.PlacedFootprint.Footprint.Height = src_region.height;
	dst_location.PlacedFootprint.Footprint.Depth = src_region.depth;
	dst_location.PlacedFootprint.Footprint.RowPitch = dst_bytes_per_row;

	D3D12_BOX src_box;
	src_box.left = src_region.x;
	src_box.top = src_region.y;
	src_box.front = src_region.z;
	src_box.right = src_region.x + src_region.width;
	src_box.bottom = src_region.y + src_region.height;
	src_box.back = src_region.z + src_region.depth;

	ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0, &src_location, &src_box);

	if (out_error) {
		out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_SUCCESS;
		out_error->message[0] = '\0';
	}
}

static void lrhi_d3d12_create_swap_chain(LRHIDevice device, LRHICommandQueue queue, LRHISwapChainInfo* info, LRHISwapChain* out_swap_chain, LRHIError* out_error)
{
	(void)device;
	(void)queue;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_swap_chain");
	*out_swap_chain = NULL;
}

static void lrhi_d3d12_destroy_swap_chain(LRHISwapChain swap_chain)
{
	if (swap_chain) {
		LRHI_FREE(swap_chain);
	}
}

static LRHITexture lrhi_d3d12_swap_chain_get_current_texture(LRHISwapChain swap_chain, LRHIError* out_error)
{
	(void)swap_chain;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_swap_chain_get_current_texture");
	return NULL;
}

static void lrhi_d3d12_swap_chain_present(LRHISwapChain swap_chain, LRHIError* out_error)
{
	(void)swap_chain;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_swap_chain_present");
}

static void lrhi_d3d12_create_texture_view(LRHIDevice device, LRHITextureViewInfo* info, LRHITextureView* out_texture_view, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_texture_view");
	*out_texture_view = NULL;
}

static void lrhi_d3d12_destroy_texture_view(LRHITextureView texture_view)
{
	if (texture_view) {
		LRHI_FREE(texture_view);
	}
}

static void lrhi_d3d12_get_texture_view_info(LRHITextureView texture_view, LRHITextureViewInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (texture_view) {
		*out_info = ((LRHITextureViewD3D12*)texture_view)->info;
	}
}

static uint32_t lrhi_d3d12_texture_view_get_bindless_index(LRHITextureView texture_view, LRHIError* out_error)
{
	(void)texture_view;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_texture_view_get_bindless_index");
	return 0;
}

static LRHIRenderPass lrhi_d3d12_render_pass_begin(LRHICommandList command_list, LRHIRenderPassInfo* info, LRHIError* out_error)
{
	(void)command_list;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_begin");
	return NULL;
}

static void lrhi_d3d12_render_pass_end(LRHIRenderPass render_pass, LRHIError* out_error)
{
	(void)render_pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_end");
}

static void lrhi_d3d12_render_pass_push_debug_group(LRHIRenderPass render_pass, const char* label, LRHIError* out_error)
{
	(void)render_pass;
	(void)label;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_push_debug_group");
}

static void lrhi_d3d12_render_pass_pop_debug_group(LRHIRenderPass render_pass, LRHIError* out_error)
{
	(void)render_pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_pop_debug_group");
}

static void lrhi_d3d12_render_pass_intra_barrier(LRHIRenderPass render_pass, LRHIRenderStage before_stage, LRHIRenderStage after_stage, LRHIError* out_error)
{
	(void)render_pass;
	(void)before_stage;
	(void)after_stage;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_intra_barrier");
}

static void lrhi_d3d12_render_pass_encoder_barrier(LRHIRenderPass render_pass, LRHIRenderStage before_stage, LRHIRenderStage after_stage, LRHIError* out_error)
{
	(void)render_pass;
	(void)before_stage;
	(void)after_stage;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_encoder_barrier");
}

static void lrhi_d3d12_render_pass_set_render_pipeline(LRHIRenderPass render_pass, LRHIRenderPipeline pipeline, LRHIError* out_error)
{
	(void)render_pass;
	(void)pipeline;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_set_render_pipeline");
}

static void lrhi_d3d12_render_pass_set_mesh_pipeline(LRHIRenderPass render_pass, LRHIMeshPipeline pipeline, LRHIError* out_error)
{
	(void)render_pass;
	(void)pipeline;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_set_mesh_pipeline");
}

static void lrhi_d3d12_render_pass_set_viewport(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, float min_depth, float max_depth, LRHIError* out_error)
{
	(void)render_pass;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)min_depth;
	(void)max_depth;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_set_viewport");
}

static void lrhi_d3d12_render_pass_set_scissor(LRHIRenderPass render_pass, uint32_t x, uint32_t y, uint32_t width, uint32_t height, LRHIError* out_error)
{
	(void)render_pass;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_set_scissor");
}

static void lrhi_d3d12_render_pass_set_push_constants(LRHIRenderPass render_pass, const void* data, uint32_t size, LRHIError* out_error)
{
	(void)render_pass;
	(void)data;
	(void)size;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_set_push_constants");
}

static void lrhi_d3d12_render_pass_draw(LRHIRenderPass render_pass, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance, LRHIError* out_error)
{
	(void)render_pass;
	(void)vertex_count;
	(void)instance_count;
	(void)first_vertex;
	(void)first_instance;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_draw");
}

static void lrhi_d3d12_render_pass_draw_indexed(LRHIRenderPass render_pass, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance, LRHIBuffer index_buffer, uint32_t index_stride, LRHIError* out_error)
{
	(void)render_pass;
	(void)index_count;
	(void)instance_count;
	(void)first_index;
	(void)vertex_offset;
	(void)first_instance;
	(void)index_buffer;
	(void)index_stride;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_draw_indexed");
}

static void lrhi_d3d12_render_pass_draw_mesh_tasks(LRHIRenderPass render_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_object_group_x, uint32_t threads_per_object_group_y, uint32_t threads_per_object_group_z, uint32_t threads_per_mesh_group_x, uint32_t threads_per_mesh_group_y, uint32_t threads_per_mesh_group_z, LRHIError* out_error)
{
	(void)render_pass;
	(void)num_groups_x;
	(void)num_groups_y;
	(void)num_groups_z;
	(void)threads_per_object_group_x;
	(void)threads_per_object_group_y;
	(void)threads_per_object_group_z;
	(void)threads_per_mesh_group_x;
	(void)threads_per_mesh_group_y;
	(void)threads_per_mesh_group_z;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_draw_mesh_tasks");
}

static void lrhi_d3d12_render_pass_execute_indirect_commands(LRHIRenderPass render_pass, LRHIBuffer indirect_command_buffer, LRHIBuffer count_buffer, uint64_t max_command_count, LRHIError* out_error)
{
	(void)render_pass;
	(void)indirect_command_buffer;
	(void)count_buffer;
	(void)max_command_count;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pass_execute_indirect_commands");
}

static void lrhi_d3d12_create_shader_module(LRHIDevice device, LRHIShaderModuleInfo* info, LRHIShaderModule* out_shader_module, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_shader_module");
	*out_shader_module = NULL;
}

static void lrhi_d3d12_destroy_shader_module(LRHIShaderModule shader_module)
{
	if (shader_module) {
		LRHI_FREE(shader_module);
	}
}

static void lrhi_d3d12_get_shader_module_info(LRHIShaderModule shader_module, LRHIShaderModuleInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (shader_module) {
		*out_info = ((LRHIShaderModuleD3D12*)shader_module)->info;
	}
}

static void lrhi_d3d12_create_render_pipeline(LRHIDevice device, LRHIRenderPipelineInfo* info, LRHIRenderPipeline* out_pipeline, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_render_pipeline");
	*out_pipeline = NULL;
}

static void lrhi_d3d12_destroy_render_pipeline(LRHIRenderPipeline pipeline)
{
	if (pipeline) {
		LRHI_FREE(pipeline);
	}
}

static void lrhi_d3d12_get_render_pipeline_info(LRHIRenderPipeline pipeline, LRHIRenderPipelineInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (pipeline) {
		*out_info = ((LRHIRenderPipelineD3D12*)pipeline)->info;
	}
}

static uint64_t lrhi_d3d12_render_pipeline_get_alloc_size(LRHIRenderPipeline pipeline, LRHIError* out_error)
{
	(void)pipeline;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_render_pipeline_get_alloc_size");
	return 0;
}

static void lrhi_d3d12_create_mesh_pipeline(LRHIDevice device, LRHIMeshPipelineInfo* info, LRHIMeshPipeline* out_pipeline, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_mesh_pipeline");
	*out_pipeline = NULL;
}

static void lrhi_d3d12_destroy_mesh_pipeline(LRHIMeshPipeline pipeline)
{
	if (pipeline) {
		LRHI_FREE(pipeline);
	}
}

static void lrhi_d3d12_get_mesh_pipeline_info(LRHIMeshPipeline pipeline, LRHIMeshPipelineInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (pipeline) {
		*out_info = ((LRHIMeshPipelineD3D12*)pipeline)->info;
	}
}

static uint64_t lrhi_d3d12_mesh_pipeline_get_alloc_size(LRHIMeshPipeline pipeline, LRHIError* out_error)
{
	(void)pipeline;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_mesh_pipeline_get_alloc_size");
	return 0;
}

static void lrhi_d3d12_create_compute_pipeline(LRHIDevice device, LRHIComputePipelineInfo* info, LRHIComputePipeline* out_pipeline, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_compute_pipeline");
	*out_pipeline = NULL;
}

static void lrhi_d3d12_destroy_compute_pipeline(LRHIComputePipeline pipeline)
{
	if (pipeline) {
		LRHI_FREE(pipeline);
	}
}

static void lrhi_d3d12_get_compute_pipeline_info(LRHIComputePipeline pipeline, LRHIComputePipelineInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (pipeline) {
		*out_info = ((LRHIComputePipelineD3D12*)pipeline)->info;
	}
}

static uint64_t lrhi_d3d12_compute_pipeline_get_alloc_size(LRHIComputePipeline pipeline, LRHIError* out_error)
{
	(void)pipeline;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pipeline_get_alloc_size");
	return 0;
}

static LRHIComputePass lrhi_d3d12_compute_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
	(void)command_list;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_begin");
	return NULL;
}

static void lrhi_d3d12_compute_pass_end(LRHIComputePass compute_pass, LRHIError* out_error)
{
	(void)compute_pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_end");
}

static void lrhi_d3d12_compute_pass_push_debug_group(LRHIComputePass compute_pass, const char* label, LRHIError* out_error)
{
	(void)compute_pass;
	(void)label;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_push_debug_group");
}

static void lrhi_d3d12_compute_pass_pop_debug_group(LRHIComputePass compute_pass, LRHIError* out_error)
{
	(void)compute_pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_pop_debug_group");
}

static void lrhi_d3d12_compute_pass_barrier(LRHIComputePass compute_pass, LRHIError* out_error)
{
	(void)compute_pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_barrier");
}

static void lrhi_d3d12_compute_pass_encoder_barrier(LRHIComputePass compute_pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
	(void)compute_pass;
	(void)after_stage;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_encoder_barrier");
}

static void lrhi_d3d12_compute_pass_set_pipeline(LRHIComputePass compute_pass, LRHIComputePipeline pipeline, LRHIError* out_error)
{
	(void)compute_pass;
	(void)pipeline;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_set_pipeline");
}

static void lrhi_d3d12_compute_pass_set_push_constants(LRHIComputePass compute_pass, const void* data, uint32_t size, LRHIError* out_error)
{
	(void)compute_pass;
	(void)data;
	(void)size;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_set_push_constants");
}

static void lrhi_d3d12_compute_pass_dispatch(LRHIComputePass compute_pass, uint32_t num_groups_x, uint32_t num_groups_y, uint32_t num_groups_z, uint32_t threads_per_group_x, uint32_t threads_per_group_y, uint32_t threads_per_group_z, LRHIError* out_error)
{
	(void)compute_pass;
	(void)num_groups_x;
	(void)num_groups_y;
	(void)num_groups_z;
	(void)threads_per_group_x;
	(void)threads_per_group_y;
	(void)threads_per_group_z;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_dispatch");
}

static void lrhi_d3d12_compute_pass_dispatch_indirect(LRHIComputePass compute_pass, LRHIBuffer indirect_command_buffer, LRHIError* out_error)
{
	(void)compute_pass;
	(void)indirect_command_buffer;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_compute_pass_dispatch_indirect");
}

static void lrhi_d3d12_create_buffer_view(LRHIDevice device, LRHIBufferViewInfo* info, LRHIBufferView* out_buffer_view, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_buffer_view");
	*out_buffer_view = NULL;
}

static void lrhi_d3d12_destroy_buffer_view(LRHIBufferView buffer_view)
{
	if (buffer_view) {
		LRHI_FREE(buffer_view);
	}
}

static void lrhi_d3d12_get_buffer_view_info(LRHIBufferView buffer_view, LRHIBufferViewInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (buffer_view) {
		*out_info = ((LRHIBufferViewD3D12*)buffer_view)->info;
	}
}

static uint32_t lrhi_d3d12_buffer_view_get_bindless_index(LRHIBufferView buffer_view, LRHIError* out_error)
{
	(void)buffer_view;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_buffer_view_get_bindless_index");
	return 0;
}

static void lrhi_d3d12_create_sampler(LRHIDevice device, LRHISamplerInfo* info, LRHISampler* out_sampler, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_sampler");
	*out_sampler = NULL;
}

static void lrhi_d3d12_destroy_sampler(LRHISampler sampler)
{
	if (sampler) {
		LRHI_FREE(sampler);
	}
}

static void lrhi_d3d12_get_sampler_info(LRHISampler sampler, LRHISamplerInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (sampler) {
		*out_info = ((LRHISamplerD3D12*)sampler)->info;
	}
}

static uint32_t lrhi_d3d12_sampler_get_bindless_index(LRHISampler sampler, LRHIError* out_error)
{
	(void)sampler;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_sampler_get_bindless_index");
	return 0;
}

static LRHIAccelerationStructurePass lrhi_d3d12_acceleration_structure_pass_begin(LRHICommandList command_list, LRHIError* out_error)
{
	(void)command_list;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_begin");
	return NULL;
}

static void lrhi_d3d12_acceleration_structure_pass_end(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
	(void)pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_end");
}

static void lrhi_d3d12_acceleration_structure_pass_push_debug_group(LRHIAccelerationStructurePass pass, const char* label, LRHIError* out_error)
{
	(void)pass;
	(void)label;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_push_debug_group");
}

static void lrhi_d3d12_acceleration_structure_pass_pop_debug_group(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
	(void)pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_pop_debug_group");
}

static void lrhi_d3d12_acceleration_structure_pass_barrier(LRHIAccelerationStructurePass pass, LRHIError* out_error)
{
	(void)pass;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_barrier");
}

static void lrhi_d3d12_acceleration_structure_pass_encoder_barrier(LRHIAccelerationStructurePass pass, LRHIRenderStage after_stage, LRHIError* out_error)
{
	(void)pass;
	(void)after_stage;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_encoder_barrier");
}

static void lrhi_d3d12_acceleration_structure_pass_build_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
	(void)pass;
	(void)blas;
	(void)scratch_buffer;
	(void)scratch_offset;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_build_blas");
}

static void lrhi_d3d12_acceleration_structure_pass_build_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
	(void)pass;
	(void)tlas;
	(void)scratch_buffer;
	(void)scratch_offset;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_build_tlas");
}

static void lrhi_d3d12_acceleration_structure_pass_write_compacted_blas_size(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer dst_buffer, uint64_t dst_offset, LRHIError* out_error)
{
	(void)pass;
	(void)blas;
	(void)dst_buffer;
	(void)dst_offset;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_write_compacted_blas_size");
}

static void lrhi_d3d12_acceleration_structure_pass_compact_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
	(void)pass;
	(void)src_blas;
	(void)dst_blas;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_compact_blas");
}

static void lrhi_d3d12_acceleration_structure_pass_refit_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure blas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
	(void)pass;
	(void)blas;
	(void)scratch_buffer;
	(void)scratch_offset;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_refit_blas");
}

static void lrhi_d3d12_acceleration_structure_pass_refit_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure tlas, LRHIBuffer scratch_buffer, uint64_t scratch_offset, LRHIError* out_error)
{
	(void)pass;
	(void)tlas;
	(void)scratch_buffer;
	(void)scratch_offset;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_refit_tlas");
}

static void lrhi_d3d12_acceleration_structure_pass_copy_blas(LRHIAccelerationStructurePass pass, LRHIBottomLevelAccelerationStructure src_blas, LRHIBottomLevelAccelerationStructure dst_blas, LRHIError* out_error)
{
	(void)pass;
	(void)src_blas;
	(void)dst_blas;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_copy_blas");
}

static void lrhi_d3d12_acceleration_structure_pass_copy_tlas(LRHIAccelerationStructurePass pass, LRHITopLevelAccelerationStructure src_tlas, LRHITopLevelAccelerationStructure dst_tlas, LRHIError* out_error)
{
	(void)pass;
	(void)src_tlas;
	(void)dst_tlas;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_acceleration_structure_pass_copy_tlas");
}

static void lrhi_d3d12_create_bottom_level_acceleration_structure(LRHIDevice device, LRHIBLASInfo* info, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_bottom_level_acceleration_structure");
	*out_blas = NULL;
}

static void lrhi_d3d12_create_compacted_bottom_level_acceleration_structure(LRHIDevice device, uint64_t compacted_size, LRHIBottomLevelAccelerationStructure* out_blas, LRHIError* out_error)
{
	(void)device;
	(void)compacted_size;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_compacted_bottom_level_acceleration_structure");
	*out_blas = NULL;
}

static void lrhi_d3d12_destroy_bottom_level_acceleration_structure(LRHIBottomLevelAccelerationStructure blas)
{
	if (blas) {
		LRHI_FREE(blas);
	}
}

static void lrhi_d3d12_get_bottom_level_acceleration_structure_info(LRHIBottomLevelAccelerationStructure blas, LRHIBLASInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (blas) {
		*out_info = ((LRHIBLASD3D12*)blas)->info;
	}
}

static LRHIAccelerationStructureBufferSizes lrhi_d3d12_bottom_level_acceleration_structure_get_build_scratch_size(LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
	LRHIAccelerationStructureBufferSizes sizes;
	(void)blas;
	memset(&sizes, 0, sizeof(sizes));
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_bottom_level_acceleration_structure_get_build_scratch_size");
	return sizes;
}

static void lrhi_d3d12_create_top_level_acceleration_structure(LRHIDevice device, LRHITLASInfo* info, LRHITopLevelAccelerationStructure* out_tlas, LRHIError* out_error)
{
	(void)device;
	(void)info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_create_top_level_acceleration_structure");
	*out_tlas = NULL;
}

static void lrhi_d3d12_destroy_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas)
{
	if (tlas) {
		LRHI_FREE(tlas);
	}
}

static void lrhi_d3d12_get_top_level_acceleration_structure_info(LRHITopLevelAccelerationStructure tlas, LRHITLASInfo* out_info)
{
	memset(out_info, 0, sizeof(*out_info));
	if (tlas) {
		*out_info = ((LRHITLASD3D12*)tlas)->info;
	}
}

static uint64_t lrhi_d3d12_top_level_acceleration_structure_get_bindless_index(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
	(void)tlas;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_top_level_acceleration_structure_get_bindless_index");
	return 0;
}

static LRHIAccelerationStructureBufferSizes lrhi_d3d12_top_level_acceleration_structure_get_build_scratch_size(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
	LRHIAccelerationStructureBufferSizes sizes;
	(void)tlas;
	memset(&sizes, 0, sizeof(sizes));
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_top_level_acceleration_structure_get_build_scratch_size");
	return sizes;
}

static void lrhi_d3d12_reset_top_level_acceleration_structure(LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
	(void)tlas;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_reset_top_level_acceleration_structure");
}

static void lrhi_d3d12_add_top_level_acceleration_structure_instance(LRHITopLevelAccelerationStructure tlas, LRHITLASInstanceInfo* instance_info, LRHIError* out_error)
{
	(void)tlas;
	(void)instance_info;
	lrhi_d3d12_set_not_implemented(out_error, "lrhi_add_top_level_acceleration_structure_instance");
}

void lrhi_d3d12_create_device(LRHIDevice* out_device, uint8_t enable_debug, LRHIError* out_error)
{
    // Create factory
	IDXGIFactory1* temp_factory = NULL;
	IDXGIFactory6* factory6 = NULL;
	HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&temp_factory);
    if (hr_to_lrhi_error(hr, out_error, "Failed to create DXGI Factory")) {
        *out_device = NULL;
        return;
    }
	IDXGIFactory1_QueryInterface(temp_factory, &IID_IDXGIFactory6, (void**)&factory6);

    // Create debug interface if enabled
    ID3D12Debug1* debug_controller = NULL;
    if (enable_debug) {
		ID3D12Debug* debug = NULL;
		hr = D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug);
        if (hr_to_lrhi_error(hr, out_error, "Failed to get D3D12 debug interface")) {
            *out_device = NULL;
            return;
        }
		ID3D12Debug_QueryInterface(debug, &IID_ID3D12Debug1, (void**)&debug_controller);
		ID3D12Debug_Release(debug);

		ID3D12Debug1_EnableDebugLayer(debug_controller);
		ID3D12Debug1_SetEnableGPUBasedValidation(debug_controller, TRUE);
    }

    // Iterate through adapters
    IDXGIAdapter1* adapter = NULL;
	if (factory6) {
		for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != IDXGIFactory6_EnumAdapterByGpuPreference(factory6, adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, (void**)&adapter); ++adapter_index) {
        DXGI_ADAPTER_DESC1 desc;
			IDXGIAdapter1_GetDesc1(adapter, &desc);

            // Only pick discrete adapters that support D3D12
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				IDXGIAdapter1_Release(adapter);
                continue;
            }

            // Check if adapter supports D3D12
			ID3D12Device* test_device = NULL;
			hr = D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&test_device);
            if (SUCCEEDED(hr)) {
				ID3D12Device_Release(test_device);
                break;
            }

			IDXGIAdapter1_Release(adapter);
            adapter = NULL;
        }
	}

	if (!adapter) {
		for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != IDXGIFactory1_EnumAdapters1(temp_factory, adapter_index, &adapter); ++adapter_index) {
        DXGI_ADAPTER_DESC1 desc;
		IDXGIAdapter1_GetDesc1(adapter, &desc);

        // Only pick discrete adapters that support D3D12
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			IDXGIAdapter1_Release(adapter);
            continue;
        }

        // Check if adapter supports D3D12
		ID3D12Device* test_device = NULL;
		hr = D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&test_device);
        if (SUCCEEDED(hr)) {
			ID3D12Device_Release(test_device);
            break;
        }

		IDXGIAdapter1_Release(adapter);
        adapter = NULL;
    }
	}

	if (!adapter) {
		if (out_error) {
			out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
			snprintf(out_error->message, sizeof(out_error->message), "Failed to find a compatible D3D12 adapter after checking high-performance and fallback DXGI enumerations");
		}
		if (factory6) {
			IDXGIFactory6_Release(factory6);
		}
		*out_device = NULL;
		return;
	}

	ID3D12Device* d3d12_device = NULL;
	hr = D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&d3d12_device);
	if (hr_to_lrhi_error(hr, out_error, "Failed to create D3D12 device")) {
		if (factory6) {
			IDXGIFactory6_Release(factory6);
		}
		*out_device = NULL;
		return;
	}

	uint8_t has_rt = 0;
	uint8_t has_ms = 0;
	uint8_t has_mdi = 0;
    if (adapter) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 raytracingData;
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 meshShaderData;
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 enhancedBarriers;

		ID3D12Device_CheckFeatureSupport(d3d12_device, D3D12_FEATURE_D3D12_OPTIONS5, &raytracingData, sizeof(raytracingData));
		ID3D12Device_CheckFeatureSupport(d3d12_device, D3D12_FEATURE_D3D12_OPTIONS7, &meshShaderData, sizeof(meshShaderData));
		ID3D12Device_CheckFeatureSupport(d3d12_device, D3D12_FEATURE_D3D12_OPTIONS12, &enhancedBarriers, sizeof(enhancedBarriers));

        if (raytracingData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            has_rt = 1;
        } else {
            has_rt = 0;
        }

        if (meshShaderData.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
            has_ms = 1;
        } else {
            has_ms = 0;
        }

        if (enhancedBarriers.EnhancedBarriersSupported == TRUE) {
            has_mdi = 1;
        } else {
            has_mdi = 0;
        }
    }

    DXGI_ADAPTER_DESC1 desc;
	IDXGIAdapter1_GetDesc1(adapter, &desc);

    LRHIDeviceInfo device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.backend = LUMINARY_RHI_BACKEND_D3D12;
    wstring_to_cstring(desc.Description, device_info.device_name, sizeof(device_info.device_name));
    device_info.features.bindless_resources = 1; // forced;
    device_info.features.mesh_shading = has_ms;
    device_info.features.multi_draw_indirect = has_mdi;
    device_info.features.ray_tracing = has_rt;
    device_info.limits.max_texture_dimension_2d = 16384;
    device_info.limits.max_texture_dimension_3d = 2048;
    device_info.limits.max_texture_array_layers = 2048;
    device_info.limits.max_buffer_size = 8 * 1024 * 1024 * 1024; // 8 gb

    // Create info queue
    if (enable_debug) {
		ID3D12InfoQueue* info_queue = NULL;
		hr = ID3D12Device_QueryInterface(d3d12_device, &IID_ID3D12InfoQueue, (void**)&info_queue);
        if (SUCCEEDED(hr)) {
            // Suppress info messages and warnings about shader compilation, as they are often not actionable and can be noisy
			ID3D12InfoQueue_SetBreakOnSeverity(info_queue, D3D12_MESSAGE_SEVERITY_INFO, FALSE);
			ID3D12InfoQueue_SetBreakOnSeverity(info_queue, D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

            D3D12_MESSAGE_SEVERITY supressSeverities[] = {
                D3D12_MESSAGE_SEVERITY_INFO
            };
            D3D12_MESSAGE_ID supressIDs[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            };

            D3D12_INFO_QUEUE_FILTER filter = {0};
            filter.DenyList.NumSeverities = ARRAYSIZE(supressSeverities);
            filter.DenyList.pSeverityList = supressSeverities;
            filter.DenyList.NumIDs = ARRAYSIZE(supressIDs);
            filter.DenyList.pIDList = supressIDs;

			ID3D12InfoQueue_PushStorageFilter(info_queue, &filter);
			ID3D12InfoQueue_SetBreakOnSeverity(info_queue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE);

			ID3D12InfoQueue_Release(info_queue);
        }
    }

    // TODO: Create bindless manager for descriptor heaps and whatnot

    // Create global root signature
    ID3D12RootSignature* global_root_signature;
    {
        D3D12_ROOT_PARAMETER1 root_parameters[2];
        memset(root_parameters, 0, sizeof(root_parameters));
        // Push constants
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[0].Constants.Num32BitValues = 128 / sizeof(uint32_t);
        root_parameters[0].Constants.RegisterSpace = 0;
        root_parameters[0].Constants.ShaderRegister = 0;
        // Draw ID
        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[1].Constants.Num32BitValues = 1;
        root_parameters[1].Constants.RegisterSpace = 0;
        root_parameters[1].Constants.ShaderRegister = 1;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
        memset(&root_signature_desc, 0, sizeof(root_signature_desc));
        root_signature_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        root_signature_desc.Desc_1_1.NumParameters = ARRAYSIZE(root_parameters);
        root_signature_desc.Desc_1_1.pParameters = root_parameters;
        root_signature_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                             D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

        ID3DBlob* serialized_root_signature;
        ID3DBlob* error_blob;
        hr = D3D12SerializeVersionedRootSignature(&root_signature_desc, &serialized_root_signature, &error_blob);
        if (hr_to_lrhi_error(hr, out_error, "Failed to serialize root signature")) {
            *out_device = NULL;
            return;
        }

		hr = ID3D12Device_CreateRootSignature(d3d12_device, 0, ID3D10Blob_GetBufferPointer(serialized_root_signature), ID3D10Blob_GetBufferSize(serialized_root_signature), &IID_ID3D12RootSignature, (void**)&global_root_signature);
		ID3D10Blob_Release(serialized_root_signature);
    }

    // Create command sigantures
    ID3D12CommandSignature* draw_indirect_command_signature;
    ID3D12CommandSignature* draw_indexed_indirect_command_signature;
    ID3D12CommandSignature* dispatch_indirect_command_signature;
	ID3D12CommandSignature* mesh_tasks_indirect_command_signature = NULL;
    {
        D3D12_INDIRECT_ARGUMENT_DESC draw_desc;
        memset(&draw_desc, 0, sizeof(draw_desc));
        draw_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_INDIRECT_ARGUMENT_DESC id_desc;
        memset(&id_desc, 0, sizeof(id_desc));
        id_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        id_desc.Constant.DestOffsetIn32BitValues = 0;
        id_desc.Constant.Num32BitValuesToSet = 1;
        id_desc.Constant.RootParameterIndex = 1;

        D3D12_INDIRECT_ARGUMENT_DESC descs[] = { id_desc, draw_desc };

        D3D12_COMMAND_SIGNATURE_DESC signature_desc;
        memset(&signature_desc, 0, sizeof(signature_desc));
        signature_desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS) + sizeof(uint32_t);
        signature_desc.NumArgumentDescs = 2;
        signature_desc.pArgumentDescs = descs;

		hr = ID3D12Device_CreateCommandSignature(d3d12_device, &signature_desc, global_root_signature, &IID_ID3D12CommandSignature, (void**)&draw_indirect_command_signature);
        if (hr_to_lrhi_error(hr, out_error, "Failed to create draw indirect command signature")) {
            *out_device = NULL;
            return;
        }
    }

    {
        // Draw indexed
        D3D12_INDIRECT_ARGUMENT_DESC draw_indexed_desc;
        memset(&draw_indexed_desc, 0, sizeof(draw_indexed_desc));
        draw_indexed_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_INDIRECT_ARGUMENT_DESC id_desc;
        memset(&id_desc, 0, sizeof(id_desc));
        id_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        id_desc.Constant.DestOffsetIn32BitValues = 0;
        id_desc.Constant.Num32BitValuesToSet = 1;
        id_desc.Constant.RootParameterIndex = 1;

        D3D12_INDIRECT_ARGUMENT_DESC descs[] = { id_desc, draw_indexed_desc };

        D3D12_COMMAND_SIGNATURE_DESC signature_desc;
        memset(&signature_desc, 0, sizeof(signature_desc));
        signature_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + sizeof(uint32_t);
        signature_desc.NumArgumentDescs = 2;
        signature_desc.pArgumentDescs = descs;

		hr = ID3D12Device_CreateCommandSignature(d3d12_device, &signature_desc, global_root_signature, &IID_ID3D12CommandSignature, (void**)&draw_indexed_indirect_command_signature);
        if (hr_to_lrhi_error(hr, out_error, "Failed to create draw indexed indirect command signature")) {
            *out_device = NULL;
            return;
        }
    }

    // Dispatch
    {
        D3D12_INDIRECT_ARGUMENT_DESC dispatch_desc;
        memset(&dispatch_desc, 0, sizeof(dispatch_desc));
        dispatch_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC signature_desc;
        memset(&signature_desc, 0, sizeof(signature_desc));
        signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
        signature_desc.NumArgumentDescs = 1;
        signature_desc.pArgumentDescs = &dispatch_desc;

		hr = ID3D12Device_CreateCommandSignature(d3d12_device, &signature_desc, NULL, &IID_ID3D12CommandSignature, (void**)&dispatch_indirect_command_signature);
        if (hr_to_lrhi_error(hr, out_error, "Failed to create dispatch indirect command signature")) {
            *out_device = NULL;
            return;
        }
    }

    // Draw mesh tasks
    if (has_ms) {
        D3D12_INDIRECT_ARGUMENT_DESC mesh_tasks_desc;
        memset(&mesh_tasks_desc, 0, sizeof(mesh_tasks_desc));
        mesh_tasks_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        
        D3D12_INDIRECT_ARGUMENT_DESC id_desc;
        memset(&id_desc, 0, sizeof(id_desc));
        id_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        id_desc.Constant.DestOffsetIn32BitValues = 0;
        id_desc.Constant.Num32BitValuesToSet = 1;
        id_desc.Constant.RootParameterIndex = 1;

        D3D12_INDIRECT_ARGUMENT_DESC descs[] = { id_desc, mesh_tasks_desc };

        D3D12_COMMAND_SIGNATURE_DESC signature_desc;
        memset(&signature_desc, 0, sizeof(signature_desc));
        signature_desc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS) + sizeof(uint32_t);
        signature_desc.NumArgumentDescs = 2;
        signature_desc.pArgumentDescs = descs;

		hr = ID3D12Device_CreateCommandSignature(d3d12_device, &signature_desc, global_root_signature, &IID_ID3D12CommandSignature, (void**)&mesh_tasks_indirect_command_signature);
        if (hr_to_lrhi_error(hr, out_error, "Failed to create mesh tasks indirect command signature")) {
            *out_device = NULL;
            return;
        }
    }

	LRHIDeviceD3D12* device = (LRHIDeviceD3D12*)LRHI_CALLOC(1, sizeof(LRHIDeviceD3D12));
	if (!device) {
		if (out_error) {
			snprintf(out_error->message, sizeof(out_error->message), "Failed to allocate D3D12 device stub");
			out_error->severity = LUMINARY_RHI_ERROR_SEVERITY_ERROR;
		}
		*out_device = NULL;
		return;
	}

	device->base.vtable = &lrhi_d3d12_device_vtable;
	device->enable_debug = enable_debug;
    device->adapter = adapter;
    device->debug_controller = debug_controller;
    device->device = d3d12_device;
    device->dispatch_indirect_command_signature = dispatch_indirect_command_signature;
    device->draw_indexed_indirect_command_signature = draw_indexed_indirect_command_signature;
    device->draw_indirect_command_signature = draw_indirect_command_signature;
	device->factory = temp_factory;
    device->global_root_signature = global_root_signature;
    device->mesh_draw_indirect_command_signature = mesh_tasks_indirect_command_signature;
	device->info = device_info;
	if (factory6) {
		IDXGIFactory6_Release(factory6);
	}

	lrhi_d3d12_bindless_manager_init(device, out_error);

	*out_device = (LRHIDevice)device;
}

// Utils

static DXGI_FORMAT lrhi_format_to_dxgi_format(LRHITextureFormat format)
{
    switch (format)
    {
        case LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED:           return DXGI_FORMAT_UNKNOWN;
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM:      return DXGI_FORMAT_B8G8R8A8_UNORM;
        case LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case LUMINARY_RHI_TEXTURE_FORMAT_R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case LUMINARY_RHI_TEXTURE_FORMAT_D24_UNORM_S8_UINT:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT:   return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC1_UNORM:           return DXGI_FORMAT_BC1_UNORM;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC3_UNORM:           return DXGI_FORMAT_BC3_UNORM;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC7_UNORM:           return DXGI_FORMAT_BC7_UNORM;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC1_SRGB:            return DXGI_FORMAT_BC1_UNORM_SRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC3_SRGB:            return DXGI_FORMAT_BC3_UNORM_SRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_BC7_SRGB:            return DXGI_FORMAT_BC7_UNORM_SRGB;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_UNORM:      return DXGI_FORMAT_UNKNOWN;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_UNORM:      return DXGI_FORMAT_UNKNOWN;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_UNORM:      return DXGI_FORMAT_UNKNOWN;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_4x4_SRGB:       return DXGI_FORMAT_UNKNOWN;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_6x6_SRGB:       return DXGI_FORMAT_UNKNOWN;
        case LUMINARY_RHI_TEXTURE_FORMAT_ASTC_8x8_SRGB:       return DXGI_FORMAT_UNKNOWN;
        default:                                              return DXGI_FORMAT_UNKNOWN;
    }
    return 0;
}

static D3D12_RESOURCE_DIMENSION lrhi_texture_dimension_to_d3d12_resource_dimension(LRHITextureDimensions dimensions)
{
    switch (dimensions)
    {
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_1D: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_2D: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        case LUMINARY_RHI_TEXTURE_DIMENSIONS_3D: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        default: return D3D12_RESOURCE_DIMENSION_UNKNOWN;
    }
    return 0;
}

static D3D12_BARRIER_SYNC lrhi_pipeline_stage_to_d3d12_barrier_sync(LRHIRenderStage stage)
{
    switch (stage)
    {
        case LUMINARY_RHI_RENDER_STAGE_VERTEX: return D3D12_BARRIER_SYNC_VERTEX_SHADING;
        case LUMINARY_RHI_RENDER_STAGE_FRAGMENT: return D3D12_BARRIER_SYNC_PIXEL_SHADING;
        case LUMINARY_RHI_RENDER_STAGE_COMPUTE: return D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        case LUMINARY_RHI_RENDER_STAGE_COPY: return D3D12_BARRIER_SYNC_COPY;
        case LUMINARY_RHI_RENDER_STAGE_MESH: return D3D12_BARRIER_SYNC_ALL_SHADING;
        case LUMINARY_RHI_RENDER_STAGE_TASK: return D3D12_BARRIER_SYNC_ALL_SHADING;
        case LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD: return D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO;
    }
    return 0;
}

static D3D12_BARRIER_ACCESS lrhi_pipeline_usage_to_d3d12_barrier_access(LRHIRenderStage usage)
{
    switch (usage)
    {
        case LUMINARY_RHI_RENDER_STAGE_VERTEX: return D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_INDEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        case LUMINARY_RHI_RENDER_STAGE_FRAGMENT: return D3D12_BARRIER_ACCESS_RENDER_TARGET | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE | D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        case LUMINARY_RHI_RENDER_STAGE_COMPUTE: return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER | D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        case LUMINARY_RHI_RENDER_STAGE_COPY: return D3D12_BARRIER_ACCESS_COPY_SOURCE | D3D12_BARRIER_ACCESS_COPY_DEST;
        case LUMINARY_RHI_RENDER_STAGE_MESH: return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        case LUMINARY_RHI_RENDER_STAGE_TASK: return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        case LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD: return D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ | D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
        default: return 0;
    }
}

static void lrhi_d3d12_create_residency_set(LRHIDevice device, LRHIResidencySet* out_residency_set, LRHIError* out_error)
{
	// Residency set is a no-op on D3D12, just return object with vtable and thats it
    (void)device;
    (void)out_error;

	LRHIResidencySetD3D12* residency_set = (LRHIResidencySetD3D12*)LRHI_MALLOC(sizeof(LRHIResidencySetD3D12));
    residency_set->base.vtable = &lrhi_d3d12_residency_set_vtable;
    *out_residency_set = (LRHIResidencySet)residency_set;
}

static void lrhi_d3d12_destroy_residency_set(LRHIResidencySet residency_set)
{
	(void)residency_set;
}

static void lrhi_d3d12_residency_set_add_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
	(void)residency_set;
	(void)texture;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_add_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
	(void)residency_set;
	(void)buffer;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_add_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
	(void)residency_set;
	(void)blas;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_add_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
	(void)residency_set;
	(void)tlas;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_remove_texture(LRHIResidencySet residency_set, LRHITexture texture, LRHIError* out_error)
{
	(void)residency_set;
	(void)texture;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_remove_buffer(LRHIResidencySet residency_set, LRHIBuffer buffer, LRHIError* out_error)
{
	(void)residency_set;
	(void)buffer;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_remove_blas(LRHIResidencySet residency_set, LRHIBottomLevelAccelerationStructure blas, LRHIError* out_error)
{
	(void)residency_set;
	(void)blas;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_remove_tlas(LRHIResidencySet residency_set, LRHITopLevelAccelerationStructure tlas, LRHIError* out_error)
{
	(void)residency_set;
	(void)tlas;
	(void)out_error;
}

static void lrhi_d3d12_residency_set_update(LRHIResidencySet residency_set, LRHIError* out_error)
{
	(void)residency_set;
	(void)out_error;
}
