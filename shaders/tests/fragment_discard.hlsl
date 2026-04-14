#include "shaders/common/LuminaryRHI.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
};

// Full-screen triangle generated from vertex ID (no vertex buffer needed)
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 pos = float2(
        (vertexId & 1) ? 3.0f : -1.0f,
        (vertexId & 2) ? 3.0f : -1.0f
    );
    output.position = float4(pos, 0.0f, 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    int cx = (int)(input.position.x / 8.0f);
    int cy = (int)(input.position.y / 8.0f);
    if ((cx + cy) % 2 == 1)
        discard;
    return float4(0.0f, 1.0f, 0.0f, 1.0f);
}
