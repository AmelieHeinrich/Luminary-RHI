#include "shaders/common/LuminaryRHI.hlsli"

struct PushData { float3 color; };
LUMINARY_PUSH_CONSTANTS(PushData, push_data);
LUMINARY_DECLARE_DRAW_ID();

struct VSOutput { float4 position : SV_Position; float3 color : COLOR; };

static const float2 offsets[2] = { float2(-0.5f, 0.0f), float2(0.5f, 0.0f) };

[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void MSMain(uint gid : SV_GroupID,
            out vertices VSOutput v[3],
            out indices uint3 t[1])
{
    SetMeshOutputCounts(3, 1);
    uint did = LUMINARY_DRAW_ID();
    float2 off = offsets[did];
    v[0].position = float4(float2(-0.25f,  0.3f) + off, 0, 1); v[0].color = push_data.color;
    v[1].position = float4(float2( 0.25f,  0.3f) + off, 0, 1); v[1].color = push_data.color;
    v[2].position = float4(float2( 0.0f,  -0.3f) + off, 0, 1); v[2].color = push_data.color;
    t[0] = uint3(0, 1, 2);
}

float4 PSMain(VSOutput i) : SV_Target
{
    return float4(i.color, 1);
}
