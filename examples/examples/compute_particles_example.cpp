#include "compute_particles_example.h"

#include "extras/shader_compiler/luminary_shader_compiler.h"
#include "../hmm/HMM.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

static constexpr uint32_t PARTICLE_COUNT = 1000000;
static constexpr uint32_t COMPUTE_GROUP_SIZE = 256;

struct ParticleCpu
{
    float radius;
    float angle;
    float height;
    float speed;
    float color[3];
    float size;
};

static std::string read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string src(sz, '\0');
    fread(src.data(), 1, sz, f);
    fclose(f);
    return src;
}

static std::pair<uint8_t*, uint64_t> compile_stage(const std::string& source,
                                                  LuminaryShaderStage stage,
                                                  const char* entry_point,
                                                  bool use_point_topology)
{
    LuminaryShaderCompilerOptions opts = {};
    opts.shading_language = LUMINARY_SHADING_LANGUAGE_HLSL;
    opts.bytecode         = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage     = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code         = const_cast<char*>(source.data());
    opts.source_code_size    = source.size();
    opts.add_debug_symbols   = 1;
    opts.use_point_topology  = use_point_topology ? 1 : 0;

    uint64_t size = 0;
    uint8_t* bytecode = luminary_compile_shader(&opts, &size);
    return {bytecode, size};
}

static LRHIShaderModule make_module(LRHIDevice device,
                                    uint8_t* bytecode,
                                    uint64_t size,
                                    LRHIShaderStage stage,
                                    const char* entry_point)
{
    LRHIShaderModuleInfo info = {};
    info.stage       = stage;
    info.entry_point = entry_point;
    info.code        = reinterpret_cast<const uint32_t*>(bytecode);
    info.code_size   = static_cast<uint32_t>(size);

    LRHIShaderModule module = nullptr;
    LRHIError err = {};
    lrhi_create_shader_module(device, &info, &module, &err);
    free(bytecode);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_shader_module: %s\n", err.message);
        return nullptr;
    }

    return module;
}

static uint32_t lcg_next(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

static float frand(uint32_t& state)
{
    return (float)(lcg_next(state) & 0x00FFFFFFu) / (float)0x01000000u;
}

static void hsv_to_rgb(float h, float s, float v, float out_rgb[3])
{
    h = h - floorf(h);
    float i_f = floorf(h * 6.0f);
    float f = h * 6.0f - i_f;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);

    int i = (int)i_f % 6;
    switch (i) {
        case 0: out_rgb[0] = v; out_rgb[1] = t; out_rgb[2] = p; break;
        case 1: out_rgb[0] = q; out_rgb[1] = v; out_rgb[2] = p; break;
        case 2: out_rgb[0] = p; out_rgb[1] = v; out_rgb[2] = t; break;
        case 3: out_rgb[0] = p; out_rgb[1] = q; out_rgb[2] = v; break;
        case 4: out_rgb[0] = t; out_rgb[1] = p; out_rgb[2] = v; break;
        default: out_rgb[0] = v; out_rgb[1] = p; out_rgb[2] = q; break;
    }
}

static std::vector<ParticleCpu> make_initial_particles()
{
    std::vector<ParticleCpu> particles(PARTICLE_COUNT);
    uint32_t rng = 0x1234ABCDu;

    for (uint32_t i = 0; i < PARTICLE_COUNT; ++i) {
        float ring = std::pow(frand(rng), 0.65f);
        float radius = 0.35f + ring * 20.0f;
        float arm_offset = (float)(i % 4u) * 1.57079632679f;
        float angle = frand(rng) * 6.28318530718f + arm_offset + radius * 0.08f;
        float height = (frand(rng) - 0.5f) * (0.3f + radius * 0.03f);
        float speed = (0.25f + 0.8f / std::sqrt(std::max(radius, 0.05f))) * (0.8f + frand(rng) * 0.4f);

        float hue = frand(rng);
        float sat = 0.7f + 0.3f * frand(rng);
        float val = 0.75f + 0.25f * frand(rng);
        float rgb[3] = {};
        hsv_to_rgb(hue, sat, val, rgb);

        particles[i].radius = radius;
        particles[i].angle = angle;
        particles[i].height = height;
        particles[i].speed = speed;
        particles[i].color[0] = rgb[0];
        particles[i].color[1] = rgb[1];
        particles[i].color[2] = rgb[2];
        particles[i].size = 0.6f + frand(rng) * 1.2f;
    }

    return particles;
}

