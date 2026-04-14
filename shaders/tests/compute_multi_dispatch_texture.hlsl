#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    LuminaryRWTexture2D<float4> texture;
    uint pass_index;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RWTexture2D<float4> tex = constants.texture.Load();
    uint2 xy = dispatchThreadId.xy;

    if (constants.pass_index == 0) {
        float2 uv = xy / float2(128.0f, 128.0f);
        tex[xy] = float4(uv.x, uv.y, 0.0f, 1.0f);
    } else if (constants.pass_index == 1) {
        float4 c = tex[xy];
        tex[xy] = float4(c.r, c.g, 1.0f, 1.0f);
    } else {
        float4 c = tex[xy];
        tex[xy] = float4(c.r, 1.0f - c.g, c.b, 1.0f);
    }
}
