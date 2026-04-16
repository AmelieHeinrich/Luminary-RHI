#include "luminary_rhi.h"
#include "tests/draw_helpers.h"
#include <string>

static constexpr uint32_t W = 128;
static constexpr uint32_t H = 128;
static const float BLACK[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static LRHITexture make_color_tex(LRHIDevice device, LRHITextureView& out_view)
{
    LRHITextureInfo info = {};
    info.width        = W; info.height = H; info.depth = 1;
    info.mip_levels   = 1; info.array_layers = 1;
    info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    info.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET |
                                           LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
    info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    LRHITexture tex = nullptr;
    lrhi_create_texture(device, &info, &tex, nullptr);
    out_view = dh_make_view(device, tex,
                            LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                            LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                            0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                            0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    return tex;
}

static LRHITexture make_storage_tex(LRHIDevice device, LRHITextureView& out_view)
{
    LRHITextureInfo info = {};
    info.width        = W; info.height = H; info.depth = 1;
    info.mip_levels   = 1; info.array_layers = 1;
    info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    info.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                           LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
    info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
    LRHITexture tex = nullptr;
    lrhi_create_texture(device, &info, &tex, nullptr);
    out_view = dh_make_view(device, tex,
                            LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                            LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                            0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                            0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    return tex;
}

// Create draw indirect buffer (2 non-indexed draws) + count buffer (value=2).
// Sets indirect command type to DRAW.
static void make_draw_indirect_buffers(LRHIDevice device,
                                       LRHIBuffer& out_draw_buf,
                                       LRHIBuffer& out_count_buf)
{
    // Draw buffer
    LRHIBufferInfo binfo = {};
    binfo.size   = 2 * sizeof(LRHIDrawIndirectCommand);
    binfo.stride = sizeof(LRHIDrawIndirectCommand);
    binfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &binfo, &out_draw_buf, nullptr);

    auto* cmds = (LRHIDrawIndirectCommand*)lrhi_buffer_map(out_draw_buf, nullptr);
    cmds[0] = {0, 3, 1, 0, 0};
    cmds[1] = {1, 3, 1, 0, 0};
    lrhi_buffer_unmap(out_draw_buf);

    // Count buffer
    LRHIBufferInfo cinfo = {};
    cinfo.size   = sizeof(uint32_t);
    cinfo.stride = sizeof(uint32_t);
    cinfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &cinfo, &out_count_buf, nullptr);
    *(uint32_t*)lrhi_buffer_map(out_count_buf, nullptr) = 2;
    lrhi_buffer_unmap(out_count_buf);

    lrhi_buffer_set_indirect_command_type(out_draw_buf, LUMINARY_RHI_COMMAND_TYPE_DRAW, nullptr);
}

// Create draw-indexed indirect buffer (2 indexed draws) + count buffer + index buffer.
// Sets indirect command type to DRAW_INDEXED.
static void make_draw_indexed_indirect_buffers(LRHIDevice device,
                                               LRHIBuffer& out_draw_buf,
                                               LRHIBuffer& out_count_buf,
                                               LRHIBuffer& out_index_buf)
{
    // Index buffer: trivial 0,1,2 indices for 1 triangle (reused for both draws)
    LRHIBufferInfo ibinfo = {};
    ibinfo.size   = 3 * sizeof(uint32_t);
    ibinfo.stride = sizeof(uint32_t);
    ibinfo.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_INDEX | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
    lrhi_create_buffer(device, &ibinfo, &out_index_buf, nullptr);
    uint32_t* idx = (uint32_t*)lrhi_buffer_map(out_index_buf, nullptr);
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    lrhi_buffer_unmap(out_index_buf);

    // Draw indexed buffer
    LRHIBufferInfo binfo = {};
    binfo.size   = 2 * sizeof(LRHIDrawIndexedIndirectCommand);
    binfo.stride = sizeof(LRHIDrawIndexedIndirectCommand);
    binfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &binfo, &out_draw_buf, nullptr);

    auto* cmds = (LRHIDrawIndexedIndirectCommand*)lrhi_buffer_map(out_draw_buf, nullptr);
    cmds[0] = {0, 3, 1, 0, 0, 0};
    cmds[1] = {1, 3, 1, 0, 0, 0};
    lrhi_buffer_unmap(out_draw_buf);

    // Count buffer
    LRHIBufferInfo cinfo = {};
    cinfo.size   = sizeof(uint32_t);
    cinfo.stride = sizeof(uint32_t);
    cinfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &cinfo, &out_count_buf, nullptr);
    *(uint32_t*)lrhi_buffer_map(out_count_buf, nullptr) = 2;
    lrhi_buffer_unmap(out_count_buf);

    lrhi_buffer_set_indirect_command_type(out_draw_buf, LUMINARY_RHI_COMMAND_TYPE_DRAW_INDEXED, nullptr);
}

