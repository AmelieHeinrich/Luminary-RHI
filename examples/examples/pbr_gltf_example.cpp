#include "pbr_gltf_example.h"

#include "extras/shader_compiler/luminary_shader_compiler.h"
#include "../ext/hmm/HMM.h"
#include "../ext/imgui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
    opts.bytecode = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code = const_cast<char*>(source.data());
    opts.source_code_size = source.size();
    opts.add_debug_symbols = 1;

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
    info.stage = stage;
    info.entry_point = entry_point;
    info.code = reinterpret_cast<const uint32_t*>(bytecode);
    info.code_size = static_cast<uint32_t>(size);

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

static HMM_Mat4 make_view_projection(float width,
                                     float height,
                                     float yaw,
                                     float pitch,
                                     HMM_Vec3 position,
                                     HMM_Vec3* out_camera_world)
{
    float aspect = (width > 0.0f && height > 0.0f) ? (width / height) : 1.0f;
    HMM_Mat4 projection = HMM_Perspective_LH_ZO(HMM_AngleDeg(60.0f), aspect, 0.05f, 1000.0f);

    float cp = cosf(pitch);
    float sp = sinf(pitch);
    float cy = cosf(yaw);
    float sy = sinf(yaw);

    HMM_Vec3 forward = HMM_NormV3(HMM_V3(cp * sy, sp, cp * cy));
    HMM_Mat4 view = HMM_LookAt_LH(position, HMM_AddV3(position, forward), HMM_V3(0.0f, 1.0f, 0.0f));

    if (out_camera_world) {
        *out_camera_world = position;
    }

    return HMM_MulM4(projection, view);
}

static bool compute_mesh_bounds(const Mesh_Result& mesh, float out_min[3], float out_max[3])
{
    out_min[0] = FLT_MAX;
    out_min[1] = FLT_MAX;
    out_min[2] = FLT_MAX;
    out_max[0] = -FLT_MAX;
    out_max[1] = -FLT_MAX;
    out_max[2] = -FLT_MAX;

    bool any = false;
    for (const MeshPrimitive& primitive : mesh.mesh_primitives) {
        if (primitive.vertex_buffer_job_index >= mesh.buffer_upload_jobs.size()) {
            continue;
        }

        const BufferUploadJob& vb = mesh.buffer_upload_jobs[primitive.vertex_buffer_job_index];
        if (vb.bytes.empty() || vb.stride < sizeof(float) * 4u) {
            continue;
        }

        size_t vertex_count = vb.bytes.size() / static_cast<size_t>(vb.stride);
        const uint8_t* base = vb.bytes.data();
        for (size_t i = 0; i < vertex_count; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * static_cast<size_t>(vb.stride));
            out_min[0] = std::min(out_min[0], p[0]);
            out_min[1] = std::min(out_min[1], p[1]);
            out_min[2] = std::min(out_min[2], p[2]);
            out_max[0] = std::max(out_max[0], p[0]);
            out_max[1] = std::max(out_max[1], p[1]);
            out_max[2] = std::max(out_max[2], p[2]);
            any = true;
        }
    }

    return any;
}

} // namespace

PbrGltfExample::PbrGltfExample(LRHIDevice in_device, LRHITextureFormat in_render_target_format)
    : device(in_device)
    , render_target_format(in_render_target_format)
    , vertex_shader(nullptr)
    , fragment_shader(nullptr)
    , pipeline(nullptr)
    , sampler(nullptr)
    , sampler_bindless_index(0)
    , depth_texture(nullptr)
    , depth_view(nullptr)
    , depth_width(0)
    , depth_height(0)
    , depth_added_to_residency(false)
    , fallback_white{}
    , fallback_black{}
    , fallback_normal{}
    , fallback_orm{}
    , fallbacks_added_to_residency(false)
    , gpu_buffers()
    , gpu_textures()
    , loaded_mesh()
    , mesh_loaded(false)
    , mesh_resources_added_to_residency(false)
    , status_message("Idle")
    , model_path{}
    , use_skinned_loader(false)
    , camera_yaw(3.14159f)
    , camera_pitch(-0.08f)
    , camera_position{0.0f, 1.2f, 2.6f}
    , camera_move_speed(4.5f)
    , camera_look_sensitivity(0.0035f)
    , exposure(1.0f)
{
    model_path[0] = '\0';
    fallback_white = {};
    fallback_black = {};
    fallback_normal = {};
    fallback_orm = {};

    if (!create_shader_pipeline()) {
        status_message = "Failed to create shader pipeline";
        return;
    }
    if (!create_sampler()) {
        status_message = "Failed to create sampler";
        return;
    }
    if (!create_fallback_textures()) {
        status_message = "Failed to create fallback textures";
        return;
    }

    const char* default_model_path = "extras/examples_assets/Sponza/Sponza.gltf";
    strncpy(model_path, default_model_path, sizeof(model_path) - 1);
    model_path[sizeof(model_path) - 1] = '\0';

    if (std::filesystem::exists(default_model_path)) {
        if (load_model_from_path(default_model_path, use_skinned_loader)) {
            status_message = "Auto-loaded default model: extras/examples_assets/Sponza/Sponza.gltf";
        } else {
            status_message = "Auto-load failed. " + status_message;
        }
    } else {
        status_message = "Default model not found. Enter a .gltf/.glb path and click Load.";
    }
}

