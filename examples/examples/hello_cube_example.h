#pragma once

#include "example.h"

class HelloCubeExample : public Example
{
public:
    HelloCubeExample(LRHIDevice device, LRHITextureFormat render_target_format);
    ~HelloCubeExample() override;

    const char* name() const override;
    bool is_ready() const override;
    void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) override;
    void draw_ui() override;

private:
    bool ensure_depth_target(int width, int height, LRHIResidencySet residency_set);
    bool ensure_texture_residency(LRHIResidencySet residency_set);
    void destroy_depth_target();

    LRHIDevice device;
    LRHITextureFormat render_target_format;
    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;
    LRHIRenderPipeline pipeline;
    LRHISampler sampler;
    LRHITexture texture;
    LRHITextureView texture_view;
    LRHITexture depth_texture;
    LRHITextureView depth_view;
    bool texture_added_to_residency;
    bool depth_added_to_residency;
    uint32_t depth_width;
    uint32_t depth_height;
    uint32_t texture_width;
    uint32_t texture_height;
    uint32_t texture_bindless_index;
    uint32_t sampler_bindless_index;
};