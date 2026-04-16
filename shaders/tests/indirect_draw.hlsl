#include "shaders/common/LuminaryRHI.hlsli"

struct PushData { float3 color; };
LUMINARY_PUSH_CONSTANTS(PushData, push_data);
LUMINARY_DECLARE_DRAW_ID();

static const float2 offsets[2] = { float2(-0.5f, 0.0f), float2(0.5f, 0.0f) };
static const float2 tri[3] = { float2(-0.25f, 0.3f), float2(0.25f, 0.3f), float2(0.0f, -0.3f) };

struct VSOut { float4 pos : SV_Position; float3 col : COLOR; };

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    uint did = LUMINARY_DRAW_ID();
    o.pos = float4(tri[vid] + offsets[did], 0, 1);
    o.col = push_data.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return float4(i.col, 1);
}