PbrGltfExample::~PbrGltfExample()
{
    clear_loaded_model_gpu();

    auto destroy_gpu_texture = [](GpuTexture& tex) {
        if (tex.view) {
            lrhi_destroy_texture_view(tex.view);
            tex.view = nullptr;
        }
        if (tex.texture) {
            lrhi_destroy_texture(tex.texture);
            tex.texture = nullptr;
        }
        tex.bindless_index = 0;
    };

    destroy_gpu_texture(fallback_white);
    destroy_gpu_texture(fallback_black);
    destroy_gpu_texture(fallback_normal);
    destroy_gpu_texture(fallback_orm);

    destroy_depth_target();

    if (sampler) {
        lrhi_destroy_sampler(sampler);
        sampler = nullptr;
    }
    if (pipeline) {
        lrhi_destroy_render_pipeline(pipeline);
        pipeline = nullptr;
    }
    if (fragment_shader) {
        lrhi_destroy_shader_module(fragment_shader);
        fragment_shader = nullptr;
    }
    if (vertex_shader) {
        lrhi_destroy_shader_module(vertex_shader);
        vertex_shader = nullptr;
    }
}

const char* PbrGltfExample::name() const
{
    return "PBR glTF";
}

bool PbrGltfExample::is_ready() const
{
    return pipeline != nullptr && sampler != nullptr &&
           fallback_white.texture != nullptr && fallback_black.texture != nullptr &&
           fallback_normal.texture != nullptr && fallback_orm.texture != nullptr;
}

bool PbrGltfExample::create_shader_pipeline()
{
    LRHIError error = {};

    std::string src = read_file("shaders/examples/pbr_gltf.hlsl");
    if (src.empty()) {
        printf("Failed to read pbr_gltf.hlsl\n");
        return false;
    }

    auto [vs_bc, vs_sz] = compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain");
    auto [ps_bc, ps_sz] = compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!vs_bc || !ps_bc) {
        free(vs_bc);
        free(ps_bc);
        printf("Shader compilation failed for pbr_gltf.hlsl\n");
        return false;
    }

    vertex_shader = make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain");
    fragment_shader = make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!vertex_shader || !fragment_shader) {
        return false;
    }

    LRHIRenderPipelineInfo rpi = {};
    rpi.fill_mode = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    rpi.cull_mode = LUMINARY_RHI_PIPELINE_CULL_MODE_BACK;
    rpi.front_face = LUMINARY_RHI_PIPELINE_FRONT_FACE_CLOCKWISE;
    rpi.topology = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    rpi.depth_test_enable = 1;
    rpi.depth_write_enable = 1;
    rpi.depth_compare_op = LUMINARY_RHI_COMPARE_OPERATION_LESS_EQUAL;
    rpi.depth_stencil_format = LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT;
    rpi.render_target_formats[0] = render_target_format;
    rpi.render_target_count = 1;
    rpi.vertex_shader = vertex_shader;
    rpi.fragment_shader = fragment_shader;
    rpi.name = "PBR glTF Pipeline";

    lrhi_create_render_pipeline(device, &rpi, &pipeline, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !pipeline) {
        printf("create_render_pipeline: %s\n", error.message);
        return false;
    }

    return true;
}

bool PbrGltfExample::create_sampler()
{
    LRHISamplerInfo info = {};
    info.min_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    info.mag_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    info.mipmap_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    info.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    info.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    info.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    info.min_lod = 0.0f;
    info.max_lod = 1000.0f;
    info.name = "PBR glTF Sampler";

    LRHIError error = {};
    lrhi_create_sampler(device, &info, &sampler, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !sampler) {
        printf("create_sampler: %s\n", error.message);
        return false;
    }

    LRHIError lerr = {};
    sampler_bindless_index = lrhi_sampler_get_bindless_index(sampler, &lerr);
    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("sampler_get_bindless_index: %s\n", lerr.message);
        return false;
    }

    return true;
}

