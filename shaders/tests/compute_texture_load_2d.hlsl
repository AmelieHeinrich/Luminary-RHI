#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTexture2D<float4>   input_texture  = LuminaryTexture2D<float4>::Create(constants.input_texture);
    LuminaryRWTexture2D<float4> output_texture = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    // Sample input texture and write to output
    uint2 coords = dispatchThreadId.xy;
    float3 color = input_texture.Load(int3(coords, 0)).xyz;

    output_texture.Store(coords, float4(color, 1.0f));
}
