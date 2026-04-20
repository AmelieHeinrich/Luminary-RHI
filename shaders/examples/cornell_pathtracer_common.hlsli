#ifndef CORNELL_PATHTRACER_COMMON_HLSLI
#define CORNELL_PATHTRACER_COMMON_HLSLI

struct TraceConstants
{
    ResourceHandle output_accum;
    ResourceHandle input_accum;
    ResourceHandle output_prev;
    ResourceHandle input_prev;

    ResourceHandle tlas;
    uint width;
    uint height;
    uint accumulated_samples;

    uint max_bounces;
    uint frame_seed;
    uint flags;
    uint _pad0;

    float4 camera_position;
    float4 camera_forward;
    float4 camera_right;
    float4 camera_up_tan;

    float camera_aspect;
    float3 _padding0;
};

struct TonemapConstants
{
    ResourceHandle accum_texture;
    ResourceHandle sampler;
    float exposure;
    uint tonemap_mode;
};

static const uint TRACE_FLAG_FORCE_RESET = 1u;

#endif
