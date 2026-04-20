#include "shaders/common/LuminaryRHI.hlsli"
#include "shaders/examples/compute_particles_common.hlsli"

struct DrawConstants
{
    float4x4 view_projection;
    ResourceHandle particle_buffer;
    float point_scale;
    float2 padding;
};
LUMINARY_PUSH_CONSTANTS(DrawConstants, constants);

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float point_size : PSIZE;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    LuminaryStructuredBuffer<Particle> particles =
        LuminaryStructuredBuffer<Particle>::Create(constants.particle_buffer);

    Particle p = particles.Load(vertex_id);
    float3 pos = particle_world_position(p);

    VSOutput output;
    output.position = mul(constants.view_projection, float4(pos, 1.0f));
    output.color = float4(p.color, 1.0f);
    output.point_size = max(1.0f, p.size * constants.point_scale);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
