#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle texture;
    uint pass_index;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryRWTexture2D<float4> tex = LuminaryRWTexture2D<float4>::Create(constants.texture);
    uint2 xy = dispatchThreadId.xy;

    if (constants.pass_index == 0) {
        float2 uv = xy / float2(128.0f, 128.0f);
        tex.Store(xy, float4(uv.x, uv.y, 0.0f, 1.0f));
    } else if (constants.pass_index == 1) {
        float4 c = tex.Load(xy);
        tex.Store(xy, float4(c.r, c.g, 1.0f, 1.0f));
    } else {
        float4 c = tex.Load(xy);
        tex.Store(xy, float4(c.r, 1.0f - c.g, c.b, 1.0f));
    }
}
