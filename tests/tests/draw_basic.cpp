#include "tests/draw_helpers.h"

static constexpr uint32_t W = 128;
static constexpr uint32_t H = 128;
static const float BLACK[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// ---------------------------------------------------------------------------
// Helpers — create a colour render target + view
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

// ---------------------------------------------------------------------------
// Helper — compile VS+PS into a render pipeline
// ---------------------------------------------------------------------------

static LRHIRenderPipeline make_render_pipeline(LRHIDevice device,
                                               LRHIShaderModule vs,
                                               LRHIShaderModule ps,
                                               LRHIPipelineTopology topology,
                                               bool use_depth = false,
                                               LRHIPipelineFillMode fill = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID,
                                               LRHIPipelineCullMode cull = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE,
                                               LRHIPipelineFrontFace ff  = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE)
{
    LRHIRenderPipelineInfo info = {};
    info.fill_mode                = fill;
    info.cull_mode                = cull;
    info.front_face               = ff;
    info.topology                 = topology;
    info.depth_test_enable        = use_depth ? 1 : 0;
    info.depth_write_enable       = use_depth ? 1 : 0;
    info.depth_compare_op         = LUMINARY_RHI_COMPARE_OPERATION_LESS;
    info.depth_stencil_format     = use_depth ? LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT
                                              : LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
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
// Test 3: draw_triangle_hardcoded
// ---------------------------------------------------------------------------

class draw_triangle_hardcoded_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_triangle_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_triangle_hardcoded";
        source_path = "tests/golden/draw_triangle_hardcoded.png";
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
        _pipeline = make_render_pipeline(device, _vs, _ps, LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST);
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

REGISTER_TEST(draw_triangle_hardcoded_test);

// ---------------------------------------------------------------------------
// Test 4: draw_triangle_indexed_hardcoded (uint32 indices)
// ---------------------------------------------------------------------------

class draw_triangle_indexed_hardcoded_test : public test
{
    LRHIDevice         _device    = nullptr;
    LRHITexture        _tex       = nullptr;
    LRHITextureView    _view      = nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;
    LRHIBuffer         _index_buf = nullptr;

public:
    draw_triangle_indexed_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_triangle_indexed_hardcoded";
        source_path = "tests/golden/draw_triangle_indexed_hardcoded.png";
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
        _pipeline = make_render_pipeline(device, _vs, _ps, LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST);

        // Create uint32 index buffer {0, 1, 2}
        LRHIBufferInfo binfo = {};
        binfo.size  = 3 * sizeof(uint32_t);
        binfo.stride = sizeof(uint32_t);
        binfo.usage = LUMINARY_RHI_BUFFER_USAGE_INDEX;
        LRHIError lerr = {};
        lrhi_create_buffer(device, &binfo, &_index_buf, &lerr);
        if (_index_buf) {
            uint32_t* ptr = static_cast<uint32_t*>(lrhi_buffer_map(_index_buf, nullptr));
            if (ptr) { ptr[0] = 0; ptr[1] = 1; ptr[2] = 2; lrhi_buffer_unmap(_index_buf); }
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
                lrhi_render_pass_draw_indexed(rp, 3, 1, 0, 0, 0, _index_buf, 4, &e);
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

REGISTER_TEST(draw_triangle_indexed_hardcoded_test);

// ---------------------------------------------------------------------------
// Test 5: draw_triangle_mesh_hardcoded
// ---------------------------------------------------------------------------

class draw_triangle_mesh_hardcoded_test : public test
{
    LRHIDevice        _device   = nullptr;
    LRHITexture       _tex      = nullptr;
    LRHITextureView   _view     = nullptr;
    LRHIShaderModule  _ms       = nullptr;
    LRHIShaderModule  _ps       = nullptr;
    LRHIMeshPipeline  _pipeline = nullptr;

public:
    draw_triangle_mesh_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_triangle_mesh_hardcoded";
        source_path = "tests/golden/draw_triangle_mesh_hardcoded.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        if (!lrhi_get_device_info(device).features.mesh_shading) return;

        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/mesh_shader.hlsl");
        if (src.empty()) return;

        auto [ms_bc, ms_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_MESH,     "MSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!ms_bc || !ps_bc) { free(ms_bc); free(ps_bc); return; }
        _ms = dh_make_module(device, ms_bc, ms_sz, LUMINARY_RHI_SHADER_STAGE_MESH,     "MSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_ms || !_ps) return;

        LRHIMeshPipelineInfo info = {};
        info.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        info.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        info.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        info.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        info.render_target_formats[0] = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.render_target_count      = 1;
        info.mesh_shader              = _ms;
        info.fragment_shader          = _ps;
        LRHIError lerr = {};
        lrhi_create_mesh_pipeline(device, &info, &_pipeline, &lerr);
    }

    test_result run(bool bake_mode) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};
        if (!_pipeline) return {false, "init failed"};

        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_mesh_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw_mesh_tasks(rp, 1, 1, 1, 1, 1, 1, 1, 1, 1, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_mesh_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);       _ps       = nullptr; }
        if (_ms)       { lrhi_destroy_shader_module(_ms);       _ms       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);      _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);            _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_triangle_mesh_hardcoded_test);

// ---------------------------------------------------------------------------
// Test 6: draw_triangle_task_mesh_hardcoded
// ---------------------------------------------------------------------------

class draw_triangle_task_mesh_hardcoded_test : public test
{
    LRHIDevice        _device   = nullptr;
    LRHITexture       _tex      = nullptr;
    LRHITextureView   _view     = nullptr;
    LRHIShaderModule  _as       = nullptr;
    LRHIShaderModule  _ms       = nullptr;
    LRHIShaderModule  _ps       = nullptr;
    LRHIMeshPipeline  _pipeline = nullptr;

public:
    draw_triangle_task_mesh_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_triangle_task_mesh_hardcoded";
        source_path = "tests/golden/draw_triangle_task_mesh_hardcoded.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        if (!lrhi_get_device_info(device).features.mesh_shading) return;

        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/task_shader.hlsl");
        if (src.empty()) return;

        auto [as_bc, as_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_TASK,     "ASMain");
        auto [ms_bc, ms_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_MESH,     "MSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!as_bc || !ms_bc || !ps_bc) { free(as_bc); free(ms_bc); free(ps_bc); return; }
        _as = dh_make_module(device, as_bc, as_sz, LUMINARY_RHI_SHADER_STAGE_TASK,     "ASMain", err);
        _ms = dh_make_module(device, ms_bc, ms_sz, LUMINARY_RHI_SHADER_STAGE_MESH,     "MSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_as || !_ms || !_ps) return;

        LRHIMeshPipelineInfo info = {};
        info.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        info.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        info.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        info.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        info.render_target_formats[0] = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.render_target_count      = 1;
        info.task_shader              = _as;
        info.mesh_shader              = _ms;
        info.fragment_shader          = _ps;
        LRHIError lerr = {};
        lrhi_create_mesh_pipeline(device, &info, &_pipeline, &lerr);
    }

    test_result run(bool bake_mode) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};
        if (!_pipeline) return {false, "init failed"};

        std::string err;
        bool ok = dh_run_draw_pass(_device, _tex, _view, nullptr, nullptr, 0, W, H, BLACK, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_set_mesh_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_draw_mesh_tasks(rp, 1, 1, 1, 1, 1, 1, 1, 1, 1, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_mesh_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);       _ps       = nullptr; }
        if (_ms)       { lrhi_destroy_shader_module(_ms);       _ms       = nullptr; }
        if (_as)       { lrhi_destroy_shader_module(_as);       _as       = nullptr; }
        if (_view)     { lrhi_destroy_texture_view(_view);      _view     = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);            _tex      = nullptr; }
    }
};