// Create mesh-tasks indirect buffer (2 draws) + count buffer.
// Sets indirect command type to DRAW_MESH_TASKS.
static void make_draw_mesh_indirect_buffers(LRHIDevice device,
                                            LRHIBuffer& out_draw_buf,
                                            LRHIBuffer& out_count_buf)
{
    LRHIBufferInfo binfo = {};
    binfo.size   = 2 * sizeof(LRHIDrawMeshTasksIndirectCommand);
    binfo.stride = sizeof(LRHIDrawMeshTasksIndirectCommand);
    binfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &binfo, &out_draw_buf, nullptr);

    auto* cmds = (LRHIDrawMeshTasksIndirectCommand*)lrhi_buffer_map(out_draw_buf, nullptr);
    cmds[0] = {0, 1, 1, 1};
    cmds[1] = {1, 1, 1, 1};
    lrhi_buffer_unmap(out_draw_buf);

    LRHIBufferInfo cinfo = {};
    cinfo.size   = sizeof(uint32_t);
    cinfo.stride = sizeof(uint32_t);
    cinfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &cinfo, &out_count_buf, nullptr);
    *(uint32_t*)lrhi_buffer_map(out_count_buf, nullptr) = 2;
    lrhi_buffer_unmap(out_count_buf);

    lrhi_buffer_set_indirect_command_type(out_draw_buf, LUMINARY_RHI_COMMAND_TYPE_DRAW_MESH_TASKS, nullptr);
}

// Create dispatch indirect buffer (1 dispatch command).
static void make_dispatch_indirect_buffer(LRHIDevice device, LRHIBuffer& out_buf)
{
    LRHIBufferInfo binfo = {};
    binfo.size   = sizeof(LRHIDispatchIndirectCommand);
    binfo.stride = sizeof(LRHIDispatchIndirectCommand);
    binfo.usage  = LUMINARY_RHI_BUFFER_USAGE_INDIRECT_COMMANDS;
    lrhi_create_buffer(device, &binfo, &out_buf, nullptr);

    auto* cmd = (LRHIDispatchIndirectCommand*)lrhi_buffer_map(out_buf, nullptr);
    *cmd = {16, 16, 1};
    lrhi_buffer_unmap(out_buf);

    lrhi_buffer_set_indirect_command_type(out_buf, LUMINARY_RHI_COMMAND_TYPE_DISPATCH, nullptr);
}

