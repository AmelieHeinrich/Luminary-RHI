#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle sampler;
    ResourceHandle output_texture;
    uint           layer_count;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTexture2DArray<float4> input  = LuminaryTexture2DArray<float4>::Create(constants.input_texture);
    LuminarySampler                samp   = LuminarySampler::Create(constants.sampler);
    LuminaryRWTexture2D<float4>    output = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    uint width, height, numLevels;
    uint elements;
    input.GetDimensions(0, width, height, elements, numLevels);

    // Output is stacked vertically: each block of `height` rows is one layer
    uint layer = dispatchThreadId.y / height;
    uint local_y = dispatchThreadId.y % height;

    if (layer >= constants.layer_count)
        return;

    float2 uv = (float2(dispatchThreadId.x, local_y) + 0.5f) / float2(width, height);
    float4 color = input.SampleLevel(samp, float3(uv, (float)layer), 0.0f);

    output.Store(dispatchThreadId.xy, color);
}
