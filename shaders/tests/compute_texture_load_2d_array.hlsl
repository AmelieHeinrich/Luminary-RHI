#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    LuminaryTexture2DArray<float4> input_texture;
    LuminaryRWTexture2DArray<float4> output_texture;
    uint layer;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Texture2DArray<float4> input_texture = constants.input_texture.Load();
    RWTexture2DArray<float4> output_texture = constants.output_texture.Load();

    // Sample input texture layer and write to output layer
    uint3 coords = uint3(dispatchThreadId.xy, constants.layer);
    float4 color = input_texture.Load(int4(coords, 0));

    output_texture[coords] = color;
}
