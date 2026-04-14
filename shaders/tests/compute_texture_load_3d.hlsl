#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    LuminaryTexture3D<float4> input_texture;
    LuminaryRWTexture3D<float4> output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(4, 4, 4)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Texture3D<float4> input_texture = constants.input_texture.Load();
    RWTexture3D<float4> output_texture = constants.output_texture.Load();

    // Sample input 3D texture and write to output
    uint3 coords = dispatchThreadId.xyz;
    float4 color = input_texture.Load(int4(coords, 0));

    output_texture[coords] = color;
}
