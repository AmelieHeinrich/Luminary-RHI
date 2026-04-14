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

static bool make_depth_rt(LRHIDevice device,
                           LRHITexture& out_tex, LRHITextureView& out_view,
                           std::string& /*err_out*/)
{
    LRHITextureInfo info = {};
    info.width        = W; info.height = H; info.depth = 1;
    info.mip_levels   = 1; info.array_layers = 1;
    info.format       = LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT;
    info.usage        = LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    LRHIError err = {};
    lrhi_create_texture(device, &info, &out_tex, &err);
    if (!out_tex) return false;
    out_view = dh_make_view(device, out_tex,
                            LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL,
                            LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                            0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                            0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    return true;
}

static LRHIRenderPipeline make_render_pipeline_ex(LRHIDevice device,
                                                   LRHIShaderModule vs,
                                                   LRHIShaderModule ps,
                                                   LRHIPipelineTopology  topology,
                                                   LRHIPipelineFillMode  fill,
                                                   LRHIPipelineCullMode  cull,
                                                   LRHIPipelineFrontFace ff,
                                                   bool  depth_test,
                                                   bool  blend_enable,
                                                   LRHIBlendFactor src_rgb   = LUMINARY_RHI_BLEND_FACTOR_ONE,
                                                   LRHIBlendFactor dst_rgb   = LUMINARY_RHI_BLEND_FACTOR_ZERO,
                                                   LRHIBlendOperation rgb_op = LUMINARY_RHI_BLEND_OPERATION_ADD,
                                                   LRHIBlendFactor src_a     = LUMINARY_RHI_BLEND_FACTOR_ONE,
                                                   LRHIBlendFactor dst_a     = LUMINARY_RHI_BLEND_FACTOR_ZERO,
                                                   LRHIBlendOperation a_op   = LUMINARY_RHI_BLEND_OPERATION_ADD)
{
    LRHIRenderPipelineInfo info = {};
    info.fill_mode                = fill;
    info.cull_mode                = cull;
    info.front_face               = ff;
    info.topology                 = topology;
    info.depth_test_enable        = depth_test ? 1 : 0;
    info.depth_write_enable       = depth_test ? 1 : 0;
    info.depth_compare_op         = LUMINARY_RHI_COMPARE_OPERATION_LESS;
    info.depth_stencil_format     = depth_test ? LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT
                                               : LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    info.blend_enable[0]          = blend_enable ? 1 : 0;
    info.blend_src_rgb_factor[0]  = src_rgb;
    info.blend_dst_rgb_factor[0]  = dst_rgb;
    info.blend_rgb_op[0]          = rgb_op;
    info.blend_src_alpha_factor[0]= src_a;
    info.blend_dst_alpha_factor[0]= dst_a;
    info.blend_alpha_op[0]        = a_op;
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
// Test 10: draw_viewport
// Draw a triangle with a viewport set to the left half (64x128) of the RT.
// The right half should remain cleared to black.
// ---------------------------------------------------------------------------

class draw_viewport_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_viewport_test()
    {
        type        = test_type::texture;
        name        = "draw_viewport";
        source_path = "tests/golden/draw_viewport.png";
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
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            false, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                // Half-width viewport: only left 64 pixels
                lrhi_render_pass_set_viewport(rp, 0, 0, W / 2, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 3, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_viewport_test);

// ---------------------------------------------------------------------------
// Test 11: draw_scissor
// Draw the same triangle but with a scissor rect limiting output to the
// left half. Full viewport is used, so the triangle extends to both halves,
// but the scissor clips the right side to black.
// ---------------------------------------------------------------------------

class draw_scissor_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_scissor_test()
    {
        type        = test_type::texture;
        name        = "draw_scissor";
        source_path = "tests/golden/draw_scissor.png";
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
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            false, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                // Scissor to left half only
                lrhi_render_pass_set_scissor(rp, 0, 0, W / 2, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 3, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_scissor_test);

// ---------------------------------------------------------------------------
// Test 12: draw_alpha_blend
// Draw an opaque background triangle, then a semi-transparent foreground
// triangle using SRC_ALPHA / ONE_MINUS_SRC_ALPHA blending.
// ---------------------------------------------------------------------------

class draw_alpha_blend_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_alpha_blend_test()
    {
        type        = test_type::texture;
        name        = "draw_alpha_blend";
        source_path = "tests/golden/draw_alpha_blend.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/alpha_blend.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            /*depth_test=*/false, /*blend=*/true,
            LUMINARY_RHI_BLEND_FACTOR_SRC_ALPHA,
            LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            LUMINARY_RHI_BLEND_OPERATION_ADD,
            LUMINARY_RHI_BLEND_FACTOR_ONE,
            LUMINARY_RHI_BLEND_FACTOR_ZERO,
            LUMINARY_RHI_BLEND_OPERATION_ADD);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                // Draw opaque background (verts 0-2) and semi-transparent foreground (verts 3-5)
                lrhi_render_pass_draw(rp, 6, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_alpha_blend_test);

// ---------------------------------------------------------------------------
// Test 13: draw_fragment_discard
// Full-screen triangle where the PS discards every other 8x8 block.
// ---------------------------------------------------------------------------

class draw_fragment_discard_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_fragment_discard_test()
    {
        type        = test_type::texture;
        name        = "draw_fragment_discard";
        source_path = "tests/golden/draw_fragment_discard.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/fragment_discard.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            false, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 3, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_fragment_discard_test);

// ---------------------------------------------------------------------------
// Test 14a: draw_cull_ccw_visible
// Front face = CCW, cull = BACK.
// The CCW red triangle is front-facing → visible.
// The CW blue triangle is back-facing → culled.
// ---------------------------------------------------------------------------

class draw_cull_ccw_visible_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_cull_ccw_visible_test()
    {
        type        = test_type::texture;
        name        = "draw_cull_ccw_visible";
        source_path = "tests/golden/draw_cull_ccw_visible.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/two_triangles.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_BACK,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            false, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 6, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_cull_ccw_visible_test);

// ---------------------------------------------------------------------------
// Test 14b: draw_cull_cw_culled
// Front face = CW, cull = BACK.
// The CW blue triangle is front-facing → visible.
// The CCW red triangle is back-facing → culled.
// ---------------------------------------------------------------------------

class draw_cull_cw_culled_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_cull_cw_culled_test()
    {
        type        = test_type::texture;
        name        = "draw_cull_cw_culled";
        source_path = "tests/golden/draw_cull_cw_culled.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/two_triangles.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_BACK,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_CLOCKWISE,
            false, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 6, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_cull_cw_culled_test);

// ---------------------------------------------------------------------------
// Test 15: draw_wireframe
// Draw both triangles in wireframe mode (edges only).
// ---------------------------------------------------------------------------

class draw_wireframe_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_wireframe_test()
    {
        type        = test_type::texture;
        name        = "draw_wireframe";
        source_path = "tests/golden/draw_wireframe.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/two_triangles.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_WIREFRAME,
            LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            false, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "init failed"};
        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 6, 1, 0, 0, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);        _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);              _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_wireframe_test);

// ---------------------------------------------------------------------------
// Test 16: draw_depth_test
// Draw the red triangle first (z=0.3), then the blue triangle (z=0.7).
// With LESS depth test: the already-written z=0.3 (red) blocks z=0.7 (blue)
// wherever they overlap, so red remains visible in the overlap region.
// ---------------------------------------------------------------------------

class draw_depth_test : public test
{
    LRHIDevice         _device    = nullptr;
    LRHITexture        _color_tex = nullptr;
    LRHITextureView    _color_view= nullptr;
    LRHITexture        _depth_tex = nullptr;
    LRHITextureView    _depth_view= nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;

public:
    draw_depth_test()
    {
        type        = test_type::texture;
        name        = "draw_depth_test";
        source_path = "tests/golden/draw_depth_test.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _color_tex, _color_view, err);
        make_depth_rt(device, _depth_tex, _depth_view, err);

        std::string src = dh_read_shader_file("shaders/tests/two_triangles.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline_ex(device, _vs, _ps,
            LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST,
            LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
            LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
            LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE,
            /*depth_test=*/true, false);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_depth_tex) return {false, "init failed"};

        // We need to add both color and depth textures to the residency set.
        // Use dh_run_draw_pass for the color target; depth view is passed as extra.
        // We'll create the pass manually to include the depth texture in residency.

        LRHIResidencySet rs = nullptr;
        lrhi_create_residency_set(_device, &rs, nullptr);
        lrhi_residency_set_add_texture(rs, _color_tex, nullptr);
        lrhi_residency_set_add_texture(rs, _depth_tex, nullptr);
        lrhi_residency_set_update(rs, nullptr);

        LRHIError err = {};
        LRHICommandQueue queue = nullptr;
        lrhi_create_command_queue(_device, &queue, &err);
        lrhi_command_queue_add_residency_set(queue, rs, nullptr);

        LRHIFence fence = nullptr;
        lrhi_create_fence(_device, 0, &fence, &err);
        LRHICommandList cmd = nullptr;
        lrhi_create_command_list(queue, &cmd, &err);

        err = {};
        lrhi_command_list_begin(cmd, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string msg = std::string("cmd begin: ") + err.message;
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
            return {false, msg};
        }

        LRHIRenderPassInfo rp_info = {};
        rp_info.color_attachments[0].texture_view   = _color_view;
        rp_info.color_attachments[0].load_action    = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.color_attachments[0].store_action   = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.color_attachments[0].clear_color[0] = 0.0f;
        rp_info.color_attachments[0].clear_color[1] = 0.0f;
        rp_info.color_attachments[0].clear_color[2] = 0.0f;
        rp_info.color_attachments[0].clear_color[3] = 1.0f;
        rp_info.color_attachment_count              = 1;
        rp_info.has_depth_stencil_attachment        = 1;
        rp_info.depth_stencil_attachment.texture_view = _depth_view;
        rp_info.depth_stencil_attachment.load_action  = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.depth_stencil_attachment.store_action = LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE;
        rp_info.depth_stencil_attachment.clear_depth  = 1.0f;
        rp_info.render_width  = W;
        rp_info.render_height = H;

        err = {};
        LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string msg = std::string("render_pass_begin: ") + err.message;
            lrhi_command_list_end(cmd, nullptr);
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
            return {false, msg};
        }

        err = {};
        lrhi_render_pass_set_render_pipeline(rp, _pipeline, &err);
        lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &err);
        lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &err);
        // Draw red (z=0.3) first — writes depth 0.3
        // Draw blue (z=0.7) — 0.7 > 0.3 where overlap, so LESS fails → red stays
        lrhi_render_pass_draw(rp, 6, 1, 0, 0, &err);

        lrhi_render_pass_end(rp, nullptr);

        std::string sub_err;
        bool ok = dh_submit_and_wait(_device, queue, cmd, fence, sub_err);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);

        if (!ok) return {false, sub_err};
        return dh_texture_test_result(_device, _color_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)   { lrhi_destroy_render_pipeline(_pipeline);  _pipeline   = nullptr; }
        if (_ps)         { lrhi_destroy_shader_module(_ps);          _ps         = nullptr; }
        if (_vs)         { lrhi_destroy_shader_module(_vs);          _vs         = nullptr; }
        if (_depth_view) { lrhi_destroy_texture_view(_depth_view);   _depth_view = nullptr; }
        if (_depth_tex)  { lrhi_destroy_texture(_depth_tex);         _depth_tex  = nullptr; }
        if (_color_view) { lrhi_destroy_texture_view(_color_view);   _color_view = nullptr; }
        if (_color_tex)  { lrhi_destroy_texture(_color_tex);         _color_tex  = nullptr; }
    }
};

REGISTER_TEST(draw_depth_test);
