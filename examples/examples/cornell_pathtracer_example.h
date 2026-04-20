#pragma once

#include "example.h"

class CornellPathtracerExample : public Example
{
public:
    CornellPathtracerExample(LRHIDevice device, LRHITextureFormat render_target_format);
    ~CornellPathtracerExample() override;

    const char* name() const override;
    bool is_ready() const override;
    void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) override;
    void draw_ui() override;

private:
    struct Vertex {
        float x;
        float y;
        float z;
    };

    bool create_scene_geometry();
    bool create_raytracing_structures();
    bool create_pipelines();
    bool create_history_targets(uint32_t width, uint32_t height, LRHIResidencySet residency_set);
    void destroy_history_targets();
    bool ensure_residency(LRHIResidencySet residency_set);
    void reset_accumulation();

    LRHIDevice device;
    LRHITextureFormat render_target_format;

    LRHIShaderModule trace_compute_shader;
    LRHIShaderModule tonemap_vertex_shader;
    LRHIShaderModule tonemap_fragment_shader;

    LRHIComputePipeline trace_pipeline;
    LRHIRenderPipeline tonemap_pipeline;

    LRHIBuffer vertex_buffer;
    LRHIBuffer index_buffer;
    LRHIBuffer scratch_buffer;

    LRHIBottomLevelAccelerationStructure blas;
    LRHITopLevelAccelerationStructure tlas;

    LRHITexture accum_textures[2];
    LRHITexture prev_textures[2];
    LRHITextureView accum_storage_views[2];
    LRHITextureView accum_sampled_views[2];
    LRHITextureView prev_storage_views[2];
    LRHITextureView prev_sampled_views[2];

    LRHISampler linear_sampler;

    uint32_t accum_storage_handles[2];
    uint32_t accum_sampled_handles[2];
    uint32_t prev_storage_handles[2];
    uint32_t prev_sampled_handles[2];
    uint32_t sampler_handle;
    uint32_t tlas_handle;

    uint32_t vertex_count;
    uint32_t index_count;
    uint64_t blas_scratch_size_aligned;

    uint32_t trace_width;
    uint32_t trace_height;
    int active_history_index;

    bool static_resources_added_to_residency;
    bool history_resources_added_to_residency;
    bool acceleration_structures_built;

    uint32_t accumulation_samples;
    bool force_reset_accumulation;

    bool half_resolution;
    int max_bounces;
    int samples_per_frame;
    float exposure;
    int tonemap_mode;

    float camera_yaw;
    float camera_pitch;
    float camera_distance;
    bool show_help;
};
