#pragma once

#include "example.h"
#include "model_loader.h"

#include <string>
#include <vector>

class PbrGltfExample : public Example
{
public:
    PbrGltfExample(LRHIDevice device, LRHITextureFormat render_target_format);
    ~PbrGltfExample() override;

    const char* name() const override;
    bool is_ready() const override;
    void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) override;
    void draw_ui() override;

private:
    struct GpuBuffer
    {
        LRHIBuffer buffer;
        LRHIBufferView view;
        uint32_t bindless_index;
    };

    struct GpuTexture
    {
        LRHITexture texture;
        LRHITextureView view;
        uint32_t bindless_index;
    };

    bool create_shader_pipeline();
    bool create_sampler();
    bool create_fallback_textures();
    bool create_single_pixel_texture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb, const char* debug_name, GpuTexture& out_texture);
    bool create_depth_target(int width, int height, LRHIResidencySet residency_set);
    void destroy_depth_target();

    bool load_model_from_path(const char* path, bool skinned_loader);
    bool upload_mesh_result(const Mesh_Result& mesh_result);
    void clear_loaded_model_gpu();

    uint32_t texture_handle_from_slot(const MaterialTextureSlot& slot, uint32_t fallback_handle) const;

    LRHIDevice device;
    LRHITextureFormat render_target_format;

    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;
    LRHIRenderPipeline pipeline;
    LRHISampler sampler;
    uint32_t sampler_bindless_index;

    LRHITexture depth_texture;
    LRHITextureView depth_view;
    uint32_t depth_width;
    uint32_t depth_height;
    bool depth_added_to_residency;

    GpuTexture fallback_white;
    GpuTexture fallback_black;
    GpuTexture fallback_normal;
    GpuTexture fallback_orm;
    bool fallbacks_added_to_residency;

    std::vector<GpuBuffer> gpu_buffers;
    std::vector<GpuTexture> gpu_textures;
    Mesh_Result loaded_mesh;
    bool mesh_loaded;
    bool mesh_resources_added_to_residency;

    std::string status_message;
    char model_path[512];
    bool use_skinned_loader;

    float camera_yaw;
    float camera_pitch;
    float camera_position[3];
    float camera_move_speed;
    float camera_look_sensitivity;
    float exposure;
};
