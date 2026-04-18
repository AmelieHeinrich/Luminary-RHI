#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_texture;
    ResourceHandle tlas;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

static const float3 kVertexColors[3] = {
    float3(1, 0, 0),  // v0 top — red
    float3(0, 1, 0),  // v1 bottom-left — green
    float3(0, 0, 1),  // v2 bottom-right — blue
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

    RayQuery<RAY_FLAG_NONE> rq;
    rq.TraceRayInline(tlas.Resource(), RAY_FLAG_NONE, 0xFF, ray);

    while (rq.Proceed()) {
        if (rq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            float2 bary = rq.CandidateTriangleBarycentrics();
            float  w    = 1.0 - bary.x - bary.y;
            // Discard hits in the red-dominant region (near v0)
            if (w > 0.5)
                continue;
            rq.CommitNonOpaqueTriangleHit();
        }
    }

    float4 color;
    if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float2 bary = rq.CommittedTriangleBarycentrics();
        float  w    = 1.0 - bary.x - bary.y;
        float3 rgb  = kVertexColors[0] * w
                    + kVertexColors[1] * bary.x
                    + kVertexColors[2] * bary.y;
        color = float4(rgb, 1.0);
    } else {
        color = float4(0, 0, 0, 1);
    }

    output.Store(tid.xy, color);
}
