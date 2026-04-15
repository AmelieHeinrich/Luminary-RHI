#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_texture;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(4, 4, 4)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryRWTexture3D<float4> output_texture = LuminaryRWTexture3D<float4>::Create(constants.output_texture);

    // Write a simple gradient pattern to the output 3D texture
    float3 uvw = dispatchThreadId.xyz / float3(64.0f, 64.0f, 64.0f);
    float4 color = float4(uvw, 1.0f);

    output_texture.Store(dispatchThreadId.xyz, color);
}
