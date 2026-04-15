#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle output_texture;
    uint layer;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTexture2DArray<float4>   input_texture  = LuminaryTexture2DArray<float4>::Create(constants.input_texture);
    LuminaryRWTexture2DArray<float4> output_texture = LuminaryRWTexture2DArray<float4>::Create(constants.output_texture);

    // Sample input texture layer and write to output layer
    uint3 coords = uint3(dispatchThreadId.xy, constants.layer);
    float4 color = input_texture.Load(int4(coords, 0));

    output_texture.Store(coords, color);
}