// Run a render indirect pass: prepare outside, then begin render pass and execute.
// body(rp, e) sets pipeline/viewport/scissor/etc. and calls execute_indirect_commands.
template<typename F>
static bool run_render_indirect_pass(LRHIDevice device,
                                     LRHITexture color_tex,
                                     LRHITextureView color_view,
                                     LRHIBuffer draw_buf,
                                     LRHIBuffer count_buf,
                                     uint64_t max_commands,
                                     LRHIDrawIndirectParameters* params,
                                     LRHIRenderPipeline pipeline,
                                     const void* push_constants,
                                     uint32_t push_constant_size,
                                     LRHIBuffer* extra_buffers,
                                     uint32_t extra_buffer_count,
                                     std::string& err_out,
                                     F&& body)
{
    LRHIResidencySet rs = nullptr;
    lrhi_create_residency_set(device, &rs, nullptr);
    lrhi_residency_set_add_texture(rs, color_tex, nullptr);
    lrhi_residency_set_add_buffer(rs, draw_buf, nullptr);
    lrhi_residency_set_add_buffer(rs, count_buf, nullptr);
    for (uint32_t i = 0; i < extra_buffer_count; ++i)
        lrhi_residency_set_add_buffer(rs, extra_buffers[i], nullptr);
    lrhi_residency_set_update(rs, nullptr);

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, nullptr);
    lrhi_command_queue_add_residency_set(queue, rs, nullptr);

    LRHIFence fence = nullptr;
    lrhi_create_fence(device, 0, &fence, nullptr);
    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, nullptr);

    LRHIError err = {};
    lrhi_command_list_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd begin: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    // Prepare indirect commands outside any pass
    err = {};
    lrhi_command_list_prepare_indirect_commands(cmd, draw_buf, count_buf, max_commands,
        params, pipeline, push_constants, push_constant_size, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("prepare_indirect: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    // Begin render pass
    LRHIRenderPassInfo rp_info = {};
    rp_info.color_attachments[0].texture_view   = color_view;
    rp_info.color_attachments[0].load_action    = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].store_action   = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].clear_color[0] = BLACK[0];
    rp_info.color_attachments[0].clear_color[1] = BLACK[1];
    rp_info.color_attachments[0].clear_color[2] = BLACK[2];
    rp_info.color_attachments[0].clear_color[3] = BLACK[3];
    rp_info.color_attachment_count              = 1;
    rp_info.render_width                        = W;
    rp_info.render_height                       = H;

    err = {};
    LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("render_pass_begin: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    body(rp, err);

    lrhi_render_pass_end(rp, nullptr);
    lrhi_command_list_end(cmd, nullptr);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(rs);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("draw: ") + err.message;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test 1: draw_indirect
// 2 red triangles positioned left/right by draw ID via non-indexed indirect draw
// ---------------------------------------------------------------------------

class draw_indirect_test : public test
{
    struct PushData { float color[3]; };

    LRHIDevice         _device    = nullptr;
    LRHITexture        _tex       = nullptr;
    LRHITextureView    _view      = nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;
    LRHIBuffer         _draw_buf  = nullptr;
    LRHIBuffer         _count_buf = nullptr;

public:
    draw_indirect_test()
    {
        type        = test_type::texture;
        name        = "draw_indirect";
        source_path = "tests/golden/draw_indirect.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        _tex = make_color_tex(device, _view);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/indirect_draw.hlsl");
        if (src.empty()) return;

        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain");
        if (!vs_bc) return;
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain", err_str);

        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!ps_bc) return;
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);

        if (!_vs || !_ps) return;

        LRHIRenderPipelineInfo info = {};
        info.supports_indirect_commands   = 1;
        info.fill_mode                    = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        info.cull_mode                    = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        info.front_face                   = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        info.topology                     = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        info.render_target_formats[0]     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.render_target_count          = 1;
        info.vertex_shader                = _vs;
        info.fragment_shader              = _ps;
        lrhi_create_render_pipeline(device, &info, &_pipeline, nullptr);

        make_draw_indirect_buffers(device, _draw_buf, _count_buf);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_draw_buf || !_count_buf) return {false, "init failed"};

        PushData push = {1.0f, 0.0f, 0.0f};
        LRHIDrawIndirectParameters params = {};
        std::string err;
        bool ok = run_render_indirect_pass(_device, _tex, _view,
            _draw_buf, _count_buf, 2, &params, _pipeline,
            &push, sizeof(push), nullptr, 0, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_encoder_barrier(rp, LUMINARY_RHI_RENDER_STAGE_VERTEX, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_execute_indirect_commands(rp, _draw_buf, _count_buf, 2, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_count_buf) { lrhi_destroy_buffer(_count_buf);          _count_buf = nullptr; }
        if (_draw_buf)  { lrhi_destroy_buffer(_draw_buf);           _draw_buf  = nullptr; }
        if (_pipeline)  { lrhi_destroy_render_pipeline(_pipeline);  _pipeline  = nullptr; }
        if (_ps)        { lrhi_destroy_shader_module(_ps);          _ps        = nullptr; }
        if (_vs)        { lrhi_destroy_shader_module(_vs);          _vs        = nullptr; }
        if (_view)      { lrhi_destroy_texture_view(_view);         _view      = nullptr; }
        if (_tex)       { lrhi_destroy_texture(_tex);               _tex       = nullptr; }
    }
};