bool PbrGltfExample::create_single_pixel_texture(uint8_t r,
                                                 uint8_t g,
                                                 uint8_t b,
                                                 uint8_t a,
                                                 bool srgb,
                                                 const char* debug_name,
                                                 GpuTexture& out_texture)
{
    out_texture = {};

    LRHIError error = {};

    LRHITextureInfo tex_info = {};
    tex_info.width = 1;
    tex_info.height = 1;
    tex_info.depth = 1;
    tex_info.mip_levels = 1;
    tex_info.array_layers = 1;
    tex_info.format = srgb ? LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB : LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    tex_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
    tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    tex_info.name = debug_name;
    lrhi_create_texture(device, &tex_info, &out_texture.texture, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !out_texture.texture) {
        printf("create_texture: %s\n", error.message);
        return false;
    }

    LRHIBufferInfo staging_info = {};
    staging_info.size = 4;
    staging_info.stride = 1;
    staging_info.usage = LUMINARY_RHI_BUFFER_USAGE_STAGING;
    staging_info.name = "PBR Fallback Texture Staging";

    LRHIBuffer staging = nullptr;
    lrhi_create_buffer(device, &staging_info, &staging, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !staging) {
        printf("create_buffer staging: %s\n", error.message);
        return false;
    }

    void* mapped = lrhi_buffer_map(staging, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
        printf("buffer_map staging: %s\n", error.message);
        lrhi_destroy_buffer(staging);
        return false;
    }
    uint8_t pixel[4] = {r, g, b, a};
    memcpy(mapped, pixel, 4);
    lrhi_buffer_unmap(staging);

    LRHIResidencySet upload_rs = nullptr;
    lrhi_create_residency_set(device, &upload_rs, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !upload_rs) {
        printf("create_residency_set: %s\n", error.message);
        lrhi_destroy_buffer(staging);
        return false;
    }
    lrhi_residency_set_add_texture(upload_rs, out_texture.texture, &error);
    lrhi_residency_set_add_buffer(upload_rs, staging, &error);
    lrhi_residency_set_update(upload_rs, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("upload residency update: %s\n", error.message);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging);
        return false;
    }

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !queue) {
        printf("create_command_queue: %s\n", error.message);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging);
        return false;
    }
    lrhi_command_queue_add_residency_set(queue, upload_rs, &error);

    LRHIFence fence = nullptr;
    lrhi_create_fence(device, 0, &fence, &error);
    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !fence || !cmd) {
        printf("fallback upload command objects failed\n");
        if (cmd) lrhi_destroy_command_list(cmd);
        if (fence) lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging);
        return false;
    }

    lrhi_command_list_begin(cmd, &error);
    LRHICopyPass copy = lrhi_copy_pass_begin(cmd, &error);
    LRHIRegion region = {};
    region.width = 1;
    region.height = 1;
    region.depth = 1;
    lrhi_copy_pass_copy_buffer_to_texture(copy,
        staging,
        0,
        4,
        4,
        out_texture.texture,
        region,
        0,
        0,
        &error);
    lrhi_copy_pass_end(copy, &error);
    lrhi_command_list_end(cmd, &error);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, &error);
    lrhi_fence_wait(fence, 1, 5ull * 1000ull * 1000ull * 1000ull, &error);

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(upload_rs);
    lrhi_destroy_buffer(staging);

    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("fallback upload failed: %s\n", error.message);
        if (out_texture.texture) {
            lrhi_destroy_texture(out_texture.texture);
            out_texture.texture = nullptr;
        }
        return false;
    }

    LRHITextureViewInfo view_info = {};
    view_info.texture = out_texture.texture;
    view_info.base_mip_level = 0;
    view_info.mip_level_count = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    view_info.base_array_layer = 0;
    view_info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    view_info.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    view_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
    view_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    lrhi_create_texture_view(device, &view_info, &out_texture.view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !out_texture.view) {
        printf("create_texture_view fallback: %s\n", error.message);
        lrhi_destroy_texture(out_texture.texture);
        out_texture.texture = nullptr;
        return false;
    }

    LRHIError lerr = {};
    out_texture.bindless_index = lrhi_texture_view_get_bindless_index(out_texture.view, &lerr);
    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("texture_view_get_bindless_index fallback: %s\n", lerr.message);
        lrhi_destroy_texture_view(out_texture.view);
        lrhi_destroy_texture(out_texture.texture);
        out_texture.view = nullptr;
        out_texture.texture = nullptr;
        return false;
    }

    return true;
}

bool PbrGltfExample::create_fallback_textures()
{
    if (!create_single_pixel_texture(255, 255, 255, 255, true, "PBR Fallback White", fallback_white)) {
        return false;
    }
    if (!create_single_pixel_texture(0, 0, 0, 255, false, "PBR Fallback Black", fallback_black)) {
        return false;
    }
    if (!create_single_pixel_texture(128, 128, 255, 255, false, "PBR Fallback Normal", fallback_normal)) {
        return false;
    }
    if (!create_single_pixel_texture(255, 255, 0, 255, false, "PBR Fallback ORM", fallback_orm)) {
        return false;
    }
    return true;
}

void PbrGltfExample::destroy_depth_target()
{
    if (depth_view) {
        lrhi_destroy_texture_view(depth_view);
        depth_view = nullptr;
    }
    if (depth_texture) {
        lrhi_destroy_texture(depth_texture);
        depth_texture = nullptr;
    }
    depth_width = 0;
    depth_height = 0;
    depth_added_to_residency = false;
}

