#include "shaders/common/LuminaryRHI.hlsli"
#include "shaders/examples/volumetrics_common.hlsli"

struct ComputeConstants
{
    ResourceHandle volume_storage;
    uint resolution;
    float noise_time;
    float padding;
};
LUMINARY_PUSH_CONSTANTS(ComputeConstants, constants);

[numthreads(4, 4, 4)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= constants.resolution ||
        dispatch_thread_id.y >= constants.resolution ||
        dispatch_thread_id.z >= constants.resolution) {
        return;
    }

    LuminaryRWTexture3D<float4> volume = LuminaryRWTexture3D<float4>::Create(constants.volume_storage);

    float3 uvw = (float3(dispatch_thread_id) + 0.5f) / (float)constants.resolution;

    float3 p0 = uvw * 7.0f + float3(constants.noise_time * 0.05f, 0.0f, constants.noise_time * 0.03f);
    float3 p1 = uvw * 13.0f + float3(17.0f, 9.0f, 3.0f);

    float w0 = 1.0f - saturate(worley3d(p0));
    float w1 = 1.0f - saturate(worley3d(p1));
    float detail = lerp(w0, w0 * w1, 0.45f);

    float h = saturate(uvw.y);
    float height_falloff = smoothstep(0.02f, 0.18f, h) * (1.0f - smoothstep(0.72f, 0.98f, h));

    float density = saturate(detail * height_falloff * 1.35f);
    volume.Store(dispatch_thread_id, float4(density, density, density, 1.0f));
}
