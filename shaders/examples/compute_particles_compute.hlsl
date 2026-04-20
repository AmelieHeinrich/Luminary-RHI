#include "shaders/common/LuminaryRHI.hlsli"
#include "shaders/examples/compute_particles_common.hlsli"

struct ComputeConstants
{
    ResourceHandle particle_buffer;
    float delta_time;
    float time_scale;
    uint particle_count;
};
LUMINARY_PUSH_CONSTANTS(ComputeConstants, constants);

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint idx = dispatch_thread_id.x;
    if (idx >= constants.particle_count) {
        return;
    }

    LuminaryRWStructuredBuffer<Particle> particles =
        LuminaryRWStructuredBuffer<Particle>::Create(constants.particle_buffer);

    Particle p = particles.Load(idx);

    float dt = constants.delta_time * constants.time_scale;
    p.angle += p.speed * dt;

    const float two_pi = 6.28318530718f;
    if (p.angle > two_pi) {
        p.angle -= two_pi;
    }

    particles.Store(idx, p);
}
