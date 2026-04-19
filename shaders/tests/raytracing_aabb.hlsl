#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_texture;
    ResourceHandle tlas;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

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

    // RAY_FLAG_NONE required for procedural geometry (no FORCE_OPAQUE)
    RayQuery<RAY_FLAG_NONE> rq;
    rq.TraceRayInline(tlas.Resource(), RAY_FLAG_NONE, 0xFF, ray);
    while (rq.Proceed()) {
        if (rq.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE) {
            rq.CommitProceduralPrimitiveHit(1.0);
        }
    }

    float4 color;
    if (rq.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
        color = float4(1.0, 0.5, 0.0, 1.0);  // orange on AABB hit
    } else {
        color = float4(0.0, 0.0, 0.0, 1.0);  // black on miss
    }

    output.Store(tid.xy, color);
}
