#include "luminary_rhi.h"
#include "window.h"
#include "extras/shader_compiler/luminary_shader_compiler.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// Shader helpers (mirrors tests/draw_helpers.h)
// ---------------------------------------------------------------------------

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
    opts.source_code      = const_cast<char*>(source.data());
    opts.source_code_size = source.size();
    opts.add_debug_symbols = 1;

    uint64_t size     = 0;
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

static LRHITextureView make_view(LRHIDevice device, LRHITexture tex,
                                 LRHITextureUsage usage,
                                 LRHITextureDimensions dimensions)
{
    LRHITextureViewInfo info = {};
    info.texture           = tex;
    info.base_mip_level    = 0;
    info.mip_level_count   = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    info.base_array_layer  = 0;
    info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    info.format            = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    info.usage             = usage;
    info.dimensions        = dimensions;
    LRHITextureView view = nullptr;
    lrhi_create_texture_view(device, &info, &view, nullptr);
    return view;
}

// ---------------------------------------------------------------------------

int main(void)
{
    LRHIError error = {};

    LRHIDevice device = nullptr;
    lrhi_create_device(LUMINARY_RHI_BACKEND_METAL4, &device, 0, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_device: %s\n", error.message);
        return 1;
    }

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_command_queue: %s\n", error.message);
        lrhi_destroy_device(device);
        return 1;
    }

    Window* window = Window::create();
    int width, height;
    window->get_width_and_height(&width, &height);

    LRHISwapChainInfo sc_info = {};
    sc_info.width              = width;
    sc_info.height             = height;
    sc_info.format             = LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM;
    sc_info.max_frames_in_flight = 3;
    sc_info.handle_type        = LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER;
    sc_info.handle.metal_layer = window->get_swap_chain_handle();

    LRHISwapChain swap_chain = nullptr;
    lrhi_create_swap_chain(device, queue, &sc_info, &swap_chain, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_swap_chain: %s\n", error.message);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    // Compile shaders
    std::string src = read_file("shaders/examples/hello_triangle.hlsl");
    if (src.empty()) {
        printf("Failed to read shader file\n");
        lrhi_destroy_swap_chain(swap_chain);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    auto [vs_bc, vs_sz] = compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
    auto [ps_bc, ps_sz] = compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");  // NOLINT
    if (!vs_bc || !ps_bc) {
        printf("Shader compilation failed\n");
        free(vs_bc); free(ps_bc);
        lrhi_destroy_swap_chain(swap_chain);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    LRHIShaderModule vs = make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain");
    LRHIShaderModule ps = make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!vs || !ps) {
        if (vs) lrhi_destroy_shader_module(vs);
        if (ps) lrhi_destroy_shader_module(ps);
        lrhi_destroy_swap_chain(swap_chain);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    // Render pipeline
    LRHIRenderPipelineInfo pi = {};
    pi.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    pi.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    pi.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    pi.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    pi.depth_test_enable        = 0;
    pi.depth_write_enable       = 0;
    pi.depth_stencil_format     = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    pi.render_target_formats[0] = LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM;
    pi.render_target_count      = 1;
    pi.vertex_shader            = vs;
    pi.fragment_shader          = ps;

    LRHIRenderPipeline pipeline = nullptr;
    lrhi_create_render_pipeline(device, &pi, &pipeline, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_render_pipeline: %s\n", error.message);
        lrhi_destroy_shader_module(vs);
        lrhi_destroy_shader_module(ps);
        lrhi_destroy_swap_chain(swap_chain);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    LRHIFence fence = nullptr;
    uint64_t  fence_val = 0;
    lrhi_create_fence(device, 0, &fence, &error);
    
    LRHIResidencySet rs = nullptr;
    lrhi_create_residency_set(device, &rs, nullptr);
    lrhi_command_queue_add_residency_set(queue, rs, nullptr);

    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &error);

    // ---------------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------------
    while (!window->should_close()) {
        @autoreleasepool {
            window->poll_events();
            
            // Acquire swapchain texture (borrowed — never destroyed by us)
            LRHITexture sc_tex = lrhi_swap_chain_get_current_texture(swap_chain, &error);
            if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                printf("get_current_texture: %s\n", error.message);
                break;
            }
        
            lrhi_residency_set_update(rs, nullptr);
        
            // Per-frame texture view
            LRHITextureView sc_view = make_view(device, sc_tex,
                LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                LUMINARY_RHI_TEXTURE_DIMENSIONS_2D);
            
            lrhi_command_list_reset(cmd, &error);
            lrhi_command_list_begin(cmd, &error);
            
            // Render pass
            LRHIRenderPassInfo rp_info = {};
            rp_info.color_attachments[0].texture_view = sc_view;
            rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
            rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
            rp_info.color_attachments[0].clear_color[0] = 0.1f;
            rp_info.color_attachments[0].clear_color[1] = 0.1f;
            rp_info.color_attachments[0].clear_color[2] = 0.1f;
            rp_info.color_attachments[0].clear_color[3] = 1.0f;
            rp_info.color_attachment_count              = 1;
            rp_info.render_width                        = (uint32_t)width;
            rp_info.render_height                       = (uint32_t)height;
            
            LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &error);
            
            lrhi_render_pass_set_render_pipeline(rp, pipeline, &error);
            lrhi_render_pass_set_viewport(rp, 0, 0, (uint32_t)width, (uint32_t)height, 0.0f, 1.0f, &error);
            lrhi_render_pass_set_scissor(rp, 0, 0, (uint32_t)width, (uint32_t)height, &error);
            lrhi_render_pass_draw(rp, 3, 1, 0, 0, &error);
            
            lrhi_render_pass_end(rp, &error);
            lrhi_command_list_end(cmd, &error);
            
            // Submit and wait
            ++fence_val;
            lrhi_command_queue_submit(queue, &cmd, 1, fence, fence_val, nullptr, 0, nullptr);
            lrhi_fence_wait(fence, fence_val, 5000000000ULL, nullptr);
            
            lrhi_swap_chain_present(swap_chain, &error);
            
            // Per-frame cleanup
            lrhi_destroy_texture_view(sc_view);
        }
    }

    // ---------------------------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------------------------
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_residency_set(rs);
    lrhi_destroy_fence(fence);
    lrhi_destroy_render_pipeline(pipeline);
    lrhi_destroy_shader_module(ps);
    lrhi_destroy_shader_module(vs);
    lrhi_destroy_swap_chain(swap_chain);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_device(device);
    delete window;
    return 0;
}