REGISTER_TEST(draw_indirect_test);

// ---------------------------------------------------------------------------
// Test 2: draw_indexed_indirect
// Same 2 triangles, but via indexed draw indirect
// ---------------------------------------------------------------------------

class draw_indexed_indirect_test : public test
{
    struct PushData { float color[3]; };

    LRHIDevice         _device    = nullptr;
    LRHITexture        _tex       = nullptr;
    LRHITextureView    _view      = nullptr;
    LRHIShaderModule   _vs        = nullptr;
    LRHIShaderModule   _ps        = nullptr;
    LRHIRenderPipeline _pipeline  = nullptr;
    LRHIBuffer         _draw_buf  = nullptr;
    LRHIBuffer         _count_buf = nullptr;
    LRHIBuffer         _index_buf = nullptr;

public:
    draw_indexed_indirect_test()
    {
        type        = test_type::texture;
        name        = "draw_indexed_indirect";
        source_path = "tests/golden/draw_indexed_indirect.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        _tex = make_color_tex(device, _view);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/indirect_draw_indexed.hlsl");
        if (src.empty()) return;

        auto [vs_bc, vs_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain");
        if (!vs_bc) return;
        _vs = dh_make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain", err_str);

        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!ps_bc) return;
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);

        if (!_vs || !_ps) return;

        LRHIRenderPipelineInfo info = {};
        info.supports_indirect_commands   = 1;
        info.fill_mode                    = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        info.cull_mode                    = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        info.front_face                   = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        info.topology                     = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        info.render_target_formats[0]     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.render_target_count          = 1;
        info.vertex_shader                = _vs;
        info.fragment_shader              = _ps;
        lrhi_create_render_pipeline(device, &info, &_pipeline, nullptr);

        make_draw_indexed_indirect_buffers(device, _draw_buf, _count_buf, _index_buf);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_draw_buf || !_count_buf || !_index_buf) return {false, "init failed"};

        PushData push = {1.0f, 0.0f, 0.0f};
        LRHIDrawIndirectParameters params = {};
        params.index_buffer = _index_buf;
        std::string err;
        bool ok = run_render_indirect_pass(_device, _tex, _view,
            _draw_buf, _count_buf, 2, &params, _pipeline,
            &push, sizeof(push), &_index_buf, 1, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_encoder_barrier(rp, LUMINARY_RHI_RENDER_STAGE_VERTEX, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_render_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_execute_indirect_commands(rp, _draw_buf, _count_buf, 2, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_index_buf) { lrhi_destroy_buffer(_index_buf);          _index_buf = nullptr; }
        if (_count_buf) { lrhi_destroy_buffer(_count_buf);          _count_buf = nullptr; }
        if (_draw_buf)  { lrhi_destroy_buffer(_draw_buf);           _draw_buf  = nullptr; }
        if (_pipeline)  { lrhi_destroy_render_pipeline(_pipeline);  _pipeline  = nullptr; }
        if (_ps)        { lrhi_destroy_shader_module(_ps);          _ps        = nullptr; }
        if (_vs)        { lrhi_destroy_shader_module(_vs);          _vs        = nullptr; }
        if (_view)      { lrhi_destroy_texture_view(_view);         _view      = nullptr; }
        if (_tex)       { lrhi_destroy_texture(_tex);               _tex       = nullptr; }
    }
};

REGISTER_TEST(draw_indexed_indirect_test);

// ---------------------------------------------------------------------------
// Test 3: dispatch_indirect
// Gradient via indirect compute dispatch (reuses compute_texture_write.hlsl)
// ---------------------------------------------------------------------------