bool PbrGltfExample::create_depth_target(int width, int height, LRHIResidencySet residency_set)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (depth_texture && depth_width == static_cast<uint32_t>(width) && depth_height == static_cast<uint32_t>(height)) {
        return true;
    }

    destroy_depth_target();

    LRHITextureInfo depth_info = {};
    depth_info.width = static_cast<uint32_t>(width);
    depth_info.height = static_cast<uint32_t>(height);
    depth_info.depth = 1;
    depth_info.mip_levels = 1;
    depth_info.array_layers = 1;
    depth_info.format = LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT;
    depth_info.usage = LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depth_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;

    LRHIError error = {};
    lrhi_create_texture(device, &depth_info, &depth_texture, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !depth_texture) {
        printf("create_depth_texture: %s\n", error.message);
        return false;
    }

    LRHITextureViewInfo view_info = {};
    view_info.texture = depth_texture;
    view_info.base_mip_level = 0;
    view_info.mip_level_count = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    view_info.base_array_layer = 0;
    view_info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    view_info.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    view_info.usage = LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    view_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    lrhi_create_texture_view(device, &view_info, &depth_view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !depth_view) {
        printf("create_depth_view: %s\n", error.message);
        destroy_depth_target();
        return false;
    }

    depth_width = static_cast<uint32_t>(width);
    depth_height = static_cast<uint32_t>(height);

    if (residency_set && !depth_added_to_residency) {
        lrhi_residency_set_add_texture(residency_set, depth_texture, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("add depth residency: %s\n", error.message);
            destroy_depth_target();
            return false;
        }
        depth_added_to_residency = true;
    }

    return true;
}

void PbrGltfExample::clear_loaded_model_gpu()
{
    for (GpuBuffer& buffer : gpu_buffers) {
        if (buffer.view) {
            lrhi_destroy_buffer_view(buffer.view);
            buffer.view = nullptr;
        }
        if (buffer.buffer) {
            lrhi_destroy_buffer(buffer.buffer);
            buffer.buffer = nullptr;
        }
        buffer.bindless_index = 0;
    }
    gpu_buffers.clear();

    for (GpuTexture& texture : gpu_textures) {
        if (texture.view) {
            lrhi_destroy_texture_view(texture.view);
            texture.view = nullptr;
        }
        if (texture.texture) {
            lrhi_destroy_texture(texture.texture);
            texture.texture = nullptr;
        }
        texture.bindless_index = 0;
    }
    gpu_textures.clear();

    loaded_mesh = {};
    mesh_loaded = false;
    mesh_resources_added_to_residency = false;
}

