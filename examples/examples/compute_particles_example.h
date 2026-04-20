#pragma once

#include "example.h"

class ComputeParticlesExample : public Example
{
public:
    ComputeParticlesExample(LRHIDevice device, LRHITextureFormat render_target_format);
    ~ComputeParticlesExample() override;

    const char* name() const override;
    bool is_ready() const override;
    void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) override;
    void draw_ui() override;

private:
    bool ensure_particle_residency(LRHIResidencySet residency_set);

    LRHIDevice device;
    LRHITextureFormat render_target_format;

    LRHIShaderModule compute_shader;
    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;

    LRHIComputePipeline compute_pipeline;
    LRHIRenderPipeline render_pipeline;

    LRHIBuffer particle_buffer;
    LRHIBufferView particle_rw_view;
    LRHIBufferView particle_read_view;

    bool particles_added_to_residency;

    uint32_t particle_rw_handle;
    uint32_t particle_read_handle;

    float camera_yaw;
    float camera_pitch;
    float camera_distance;

    float simulation_speed;
    float point_scale;
    bool paused;
    bool show_help;

    double last_frame_time;
};
