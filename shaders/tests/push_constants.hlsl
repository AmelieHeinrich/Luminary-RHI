#include "shaders/common/LuminaryRHI.hlsli"

struct PushData { float3 color; float x_offset; };
LUMINARY_PUSH_CONSTANTS(PushData, push_data);

static const float2 tri_positions[3] = {
    float2(-0.3f,  0.3f),
    float2( 0.3f,  0.3f),
    float2( 0.0f, -0.3f)
};

struct VSOut {
    float4 pos : SV_Position;
    float4 col : COLOR;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    float2 p = tri_positions[vid];
    p.x += push_data.x_offset;
    VSOut o;
    o.pos = float4(p, 0.0f, 1.0f);
    o.col = float4(push_data.color, 1.0f);
    return o;
}

float4 PSMain(VSOut input) : SV_Target
{
    return input.col;
}
