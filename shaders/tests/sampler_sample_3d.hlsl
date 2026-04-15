#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle sampler;
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTexture3D<float4>   input  = LuminaryTexture3D<float4>::Create(constants.input_texture);
    LuminarySampler             samp   = LuminarySampler::Create(constants.sampler);
    LuminaryRWTexture2D<float4> output = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    uint width, height, depth, numLevels;
    input.GetDimensions(0, width, height, depth, numLevels);

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(width, height);
    // Sample at the mid-depth slice
    float4 color = input.SampleLevel(samp, float3(uv, 0.5f), 0.0f);

    output.Store(dispatchThreadId.xy, color);
}
