#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    LuminaryTexture2D<float4> input_texture;
    LuminaryRWTexture2D<float4> output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Texture2D<float4> input_texture = constants.input_texture.Load();
    RWTexture2D<float4> output_texture = constants.output_texture.Load();

    // Sample input texture and write to output
    uint2 coords = dispatchThreadId.xy;
    float4 color = input_texture.Load(int3(coords, 0));

    output_texture[coords] = color;
}