REGISTER_TEST(draw_triangle_task_mesh_hardcoded_test);

// ---------------------------------------------------------------------------
// Test 7: draw_lines_hardcoded
// ---------------------------------------------------------------------------

class draw_lines_hardcoded_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_lines_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_lines_hardcoded";
        source_path = "tests/golden/draw_lines_hardcoded.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/lines.hlsl");
        if (src.empty()) return;

        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline(device, _vs, _ps, LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST);
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
                lrhi_render_pass_draw(rp, 12, 1, 0, 0, &e);
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

REGISTER_TEST(draw_lines_hardcoded_test);

// ---------------------------------------------------------------------------
// Test 8: draw_lines_indexed_hardcoded
// ---------------------------------------------------------------------------

class draw_lines_indexed_hardcoded_test : public test
{
    LRHIDevice         _device    = nullptr;
    LRHITexture        _tex       = nullptr;
    LRHITextureView    _view      = nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;
    LRHIBuffer         _index_buf = nullptr;

public:
    draw_lines_indexed_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_lines_indexed_hardcoded";
        source_path = "tests/golden/draw_lines_indexed_hardcoded.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/indexed_line.hlsl");
        if (src.empty()) return;

        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline(device, _vs, _ps, LUMINARY_RHI_PIPELINE_TOPOLOGY_LINE_LIST);

        // Rectangle: 4 lines connecting 4 corner vertices
        static const uint32_t indices[8] = { 0, 1,  1, 2,  2, 3,  3, 0 };
        LRHIBufferInfo binfo = {};
        binfo.size   = sizeof(indices);
        binfo.stride = sizeof(uint32_t);
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
                lrhi_render_pass_draw_indexed(rp, 8, 1, 0, 0, 0, _index_buf, 4, &e);
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

REGISTER_TEST(draw_lines_indexed_hardcoded_test);

// ---------------------------------------------------------------------------
// Test 9: draw_points_hardcoded
// ---------------------------------------------------------------------------

class draw_points_hardcoded_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHITexture        _tex      = nullptr;
    LRHITextureView    _view     = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    draw_points_hardcoded_test()
    {
        type        = test_type::texture;
        name        = "draw_points_hardcoded";
        source_path = "tests/golden/draw_points_hardcoded.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        make_color_rt(device, _tex, _view, err);

        std::string src = dh_read_shader_file("shaders/tests/points.hlsl");
        if (src.empty()) return;

        // use_point_topology = true required for Metal Shader Converter path
        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain",
                                                nullptr, 0, /*use_point_topology=*/true);
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain",
                                                nullptr, 0, /*use_point_topology=*/true);
        if (!vs_bc || !ps_bc) { free(vs_bc); free(ps_bc); return; }
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err);
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err);
        if (!_vs || !_ps) return;
        _pipeline = make_render_pipeline(device, _vs, _ps, LUMINARY_RHI_PIPELINE_TOPOLOGY_POINT_LIST);
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
                lrhi_render_pass_draw(rp, 64, 1, 0, 0, &e);
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

REGISTER_TEST(draw_points_hardcoded_test);
