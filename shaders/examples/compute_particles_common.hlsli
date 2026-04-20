#ifndef COMPUTE_PARTICLES_COMMON_HLSLI
#define COMPUTE_PARTICLES_COMMON_HLSLI

struct Particle
{
    float radius;
    float angle;
    float height;
    float speed;
    float3 color;
    float size;
};

static float3 particle_world_position(Particle p)
{
    float x = cos(p.angle) * p.radius;
    float z = sin(p.angle) * p.radius;
    float y = p.height + sin(p.angle * 3.0f + p.radius * 0.2f) * 0.2f;
    return float3(x, y, z);
}

#endif // COMPUTE_PARTICLES_COMMON_HLSLI