bool PbrGltfExample::upload_mesh_result(const Mesh_Result& mesh_result)
{
    clear_loaded_model_gpu();

    LRHIError error = {};
    LRHICopyPass copy_pass = nullptr;
    size_t staging_cursor = 0;

    std::vector<LRHIBuffer> staging_buffers;
    std::vector<LRHIBuffer> dst_buffers(mesh_result.buffer_upload_jobs.size(), nullptr);
    std::vector<LRHITexture> dst_textures(mesh_result.texture_upload_jobs.size(), nullptr);

    LRHIResidencySet upload_rs = nullptr;
    lrhi_create_residency_set(device, &upload_rs, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !upload_rs) {
        status_message = "Failed to create upload residency set";
        return false;
    }

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !queue) {
        lrhi_destroy_residency_set(upload_rs);
        status_message = "Failed to create upload queue";
        return false;
    }
    lrhi_command_queue_add_residency_set(queue, upload_rs, &error);

    LRHIFence fence = nullptr;
    lrhi_create_fence(device, 0, &fence, &error);
    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !fence || !cmd) {
        if (cmd) lrhi_destroy_command_list(cmd);
        if (fence) lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        status_message = "Failed to create upload command objects";
        return false;
    }

    for (size_t i = 0; i < mesh_result.buffer_upload_jobs.size(); ++i) {
        const BufferUploadJob& job = mesh_result.buffer_upload_jobs[i];

        LRHIBufferInfo dst_info = {};
        dst_info.size = static_cast<uint64_t>(job.bytes.size());
        dst_info.stride = job.stride;
        dst_info.usage = job.usage;
        dst_info.name = job.debug_name.c_str();
        lrhi_create_buffer(device, &dst_info, &dst_buffers[i], &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !dst_buffers[i]) {
            status_message = "Failed to create destination buffer";
            goto upload_fail;
        }

        LRHIBufferInfo staging_info = {};
        staging_info.size = static_cast<uint64_t>(job.bytes.size());
        staging_info.stride = 1;
        staging_info.usage = LUMINARY_RHI_BUFFER_USAGE_STAGING;
        staging_info.name = "PBR Upload Staging Buffer";

        LRHIBuffer staging = nullptr;
        lrhi_create_buffer(device, &staging_info, &staging, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !staging) {
            status_message = "Failed to create staging buffer";
            goto upload_fail;
        }

        void* mapped = lrhi_buffer_map(staging, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
            status_message = "Failed to map staging buffer";
            lrhi_destroy_buffer(staging);
            goto upload_fail;
        }
        if (!job.bytes.empty()) {
            memcpy(mapped, job.bytes.data(), job.bytes.size());
        }
        lrhi_buffer_unmap(staging);

        staging_buffers.push_back(staging);
        lrhi_residency_set_add_buffer(upload_rs, dst_buffers[i], &error);
        lrhi_residency_set_add_buffer(upload_rs, staging, &error);
    }

    for (size_t i = 0; i < mesh_result.texture_upload_jobs.size(); ++i) {
        const TextureUploadJob& job = mesh_result.texture_upload_jobs[i];

        LRHITextureInfo tex_info = {};
        tex_info.width = job.width;
        tex_info.height = job.height;
        tex_info.depth = 1;
        tex_info.mip_levels = 1;
        tex_info.array_layers = 1;
        tex_info.format = job.format;
        tex_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        tex_info.name = job.debug_name.c_str();
        lrhi_create_texture(device, &tex_info, &dst_textures[i], &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !dst_textures[i]) {
            status_message = "Failed to create destination texture";
            goto upload_fail;
        }

        LRHIBufferInfo staging_info = {};
        staging_info.size = static_cast<uint64_t>(job.pixels_rgba8.size());
        staging_info.stride = 1;
        staging_info.usage = LUMINARY_RHI_BUFFER_USAGE_STAGING;
        staging_info.name = "PBR Upload Texture Staging";

        LRHIBuffer staging = nullptr;
        lrhi_create_buffer(device, &staging_info, &staging, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !staging) {
            status_message = "Failed to create texture staging buffer";
            goto upload_fail;
        }

        void* mapped = lrhi_buffer_map(staging, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
            status_message = "Failed to map texture staging buffer";
            lrhi_destroy_buffer(staging);
            goto upload_fail;
        }
        if (!job.pixels_rgba8.empty()) {
            memcpy(mapped, job.pixels_rgba8.data(), job.pixels_rgba8.size());
        }
        lrhi_buffer_unmap(staging);

        staging_buffers.push_back(staging);
        lrhi_residency_set_add_texture(upload_rs, dst_textures[i], &error);
        lrhi_residency_set_add_buffer(upload_rs, staging, &error);
    }

    lrhi_residency_set_update(upload_rs, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        status_message = "Failed to update upload residency set";
        goto upload_fail;
    }

    lrhi_command_list_begin(cmd, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        status_message = "Failed to begin upload command list";
        goto upload_fail;
    }

    copy_pass = lrhi_copy_pass_begin(cmd, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !copy_pass) {
        status_message = "Failed to begin upload copy pass";
        goto upload_fail;
    }

    staging_cursor = 0;
    for (size_t i = 0; i < mesh_result.buffer_upload_jobs.size(); ++i) {
        const BufferUploadJob& job = mesh_result.buffer_upload_jobs[i];
        lrhi_copy_pass_copy_buffer_to_buffer(copy_pass,
                                             staging_buffers[staging_cursor++],
                                             0,
                                             dst_buffers[i],
                                             0,
                                             static_cast<uint64_t>(job.bytes.size()),
                                             &error);
    }

    for (size_t i = 0; i < mesh_result.texture_upload_jobs.size(); ++i) {
        const TextureUploadJob& job = mesh_result.texture_upload_jobs[i];
        LRHIRegion region = {};
        region.width = job.width;
        region.height = job.height;
        region.depth = 1;
        lrhi_copy_pass_copy_buffer_to_texture(copy_pass,
                                              staging_buffers[staging_cursor++],
                                              0,
                                              job.bytes_per_row,
                                              job.bytes_per_image,
                                              dst_textures[i],
                                              region,
                                              0,
                                              0,
                                              &error);
    }

    lrhi_copy_pass_end(copy_pass, &error);
    lrhi_command_list_end(cmd, &error);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, &error);
    lrhi_fence_wait(fence, 1, 30ull * 1000ull * 1000ull * 1000ull, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        status_message = "Upload submit/wait failed";
        goto upload_fail;
    }

    gpu_buffers.resize(mesh_result.buffer_upload_jobs.size());
    for (size_t i = 0; i < mesh_result.buffer_upload_jobs.size(); ++i) {
        gpu_buffers[i] = {};
        gpu_buffers[i].buffer = dst_buffers[i];

        const BufferUploadJob& job = mesh_result.buffer_upload_jobs[i];
        bool needs_structured_view = (job.kind == MeshBufferKind::Vertex ||
                                      job.kind == MeshBufferKind::SkinMatrices ||
                                      job.kind == MeshBufferKind::Material ||
                                      job.kind == MeshBufferKind::Instance);

        if (needs_structured_view) {
            LRHIBufferViewInfo view_info = {};
            view_info.buffer = dst_buffers[i];
            view_info.offset = 0;
            view_info.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_STRUCTURED;
            lrhi_create_buffer_view(device, &view_info, &gpu_buffers[i].view, &error);
            if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !gpu_buffers[i].view) {
                status_message = "Failed to create buffer view";
                goto upload_fail;
            }

            LRHIError lerr = {};
            gpu_buffers[i].bindless_index = lrhi_buffer_view_get_bindless_index(gpu_buffers[i].view, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                status_message = "Failed to get buffer bindless index";
                goto upload_fail;
            }
        }
    }

    gpu_textures.resize(mesh_result.texture_upload_jobs.size());
    for (size_t i = 0; i < mesh_result.texture_upload_jobs.size(); ++i) {
        gpu_textures[i] = {};
        gpu_textures[i].texture = dst_textures[i];

        LRHITextureViewInfo view_info = {};
        view_info.texture = dst_textures[i];
        view_info.base_mip_level = 0;
        view_info.mip_level_count = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
        view_info.base_array_layer = 0;
        view_info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
        view_info.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
        view_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        view_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture_view(device, &view_info, &gpu_textures[i].view, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !gpu_textures[i].view) {
            status_message = "Failed to create texture view";
            goto upload_fail;
        }

        LRHIError lerr = {};
        gpu_textures[i].bindless_index = lrhi_texture_view_get_bindless_index(gpu_textures[i].view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            status_message = "Failed to get texture bindless index";
            goto upload_fail;
        }
    }

    for (LRHIBuffer staging : staging_buffers) {
        lrhi_destroy_buffer(staging);
    }
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(upload_rs);

    return true;

