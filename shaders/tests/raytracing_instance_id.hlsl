#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_texture;
    ResourceHandle tlas;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

static const float3 kInstanceColors[3] = {
    float3(1, 0, 0),  // instance 0 — red
    float3(0, 1, 0),  // instance 1 — green
    float3(0, 0, 1),  // instance 2 — blue
};

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    LuminaryRWTexture2D<float4> output = LuminaryRWTexture2D<float4>::Create(constants.output_texture);
    LuminaryAccelerationStructure tlas = LuminaryAccelerationStructure::Create(constants.tlas);

    float2 uv  = (tid.xy + 0.5) / float2(128.0, 128.0);
    float2 ndc = uv * 2.0 - 1.0;

    RayDesc ray;
    ray.Origin    = float3(ndc.x, -ndc.y, -2.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin      = 0.001;
    ray.TMax      = 100.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> rq;
    rq.TraceRayInline(tlas.Resource(), RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);
    rq.Proceed();

    float4 color;
    if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        uint id = rq.CommittedInstanceID();
        color = float4(kInstanceColors[min(id, 2u)], 1.0);
    } else {
        color = float4(0, 0, 0, 1);
    }

    output.Store(tid.xy, color);
}
