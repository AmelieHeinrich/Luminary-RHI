#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryRWTexture2D<float4> output_texture = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    // Write a simple gradient pattern to the output texture
    float2 uv = dispatchThreadId.xy / float2(128.0f, 128.0f);
    float4 color = float4(uv, 0.5f + 0.5f * sin(uv.x * 10.0f), 1.0f);

    output_texture.Store(dispatchThreadId.xy, color);
}
