#include "shaders/common/LuminaryRHI.hlsli"

static const float3 vertices[3] = {
    float3(0.0f, 0.5f, 0.0f),
    float3(0.5f, -0.5f, 0.0f),
    float3(-0.5f, -0.5f, 0.0f)
};

static const float3 colors[3] = {
    float3(1.0f, 0.0f, 0.0f),
    float3(0.0f, 1.0f, 0.0f),
    float3(0.0f, 0.0f, 1.0f)
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.position = float4(vertices[vertexId], 1.0f);
    output.color = colors[vertexId];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}