class dispatch_indirect_test : public test
{
    LRHIDevice          _device   = nullptr;
    LRHITexture         _tex      = nullptr;
    LRHITextureView     _tex_view = nullptr;
    LRHIShaderModule    _cs       = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet    _rs       = nullptr;
    LRHIBuffer          _icb      = nullptr;

public:
    dispatch_indirect_test()
    {
        type        = test_type::texture;
        name        = "dispatch_indirect";
        source_path = "tests/golden/dispatch_indirect.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        _tex = make_storage_tex(device, _tex_view);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_write.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_cs) return;

        LRHIComputePipelineInfo info = {};
        info.compute_shader              = _cs;
        info.supports_indirect_commands  = 1;
        lrhi_create_compute_pipeline(device, &info, &_pipeline, nullptr);

        make_dispatch_indirect_buffer(device, _icb);

        lrhi_create_residency_set(device, &_rs, nullptr);
        lrhi_residency_set_add_texture(_rs, _tex, nullptr);
        lrhi_residency_set_add_buffer(_rs, _icb, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_tex || !_tex_view || !_icb) return {false, "init failed"};

        LRHIError lerr = {};
        uint32_t bindless_index = lrhi_texture_view_get_bindless_index(_tex_view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get bindless index"};

        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        lrhi_create_command_queue(_device, &queue, nullptr);
        lrhi_create_fence(_device, 0, &fence, nullptr);
        lrhi_create_command_list(queue, &cmd, nullptr);
        lrhi_command_queue_add_residency_set(queue, _rs, nullptr);

        lerr = {};
        lrhi_command_list_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_command_list(cmd);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return {false, std::string("cmd begin: ") + lerr.message};
        }

        // Prepare dispatch indirect (pipeline param is null for compute — dispatch case ignores it)
        lerr = {};
        LRHIDrawIndirectParameters params = {};
        params.threads_per_group_x = 8;
        params.threads_per_group_y = 8;
        params.threads_per_group_z = 1;
        lrhi_command_list_prepare_indirect_commands(cmd, _icb, nullptr, 1,
            &params, nullptr, nullptr, 0, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_command_list_end(cmd, nullptr);
            lrhi_destroy_command_list(cmd);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return {false, std::string("prepare_indirect: ") + lerr.message};
        }

        lerr = {};
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_command_list_end(cmd, nullptr);
            lrhi_destroy_command_list(cmd);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return {false, std::string("compute_pass_begin: ") + lerr.message};
        }

        lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &lerr);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &lerr);
        struct PushConstants { uint32_t texture_descriptor; } push_data = {bindless_index};
        lrhi_compute_pass_set_push_constants(cp, &push_data, sizeof(push_data), &lerr);
        lrhi_compute_pass_dispatch_indirect(cp, _icb, &lerr);
        lrhi_compute_pass_end(cp, nullptr);

        lrhi_command_list_end(cmd, nullptr);
        lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
        lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
        lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);

        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, std::string("compute: ") + lerr.message};

        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_icb)      { lrhi_destroy_buffer(_icb);                 _icb      = nullptr; }
        if (_rs)       { lrhi_destroy_residency_set(_rs);           _rs       = nullptr; }
        if (_pipeline) { lrhi_destroy_compute_pipeline(_pipeline);  _pipeline = nullptr; }
        if (_cs)       { lrhi_destroy_shader_module(_cs);           _cs       = nullptr; }
        if (_tex_view) { lrhi_destroy_texture_view(_tex_view);      _tex_view = nullptr; }
        if (_tex)      { lrhi_destroy_texture(_tex);                _tex      = nullptr; }
    }
};

REGISTER_TEST(dispatch_indirect_test);

// ---------------------------------------------------------------------------
// Test 4: draw_mesh_indirect
// 2 mesh-shaded triangles positioned by draw ID via indirect draw_mesh_tasks
// ---------------------------------------------------------------------------

class draw_mesh_indirect_test : public test
{
    struct PushData { float color[3]; };

