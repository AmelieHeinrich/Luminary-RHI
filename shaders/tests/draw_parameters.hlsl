#include "shaders/common/LuminaryRHI.hlsli"

struct DrawParams { uint vertex_offset; };
LUMINARY_PUSH_CONSTANTS(DrawParams, draw_params);

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput o = (VSOutput)0;

    if (instanceId == 1u) {
        o.color = float4(0.0f, 1.0f, 0.0f, 1.0f);
    } else if (instanceId == 2u) {
        o.color = float4(0.0f, 0.0f, 1.0f, 1.0f);
    } else {
        o.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }

    uint vid = vertexId + draw_params.vertex_offset;

    float2 pos = float2(0.0f, 0.0f);
    if (vid == 3u) pos = float2(-0.4f, -0.4f);
    else if (vid == 4u) pos = float2( 0.4f, -0.4f);
    else if (vid == 5u) pos = float2( 0.0f,  0.4f);
    else if (vid == 6u) pos = float2(-0.4f,  0.4f);
    else if (vid == 7u) pos = float2( 0.4f,  0.4f);
    else if (vid == 8u) pos = float2( 0.0f, -0.4f);

    // Shift slightly depending on instance
    pos.x += (float(instanceId) - 1.0f) * 0.2f;

    o.position = float4(pos, 0.0f, 1.0f);
    return o;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
