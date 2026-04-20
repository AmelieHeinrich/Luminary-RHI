#include "shaders/common/LuminaryRHI.hlsli"
#include "shaders/examples/cornell_pathtracer_common.hlsli"

LUMINARY_PUSH_CONSTANTS(TonemapConstants, constants);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    float2 p = float2((vertex_id << 1) & 2, vertex_id & 2);

    VSOutput o;
    o.uv = p;
    o.position = float4(p * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float3 tonemap_reinhard(float3 c)
{
    return c / (1.0f + c);
}

float3 tonemap_aces(float3 c)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((c * (a * c + b)) / (c * (2.43f * c + d) + e));
}

float4 PSMain(VSOutput input) : SV_Target
{
    LuminaryTexture2D<float4> accum = LuminaryTexture2D<float4>::Create(constants.accum_texture);
    LuminarySampler linear_sampler = LuminarySampler::Create(constants.sampler);

    float3 hdr = accum.SampleLevel(linear_sampler, input.uv, 0.0f).rgb * constants.exposure;
    float3 ldr = (constants.tonemap_mode == 0u) ? tonemap_aces(hdr) : tonemap_reinhard(hdr);
    ldr = pow(ldr, 1.0f / 2.2f);
    return float4(ldr, 1.0f);
}