    LRHIDevice        _device    = nullptr;
    LRHITexture       _tex       = nullptr;
    LRHITextureView   _view      = nullptr;
    LRHIShaderModule  _ms        = nullptr;
    LRHIShaderModule  _ps        = nullptr;
    LRHIMeshPipeline  _pipeline  = nullptr;
    LRHIBuffer        _draw_buf  = nullptr;
    LRHIBuffer        _count_buf = nullptr;

public:
    draw_mesh_indirect_test()
    {
        type        = test_type::texture;
        name        = "draw_mesh_indirect";
        source_path = "tests/golden/draw_mesh_indirect.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        if (!lrhi_get_device_info(device).features.mesh_shading) return;
        _tex = make_color_tex(device, _view);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/indirect_mesh.hlsl");
        if (src.empty()) return;

        auto [ms_bc, ms_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_MESH, "MSMain");
        if (!ms_bc) return;
        _ms = dh_make_module(device, ms_bc, ms_sz, LUMINARY_RHI_SHADER_STAGE_MESH, "MSMain", err_str);

        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!ps_bc) return;
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);

        if (!_ms || !_ps) return;

        LRHIMeshPipelineInfo info = {};
        info.supports_indirect_commands   = 1;
        info.fill_mode                    = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        info.cull_mode                    = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        info.front_face                   = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        info.topology                     = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        info.render_target_formats[0]     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.render_target_count          = 1;
        info.mesh_shader                  = _ms;
        info.fragment_shader              = _ps;
        lrhi_create_mesh_pipeline(device, &info, &_pipeline, nullptr);

        make_draw_mesh_indirect_buffers(device, _draw_buf, _count_buf);
    }

    test_result run(bool bake_mode) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};
        if (!_pipeline || !_draw_buf || !_count_buf) return {false, "init failed"};

        PushData push = {1.0f, 0.0f, 0.0f};
        LRHIDrawIndirectParameters params = {};
        params.threads_per_object_groups_x = 1;
        params.threads_per_object_groups_y = 1;
        params.threads_per_object_groups_z = 1;
        params.threads_per_mesh_groups_x   = 1;
        params.threads_per_mesh_groups_y   = 1;
        params.threads_per_mesh_groups_z   = 1;

        std::string err;
        bool ok = run_render_indirect_pass(_device, _tex, _view,
            _draw_buf, _count_buf, 2, &params,
            (LRHIRenderPipeline)_pipeline,
            &push, sizeof(push), nullptr, 0, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_encoder_barrier(rp, LUMINARY_RHI_RENDER_STAGE_MESH, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_mesh_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_execute_indirect_commands(rp, _draw_buf, _count_buf, 2, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_count_buf) { lrhi_destroy_buffer(_count_buf);         _count_buf = nullptr; }
        if (_draw_buf)  { lrhi_destroy_buffer(_draw_buf);          _draw_buf  = nullptr; }
        if (_pipeline)  { lrhi_destroy_mesh_pipeline(_pipeline);   _pipeline  = nullptr; }
        if (_ps)        { lrhi_destroy_shader_module(_ps);         _ps        = nullptr; }
        if (_ms)        { lrhi_destroy_shader_module(_ms);         _ms        = nullptr; }
        if (_view)      { lrhi_destroy_texture_view(_view);        _view      = nullptr; }
        if (_tex)       { lrhi_destroy_texture(_tex);              _tex       = nullptr; }
    }
};

REGISTER_TEST(draw_mesh_indirect_test);

// ---------------------------------------------------------------------------
// Test 5: draw_task_mesh_indirect
// 2 task+mesh shaded triangles positioned by draw ID via indirect draw_mesh_tasks
// ---------------------------------------------------------------------------

class draw_task_mesh_indirect_test : public test
{
    struct PushData { float color[3]; };

