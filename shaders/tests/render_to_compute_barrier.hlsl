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

    // Read from render target and invert colors
    uint2 coords = dispatchThreadId.xy;
    float4 color = input_texture.Load(int3(coords, 0));
    
    // Invert RGB but keep alpha
    float4 inverted = float4(1.0f - color.rgb, color.a);

    output_texture[coords] = inverted;
}
