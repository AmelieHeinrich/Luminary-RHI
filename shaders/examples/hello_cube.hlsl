#include "shaders/common/LuminaryRHI.hlsli"

struct PushConstants
{
    float4x4 mvp;
    ResourceHandle texture_handle;
    ResourceHandle sampler_handle;
    float2 padding;
};
LUMINARY_PUSH_CONSTANTS(PushConstants, constants);

static const float3 cube_positions[36] = {
    float3(-1.0f, -1.0f,  1.0f), float3( 1.0f, -1.0f,  1.0f), float3( 1.0f,  1.0f,  1.0f),
    float3(-1.0f, -1.0f,  1.0f), float3( 1.0f,  1.0f,  1.0f), float3(-1.0f,  1.0f,  1.0f),
    float3( 1.0f, -1.0f, -1.0f), float3(-1.0f, -1.0f, -1.0f), float3(-1.0f,  1.0f, -1.0f),
    float3( 1.0f, -1.0f, -1.0f), float3(-1.0f,  1.0f, -1.0f), float3( 1.0f,  1.0f, -1.0f),
    float3( 1.0f, -1.0f,  1.0f), float3( 1.0f, -1.0f, -1.0f), float3( 1.0f,  1.0f, -1.0f),
    float3( 1.0f, -1.0f,  1.0f), float3( 1.0f,  1.0f, -1.0f), float3( 1.0f,  1.0f,  1.0f),
    float3(-1.0f, -1.0f, -1.0f), float3(-1.0f, -1.0f,  1.0f), float3(-1.0f,  1.0f,  1.0f),
    float3(-1.0f, -1.0f, -1.0f), float3(-1.0f,  1.0f,  1.0f), float3(-1.0f,  1.0f, -1.0f),
    float3(-1.0f,  1.0f,  1.0f), float3( 1.0f,  1.0f,  1.0f), float3( 1.0f,  1.0f, -1.0f),
    float3(-1.0f,  1.0f,  1.0f), float3( 1.0f,  1.0f, -1.0f), float3(-1.0f,  1.0f, -1.0f),
    float3(-1.0f, -1.0f, -1.0f), float3( 1.0f, -1.0f, -1.0f), float3( 1.0f, -1.0f,  1.0f),
    float3(-1.0f, -1.0f, -1.0f), float3( 1.0f, -1.0f,  1.0f), float3(-1.0f, -1.0f,  1.0f),
};

static const float2 cube_uvs[36] = {
    float2(0.0f, 1.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(0.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(0.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(0.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(0.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(0.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(0.0f, 0.0f),
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.position = mul(constants.mvp, float4(cube_positions[vertexId], 1.0f));
    output.uv = cube_uvs[vertexId];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    LuminaryTexture2D<float4> texture = LuminaryTexture2D<float4>::Create(constants.texture_handle);
    LuminarySampler sampler = LuminarySampler::Create(constants.sampler_handle);
    return texture.Sample(sampler, input.uv);
}