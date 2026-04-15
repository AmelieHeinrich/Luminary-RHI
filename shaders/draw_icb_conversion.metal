#include <metal_stdlib>
using namespace metal;

struct DrawCommand {
    uint draw_id;
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

struct PerDrawConstant {
    uint data[32];
    uint draw_id;
};

struct Arguments {
    command_buffer cmd_buffer;
};

kernel void encode_draws(
    device const DrawCommand* draw_commands       [[buffer(0)]],
    device const uint*        draw_count          [[buffer(1)]],
    device const uint*        primitive_type_buf  [[buffer(2)]],
    device Arguments*         args                [[buffer(3)]],
    device PerDrawConstant*   per_draw_constants  [[buffer(4)]],
    device const uint*        resource_heap       [[buffer(5)]],
    device const uint*        sampler_heap        [[buffer(6)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= *draw_count)
        return;

    threadgroup atomic_uint counter;
    if (gid == 0)
        atomic_store_explicit(&counter, 0, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const DrawCommand draw = draw_commands[gid];
    device PerDrawConstant* dst = per_draw_constants + gid;

    // Copy base constants from slot 0
    if (gid > 0) {
        device uint8_t* dPtr = reinterpret_cast<device uint8_t*>(dst);
        device const uint8_t* sPtr = reinterpret_cast<device const uint8_t*>(per_draw_constants);
        for (uint i = 0; i < 128; ++i)
            dPtr[i] = sPtr[i];
    }

    dst->draw_id = atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);

    uint primitive = primitive_type_buf[0];
    primitive_type pt;
    switch (primitive) {
        case 0: pt = primitive_type::point;    break;
        case 1: pt = primitive_type::line;     break;
        case 2: pt = primitive_type::triangle; break;
        default: return;
    }

    render_command rc(args->cmd_buffer, gid);
    rc.set_vertex_buffer(resource_heap, 0);
    rc.set_fragment_buffer(resource_heap, 0);
    rc.set_vertex_buffer(sampler_heap, 1);
    rc.set_fragment_buffer(sampler_heap, 1);
    rc.set_vertex_buffer(dst, 2);
    rc.set_fragment_buffer(dst, 2);
    rc.draw_primitives(pt, draw.first_vertex, draw.vertex_count, draw.instance_count, draw.first_instance);
}