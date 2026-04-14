#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    LuminaryRWTexture2DArray<float4> output_texture;
    uint layer;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RWTexture2DArray<float4> output_texture = constants.output_texture.Load();

    // Write a simple gradient pattern to the output texture layer
    float2 uv = dispatchThreadId.xy / float2(128.0f, 128.0f);
    float4 color = float4(uv, 0.5f + 0.5f * sin(uv.x * 10.0f), 1.0f);

    output_texture[uint3(dispatchThreadId.xy, constants.layer)] = color;
}
