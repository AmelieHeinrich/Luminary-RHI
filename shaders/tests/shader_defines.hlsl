#include "shaders/common/LuminaryRHI.hlsli"

static const float3 vertices[3] =
{
    float3( 0.0f,  0.5f, 0.0f),
    float3( 0.5f, -0.5f, 0.0f),
    float3(-0.5f, -0.5f, 0.0f),
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.position = float4(vertices[vertexId], 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
#ifdef USE_RED_COLOR
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
#else
    return float4(0.0f, 1.0f, 0.0f, 1.0f);
#endif
}
