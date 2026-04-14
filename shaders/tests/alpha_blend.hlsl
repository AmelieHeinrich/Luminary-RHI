#include "shaders/common/LuminaryRHI.hlsli"

// Background: large opaque red triangle (covers most of screen)
static const float3 bg_verts[3] =
{
    float3(-0.9f, -0.9f, 0.0f),
    float3( 0.9f, -0.9f, 0.0f),
    float3( 0.0f,  0.9f, 0.0f),
};

// Foreground: smaller semi-transparent blue triangle (centered, sits on top of BG)
static const float3 fg_verts[3] =
{
    float3(-0.5f, -0.5f, 0.0f),
    float3( 0.5f, -0.5f, 0.0f),
    float3( 0.0f,  0.5f, 0.0f),
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput o;
    if (vertexId < 3) {
        o.position = float4(bg_verts[vertexId], 1.0f);
        o.color    = float4(0.8f, 0.2f, 0.2f, 1.0f);  // opaque red
    } else {
        o.position = float4(fg_verts[vertexId - 3], 1.0f);
        o.color    = float4(0.2f, 0.2f, 0.8f, 0.5f);  // semi-transparent blue
    }
    return o;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