upload_fail:
    for (LRHIBuffer staging : staging_buffers) {
        lrhi_destroy_buffer(staging);
    }
    for (LRHIBuffer& b : dst_buffers) {
        if (b) {
            lrhi_destroy_buffer(b);
            b = nullptr;
        }
    }
    for (LRHITexture& t : dst_textures) {
        if (t) {
            lrhi_destroy_texture(t);
            t = nullptr;
        }
    }
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(upload_rs);
    clear_loaded_model_gpu();
    return false;
}

bool PbrGltfExample::load_model_from_path(const char* path, bool skinned_loader)
{
    if (!path || !path[0]) {
        status_message = "Path is empty";
        return false;
    }

    Mesh_Result mesh = skinned_loader ? load_skinned_mesh(path) : load_mesh(path);
    if (!mesh.success) {
        status_message = "Load failed: " + mesh.error;
        return false;
    }

    float bounds_min[3] = {};
    float bounds_max[3] = {};
    if (compute_mesh_bounds(mesh, bounds_min, bounds_max)) {
        float extent_x = bounds_max[0] - bounds_min[0];
        float extent_y = bounds_max[1] - bounds_min[1];
        float extent_z = bounds_max[2] - bounds_min[2];
        float radius = 0.5f * std::max(extent_x, std::max(extent_y, extent_z));
        float center_x = 0.5f * (bounds_min[0] + bounds_max[0]);
        float center_y = 0.5f * (bounds_min[1] + bounds_max[1]);
        float center_z = 0.5f * (bounds_min[2] + bounds_max[2]);
        float distance = std::max(1.2f, radius * 1.8f);

        camera_position[0] = center_x;
        camera_position[1] = center_y + std::max(0.6f, radius * 0.35f);
        camera_position[2] = center_z + distance;
        camera_yaw = 3.14159f;
        camera_pitch = -0.08f;
        camera_move_speed = std::max(2.0f, radius * 2.0f);
    }

    if (!upload_mesh_result(mesh)) {
        if (status_message.empty()) {
            status_message = "Failed to upload mesh";
        }
        return false;
    }

    loaded_mesh = std::move(mesh);
    mesh_loaded = true;
    mesh_resources_added_to_residency = false;
    status_message = "Loaded model successfully";
    return true;
}

uint32_t PbrGltfExample::texture_handle_from_slot(const MaterialTextureSlot& slot, uint32_t fallback_handle) const
{
    if (slot.texture_index == MODEL_LOADER_INVALID_INDEX) {
        return fallback_handle;
    }
    if (slot.texture_index >= loaded_mesh.model_textures.size()) {
        return fallback_handle;
    }

    const ModelTexture& model_texture = loaded_mesh.model_textures[slot.texture_index];
    if (model_texture.upload_job_index >= gpu_textures.size()) {
        return fallback_handle;
    }
    return gpu_textures[model_texture.upload_job_index].bindless_index;
}

