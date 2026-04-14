#include "tests/draw_helpers.h"

static constexpr uint32_t W = 128;
static constexpr uint32_t H = 128;
static const float BLACK[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool make_color_rt(LRHIDevice device,
                           LRHITexture& out_tex, LRHITextureView& out_view,
                           std::string& err_out)
{
    LRHITextureInfo info = {};
    info.width        = W; info.height = H; info.depth = 1;
    info.mip_levels   = 1; info.array_layers = 1;
    info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    info.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET |
                                           LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
    info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    LRHIError err = {};
    lrhi_create_texture(device, &info, &out_tex, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create_texture: ") + err.message;
        return false;
    }
    out_view = dh_make_view(device, out_tex,
                            LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                            LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                            0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                            0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    return true;
}

static LRHIRenderPipeline make_basic_pipeline(LRHIDevice device,
                                               LRHIShaderModule vs,
                                               LRHIShaderModule ps)
{
    LRHIRenderPipelineInfo info = {};
    info.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    info.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    info.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    info.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    info.render_target_formats[0] = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    info.render_target_count      = 1;
    info.vertex_shader            = vs;
    info.fragment_shader          = ps;
    LRHIRenderPipeline pipeline = nullptr;
    LRHIError err = {};
    lrhi_create_render_pipeline(device, &info, &pipeline, &err);
    return pipeline;
}

// ---------------------------------------------------------------------------
// Test 17: draw_indexed_uint16
// Same triangle as draw_triangle_hardcoded but with a uint16 index buffer
// (index_stride = 2). The visual output must be identical.
// ---------------------------------------------------------------------------

class draw_indexed_uint16_test : public test
{
    LRHIDevice         _device    = nullptr;
    LRHITexture        _tex       = nullptr;
    LRHITextureView    _view      = nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;
    LRHIBuffer         _index_buf = nullptr;

public:
    draw_indexed_uint16_test()
    {
        type        = test_type::texture;
        name        = "draw_indexed_uint16";
        source_path = "tests/golden/draw_indexed_uint16.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/streamed_triangle.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_basic_pipeline(device, _vs, _ps);

        // uint16 index buffer: 3 * 2 = 6 bytes, stride = 2
        static const uint16_t indices[3] = { 0, 1, 2 };
        LRHIBufferInfo binfo = {};
        binfo.size   = sizeof(indices);
        binfo.stride = sizeof(uint16_t);
        binfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDEX;
        LRHIError lerr = {};
        lrhi_create_buffer(device, &binfo, &_index_buf, &lerr);
        if (_index_buf) {
            void* ptr = lrhi_buffer_map(_index_buf, nullptr);
            if (ptr) { memcpy(ptr, indices, sizeof(indices)); lrhi_buffer_unmap(_index_buf); }
        }
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_index_buf) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, &_index_buf, 1, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                // index_stride = 2 (uint16)
                lrhi_render_pass_draw_indexed(rp, 3, 1, 0, 0, 0, _index_buf, 2, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)  { lrhi_destroy_render_pipeline(_pipeline); _pipeline  = nullptr; }
        if (_ps)        { lrhi_destroy_shader_module(_ps);         _ps        = nullptr; }
        if (_vs)        { lrhi_destroy_shader_module(_vs);         _vs        = nullptr; }
        if (_index_buf) { lrhi_destroy_buffer(_index_buf);         _index_buf = nullptr; }
        if (_view)      { lrhi_destroy_texture_view(_view);        _view      = nullptr; }
        if (_tex)       { lrhi_destroy_texture(_tex);              _tex       = nullptr; }
    }
};

REGISTER_TEST(draw_indexed_uint16_test);

// ---------------------------------------------------------------------------
// Test 18: draw_shader_defines
// Compile shader_defines.hlsl twice: once with USE_RED_COLOR defined (red
// triangle) and once without (green triangle). Sample the center pixel of
// each output to verify the correct color was selected.
// ---------------------------------------------------------------------------

class draw_shader_defines_test : public test
{
    LRHIDevice _device = nullptr;

public:
    draw_shader_defines_test()
    {
        type        = test_type::validation;
        name        = "draw_shader_defines";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override { _device = device; }

    // Draw a full triangle to a 128x128 RT using the given pipeline,
    // then readback the pixel at (W/2, H/2) and return it.
    // Returns false on GPU error.
    bool draw_and_sample(LRHIDevice device,
                         LRHIRenderPipeline pipeline,
                         uint8_t out_rgba[4],
                         std::string& err_out)
    {
        LRHITexture     tex  = nullptr;
        LRHITextureView view = nullptr;

        LRHITextureInfo tinfo = {};
        tinfo.width        = W; tinfo.height = H; tinfo.depth = 1;
        tinfo.mip_levels   = 1; tinfo.array_layers = 1;
        tinfo.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tinfo.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET |
                                                LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        tinfo.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError lerr = {};
        lrhi_create_texture(device, &tinfo, &tex, &lerr);
        view = dh_make_view(device, tex,
                            LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                            LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                            0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                            0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        bool ok = dh_run_draw_pass(device, tex, view, nullptr, nullptr, 0, W, H, BLACK, err_out,
            [pipeline](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 3, 1, 0, 0, &e);
            });

        if (ok) {
            std::vector<uint8_t> readback;
            test_tools::rhi_readback_texture(device, tex, readback, 0, 0);
            // Center pixel: row = H/2, col = W/2, stride = W*4
            uint32_t idx = (H / 2) * W * 4 + (W / 2) * 4;
            if (idx + 3 < readback.size()) {
                out_rgba[0] = readback[idx + 0];
                out_rgba[1] = readback[idx + 1];
                out_rgba[2] = readback[idx + 2];
                out_rgba[3] = readback[idx + 3];
            }
        }

        if (view) lrhi_destroy_texture_view(view);
        if (tex)  lrhi_destroy_texture(tex);
        return ok;
    }

    test_result run(bool /*bake_mode*/) override
    {
        std::string src = dh_read_shader_file("shaders/tests/shader_defines.hlsl");
        if (src.empty())
            return {false, "failed to read shader_defines.hlsl"};

        // --- Pipeline A: compiled with USE_RED_COLOR ---
        const char* red_define = "USE_RED_COLOR";

        auto [vs_a_bc, vs_a_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain",
                                                     &red_define, 1);
        auto [ps_a_bc, ps_a_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain",
                                                     &red_define, 1);
        if (!vs_a_bc || !ps_a_bc) { free(vs_a_bc); free(ps_a_bc); return {false, "compilation (red) failed"}; }

        std::string err;
        LRHIShaderModule vs_a = dh_make_module(_device, vs_a_bc, vs_a_sz,
                                                LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain", err);
        LRHIShaderModule ps_a = dh_make_module(_device, ps_a_bc, ps_a_sz,
                                                LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!vs_a || !ps_a) {
            if (vs_a) lrhi_destroy_shader_module(vs_a);
            if (ps_a) lrhi_destroy_shader_module(ps_a);
            return {false, "module creation (red) failed: " + err};
        }

        LRHIRenderPipelineInfo pinfo = {};
        pinfo.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        pinfo.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        pinfo.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        pinfo.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        pinfo.render_target_formats[0] = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        pinfo.render_target_count      = 1;
        pinfo.vertex_shader            = vs_a;
        pinfo.fragment_shader          = ps_a;
        LRHIRenderPipeline pipeline_a = nullptr;
        LRHIError lerr = {};
        lrhi_create_render_pipeline(_device, &pinfo, &pipeline_a, &lerr);

        // --- Pipeline B: compiled without USE_RED_COLOR ---
        auto [vs_b_bc, vs_b_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_b_bc, ps_b_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_b_bc || !ps_b_bc) {
            free(vs_b_bc); free(ps_b_bc);
            if (pipeline_a) lrhi_destroy_render_pipeline(pipeline_a);
            lrhi_destroy_shader_module(vs_a); lrhi_destroy_shader_module(ps_a);
            return {false, "compilation (green) failed"};
        }

        LRHIShaderModule vs_b = dh_make_module(_device, vs_b_bc, vs_b_sz,
                                                LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain", err);
        LRHIShaderModule ps_b = dh_make_module(_device, ps_b_bc, ps_b_sz,
                                                LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);

        pinfo.vertex_shader   = vs_b;
        pinfo.fragment_shader = ps_b;
        LRHIRenderPipeline pipeline_b = nullptr;
        lrhi_create_render_pipeline(_device, &pinfo, &pipeline_b, &lerr);

        // --- Draw and sample ---
        uint8_t red_pixel[4]   = {0, 0, 0, 0};
        uint8_t green_pixel[4] = {0, 0, 0, 0};
        std::string draw_err;
        bool ok_a = draw_and_sample(_device, pipeline_a, red_pixel,   draw_err);
        bool ok_b = draw_and_sample(_device, pipeline_b, green_pixel, draw_err);

        // Cleanup
        if (pipeline_b) lrhi_destroy_render_pipeline(pipeline_b);
        if (pipeline_a) lrhi_destroy_render_pipeline(pipeline_a);
        if (vs_b) lrhi_destroy_shader_module(vs_b);
        if (ps_b) lrhi_destroy_shader_module(ps_b);
        lrhi_destroy_shader_module(vs_a);
        lrhi_destroy_shader_module(ps_a);

        if (!ok_a || !ok_b)
            return {false, "draw failed: " + draw_err};

        // Verify center pixel of red pipeline is red (R high, G low)
        if (red_pixel[0] < 200 || red_pixel[1] > 50)
            return {false, "expected red pixel but got R=" + std::to_string(red_pixel[0])
                         + " G=" + std::to_string(red_pixel[1])};

        // Verify center pixel of green pipeline is green (R low, G high)
        if (green_pixel[1] < 200 || green_pixel[0] > 50)
            return {false, "expected green pixel but got R=" + std::to_string(green_pixel[0])
                         + " G=" + std::to_string(green_pixel[1])};

        return {true, ""};
    }

    void cleanup() override {}
};

REGISTER_TEST(draw_shader_defines_test);
