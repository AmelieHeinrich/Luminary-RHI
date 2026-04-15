#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle sampler;
    ResourceHandle output_texture;
    float          compare_value;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTexture2D<float>    input  = LuminaryTexture2D<float>::Create(constants.input_texture);
    LuminaryComparisonSampler   samp   = LuminaryComparisonSampler::Create(constants.sampler);
    LuminaryRWTexture2D<float4> output = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    uint width, height, numLevels;
    input.GetDimensions(0, width, height, numLevels);

    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(width, height);
    float result = input.SampleCmpLevelZero(samp, uv, constants.compare_value);

    output.Store(dispatchThreadId.xy, float4(result, result, result, 1.0f));
}