void PbrGltfExample::record(LRHICommandList command_list,
                            LRHITextureView target_view,
                            int width,
                            int height,
                            LRHIResidencySet residency_set)
{
    if (!is_ready() || !create_depth_target(width, height, residency_set)) {
        return;
    }

    LRHIError error = {};

    if (residency_set && !fallbacks_added_to_residency) {
        lrhi_residency_set_add_texture(residency_set, fallback_white.texture, &error);
        lrhi_residency_set_add_texture(residency_set, fallback_black.texture, &error);
        lrhi_residency_set_add_texture(residency_set, fallback_normal.texture, &error);
        lrhi_residency_set_add_texture(residency_set, fallback_orm.texture, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("add fallback residency: %s\n", error.message);
            return;
        }
        fallbacks_added_to_residency = true;
    }

    if (residency_set && mesh_loaded && !mesh_resources_added_to_residency) {
        for (const GpuBuffer& b : gpu_buffers) {
            if (b.buffer) {
                lrhi_residency_set_add_buffer(residency_set, b.buffer, &error);
            }
        }
        for (const GpuTexture& t : gpu_textures) {
            if (t.texture) {
                lrhi_residency_set_add_texture(residency_set, t.texture, &error);
            }
        }
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("add mesh residency: %s\n", error.message);
            return;
        }
        mesh_resources_added_to_residency = true;
    }

    if (residency_set) {
        lrhi_residency_set_update(residency_set, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("residency_set_update: %s\n", error.message);
            return;
        }
    }

    LRHIRenderPassInfo info = {};
    info.color_attachments[0].texture_view = target_view;
    info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
    info.color_attachments[0].clear_color[0] = 0.03f;
    info.color_attachments[0].clear_color[1] = 0.035f;
    info.color_attachments[0].clear_color[2] = 0.045f;
    info.color_attachments[0].clear_color[3] = 1.0f;
    info.color_attachment_count = 1;
    info.has_depth_stencil_attachment = 1;
    info.depth_stencil_attachment.texture_view = depth_view;
    info.depth_stencil_attachment.load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    info.depth_stencil_attachment.store_action = LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE;
    info.depth_stencil_attachment.clear_depth = 1.0f;
    info.render_width = static_cast<uint32_t>(width);
    info.render_height = static_cast<uint32_t>(height);

    LRHIRenderPass rp = lrhi_render_pass_begin(command_list, &info, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
        printf("render_pass_begin: %s\n", error.message);
        return;
    }

    lrhi_render_pass_set_render_pipeline(rp, pipeline, &error);
    lrhi_render_pass_set_viewport(rp, 0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 0.0f, 1.0f, &error);
    lrhi_render_pass_set_scissor(rp, 0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height), &error);

    ImGuiIO& io = ImGui::GetIO();
    float delta_time = std::clamp(io.DeltaTime, 1.0f / 500.0f, 0.1f);

    if (!io.WantCaptureMouse && io.MouseDown[1]) {
        camera_yaw += io.MouseDelta.x * camera_look_sensitivity;
        camera_pitch -= io.MouseDelta.y * camera_look_sensitivity;
        camera_pitch = std::clamp(camera_pitch, -1.5f, 1.5f);
    }

    if (!io.WantCaptureKeyboard) {
        float cp = cosf(camera_pitch);
        float sp = sinf(camera_pitch);
        float cy = cosf(camera_yaw);
        float sy = sinf(camera_yaw);

        HMM_Vec3 forward = HMM_NormV3(HMM_V3(cp * sy, sp, cp * cy));
        HMM_Vec3 flat_forward = HMM_NormV3(HMM_V3(forward.X, 0.0f, forward.Z));
        HMM_Vec3 right = HMM_NormV3(HMM_Cross(HMM_V3(0.0f, 1.0f, 0.0f), forward));

        HMM_Vec3 move = HMM_V3(0.0f, 0.0f, 0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) move = HMM_AddV3(move, flat_forward);
        if (ImGui::IsKeyDown(ImGuiKey_S)) move = HMM_SubV3(move, flat_forward);
        if (ImGui::IsKeyDown(ImGuiKey_D)) move = HMM_AddV3(move, right);
        if (ImGui::IsKeyDown(ImGuiKey_A)) move = HMM_SubV3(move, right);
        if (ImGui::IsKeyDown(ImGuiKey_E)) move = HMM_AddV3(move, HMM_V3(0.0f, 1.0f, 0.0f));
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move = HMM_SubV3(move, HMM_V3(0.0f, 1.0f, 0.0f));

        float move_len_sq = move.X * move.X + move.Y * move.Y + move.Z * move.Z;
        if (move_len_sq > 1e-6f) {
            move = HMM_NormV3(move);
            float speed_scale = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f;
            float step = camera_move_speed * speed_scale * delta_time;
            camera_position[0] += move.X * step;
            camera_position[1] += move.Y * step;
            camera_position[2] += move.Z * step;
        }
    }

    HMM_Vec3 camera_world = HMM_V3(0.0f, 0.0f, 2.0f);
    HMM_Mat4 view_projection = make_view_projection(static_cast<float>(width),
                                                    static_cast<float>(height),
                                                    camera_yaw,
                                                    camera_pitch,
                                                    HMM_V3(camera_position[0], camera_position[1], camera_position[2]),
                                                    &camera_world);

    struct PushConstants
    {
        float view_projection[16];
        float camera_world[4];
        float base_color_factor[4];
        float surface_params[4];
        uint32_t handles0[4];
    } push = {};

    memcpy(push.view_projection, view_projection.Elements, sizeof(push.view_projection));
    push.camera_world[0] = camera_world.X;
    push.camera_world[1] = camera_world.Y;
    push.camera_world[2] = camera_world.Z;
    push.camera_world[3] = exposure;
    push.handles0[1] = sampler_bindless_index;

    if (mesh_loaded) {
        for (const MeshPrimitive& primitive : loaded_mesh.mesh_primitives) {
            if (primitive.vertex_buffer_job_index >= gpu_buffers.size() ||
                primitive.index_buffer_job_index >= gpu_buffers.size()) {
                continue;
            }

            const GpuBuffer& vb = gpu_buffers[primitive.vertex_buffer_job_index];
            const GpuBuffer& ib = gpu_buffers[primitive.index_buffer_job_index];
            if (!vb.buffer || !vb.view || !ib.buffer) {
                continue;
            }

            const ModelMaterial* material = nullptr;
            if (primitive.material_index < loaded_mesh.model_materials.size()) {
                material = &loaded_mesh.model_materials[primitive.material_index];
            } else if (!loaded_mesh.model_materials.empty()) {
                material = &loaded_mesh.model_materials[0];
            }
            if (!material) {
                continue;
            }

            for (int i = 0; i < 4; ++i) {
                push.base_color_factor[i] = material->base_color_factor[i];
            }
            push.surface_params[0] = material->roughness_factor;
            push.surface_params[1] = material->normal.scale;
            push.surface_params[2] = material->occlusion.scale;
            push.surface_params[3] = material->metallic_factor;

            push.handles0[0] = vb.bindless_index;
            push.handles0[2] = texture_handle_from_slot(material->base_color, fallback_white.bindless_index);
            push.handles0[3] = texture_handle_from_slot(material->normal, fallback_normal.bindless_index);

            lrhi_render_pass_set_push_constants(rp, &push, sizeof(push), &error);
            lrhi_render_pass_draw_indexed(rp,
                                          primitive.index_count,
                                          1,
                                          primitive.first_index,
                                          0,
                                          0,
                                          ib.buffer,
                                          sizeof(uint32_t),
                                          &error);
        }
    }

    lrhi_render_pass_end(rp, &error);
}