static HMM_Mat4 make_view_projection(float width,
                                     float height,
                                     float yaw,
                                     float pitch,
                                     float distance)
{
    float aspect = width > 0.0f && height > 0.0f ? (width / height) : 1.0f;
    HMM_Mat4 projection = HMM_Perspective_LH_ZO(HMM_AngleDeg(60.0f), aspect, 0.1f, 500.0f);

    float cp = cosf(pitch);
    float sp = sinf(pitch);
    float cy = cosf(yaw);
    float sy = sinf(yaw);

    HMM_Vec3 eye = HMM_V3(cp * sy * distance, sp * distance, cp * cy * distance);
    HMM_Mat4 view = HMM_LookAt_LH(eye, HMM_V3(0.0f, 0.0f, 0.0f), HMM_V3(0.0f, 1.0f, 0.0f));
    return HMM_MulM4(projection, view);
}

} // namespace

ComputeParticlesExample::ComputeParticlesExample(LRHIDevice in_device, LRHITextureFormat in_render_target_format)
    : device(in_device)
    , render_target_format(in_render_target_format)
    , compute_shader(nullptr)
    , vertex_shader(nullptr)
    , fragment_shader(nullptr)
    , compute_pipeline(nullptr)
    , render_pipeline(nullptr)
    , particle_buffer(nullptr)
    , particle_rw_view(nullptr)
    , particle_read_view(nullptr)
    , particles_added_to_residency(false)
    , particle_rw_handle(0)
    , particle_read_handle(0)
    , camera_yaw(0.0f)
    , camera_pitch(0.35f)
    , camera_distance(55.0f)
    , simulation_speed(1.0f)
    , point_scale(1.5f)
    , paused(false)
    , show_help(true)
    , last_frame_time(0.0)
{
    LRHIError error = {};

    std::string compute_src = read_file("shaders/examples/compute_particles_compute.hlsl");
    std::string render_src  = read_file("shaders/examples/compute_particles_render.hlsl");
    if (compute_src.empty() || render_src.empty()) {
        printf("Failed to read compute particles shaders\n");
        return;
    }

    auto [cs_bc, cs_sz] = compile_stage(compute_src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain", false);
    auto [vs_bc, vs_sz] = compile_stage(render_src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain", true);
    auto [ps_bc, ps_sz] = compile_stage(render_src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain", true);
    if (!cs_bc || !vs_bc || !ps_bc) {
        free(cs_bc);
        free(vs_bc);
        free(ps_bc);
        printf("Shader compilation failed\n");
        return;
    }

    compute_shader  = make_module(device, cs_bc, cs_sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain");
    vertex_shader   = make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain");
    fragment_shader = make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!compute_shader || !vertex_shader || !fragment_shader) {
        return;
    }

    LRHIComputePipelineInfo cpi = {};
    cpi.compute_shader = compute_shader;
    cpi.supports_indirect_commands = 0;
    lrhi_create_compute_pipeline(device, &cpi, &compute_pipeline, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !compute_pipeline) {
        printf("create_compute_pipeline: %s\n", error.message);
        return;
    }

    LRHIRenderPipelineInfo rpi = {};
    rpi.fill_mode = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    rpi.cull_mode = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    rpi.front_face = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    rpi.topology = LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST;
    rpi.depth_test_enable = 0;
    rpi.depth_write_enable = 0;
    rpi.depth_stencil_format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    rpi.render_target_formats[0] = render_target_format;
    rpi.render_target_count = 1;
    rpi.vertex_shader = vertex_shader;
    rpi.fragment_shader = fragment_shader;
    lrhi_create_render_pipeline(device, &rpi, &render_pipeline, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !render_pipeline) {
        printf("create_render_pipeline: %s\n", error.message);
        return;
    }

    LRHIBufferInfo bi = {};
    bi.size = (uint64_t)PARTICLE_COUNT * sizeof(ParticleCpu);
    bi.stride = sizeof(ParticleCpu);
    bi.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
    bi.name = "Compute Particles Buffer";
    lrhi_create_buffer(device, &bi, &particle_buffer, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !particle_buffer) {
        printf("create_buffer: %s\n", error.message);
        return;
    }

    LRHIBufferViewInfo rwv = {};
    rwv.buffer = particle_buffer;
    rwv.offset = 0;
    rwv.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_STRUCTURED;
    lrhi_create_buffer_view(device, &rwv, &particle_rw_view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !particle_rw_view) {
        printf("create_buffer_view rw: %s\n", error.message);
        return;
    }

    LRHIBufferViewInfo rv = {};
    rv.buffer = particle_buffer;
    rv.offset = 0;
    rv.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_STRUCTURED;
    lrhi_create_buffer_view(device, &rv, &particle_read_view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !particle_read_view) {
        printf("create_buffer_view read: %s\n", error.message);
        return;
    }

    LRHIError lerr = {};
    particle_rw_handle = lrhi_buffer_view_get_bindless_index(particle_rw_view, &lerr);
    particle_read_handle = lrhi_buffer_view_get_bindless_index(particle_read_view, &lerr);
    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("buffer_view_get_bindless_index: %s\n", lerr.message);
        return;
    }

    std::vector<ParticleCpu> particles = make_initial_particles();
    void* mapped = lrhi_buffer_map(particle_buffer, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
        printf("buffer_map: %s\n", error.message);
        return;
    }
    memcpy(mapped, particles.data(), particles.size() * sizeof(ParticleCpu));
    lrhi_buffer_unmap(particle_buffer);

    last_frame_time = ImGui::GetTime();
}

ComputeParticlesExample::~ComputeParticlesExample()
{
    if (particle_read_view) {
        lrhi_destroy_buffer_view(particle_read_view);
        particle_read_view = nullptr;
    }
    if (particle_rw_view) {
        lrhi_destroy_buffer_view(particle_rw_view);
        particle_rw_view = nullptr;
    }
    if (particle_buffer) {
        lrhi_destroy_buffer(particle_buffer);
        particle_buffer = nullptr;
    }
    if (render_pipeline) {
        lrhi_destroy_render_pipeline(render_pipeline);
        render_pipeline = nullptr;
    }
    if (compute_pipeline) {
        lrhi_destroy_compute_pipeline(compute_pipeline);
        compute_pipeline = nullptr;
    }
    if (fragment_shader) {
        lrhi_destroy_shader_module(fragment_shader);
        fragment_shader = nullptr;
    }
    if (vertex_shader) {
        lrhi_destroy_shader_module(vertex_shader);
        vertex_shader = nullptr;
    }
    if (compute_shader) {
        lrhi_destroy_shader_module(compute_shader);
        compute_shader = nullptr;
    }
}

const char* ComputeParticlesExample::name() const
{
    return "Compute Particles";
}

bool ComputeParticlesExample::is_ready() const
{
    return compute_pipeline != nullptr &&
           render_pipeline != nullptr &&
           particle_buffer != nullptr &&
           particle_rw_view != nullptr &&
           particle_read_view != nullptr;
}

bool ComputeParticlesExample::ensure_particle_residency(LRHIResidencySet residency_set)
{
    if (!residency_set) {
        return false;
    }

    if (!particles_added_to_residency) {
        LRHIError error = {};
        lrhi_residency_set_add_buffer(residency_set, particle_buffer, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("add particle residency: %s\n", error.message);
            return false;
        }
        particles_added_to_residency = true;
    }

    return true;
}

void ComputeParticlesExample::record(LRHICommandList command_list,
                                     LRHITextureView target_view,
                                     int width,
                                     int height,
                                     LRHIResidencySet residency_set)
{
    if (!is_ready()) {
        return;
    }

    if (!ensure_particle_residency(residency_set)) {
        return;
    }
    lrhi_residency_set_update(residency_set, nullptr);

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && io.MouseDown[0]) {
        camera_yaw += io.MouseDelta.x * 0.0055f;
        camera_pitch += io.MouseDelta.y * 0.0055f;
        camera_pitch = std::clamp(camera_pitch, -1.45f, 1.45f);
    }
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
        camera_distance *= (1.0f - io.MouseWheel * 0.08f);
        camera_distance = std::clamp(camera_distance, 8.0f, 180.0f);
    }

    double now = ImGui::GetTime();
    float dt = (float)(now - last_frame_time);
    last_frame_time = now;
    dt = std::clamp(dt, 0.0f, 0.05f);
    if (paused) {
        dt = 0.0f;
    }

    struct ComputePush {
        uint32_t particle_buffer;
        float delta_time;
        float time_scale;
        uint32_t particle_count;
    } cp = {};
    cp.particle_buffer = particle_rw_handle;
    cp.delta_time = dt;
    cp.time_scale = simulation_speed;
    cp.particle_count = PARTICLE_COUNT;

    LRHIError error = {};
    LRHIComputePass compute_pass = lrhi_compute_pass_begin(command_list, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !compute_pass) {
        return;
    }

    lrhi_compute_pass_set_pipeline(compute_pass, compute_pipeline, &error);
    lrhi_compute_pass_set_push_constants(compute_pass, &cp, sizeof(cp), &error);
    lrhi_compute_pass_dispatch(
        compute_pass,
        (PARTICLE_COUNT + COMPUTE_GROUP_SIZE - 1u) / COMPUTE_GROUP_SIZE,
        1,
        1,
        COMPUTE_GROUP_SIZE,
        1,
        1,
        &error);
    lrhi_compute_pass_end(compute_pass, &error);

    LRHIRenderPassInfo rp_info = {};
    rp_info.color_attachments[0].texture_view = target_view;
    rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
    rp_info.color_attachments[0].clear_color[0] = 0.005f;
    rp_info.color_attachments[0].clear_color[1] = 0.005f;
    rp_info.color_attachments[0].clear_color[2] = 0.015f;
    rp_info.color_attachments[0].clear_color[3] = 1.0f;
    rp_info.color_attachment_count = 1;
    rp_info.render_width = (uint32_t)width;
    rp_info.render_height = (uint32_t)height;

    LRHIRenderPass rp = lrhi_render_pass_begin(command_list, &rp_info, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
        return;
    }

    lrhi_render_pass_encoder_barrier(
        rp,
        LUMINARY_RHI_RENDER_STAGE_COMPUTE,
        LUMINARY_RHI_RENDER_STAGE_VERTEX,
        &error);

    lrhi_render_pass_set_render_pipeline(rp, render_pipeline, &error);
    lrhi_render_pass_set_viewport(rp, 0, 0, (uint32_t)width, (uint32_t)height, 0.0f, 1.0f, &error);
    lrhi_render_pass_set_scissor(rp, 0, 0, (uint32_t)width, (uint32_t)height, &error);

    struct DrawPush {
        HMM_Mat4 view_projection;
        uint32_t particle_buffer;
        float point_scale;
        float padding[2];
    } dp = {};
    dp.view_projection = make_view_projection((float)width, (float)height, camera_yaw, camera_pitch, camera_distance);
    dp.particle_buffer = particle_read_handle;
    dp.point_scale = point_scale;

    lrhi_render_pass_set_push_constants(rp, &dp, sizeof(dp), &error);
    lrhi_render_pass_draw(rp, PARTICLE_COUNT, 1, 0, 0, &error);
    lrhi_render_pass_end(rp, &error);
}

void ComputeParticlesExample::draw_ui()
{
    if (show_help) {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);

        ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

        ImGui::Begin("Compute Particles", nullptr, overlay_flags);
        ImGui::TextUnformatted("Left-drag: orbit camera");
        ImGui::TextUnformatted("Scroll: zoom");
        ImGui::TextUnformatted("Press Escape to return to menu.");
        ImGui::Separator();
        ImGui::Text("Particles: %u", PARTICLE_COUNT);
        ImGui::Checkbox("Paused", &paused);
        ImGui::SliderFloat("Simulation speed", &simulation_speed, 0.0f, 4.0f, "%.2fx");
        ImGui::SliderFloat("Point scale", &point_scale, 0.5f, 4.0f, "%.2f");
        ImGui::SliderFloat("Camera distance", &camera_distance, 8.0f, 180.0f, "%.1f");
        ImGui::Checkbox("Show help", &show_help);
        ImGui::End();
    } else {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.2f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav;
        ImGui::Begin("Particles HUD", nullptr, flags);
        ImGui::Text("1,000,000 particles");
        ImGui::Checkbox("Show help", &show_help);
        ImGui::End();
    }
}
