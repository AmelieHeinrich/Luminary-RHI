#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(4, 4, 4)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTexture3D<float4>   input_texture  = LuminaryTexture3D<float4>::Create(constants.input_texture);
    LuminaryRWTexture3D<float4> output_texture = LuminaryRWTexture3D<float4>::Create(constants.output_texture);

    // Sample input 3D texture and write to output
    uint3 coords = dispatchThreadId.xyz;
    float4 color = input_texture.Load(int4(coords, 0));

    output_texture.Store(coords, color);
}
