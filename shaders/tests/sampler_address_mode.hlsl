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
    LuminaryTexture2D<float4>   input  = LuminaryTexture2D<float4>::Create(constants.input_texture);
    LuminarySampler             samp   = LuminarySampler::Create(constants.sampler);
    LuminaryRWTexture2D<float4> output = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    uint out_w, out_h;
    output.GetDimensions(out_w, out_h);

    // Map output pixels to UV range [-0.5, 1.5] to exercise address modes
    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(out_w, out_h);
    uv = uv * 2.0f - 0.5f;

    float4 color = input.SampleLevel(samp, uv, 0.0f);
    output.Store(dispatchThreadId.xy, color);
}
