#include "shaders/common/LuminaryRHI.hlsli"

struct VertexData
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 uv0;
    float4 color0;
};

struct PushConstants
{
    float4x4 view_projection;
    float4 camera_world;
    float4 base_color_factor;
    float4 surface_params; // roughness, normal_scale, occlusion_scale, metallic
    uint4 handles0;
};
LUMINARY_PUSH_CONSTANTS(PushConstants, constants);

struct VSOutput
{
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 color : COLOR0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    LuminaryStructuredBuffer<VertexData> vertices =
        LuminaryStructuredBuffer<VertexData>::Create(constants.handles0.x);

    VertexData v = vertices.Load(vertex_id);

    VSOutput outv;
    outv.world_position = v.position.xyz;
    outv.normal = normalize(v.normal.xyz);
    outv.tangent = float4(normalize(v.tangent.xyz), v.tangent.w);
    outv.uv = v.uv0.xy;
    outv.color = v.color0;
    outv.position = mul(constants.view_projection, float4(v.position.xyz, 1.0f));
    return outv;
}

float distribution_ggx(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * denom * denom, 1e-6f);
}

float geometry_schlick_ggx(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / max(NdotX * (1.0f - k) + k, 1e-6f);
}

float geometry_smith(float NdotV, float NdotL, float roughness)
{
    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
}

float3 fresnel_schlick(float cos_theta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cos_theta, 5.0f);
}

float4 PSMain(VSOutput input) : SV_Target
{
    LuminarySampler samp = LuminarySampler::Create(constants.handles0.y);
    LuminaryTexture2D<float4> tex_base = LuminaryTexture2D<float4>::Create(constants.handles0.z);
    LuminaryTexture2D<float4> tex_normal = LuminaryTexture2D<float4>::Create(constants.handles0.w);

    float4 base_sample = tex_base.Sample(samp, input.uv);
    float3 albedo = saturate(base_sample.rgb * constants.base_color_factor.rgb * input.color.rgb);

    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz);
    float3 B = normalize(cross(N, T) * input.tangent.w);
    float3x3 TBN = float3x3(T, B, N);

    float normal_scale = max(constants.surface_params.y, 0.0f);
    float3 nrm = tex_normal.Sample(samp, input.uv).xyz * 2.0f - 1.0f;
    nrm.xy *= normal_scale;
    float3 Nworld = normalize(mul(nrm, TBN));

    float occlusion = saturate(constants.surface_params.z);
    float roughness = saturate(max(0.04f, constants.surface_params.x));
    float metallic = saturate(constants.surface_params.w);
    float3 emissive = 0.0f.xxx;

    float3 V = normalize(constants.camera_world.xyz - input.world_position);
    float3 L = normalize(float3(0.45f, 0.8f, 0.3f));
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(Nworld, L));
    float NdotV = saturate(dot(Nworld, V));
    float NdotH = saturate(dot(Nworld, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = fresnel_schlick(VdotH, F0);
    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);

    float3 numerator = D * G * F;
    float denominator = max(4.0f * NdotV * NdotL, 1e-5f);
    float3 specular = numerator / denominator;

    float3 kd = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kd * albedo / 3.14159265f;

    float3 direct = (diffuse + specular) * NdotL;
    float3 ambient = 0.03f * albedo;
    float3 color = (ambient + direct) * occlusion + emissive;

    float exposure = max(constants.camera_world.w, 0.0f);
    color *= exposure;

    color = color / (1.0f + color);
    color = pow(color, 1.0f / 2.2f);

    return float4(color, base_sample.a * constants.base_color_factor.a * input.color.a);
}