    LRHIDevice        _device    = nullptr;
    LRHITexture       _tex       = nullptr;
    LRHITextureView   _view      = nullptr;
    LRHIShaderModule  _as        = nullptr;
    LRHIShaderModule  _ms        = nullptr;
    LRHIShaderModule  _ps        = nullptr;
    LRHIMeshPipeline  _pipeline  = nullptr;
    LRHIBuffer        _draw_buf  = nullptr;
    LRHIBuffer        _count_buf = nullptr;

public:
    draw_task_mesh_indirect_test()
    {
        type        = test_type::texture;
        name        = "draw_task_mesh_indirect";
        source_path = "tests/golden/draw_task_mesh_indirect.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        if (!lrhi_get_device_info(device).features.mesh_shading) return;
        _tex = make_color_tex(device, _view);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/indirect_task_mesh.hlsl");
        if (src.empty()) return;

        auto [as_bc, as_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_TASK, "ASMain");
        if (!as_bc) return;
        _as = dh_make_module(device, as_bc, as_sz, LUMINARY_RHI_SHADER_STAGE_TASK, "ASMain", err_str);

        auto [ms_bc, ms_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_MESH, "MSMain");
        if (!ms_bc) return;
        _ms = dh_make_module(device, ms_bc, ms_sz, LUMINARY_RHI_SHADER_STAGE_MESH, "MSMain", err_str);

        auto [ps_bc, ps_sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain");
        if (!ps_bc) return;
        _ps = dh_make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain", err_str);

        if (!_as || !_ms || !_ps) return;

        LRHIMeshPipelineInfo info = {};
        info.supports_indirect_commands   = 1;
        info.fill_mode                    = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
        info.cull_mode                    = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
        info.front_face                   = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
        info.topology                     = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
        info.render_target_formats[0]     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.render_target_count          = 1;
        info.task_shader                  = _as;
        info.mesh_shader                  = _ms;
        info.fragment_shader              = _ps;
        lrhi_create_mesh_pipeline(device, &info, &_pipeline, nullptr);

        make_draw_mesh_indirect_buffers(device, _draw_buf, _count_buf);
    }

    test_result run(bool bake_mode) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};
        if (!_pipeline || !_draw_buf || !_count_buf) return {false, "init failed"};

        PushData push = {1.0f, 0.0f, 0.0f};
        LRHIDrawIndirectParameters params = {};
        params.threads_per_object_groups_x = 1;
        params.threads_per_object_groups_y = 1;
        params.threads_per_object_groups_z = 1;
        params.threads_per_mesh_groups_x   = 1;
        params.threads_per_mesh_groups_y   = 1;
        params.threads_per_mesh_groups_z   = 1;

        std::string err;
        bool ok = run_render_indirect_pass(_device, _tex, _view,
            _draw_buf, _count_buf, 2, &params,
            (LRHIRenderPipeline)_pipeline,
            &push, sizeof(push), nullptr, 0, err,
            [this](LRHIRenderPass rp, LRHIError& e) {
                lrhi_render_pass_encoder_barrier(rp, LUMINARY_RHI_RENDER_STAGE_TASK, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_mesh_pipeline(rp, _pipeline, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_viewport(rp, 0, 0, W, H, 0.0f, 1.0f, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_set_scissor(rp, 0, 0, W, H, &e);
                if (e.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) return;
                lrhi_render_pass_execute_indirect_commands(rp, _draw_buf, _count_buf, 2, &e);
            });
        if (!ok) return {false, err};
        return dh_texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_count_buf) { lrhi_destroy_buffer(_count_buf);         _count_buf = nullptr; }
        if (_draw_buf)  { lrhi_destroy_buffer(_draw_buf);          _draw_buf  = nullptr; }
        if (_pipeline)  { lrhi_destroy_mesh_pipeline(_pipeline);   _pipeline  = nullptr; }
        if (_ps)        { lrhi_destroy_shader_module(_ps);         _ps        = nullptr; }
        if (_ms)        { lrhi_destroy_shader_module(_ms);         _ms        = nullptr; }
        if (_as)        { lrhi_destroy_shader_module(_as);         _as        = nullptr; }
        if (_view)      { lrhi_destroy_texture_view(_view);        _view      = nullptr; }
        if (_tex)       { lrhi_destroy_texture(_tex);              _tex       = nullptr; }
    }
};

REGISTER_TEST(draw_task_mesh_indirect_test);
