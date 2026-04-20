#pragma once

#include "example.h"

class VolumetricsExample : public Example
{
public:
    VolumetricsExample(LRHIDevice device, LRHITextureFormat render_target_format);
    ~VolumetricsExample() override;

    const char* name() const override;
    bool is_ready() const override;
    void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) override;
    void draw_ui() override;

private:
    bool ensure_residency(LRHIResidencySet residency_set);

    LRHIDevice device;
    LRHITextureFormat render_target_format;

    LRHIShaderModule compute_shader;
    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;

    LRHIComputePipeline compute_pipeline;
    LRHIRenderPipeline render_pipeline;

    LRHITexture volume_texture;
    LRHITextureView volume_storage_view;
    LRHITextureView volume_sampled_view;
    LRHISampler volume_sampler;

    bool resources_added_to_residency;
    bool noise_initialized;
    bool regenerate_requested;

    uint32_t volume_storage_handle;
    uint32_t volume_sampled_handle;
    uint32_t sampler_handle;

    uint32_t volume_resolution;
    uint32_t max_steps;
    float density_multiplier;
    float absorption;
    float step_scale;
    bool animate_noise;
    float wind_speed;

    float camera_yaw;
    float camera_pitch;
    float camera_distance;
};
