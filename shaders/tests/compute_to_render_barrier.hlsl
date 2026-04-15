#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryRWTexture2D<float4> output_texture = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    // Write a specific pattern that render pass will verify
    // Red gradient from left to right
    float2 uv = dispatchThreadId.xy / float2(256.0f, 256.0f);
    float4 color = float4(uv.x, 0.0f, 1.0f - uv.x, 1.0f);

    output_texture.Store(dispatchThreadId.xy, color);
}
