#include "tests/draw_helpers.h"

// ---------------------------------------------------------------------------
// Helper: compile VS+PS from a file and create a basic render pipeline.
// Returns the pipeline or nullptr on failure (err_out set).
// ---------------------------------------------------------------------------

static LRHIRenderPipeline make_basic_render_pipeline(LRHIDevice device,
                                                      const char* shader_path,
                                                      LRHIShaderModule& out_vs,
                                                      LRHIShaderModule& out_ps,
                                                      std::string& err_out)
{
    std::string src = dh_read_shader_file(shader_path);
    if (src.empty()) {
        err_out = std::string("failed to read ") + shader_path;
        return nullptr;
    }

    auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX,   "VSMain");
    auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!vs_bc || !ps_bc) {
        free(vs_bc); free(ps_bc);
        err_out = "shader compilation failed";
        return nullptr;
    }

    out_vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX,   "VSMain", err_out);
    if (!out_vs) return nullptr;
    out_ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_out);
    if (!out_ps) return nullptr;

    LRHIRenderPipelineInfo info = {};
    info.fill_mode                = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    info.cull_mode                = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    info.front_face               = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    info.topology                 = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    info.render_target_formats[0] = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    info.render_target_count      = 1;
    info.vertex_shader            = out_vs;
    info.fragment_shader          = out_ps;

    LRHIRenderPipeline pipeline = nullptr;
    LRHIError err = {};
    lrhi_create_render_pipeline(device, &info, &pipeline, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create_render_pipeline: ") + err.message;
        return nullptr;
    }
    return pipeline;
}

// ---------------------------------------------------------------------------
// Test 1: render_pipeline_size
// ---------------------------------------------------------------------------

class render_pipeline_size_test : public test
{
    LRHIDevice         _device   = nullptr;
    LRHIShaderModule   _vs       = nullptr;
    LRHIShaderModule   _ps       = nullptr;
    LRHIRenderPipeline _pipeline = nullptr;

public:
    render_pipeline_size_test()
    {
        type        = test_type::validation;
        name        = "render_pipeline_size";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err;
        _pipeline = make_basic_render_pipeline(device,
            "shaders/tests/streamed_triangle.hlsl", _vs, _ps, err);
    }

    test_result run(bool /*bake_mode*/) override
    {
        if (!_pipeline)
            return {false, "pipeline creation failed"};

        LRHIError err = {};
        uint64_t size = lrhi_render_pipeline_get_alloc_size(_pipeline, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, std::string("get_alloc_size error: ") + err.message};
        if (size == 0)
            return {false, "render pipeline alloc size is 0"};

        return {true, ""};
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_render_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);         _ps       = nullptr; }
        if (_vs)       { lrhi_destroy_shader_module(_vs);         _vs       = nullptr; }
    }
};

REGISTER_TEST(render_pipeline_size_test);

// ---------------------------------------------------------------------------
// Test 2: mesh_pipeline_size
// ---------------------------------------------------------------------------

class mesh_pipeline_size_test : public test
{
    LRHIDevice        _device   = nullptr;
    LRHIShaderModule  _ms       = nullptr;
    LRHIShaderModule  _ps       = nullptr;
    LRHIMeshPipeline  _pipeline = nullptr;

public:
    mesh_pipeline_size_test()
    {
        type        = test_type::validation;
        name        = "mesh_pipeline_size";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        if (!lrhi_get_device_info(device).features.mesh_shading)
            return;

        std::string src = dh_read_shader_file("shaders/tests/mesh_shader.hlsl");
        if (src.empty()) return;

        auto [ms_bc, ms_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_MESH,     "MSMain");
        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!ms_bc || !ps_bc) { free(ms_bc); free(ps_bc); return; }

        std::string err;
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

        LRHIError lrhi_err = {};
        lrhi_create_mesh_pipeline(device, &info, &_pipeline, &lrhi_err);
    }

    test_result run(bool /*bake_mode*/) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};
        if (!_pipeline)
            return {false, "mesh pipeline creation failed"};

        LRHIError err = {};
        uint64_t size = lrhi_mesh_pipeline_get_alloc_size(_pipeline, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, std::string("get_alloc_size error: ") + err.message};
        if (size == 0)
            return {false, "mesh pipeline alloc size is 0"};

        return {true, ""};
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_mesh_pipeline(_pipeline); _pipeline = nullptr; }
        if (_ps)       { lrhi_destroy_shader_module(_ps);       _ps       = nullptr; }
        if (_ms)       { lrhi_destroy_shader_module(_ms);       _ms       = nullptr; }
    }
};

REGISTER_TEST(mesh_pipeline_size_test);
