#include "shaders/common/LuminaryRHI.hlsli"

static const float3 lineVerts[12] =
{
    float3(-1.0, -1.0, 0.0), float3( 1.0, -1.0, 0.0),
    float3( 1.0, -1.0, 0.0), float3( 1.0,  1.0, 0.0),
    float3( 1.0,  1.0, 0.0), float3(-1.0,  1.0, 0.0),
    float3(-1.0,  1.0, 0.0), float3(-1.0, -1.0, 0.0),
    float3(-1.0, -1.0, 0.0), float3( 1.0,  1.0, 0.0),
    float3( 1.0, -1.0, 0.0), float3(-1.0,  1.0, 0.0) 
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

float3 HashVertexIDToColor(uint vertexId)
{
    uint hash = vertexId * 2654435761u; // Knuth's multiplicative hash
    return float3(
        ((hash >> 16) & 0xFF) / 255.0,
        ((hash >> 8) & 0xFF) / 255.0,
        (hash & 0xFF) / 255.0
    );
}

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.position = float4(lineVerts[vertexId], 1.0f);
    output.color = HashVertexIDToColor(vertexId);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}
