#include "shaders/common/LuminaryRHI.hlsli"
#include "shaders/examples/cornell_pathtracer_common.hlsli"

LUMINARY_PUSH_CONSTANTS(TraceConstants, constants);

static const float PI = 3.14159265359f;

float hash11(float p)
{
    p = frac(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return frac(p);
}

float2 hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float3 sample_cosine_hemisphere(float2 xi)
{
    float r = sqrt(xi.x);
    float phi = 2.0f * PI * xi.y;
    return float3(r * cos(phi), r * sin(phi), sqrt(saturate(1.0f - xi.x)));
}

void build_orthonormal_basis(float3 n, out float3 t, out float3 b)
{
    float3 up = (abs(n.y) < 0.999f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float signed_distance_to_plane(float x, float plane)
{
    return abs(x - plane);
}

float3 estimate_scene_normal(float3 p)
{
    float best = 1e9f;
    float3 best_n = float3(0.0f, 1.0f, 0.0f);

    // Cornell room (open front, inward-facing normals)
    {
        float d = signed_distance_to_plane(p.x, -1.0f); if (d < best) { best = d; best_n = float3(1.0f, 0.0f, 0.0f); }
        d = signed_distance_to_plane(p.x, 1.0f);        if (d < best) { best = d; best_n = float3(-1.0f, 0.0f, 0.0f); }
        d = signed_distance_to_plane(p.y, 0.0f);        if (d < best) { best = d; best_n = float3(0.0f, 1.0f, 0.0f); }
        d = signed_distance_to_plane(p.y, 2.0f);        if (d < best) { best = d; best_n = float3(0.0f, -1.0f, 0.0f); }
        d = signed_distance_to_plane(p.z, 1.0f);        if (d < best) { best = d; best_n = float3(0.0f, 0.0f, -1.0f); }
    }

    // Short box
    {
        float d = signed_distance_to_plane(p.x, -0.55f); if (d < best) { best = d; best_n = float3(-1.0f, 0.0f, 0.0f); }
        d = signed_distance_to_plane(p.x, -0.05f);       if (d < best) { best = d; best_n = float3(1.0f, 0.0f, 0.0f); }
        d = signed_distance_to_plane(p.y, 0.0f);         if (d < best) { best = d; best_n = float3(0.0f, -1.0f, 0.0f); }
        d = signed_distance_to_plane(p.y, 0.6f);         if (d < best) { best = d; best_n = float3(0.0f, 1.0f, 0.0f); }
        d = signed_distance_to_plane(p.z, -0.15f);       if (d < best) { best = d; best_n = float3(0.0f, 0.0f, -1.0f); }
        d = signed_distance_to_plane(p.z, 0.4f);         if (d < best) { best = d; best_n = float3(0.0f, 0.0f, 1.0f); }
    }

    // Tall box
    {
        float d = signed_distance_to_plane(p.x, 0.2f);  if (d < best) { best = d; best_n = float3(-1.0f, 0.0f, 0.0f); }
        d = signed_distance_to_plane(p.x, 0.7f);        if (d < best) { best = d; best_n = float3(1.0f, 0.0f, 0.0f); }
        d = signed_distance_to_plane(p.y, 0.0f);        if (d < best) { best = d; best_n = float3(0.0f, -1.0f, 0.0f); }
        d = signed_distance_to_plane(p.y, 1.2f);        if (d < best) { best = d; best_n = float3(0.0f, 1.0f, 0.0f); }
        d = signed_distance_to_plane(p.z, -0.7f);       if (d < best) { best = d; best_n = float3(0.0f, 0.0f, -1.0f); }
        d = signed_distance_to_plane(p.z, -0.2f);       if (d < best) { best = d; best_n = float3(0.0f, 0.0f, 1.0f); }
    }

    return best_n;
}

void evaluate_material(float3 p, out float3 albedo, out float3 emission)
{
    albedo = float3(0.725f, 0.715f, 0.68f);
    emission = 0.0f.xxx;

    if (abs(p.x + 1.0f) < 0.015f) {
        albedo = float3(0.63f, 0.065f, 0.05f);
    } else if (abs(p.x - 1.0f) < 0.015f) {
        albedo = float3(0.14f, 0.45f, 0.091f);
    }

    if (abs(p.y - 2.0f) < 0.02f && abs(p.x) < 0.28f && p.z > -0.28f && p.z < 0.28f) {
        emission = float3(18.0f, 16.5f, 14.0f);
    }
}

bool trace_scene(float3 ray_origin, float3 ray_dir, out float t_hit)
{
    LuminaryAccelerationStructure tlas = LuminaryAccelerationStructure::Create(constants.tlas);

    RayDesc ray;
    ray.Origin = ray_origin;
    ray.Direction = ray_dir;
    ray.TMin = 0.001f;
    ray.TMax = 1000.0f;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> rq;
    rq.TraceRayInline(tlas.Resource(), RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);

    while (rq.Proceed()) {
    }

    if (rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        t_hit = rq.CommittedRayT();
        return true;
    }

    t_hit = 0.0f;
    return false;
}

float3 trace_path(uint2 pixel, uint frame_seed)
{
    float2 jitter = hash22(float2(pixel) + float2(frame_seed * 1.37f, frame_seed * 2.91f));
    float2 uv = (float2(pixel) + jitter) / float2(constants.width, constants.height);
    float2 ndc = uv * 2.0f - 1.0f;

    float3 origin = constants.camera_position.xyz;
    float3 dir = normalize(constants.camera_forward.xyz
        + ndc.x * constants.camera_aspect * constants.camera_up_tan.w * constants.camera_right.xyz
        - ndc.y * constants.camera_up_tan.w * constants.camera_up_tan.xyz);

    float3 throughput = 1.0f.xxx;
    float3 radiance = 0.0f.xxx;

    const uint bounce_count = max(constants.max_bounces, 1u);

    for (uint bounce = 0; bounce < bounce_count; ++bounce) {
        float t_hit = 0.0f;
        if (!trace_scene(origin, dir, t_hit)) {
            float sky_t = saturate(0.5f * dir.y + 0.5f);
            float3 sky = lerp(float3(0.01f, 0.01f, 0.015f), float3(0.04f, 0.05f, 0.08f), sky_t);
            radiance += throughput * sky;
            break;
        }

        float3 p = origin + dir * t_hit;
        float3 n = estimate_scene_normal(p);
        if (dot(n, dir) > 0.0f) {
            n = -n;
        }

        float3 albedo;
        float3 emission;
        evaluate_material(p, albedo, emission);
        radiance += throughput * emission;

        float2 xi = hash22(float2(pixel) * (float)(bounce + 1u) + float2(frame_seed * 0.17f + bounce * 13.0f, frame_seed * 0.63f + bounce * 7.0f));
        float3 local_dir = sample_cosine_hemisphere(xi);
        float3 tangent;
        float3 bitangent;
        build_orthonormal_basis(n, tangent, bitangent);
        float3 new_dir = normalize(local_dir.x * tangent + local_dir.y * bitangent + local_dir.z * n);

        origin = p + n * 0.002f;
        dir = new_dir;
        throughput *= albedo;

        float roulette = max(throughput.r, max(throughput.g, throughput.b));
        if (bounce > 1 && hash11((float)frame_seed + (float)bounce * 17.0f + pixel.x * 0.31f + pixel.y * 0.77f) > roulette) {
            break;
        }
        throughput /= max(roulette, 0.05f);
    }

    return max(radiance, 0.0f.xxx);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= constants.width || pixel.y >= constants.height) {
        return;
    }

    LuminaryTexture2D<float4> input_accum = LuminaryTexture2D<float4>::Create(constants.input_accum);
    LuminaryTexture2D<float4> input_prev = LuminaryTexture2D<float4>::Create(constants.input_prev);
    LuminaryRWTexture2D<float4> output_accum = LuminaryRWTexture2D<float4>::Create(constants.output_accum);
    LuminaryRWTexture2D<float4> output_prev = LuminaryRWTexture2D<float4>::Create(constants.output_prev);

    float3 current = trace_path(pixel, constants.frame_seed);
    float3 accum_prev = input_accum.Load(int3(pixel, 0)).rgb;

    bool force_reset = (constants.flags & TRACE_FLAG_FORCE_RESET) != 0u;
    bool reject_history = force_reset || constants.accumulated_samples == 0u;

    float3 accum = current;
    if (!reject_history) {
        float n = (float)constants.accumulated_samples;
        accum = (accum_prev * n + current) / (n + 1.0f);
    }

    output_accum.Store(pixel, float4(accum, 1.0f));
    output_prev.Store(pixel, float4(current, 1.0f));
}
