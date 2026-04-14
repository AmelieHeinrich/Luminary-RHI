#include "shaders/common/LuminaryRHI.hlsli"

static const float2 positions[9] = {
    // 0-2: Junk
    float2( 0.0f,  0.0f), float2( 0.0f,  0.0f), float2( 0.0f,  0.0f),
    // 3-5: Triangle pointing up (red)
    float2(-0.4f, -0.4f), float2( 0.4f, -0.4f), float2( 0.0f,  0.4f),
    // 6-8: Triangle pointing down (blue)
    float2(-0.4f,  0.4f), float2( 0.4f,  0.4f), float2( 0.0f, -0.4f)
};

static const float3 colors[3] = {
    // Instance 0 -> Red
    float3(1.0f, 0.0f, 0.0f),
    // Instance 1 -> Green
    float3(0.0f, 1.0f, 0.0f),
    // Instance 2 -> Blue
    float3(0.0f, 0.0f, 1.0f)
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput o;

    // Use SV_InstanceID to choose color and apply a shift
    o.color = float4(colors[instanceId % 3], 1.0f);
    
    float2 pos = positions[vertexId];
    
    // Shift slightly depending on instance
    pos.x += (float(instanceId) - 1.0f) * 0.2f;

    o.position = float4(pos, 0.0f, 1.0f);
    return o;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
