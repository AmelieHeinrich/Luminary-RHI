#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle input_texture;
    ResourceHandle sampler;
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

// Sample direction for each cube face, given normalized (s, t) in [-1, 1]
float3 cube_direction(uint face, float s, float t)
{
    switch (face)
    {
        case 0: return normalize(float3( 1.0f,  t,    -s));   // +X
        case 1: return normalize(float3(-1.0f,  t,     s));   // -X
        case 2: return normalize(float3( s,     1.0f, -t));   // +Y
        case 3: return normalize(float3( s,    -1.0f,  t));   // -Y
        case 4: return normalize(float3( s,     t,     1.0f)); // +Z
        case 5: return normalize(float3(-s,     t,    -1.0f)); // -Z
        default: return float3(0, 0, 1);
    }
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryTextureCube<float4> input  = LuminaryTextureCube<float4>::Create(constants.input_texture);
    LuminarySampler             samp   = LuminarySampler::Create(constants.sampler);
    LuminaryRWTexture2D<float4> output = LuminaryRWTexture2D<float4>::Create(constants.output_texture);

    uint width, height, numLevels;
    input.GetDimensions(0, width, height, numLevels);

    // Output is 6 faces laid out horizontally: x in [0, 6*width)
    uint face    = dispatchThreadId.x / width;
    uint local_x = dispatchThreadId.x % width;

    if (face >= 6)
        return;

    float s = (float(local_x) + 0.5f) / float(width)  * 2.0f - 1.0f;
    float t = (float(dispatchThreadId.y) + 0.5f) / float(height) * 2.0f - 1.0f;

    float3 dir   = cube_direction(face, s, t);
    float4 color = input.SampleLevel(samp, dir, 0.0f);

    output.Store(dispatchThreadId.xy, color);
}
