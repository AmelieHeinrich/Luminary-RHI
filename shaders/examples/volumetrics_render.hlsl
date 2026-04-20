#include "shaders/common/LuminaryRHI.hlsli"

struct DrawConstants
{
    float3 camera_position;
    float density_multiplier;
    float3 camera_forward;
    float absorption;
    float3 camera_right;
    float step_scale;
    float3 camera_up;
    float tan_half_fov;
    float3 sun_direction;
    float aspect;
    float3 cloud_bounds_min;
    float pad0;
    float3 cloud_bounds_max;
    float pad1;
    ResourceHandle volume_texture;
    ResourceHandle volume_sampler;
    uint max_steps;
    float noise_scroll;
};
LUMINARY_PUSH_CONSTANTS(DrawConstants, constants);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f),
    };

    VSOutput o;
    o.position = float4(positions[vertex_id], 0.0f, 1.0f);
    o.uv = positions[vertex_id] * 0.5f + 0.5f;
    return o;
}

static bool intersect_aabb(float3 ro, float3 rd, float3 bmin, float3 bmax, out float tmin, out float tmax)
{
    float3 inv_rd = 1.0f / rd;
    float3 t0 = (bmin - ro) * inv_rd;
    float3 t1 = (bmax - ro) * inv_rd;
    float3 lo = min(t0, t1);
    float3 hi = max(t0, t1);

    tmin = max(max(lo.x, lo.y), lo.z);
    tmax = min(min(hi.x, hi.y), hi.z);
    return tmax >= max(tmin, 0.0f);
}

float4 PSMain(VSOutput input) : SV_Target
{
    float2 screen = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);
    float3 ro = constants.camera_position;
    float3 rd = normalize(
        constants.camera_forward +
        constants.camera_right * (screen.x * constants.aspect * constants.tan_half_fov) +
        constants.camera_up * (screen.y * constants.tan_half_fov));

    float t0 = 0.0f;
    float t1 = 0.0f;
    if (!intersect_aabb(ro, rd, constants.cloud_bounds_min, constants.cloud_bounds_max, t0, t1)) {
        float sky = saturate(0.3f + rd.y * 0.45f);
        return float4(lerp(float3(0.03f, 0.05f, 0.08f), float3(0.25f, 0.45f, 0.8f), sky), 1.0f);
    }

    t0 = max(t0, 0.0f);
    float segment = max(t1 - t0, 0.0001f);
    uint steps = max(1u, constants.max_steps);
    float dt = (segment / (float)steps) * constants.step_scale;

    LuminaryTexture3D<float4> volume = LuminaryTexture3D<float4>::Create(constants.volume_texture);
    LuminarySampler samp = LuminarySampler::Create(constants.volume_sampler);

    float transmittance = 1.0f;
    float3 scattering = float3(0.0f, 0.0f, 0.0f);

    float t = t0;
    [loop]
    for (uint i = 0; i < steps; ++i) {
        float3 p = ro + rd * t;
        float3 uvw = (p - constants.cloud_bounds_min) / (constants.cloud_bounds_max - constants.cloud_bounds_min);
        float3 animated_uvw = frac(uvw + float3(constants.noise_scroll * 0.05f, 0.0f, constants.noise_scroll * 0.03f));
        float density = volume.SampleLevel(samp, animated_uvw, 0.0f).r * constants.density_multiplier;

        if (density > 0.0001f) {
            float3 lp = p + normalize(constants.sun_direction) * 0.06f;
            float3 luv = (lp - constants.cloud_bounds_min) / (constants.cloud_bounds_max - constants.cloud_bounds_min);
            float3 animated_luv = frac(luv + float3(constants.noise_scroll * 0.05f, 0.0f, constants.noise_scroll * 0.03f));
            float light_density = volume.SampleLevel(samp, animated_luv, 0.0f).r * constants.density_multiplier;
            float light = exp(-light_density * constants.absorption * 0.6f);

            float atten = exp(-density * constants.absorption * dt);
            float contrib = (1.0f - atten) * light;
            scattering += transmittance * contrib * float3(1.0f, 0.98f, 0.95f);
            transmittance *= atten;

            if (transmittance < 0.015f) {
                break;
            }
        }

        t += dt;
    }

    float sky = saturate(0.3f + rd.y * 0.45f);
    float3 sky_col = lerp(float3(0.03f, 0.05f, 0.08f), float3(0.25f, 0.45f, 0.8f), sky);
    float3 color = sky_col * transmittance + scattering;
    return float4(color, 1.0f);
}
