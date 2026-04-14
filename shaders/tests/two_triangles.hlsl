#include "shaders/common/LuminaryRHI.hlsli"

// 6 vertices forming two overlapping triangles:
//   Tri A (verts 0-2): CCW winding, z=0.3 (closer), red
//   Tri B (verts 3-5): CW winding,  z=0.7 (farther), blue
static const float3 verts[6] =
{
    // Triangle A (CCW) - Left-pointing rightward, red
    float3(-0.8f, -0.8f, 0.3f), // Top-Left
    float3(-0.8f,  0.8f, 0.3f), // Bottom-Left
    float3( 0.4f,  0.0f, 0.3f), // Right-Middle (overlap)

    // Triangle B (CW) - Right-pointing leftward, blue
    float3( 0.8f, -0.8f, 0.7f), // Top-Right
    float3( 0.8f,  0.8f, 0.7f), // Bottom-Right
    float3(-0.4f,  0.0f, 0.7f), // Left-Middle (overlap)
};

static const float3 colors[6] =
{
    float3(1.0f, 0.0f, 0.0f),
    float3(1.0f, 0.0f, 0.0f),
    float3(1.0f, 0.0f, 0.0f),

    float3(0.0f, 0.0f, 1.0f),
    float3(0.0f, 0.0f, 1.0f),
    float3(0.0f, 0.0f, 1.0f),
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.position = float4(verts[vertexId], 1.0f);
    output.color    = colors[vertexId];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}
