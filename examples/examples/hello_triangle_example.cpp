#include "hello_triangle_example.h"

#include "extras/shader_compiler/luminary_shader_compiler.h"
#include "../ext/imgui/imgui.h"

#include <cstdio>
#include <cstring>
#include <string>

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

} // namespace

HelloTriangleExample::HelloTriangleExample(LRHIDevice in_device, LRHITextureFormat render_target_format)
    : device(in_device)
    , vertex_shader(nullptr)
    , fragment_shader(nullptr)
    , pipeline(nullptr)
{
    std::string src = read_file("shaders/examples/hello_triangle.hlsl");
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
    pi.depth_test_enable        = 0;
    pi.depth_write_enable       = 0;
    pi.depth_stencil_format     = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
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
    }
}

HelloTriangleExample::~HelloTriangleExample()
{
    if (pipeline) {
        lrhi_destroy_render_pipeline(pipeline);
    }
    if (fragment_shader) {
        lrhi_destroy_shader_module(fragment_shader);
    }
    if (vertex_shader) {
        lrhi_destroy_shader_module(vertex_shader);
    }
}

const char* HelloTriangleExample::name() const
{
    return "Hello Triangle";
}

bool HelloTriangleExample::is_ready() const
{
    return pipeline != nullptr;
}

void HelloTriangleExample::record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set)
{
    (void)residency_set;
    if (!pipeline) {
        return;
    }

    LRHIRenderPassInfo info = {};
    info.color_attachments[0].texture_view = target_view;
    info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
    info.color_attachments[0].clear_color[0] = 0.1f;
    info.color_attachments[0].clear_color[1] = 0.1f;
    info.color_attachments[0].clear_color[2] = 0.1f;
    info.color_attachments[0].clear_color[3] = 1.0f;
    info.color_attachment_count = 1;
    info.render_width = (uint32_t)width;
    info.render_height = (uint32_t)height;

    LRHIError error = {};
    LRHIRenderPass rp = lrhi_render_pass_begin(command_list, &info, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
        return;
    }

    lrhi_render_pass_set_render_pipeline(rp, pipeline, &error);
    lrhi_render_pass_set_viewport(rp, 0, 0, (uint32_t)width, (uint32_t)height, 0.0f, 1.0f, &error);
    lrhi_render_pass_set_scissor(rp, 0, 0, (uint32_t)width, (uint32_t)height, &error);
    lrhi_render_pass_draw(rp, 3, 1, 0, 0, &error);
    lrhi_render_pass_end(rp, &error);
}

void HelloTriangleExample::draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav;

    ImGui::Begin("Press Escape", nullptr, overlay_flags);
    ImGui::TextUnformatted("Press Escape to return to the examples menu.");
    ImGui::End();
}