void PbrGltfExample::draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Once);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("PBR glTF Example", nullptr, flags);
    ImGui::TextUnformatted("Press Escape to return to the examples menu.");
    ImGui::TextUnformatted("RMB drag: look, WASD: move, Q/E: down/up, Shift: faster");
    ImGui::Separator();

    ImGui::InputText("Model Path", model_path, sizeof(model_path));
    ImGui::Checkbox("Use load_skinned_mesh", &use_skinned_loader);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        load_model_from_path(model_path, use_skinned_loader);
    }

    ImGui::Text("Status: %s", status_message.c_str());

    ImGui::Separator();
    ImGui::SliderFloat("Yaw", &camera_yaw, -3.14159f, 3.14159f);
    ImGui::SliderFloat("Pitch", &camera_pitch, -1.5f, 1.5f);
    ImGui::SliderFloat("Move Speed", &camera_move_speed, 0.5f, 40.0f, "%.2f");
    ImGui::SliderFloat("Look Sensitivity", &camera_look_sensitivity, 0.0005f, 0.02f, "%.4f");
    ImGui::SliderFloat("Exposure", &exposure, 0.1f, 4.0f, "%.2f");
    ImGui::Text("Camera Pos: %.2f %.2f %.2f", camera_position[0], camera_position[1], camera_position[2]);

    if (mesh_loaded) {
        ImGui::Separator();
        ImGui::Text("Loaded primitives: %u", (unsigned)loaded_mesh.mesh_primitives.size());
        ImGui::Text("Loaded instances: %u", (unsigned)loaded_mesh.model_instances.size());
        ImGui::Text("Loaded materials: %u", (unsigned)loaded_mesh.model_materials.size());
        ImGui::Text("Loaded textures: %u", (unsigned)loaded_mesh.model_textures.size());
        ImGui::Text("Optional animations: %u", (unsigned)loaded_mesh.optional_animations.size());
        ImGui::Text("Skins: %u", (unsigned)loaded_mesh.model_skins.size());

        if (!loaded_mesh.warnings.empty()) {
            ImGui::TextUnformatted("Warnings:");
            for (const std::string& warning : loaded_mesh.warnings) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
    }

    ImGui::End();
}
