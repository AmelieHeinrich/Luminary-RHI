#include "tests/draw_helpers.h"
#include <string>

static constexpr uint32_t W = 128;
static constexpr uint32_t H = 128;
static const float BLACK[4] = {0.0f, 0.0f, 0.0f, 1.0f};

struct PushData { float color[3]; float x_offset; };

// ---------------------------------------------------------------------------
// Shared pipeline factory
// ---------------------------------------------------------------------------

static LRHIRenderPipeline make_push_constants_pipeline(LRHIDevice device,
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
    LRHIRenderPipeline pipeline   = nullptr;
    LRHIError err = {};
    lrhi_create_render_pipeline(device, &info, &pipeline, &err);
    return pipeline;
}

// ---------------------------------------------------------------------------
// Test: push_constants
// Draws a single red triangle using a float3 color push constant.
// ---------------------------------------------------------------------------

class push_constants_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    push_constants_test()
    {
        type        = test_type::texture;
        name        = "push_constants";
        source_path = "tests/golden/push_constants.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET |
                                               LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(device, &info, &_tex, &err);
        _view = dh_make_view(device, _tex,
                             LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                             LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                             0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                             0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string src = dh_read_shader_file("shaders/tests/push_constants.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err_str);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);
        if (!_vs || !_ps) return;
        _pipeline = make_push_constants_pipeline(device, _vs, _ps);
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
                PushData p = { {1.0f, 0.0f, 0.0f}, 0.0f };
                lrhi_render_pass_set_push_constants(rp, &p, sizeof(p), &e);
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

REGISTER_TEST(push_constants_test);

// ---------------------------------------------------------------------------
// Test: push_constants_multi
// Draws three triangles (red left, green center, blue right) using separate
// push constant uploads before each draw.
// ---------------------------------------------------------------------------

class push_constants_multi_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    push_constants_multi_test()
    {
        type        = test_type::texture;
        name        = "push_constants_multi";
        source_path = "tests/golden/push_constants_multi.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET |
                                               LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(device, &info, &_tex, &err);
        _view = dh_make_view(device, _tex,
                             LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                             LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                             0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                             0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string src = dh_read_shader_file("shaders/tests/push_constants.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err_str);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);
        if (!_vs || !_ps) return;
        _pipeline = make_push_constants_pipeline(device, _vs, _ps);
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

                // Draw 1: red triangle on the left
                PushData p1 = { {1.0f, 0.0f, 0.0f}, -0.5f };
                lrhi_render_pass_set_push_constants(rp, &p1, sizeof(p1), &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 3, 1, 0, 0, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

                // Draw 2: green triangle at center
                PushData p2 = { {0.0f, 1.0f, 0.0f}, 0.0f };
                lrhi_render_pass_set_push_constants(rp, &p2, sizeof(p2), &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw(rp, 3, 1, 0, 0, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;

                // Draw 3: blue triangle on the right
                PushData p3 = { {0.0f, 0.0f, 1.0f}, 0.5f };
                lrhi_render_pass_set_push_constants(rp, &p3, sizeof(p3), &e);
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

REGISTER_TEST(push_constants_multi_test);
