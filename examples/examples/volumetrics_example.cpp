#include "volumetrics_example.h"

#include "extras/shader_compiler/luminary_shader_compiler.h"
#include "../hmm/HMM.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

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
                                                  const char* entry_point)
{
    LuminaryShaderCompilerOptions opts = {};
    opts.shading_language = LUMINARY_SHADING_LANGUAGE_HLSL;
    opts.bytecode         = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage     = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code         = const_cast<char*>(source.data());
    opts.source_code_size    = source.size();
    opts.add_debug_symbols   = 1;

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

} // namespace

VolumetricsExample::VolumetricsExample(LRHIDevice in_device, LRHITextureFormat in_render_target_format)
    : device(in_device)
    , render_target_format(in_render_target_format)
    , compute_shader(nullptr)
    , vertex_shader(nullptr)
    , fragment_shader(nullptr)
    , compute_pipeline(nullptr)
    , render_pipeline(nullptr)
    , volume_texture(nullptr)
    , volume_storage_view(nullptr)
    , volume_sampled_view(nullptr)
    , volume_sampler(nullptr)
    , resources_added_to_residency(false)
    , noise_initialized(false)
    , regenerate_requested(true)
    , volume_storage_handle(0)
    , volume_sampled_handle(0)
    , sampler_handle(0)
    , volume_resolution(64)
    , max_steps(64)
    , density_multiplier(2.0f)
    , absorption(1.7f)
    , step_scale(1.0f)
    , animate_noise(false)
    , wind_speed(0.35f)
    , camera_yaw(0.0f)
    , camera_pitch(-0.12f)
    , camera_distance(10.0f)
{
    LRHIError error = {};

    std::string compute_src = read_file("shaders/examples/volumetrics_noise_compute.hlsl");
    std::string render_src = read_file("shaders/examples/volumetrics_render.hlsl");
    if (compute_src.empty() || render_src.empty()) {
        printf("Failed to read volumetrics shaders\n");
        return;
    }

    auto [cs_bc, cs_sz] = compile_stage(compute_src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
    auto [vs_bc, vs_sz] = compile_stage(render_src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain");
    auto [ps_bc, ps_sz] = compile_stage(render_src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!cs_bc || !vs_bc || !ps_bc) {
        free(cs_bc);
        free(vs_bc);
        free(ps_bc);
        printf("Shader compilation failed\n");
        return;
    }

    compute_shader = make_module(device, cs_bc, cs_sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain");
    vertex_shader = make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain");
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
    rpi.topology = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
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

    LRHITextureInfo ti = {};
    ti.width = volume_resolution;
    ti.height = volume_resolution;
    ti.depth = volume_resolution;
    ti.mip_levels = 1;
    ti.array_layers = 1;
    ti.format = LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT;
    ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
    ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_3D;
    ti.name = "Volumetrics 3D Density";
    lrhi_create_texture(device, &ti, &volume_texture, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !volume_texture) {
        printf("create_texture: %s\n", error.message);
        return;
    }

    LRHITextureViewInfo svi = {};
    svi.texture = volume_texture;
    svi.base_mip_level = 0;
    svi.mip_level_count = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    svi.base_array_layer = 0;
    svi.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    svi.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    svi.usage = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
    svi.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_3D;
    lrhi_create_texture_view(device, &svi, &volume_storage_view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !volume_storage_view) {
        printf("create_storage_view: %s\n", error.message);
        return;
    }

    LRHITextureViewInfo rvi = svi;
    rvi.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
    lrhi_create_texture_view(device, &rvi, &volume_sampled_view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !volume_sampled_view) {
        printf("create_sampled_view: %s\n", error.message);
        return;
    }

    LRHISamplerInfo si = {};
    si.min_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    si.mag_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    si.mipmap_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    si.min_lod = 0.0f;
    si.max_lod = 0.0f;
    lrhi_create_sampler(device, &si, &volume_sampler, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !volume_sampler) {
        printf("create_sampler: %s\n", error.message);
        return;
    }

    LRHIError lerr = {};
    volume_storage_handle = lrhi_texture_view_get_bindless_index(volume_storage_view, &lerr);
    volume_sampled_handle = lrhi_texture_view_get_bindless_index(volume_sampled_view, &lerr);
    sampler_handle = lrhi_sampler_get_bindless_index(volume_sampler, &lerr);
    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("get_bindless_index: %s\n", lerr.message);
        return;
    }
}

VolumetricsExample::~VolumetricsExample()
{
    if (volume_sampler) {
        lrhi_destroy_sampler(volume_sampler);
        volume_sampler = nullptr;
    }
    if (volume_sampled_view) {
        lrhi_destroy_texture_view(volume_sampled_view);
        volume_sampled_view = nullptr;
    }
    if (volume_storage_view) {
        lrhi_destroy_texture_view(volume_storage_view);
        volume_storage_view = nullptr;
    }
    if (volume_texture) {
        lrhi_destroy_texture(volume_texture);
        volume_texture = nullptr;
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

const char* VolumetricsExample::name() const
{
    return "Volumetrics";
}

bool VolumetricsExample::is_ready() const
{
    return compute_pipeline != nullptr &&
           render_pipeline != nullptr &&
           volume_texture != nullptr &&
           volume_storage_view != nullptr &&
           volume_sampled_view != nullptr &&
           volume_sampler != nullptr;
}

bool VolumetricsExample::ensure_residency(LRHIResidencySet residency_set)
{
    if (!residency_set) {
        return false;
    }

    if (!resources_added_to_residency) {
        LRHIError error = {};
        lrhi_residency_set_add_texture(residency_set, volume_texture, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("residency add texture: %s\n", error.message);
            return false;
        }
        resources_added_to_residency = true;
    }

    return true;
}

void VolumetricsExample::record(LRHICommandList command_list,
                                LRHITextureView target_view,
                                int width,
                                int height,
                                LRHIResidencySet residency_set)
{
    if (!is_ready()) {
        return;
    }
    if (!ensure_residency(residency_set)) {
        return;
    }
    lrhi_residency_set_update(residency_set, nullptr);

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && io.MouseDown[0]) {
        camera_yaw += io.MouseDelta.x * 0.0055f;
        camera_pitch += io.MouseDelta.y * 0.0055f;
        camera_pitch = std::clamp(camera_pitch, -1.35f, 1.35f);
    }
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
        camera_distance *= (1.0f - io.MouseWheel * 0.1f);
        camera_distance = std::clamp(camera_distance, 2.2f, 40.0f);
    }

    float noise_time = (float)ImGui::GetTime() * wind_speed;
    bool should_generate = !noise_initialized || regenerate_requested;

    LRHIError error = {};
    if (should_generate) {
        struct ComputePush {
            uint32_t volume_storage;
            uint32_t resolution;
            float noise_time;
            float padding;
        } cp = {};
        cp.volume_storage = volume_storage_handle;
        cp.resolution = volume_resolution;
        cp.noise_time = noise_time;

        LRHIComputePass compute_pass = lrhi_compute_pass_begin(command_list, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !compute_pass) {
            return;
        }

        lrhi_compute_pass_set_pipeline(compute_pass, compute_pipeline, &error);
        lrhi_compute_pass_set_push_constants(compute_pass, &cp, sizeof(cp), &error);

        const uint32_t tg = 4;
        uint32_t groups = (volume_resolution + tg - 1u) / tg;
        lrhi_compute_pass_dispatch(compute_pass, groups, groups, groups, tg, tg, tg, &error);
        lrhi_compute_pass_end(compute_pass, &error);

        noise_initialized = true;
        regenerate_requested = false;
    }

    LRHIRenderPassInfo rp_info = {};
    rp_info.color_attachments[0].texture_view = target_view;
    rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
    rp_info.color_attachments[0].clear_color[0] = 0.02f;
    rp_info.color_attachments[0].clear_color[1] = 0.03f;
    rp_info.color_attachments[0].clear_color[2] = 0.06f;
    rp_info.color_attachments[0].clear_color[3] = 1.0f;
    rp_info.color_attachment_count = 1;
    rp_info.render_width = (uint32_t)width;
    rp_info.render_height = (uint32_t)height;

    LRHIRenderPass rp = lrhi_render_pass_begin(command_list, &rp_info, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
        return;
    }

    if (should_generate) {
        lrhi_render_pass_encoder_barrier(rp,
                                         LUMINARY_RHI_RENDER_STAGE_COMPUTE,
                                         LUMINARY_RHI_RENDER_STAGE_FRAGMENT,
                                         &error);
    }

    lrhi_render_pass_set_render_pipeline(rp, render_pipeline, &error);
    lrhi_render_pass_set_viewport(rp, 0, 0, (uint32_t)width, (uint32_t)height, 0.0f, 1.0f, &error);
    lrhi_render_pass_set_scissor(rp, 0, 0, (uint32_t)width, (uint32_t)height, &error);

    float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    float tan_half_fov = tanf(HMM_AngleDeg(60.0f) * 0.5f);

    HMM_Vec3 center = HMM_V3(0.0f, 1.2f, 0.0f);
    HMM_Vec3 world_up = HMM_V3(0.0f, 1.0f, 0.0f);

    float cp = cosf(camera_pitch);
    float sp = sinf(camera_pitch);
    float cy = cosf(camera_yaw);
    float sy = sinf(camera_yaw);
    HMM_Vec3 orbit_offset = HMM_V3(cp * sy * camera_distance,
                                   sp * camera_distance,
                                   cp * cy * camera_distance);
    HMM_Vec3 eye = HMM_AddV3(center, orbit_offset);

    HMM_Vec3 forward = HMM_NormV3(HMM_SubV3(center, eye));
    HMM_Vec3 right = HMM_NormV3(HMM_Cross(forward, world_up));
    HMM_Vec3 up = HMM_NormV3(HMM_Cross(right, forward));

    struct DrawPush {
        float camera_position[3];
        float density_multiplier;
        float camera_forward[3];
        float absorption;
        float camera_right[3];
        float step_scale;
        float camera_up[3];
        float tan_half_fov;
        float sun_direction[3];
        float aspect;
        float cloud_bounds_min[3];
        float pad0;
        float cloud_bounds_max[3];
        float pad1;
        uint32_t volume_texture;
        uint32_t volume_sampler;
        uint32_t max_steps;
        float noise_scroll;
    } dp = {};

    dp.camera_position[0] = eye.X;
    dp.camera_position[1] = eye.Y;
    dp.camera_position[2] = eye.Z;
    dp.density_multiplier = density_multiplier;

    dp.camera_forward[0] = forward.X;
    dp.camera_forward[1] = forward.Y;
    dp.camera_forward[2] = forward.Z;

    dp.camera_right[0] = right.X;
    dp.camera_right[1] = right.Y;
    dp.camera_right[2] = right.Z;

    dp.camera_up[0] = up.X;
    dp.camera_up[1] = up.Y;
    dp.camera_up[2] = up.Z;
    dp.tan_half_fov = tan_half_fov;

    HMM_Vec3 sun_dir = HMM_NormV3(HMM_V3(0.45f, 0.75f, -0.35f));
    dp.sun_direction[0] = sun_dir.X;
    dp.sun_direction[1] = sun_dir.Y;
    dp.sun_direction[2] = sun_dir.Z;
    dp.aspect = aspect;

    dp.absorption = absorption;
    dp.volume_texture = volume_sampled_handle;
    dp.volume_sampler = sampler_handle;
    dp.step_scale = step_scale;
    dp.max_steps = max_steps;
    dp.noise_scroll = animate_noise ? noise_time : 0.0f;

    dp.cloud_bounds_min[0] = -3.2f;
    dp.cloud_bounds_min[1] = 0.0f;
    dp.cloud_bounds_min[2] = -3.2f;
    dp.cloud_bounds_max[0] = 3.2f;
    dp.cloud_bounds_max[1] = 3.4f;
    dp.cloud_bounds_max[2] = 3.2f;

    lrhi_render_pass_set_push_constants(rp, &dp, sizeof(dp), &error);
    lrhi_render_pass_draw(rp, 3, 1, 0, 0, &error);
    lrhi_render_pass_end(rp, &error);
}

void VolumetricsExample::draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    ImGui::Begin("Volumetrics", nullptr, flags);
    ImGui::TextUnformatted("Simple Worley volume + raymarch");
    ImGui::TextUnformatted("Left-drag: orbit camera | Scroll: zoom");
    ImGui::TextUnformatted("Press Escape to return to menu.");
    ImGui::Separator();

    if (ImGui::Button("Reset Camera")) {
        camera_yaw = 0.0f;
        camera_pitch = -0.12f;
        camera_distance = 10.0f;
    }
    ImGui::SliderFloat("Camera Distance", &camera_distance, 2.2f, 40.0f, "%.2f");

    if (ImGui::Button("Regenerate Noise")) {
        regenerate_requested = true;
    }
    ImGui::Checkbox("Animate Noise", &animate_noise);
    ImGui::SliderFloat("Wind Speed", &wind_speed, 0.0f, 2.5f, "%.2f");
    ImGui::SliderFloat("Density", &density_multiplier, 0.2f, 3.0f, "%.2f");
    ImGui::SliderFloat("Absorption", &absorption, 0.4f, 4.0f, "%.2f");
    ImGui::SliderFloat("Step Scale", &step_scale, 0.5f, 2.0f, "%.2f");

    int steps = (int)max_steps;
    if (ImGui::SliderInt("Max Steps", &steps, 16, 128)) {
        max_steps = (uint32_t)steps;
    }

    ImGui::Text("Volume: %u^3", volume_resolution);
    ImGui::End();
}
