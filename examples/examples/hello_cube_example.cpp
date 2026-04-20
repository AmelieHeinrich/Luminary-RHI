#include "hello_cube_example.h"

#include "extras/shader_compiler/luminary_shader_compiler.h"
#include "../ext/hmm/HMM.h"
#include "../ext/imgui/imgui.h"

#include <cstdio>
#include <cstring>
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
    opts.bytecode         = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage     = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code       = const_cast<char*>(source.data());
    opts.source_code_size  = source.size();
    opts.add_debug_symbols = 1;

    uint64_t size = 0;
    uint8_t* bytecode = luminary_compile_shader(&opts, &size);
    return {bytecode, size};
}

static LRHIShaderModule make_module(LRHIDevice device,
                                    uint8_t* bytecode, uint64_t size,
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

static std::vector<uint8_t> make_rainbow_checkerboard(uint32_t width, uint32_t height)
{
    static const uint8_t palette[][3] = {
        {255,  48,  48},
        {255, 128,  48},
        {255, 224,  48},
        { 48, 255,  96},
        { 48, 224, 255},
        { 72,  96, 255},
        {208,  72, 255},
        {255,  72, 176},
    };

    constexpr uint32_t cell_size = 16;
    std::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t tile_x = x / cell_size;
            uint32_t tile_y = y / cell_size;
            uint32_t checker = (tile_x + tile_y) & 1u;
            uint32_t palette_index = (tile_x + tile_y) % (uint32_t)(sizeof(palette) / sizeof(palette[0]));
            const uint8_t* base = palette[palette_index];

            uint8_t r = checker ? base[0] : (uint8_t)(base[0] / 5);
            uint8_t g = checker ? base[1] : (uint8_t)(base[1] / 5);
            uint8_t b = checker ? base[2] : (uint8_t)(base[2] / 5);

            uint32_t i = (y * width + x) * 4;
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

static bool upload_texture_with_staging(LRHIDevice device,
                                        LRHITexture texture,
                                        const void* data,
                                        uint32_t data_size,
                                        uint32_t bytes_per_row,
                                        uint32_t bytes_per_image,
                                        uint32_t width,
                                        uint32_t height,
                                        std::string& err_out)
{
    LRHIError err = {};

    LRHIBuffer staging_buffer = nullptr;
    LRHIBufferInfo staging_info = {};
    staging_info.size = data_size;
    staging_info.stride = 1;
    staging_info.usage = LUMINARY_RHI_BUFFER_USAGE_STAGING;
    staging_info.name = "Hello Cube Texture Staging Buffer";
    lrhi_create_buffer(device, &staging_info, &staging_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !staging_buffer) {
        err_out = std::string("create staging buffer: ") + err.message;
        return false;
    }

    void* mapped = lrhi_buffer_map(staging_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
        err_out = std::string("map staging buffer: ") + err.message;
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }
    memcpy(mapped, data, data_size);
    lrhi_buffer_unmap(staging_buffer);

    LRHIResidencySet upload_rs = nullptr;
    lrhi_create_residency_set(device, &upload_rs, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !upload_rs) {
        err_out = std::string("create upload residency set: ") + err.message;
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }
    lrhi_residency_set_add_texture(upload_rs, texture, &err);
    lrhi_residency_set_add_buffer(upload_rs, staging_buffer, &err);
    lrhi_residency_set_update(upload_rs, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("upload residency update: ") + err.message;
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !queue) {
        err_out = std::string("create upload queue: ") + err.message;
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }
    lrhi_command_queue_add_residency_set(queue, upload_rs, &err);

    LRHIFence fence = nullptr;
    lrhi_create_fence(device, 0, &fence, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !fence) {
        err_out = std::string("create upload fence: ") + err.message;
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !cmd) {
        err_out = std::string("create upload command list: ") + err.message;
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_command_list_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("begin upload command list: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    LRHIRegion region = {};
    region.x = 0;
    region.y = 0;
    region.z = 0;
    region.width = width;
    region.height = height;
    region.depth = 1;

    LRHICopyPass copy_pass = lrhi_copy_pass_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("begin copy pass: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_copy_pass_copy_buffer_to_texture(copy_pass,
        staging_buffer, 0, bytes_per_row, bytes_per_image,
        texture, region, 0, 0, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("copy buffer to texture: ") + err.message;
        lrhi_copy_pass_end(copy_pass, nullptr);
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_copy_pass_end(copy_pass, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("end copy pass: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_command_list_end(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("end upload command list: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("submit upload command list: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_fence_wait(fence, 1, 5000000000ULL, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("wait upload fence: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(upload_rs);
        lrhi_destroy_buffer(staging_buffer);
        return false;
    }

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(upload_rs);
    lrhi_destroy_buffer(staging_buffer);
    return true;
}

static HMM_Mat4 make_mvp(float aspect, float time_seconds)
{
    HMM_Mat4 projection = HMM_Perspective_LH_ZO(HMM_AngleDeg(60.0f), aspect, 0.1f, 50.0f);
    HMM_Mat4 view = HMM_LookAt_LH(HMM_V3(0.0f, 0.0f, -4.0f), HMM_V3(0.0f, 0.0f, 0.0f), HMM_V3(0.0f, 1.0f, 0.0f));
    HMM_Mat4 spin_x = HMM_Rotate_LH(time_seconds * 0.65f, HMM_V3(1.0f, 0.0f, 0.0f));
    HMM_Mat4 spin_y = HMM_Rotate_LH(time_seconds * 1.05f, HMM_V3(0.0f, 1.0f, 0.0f));
    HMM_Mat4 model = HMM_MulM4(spin_y, spin_x);
    return HMM_MulM4(HMM_MulM4(projection, view), model);
}

} // namespace

HelloCubeExample::HelloCubeExample(LRHIDevice in_device, LRHITextureFormat in_render_target_format)
    : device(in_device)
    , render_target_format(in_render_target_format)
    , vertex_shader(nullptr)
    , fragment_shader(nullptr)
    , pipeline(nullptr)
    , sampler(nullptr)
    , texture(nullptr)
    , texture_view(nullptr)
    , depth_texture(nullptr)
    , depth_view(nullptr)
    , texture_added_to_residency(false)
    , depth_added_to_residency(false)
    , depth_width(0)
    , depth_height(0)
    , texture_width(256)
    , texture_height(256)
    , texture_bindless_index(0)
    , sampler_bindless_index(0)
{
    std::string src = read_file("shaders/examples/hello_cube.hlsl");
    if (src.empty()) {
        printf("Failed to read shader file\n");
        return;
    }

    auto [vs_bc, vs_sz] = compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain");
    auto [ps_bc, ps_sz] = compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!vs_bc || !ps_bc) {
        printf("Shader compilation failed\n");
        free(vs_bc);
        free(ps_bc);
        return;
    }

    vertex_shader = make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain");
    fragment_shader = make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) lrhi_destroy_shader_module(vertex_shader);
        if (fragment_shader) lrhi_destroy_shader_module(fragment_shader);
        vertex_shader = nullptr;
        fragment_shader = nullptr;
        return;
    }

    LRHIRenderPipelineInfo pi = {};
    pi.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    pi.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    pi.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    pi.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    pi.depth_test_enable        = 1;
    pi.depth_write_enable       = 1;
    pi.depth_compare_op         = LUMINARY_RHI_COMPARE_OPERATION_LESS;
    pi.depth_stencil_format     = LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT;
    pi.render_target_formats[0] = render_target_format;
    pi.render_target_count      = 1;
    pi.vertex_shader            = vertex_shader;
    pi.fragment_shader          = fragment_shader;

    LRHIError error = {};
    lrhi_create_render_pipeline(device, &pi, &pipeline, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_render_pipeline: %s\n", error.message);
        lrhi_destroy_shader_module(vertex_shader);
        lrhi_destroy_shader_module(fragment_shader);
        vertex_shader = nullptr;
        fragment_shader = nullptr;
        pipeline = nullptr;
        return;
    }

    LRHITextureInfo texture_info = {};
    texture_info.width = texture_width;
    texture_info.height = texture_height;
    texture_info.depth = 1;
    texture_info.mip_levels = 1;
    texture_info.array_layers = 1;
    texture_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    texture_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
    texture_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;

    lrhi_create_texture(device, &texture_info, &texture, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !texture) {
        printf("create_texture: %s\n", error.message);
        return;
    }

    LRHITextureViewInfo view_info = {};
    view_info.texture = texture;
    view_info.base_mip_level = 0;
    view_info.mip_level_count = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    view_info.base_array_layer = 0;
    view_info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    view_info.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    view_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
    view_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    lrhi_create_texture_view(device, &view_info, &texture_view, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !texture_view) {
        printf("create_texture_view: %s\n", error.message);
        return;
    }

    LRHISamplerInfo sampler_info = {};
    sampler_info.min_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    sampler_info.mag_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    sampler_info.mipmap_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    sampler_info.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.min_lod = 0.0f;
    sampler_info.max_lod = 0.0f;
    lrhi_create_sampler(device, &sampler_info, &sampler, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !sampler) {
        printf("create_sampler: %s\n", error.message);
        return;
    }

    std::vector<uint8_t> pixels = make_rainbow_checkerboard(texture_width, texture_height);
    std::string upload_err;
    if (!upload_texture_with_staging(device, texture, pixels.data(), (uint32_t)pixels.size(), texture_width * 4, texture_width * texture_height * 4, texture_width, texture_height, upload_err)) {
        printf("upload_texture: %s\n", upload_err.c_str());
        return;
    }

    LRHIError lerr = {};
    texture_bindless_index = lrhi_texture_view_get_bindless_index(texture_view, &lerr);
    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("texture_view_get_bindless_index: %s\n", lerr.message);
        return;
    }
    sampler_bindless_index = lrhi_sampler_get_bindless_index(sampler, &lerr);
    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("sampler_get_bindless_index: %s\n", lerr.message);
        return;
    }
}

HelloCubeExample::~HelloCubeExample()
{
    destroy_depth_target();
    if (texture_view) {
        lrhi_destroy_texture_view(texture_view);
        texture_view = nullptr;
    }
    if (texture) {
        lrhi_destroy_texture(texture);
        texture = nullptr;
    }
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

const char* HelloCubeExample::name() const
{
    return "Hello Cube";
}

bool HelloCubeExample::is_ready() const
{
    return pipeline != nullptr && texture != nullptr && texture_view != nullptr && sampler != nullptr;
}

bool HelloCubeExample::ensure_texture_residency(LRHIResidencySet residency_set)
{
    if (!residency_set) {
        return false;
    }

    LRHIError error = {};
    if (!texture_added_to_residency) {
        lrhi_residency_set_add_texture(residency_set, texture, &error);
        if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("add texture residency: %s\n", error.message);
            return false;
        }
        texture_added_to_residency = true;
    }

    return true;
}

void HelloCubeExample::destroy_depth_target()
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

bool HelloCubeExample::ensure_depth_target(int width, int height, LRHIResidencySet residency_set)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (depth_texture && depth_width == (uint32_t)width && depth_height == (uint32_t)height) {
        return true;
    }

    destroy_depth_target();

    LRHITextureInfo depth_info = {};
    depth_info.width = (uint32_t)width;
    depth_info.height = (uint32_t)height;
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

    depth_width = (uint32_t)width;
    depth_height = (uint32_t)height;

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

void HelloCubeExample::record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set)
{
    if (!pipeline || !texture || !texture_view || !sampler) {
        return;
    }

    if (!ensure_texture_residency(residency_set)) {
        return;
    }
    if (!ensure_depth_target(width, height, residency_set)) {
        return;
    }
    lrhi_residency_set_update(residency_set, nullptr);

    LRHIRenderPassInfo info = {};
    info.color_attachments[0].texture_view = target_view;
    info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
    info.color_attachments[0].clear_color[0] = 0.03f;
    info.color_attachments[0].clear_color[1] = 0.03f;
    info.color_attachments[0].clear_color[2] = 0.05f;
    info.color_attachments[0].clear_color[3] = 1.0f;
    info.color_attachment_count = 1;
    info.has_depth_stencil_attachment = 1;
    info.depth_stencil_attachment.texture_view = depth_view;
    info.depth_stencil_attachment.load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    info.depth_stencil_attachment.store_action = LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE;
    info.depth_stencil_attachment.clear_depth = 1.0f;
    info.render_width = (uint32_t)width;
    info.render_height = (uint32_t)height;

    LRHIError error = {};
    LRHIRenderPass rp = lrhi_render_pass_begin(command_list, &info, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
        printf("render_pass_begin: %s\n", error.message);
        return;
    }

    lrhi_render_pass_set_render_pipeline(rp, pipeline, &error);
    lrhi_render_pass_set_viewport(rp, 0, 0, (uint32_t)width, (uint32_t)height, 0.0f, 1.0f, &error);
    lrhi_render_pass_set_scissor(rp, 0, 0, (uint32_t)width, (uint32_t)height, &error);

    float time_seconds = (float)ImGui::GetTime();
    HMM_Mat4 mvp = make_mvp((float)width / (float)height, time_seconds);

    struct PushConstants {
        HMM_Mat4 mvp;
        uint32_t texture_handle;
        uint32_t sampler_handle;
        float padding[2];
    } push = {};
    push.mvp = mvp;
    push.texture_handle = texture_bindless_index;
    push.sampler_handle = sampler_bindless_index;

    lrhi_render_pass_set_push_constants(rp, &push, sizeof(push), &error);
    lrhi_render_pass_draw(rp, 36, 1, 0, 0, &error);
    lrhi_render_pass_end(rp, &error);
}

void HelloCubeExample::draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav |
                                     ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("Press Escape", nullptr, overlay_flags);
    ImGui::TextUnformatted("Press Escape to return to the examples menu.");
    ImGui::TextUnformatted("Spinning cube with a CPU-generated checkerboard texture.");
    ImGui::End();
}