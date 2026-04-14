#include "tests/draw_helpers.h"
#include <string>

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
// Test: draw_parameters
// Tests first_vertex and first_instance parameters.
// ---------------------------------------------------------------------------

class draw_parameters_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_parameters_test()
    {
        type        = test_type::texture;
        name        = "draw_parameters";
        // Should draw the inverted blue triangle at pos 6-8, instance 2
        source_path = "tests/golden/draw_parameters.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        make_color_rt(device, _tex, _view, err_str);

        std::string src = dh_read_shader_file("shaders/tests/draw_parameters.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err_str);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);
        if (!_vs || !_ps) return;
        _pipeline = make_basic_pipeline(device, _vs, _ps);
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
                // Draw 1 instance, 3 vertices.
                // first_vertex=6 -> gets down triangle
                // first_instance=2 -> gets blue color, pos.x += 0.2f
                lrhi_render_pass_draw(rp, 3, 1, 6, 2, &e);
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

REGISTER_TEST(draw_parameters_test);

// ---------------------------------------------------------------------------
// Test: draw_parameters_indexed
// Tests first_index, vertex_offset, and first_instance.
// ---------------------------------------------------------------------------

class draw_parameters_indexed_test : public test
{
    LRHIDevice         _device    = nullptr;
    LRHITexture        _tex       = nullptr;
    LRHITextureView    _view      = nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;
    LRHIBuffer         _index_buf = nullptr;

public:
    draw_parameters_indexed_test()
    {
        type        = test_type::texture;
        name        = "draw_parameters_indexed";
        // Should draw the inverted up triangle (baseVertex offest shifts 3 -> 6 for down triangle, so vertex 0 + 6)
        source_path = "tests/golden/draw_parameters_indexed.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        make_color_rt(device, _tex, _view, err_str);

        // Indices: length 6
        // [0..2] = {0, 1, 2}
        // [3..5] = {0, 1, 2}
        uint32_t indices[] = {0, 1, 2, 0, 1, 2};
        LRHIBufferInfo buf_info = {};
        buf_info.size   = sizeof(indices);
        buf_info.stride = 4;
        buf_info.usage  = LUMINARY_RHI_BUFFER_USAGE_INDEX;
        LRHIError err = {};
        lrhi_create_buffer(_device, &buf_info, &_index_buf, &err);
        if (_index_buf) {
            void* dst = lrhi_buffer_map(_index_buf, &err);
            memcpy(dst, indices, sizeof(indices));
            lrhi_buffer_unmap(_index_buf);
        }

        std::string src = dh_read_shader_file("shaders/tests/draw_parameters.hlsl");
        if (src.empty()) return;
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err_str);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);
        if (!_vs || !_ps) return;
        _pipeline = make_basic_pipeline(device, _vs, _ps);
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
                // Draw 1 instance, 3 indices.
                // first_index=3 -> reads {0, 1, 2}
                // vertex_offset=3 -> {3, 4, 5} gets upward triangle
                // first_instance=1 -> gets green color, pos.x += 0.0f
                lrhi_render_pass_draw_indexed(rp, 3, 1, 3, 3, 1, _index_buf, 4, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_index_buf) { lrhi_destroy_buffer(_index_buf);        _index_buf = nullptr; }
        if (_pipeline)  { lrhi_destroy_render_pipeline(_pipeline); _pipeline  = nullptr; }
        if (_ps)        { lrhi_destroy_shader_module(_ps);         _ps        = nullptr; }
        if (_vs)        { lrhi_destroy_shader_module(_vs);         _vs        = nullptr; }
        if (_view)      { lrhi_destroy_texture_view(_view);        _view      = nullptr; }
        if (_tex)       { lrhi_destroy_texture(_tex);              _tex       = nullptr; }
    }
};

REGISTER_TEST(draw_parameters_indexed_test);
