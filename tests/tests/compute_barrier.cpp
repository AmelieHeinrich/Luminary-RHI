#include "luminary_rhi.h"
#include "tests/draw_helpers.h"
#include <string>
#include <vector>

static constexpr uint32_t W = 128;
static constexpr uint32_t H = 128;

static bool begin_cmd(LRHIDevice device,
                      LRHICommandQueue* out_queue,
                      LRHIFence* out_fence,
                      LRHICommandList* out_cmd,
                      std::string& err_out)
{
    LRHIError err = {};
    lrhi_create_command_queue(device, out_queue, &err);
    lrhi_create_fence(device, 0, out_fence, &err);
    lrhi_create_command_list(*out_queue, out_cmd, &err);

    lrhi_command_list_begin(*out_cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd begin: ") + err.message;
        lrhi_destroy_command_list(*out_cmd); *out_cmd = nullptr;
        lrhi_destroy_fence(*out_fence);      *out_fence = nullptr;
        lrhi_destroy_command_queue(*out_queue); *out_queue = nullptr;
        return false;
    }
    return true;
}

static bool end_cmd(LRHICommandQueue queue,
                    LRHIFence fence,
                    LRHICommandList cmd,
                    LRHIResidencySet rs,
                    std::string& err_out)
{
    LRHIError err = {};
    lrhi_command_list_end(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd end: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        return false;
    }
    lrhi_command_queue_add_residency_set(queue, rs, nullptr);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    return true;
}

static test_result texture_result(LRHIDevice device, LRHITexture tex, const char* test_name, const char* golden_path, bool bake_mode)
{
    LRHITextureInfo info = {};
    lrhi_get_texture_info(tex, &info);
    std::vector<uint8_t> rb;
    test_tools::rhi_readback_texture(device, tex, rb, 0, 0);

    std::string out = std::string("tests/output/") + test_name + ".png";
    std::string flip = std::string("tests/output/") + test_name + "_flip.png";
    if (bake_mode) {
        test_tools::save_texture(golden_path, rb, info, 0);
        test_result r; r.passed = true; r.message = "baked"; r.golden_image = golden_path;
        return r;
    }

    test_tools::save_texture(out.c_str(), rb, info, 0);
    float mean_err = 0.0f;
    bool passed = test_tools::validate_texture(golden_path, rb, info, false, mean_err, flip.c_str());
    test_result r; r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
    r.golden_image = golden_path; r.output_image = out; r.flip_image = flip; r.flip_mean_error = mean_err;
    return r;
}

static bool run_clear_render_pass(LRHICommandList cmd,
                                  LRHITextureView view,
                                  uint32_t width,
                                  uint32_t height,
                                  const float clear[4],
                                  LRHIRenderPass* out_pass,
                                  LRHIError* out_err)
{
    LRHIRenderPassInfo rp_info = {};
    rp_info.color_attachments[0].texture_view = view;
    rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].clear_color[0] = clear[0];
    rp_info.color_attachments[0].clear_color[1] = clear[1];
    rp_info.color_attachments[0].clear_color[2] = clear[2];
    rp_info.color_attachments[0].clear_color[3] = clear[3];
    rp_info.color_attachment_count = 1;
    rp_info.render_width = width;
    rp_info.render_height = height;

    *out_pass = lrhi_render_pass_begin(cmd, &rp_info, out_err);
    return out_err->severity != LUMINARY_RHI_ERROR_SEVERITY_ERROR;
}

class compute_to_render_barrier_test : public test
{
    LRHIDevice _device = nullptr;
    LRHITexture _tex = nullptr;
    LRHITextureView _storage_view = nullptr;
    LRHITextureView _render_view = nullptr;
    LRHIShaderModule _cs = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    compute_to_render_barrier_test()
    {
        type = test_type::texture;
        name = "compute_to_render_barrier";
        source_path = "tests/golden/compute_to_render_barrier.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                      LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET |
                                      LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(device, &ti, &_tex, &err);

        _storage_view = dh_make_view(device, _tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _render_view = dh_make_view(device, _tex, LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/compute_to_render_barrier.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _tex, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!begin_cmd(_device, &q, &f, &cmd, err_str)) return {false, err_str};

        LRHIError err = {};
        uint32_t storage_idx = lrhi_texture_view_get_bindless_index(_storage_view, &err);

        LRHIRenderPassInfo rp_info = {};
        rp_info.color_attachments[0].texture_view = _render_view;
        rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_LOAD;
        rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.color_attachment_count = 1;
        rp_info.render_width = W;
        rp_info.render_height = H;
        LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &err);
        lrhi_render_pass_end(rp, &err);

        struct C { uint32_t output_texture; } c = { storage_idx };
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp, &c, sizeof(c), &err);
        lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_FRAGMENT, &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);
        lrhi_compute_pass_end(cp, &err);

        if (!end_cmd(q, f, cmd, _rs, err_str)) return {false, err_str};
        return texture_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) lrhi_destroy_compute_pipeline(_pipeline);
        if (_cs) lrhi_destroy_shader_module(_cs);
        if (_render_view) lrhi_destroy_texture_view(_render_view);
        if (_storage_view) lrhi_destroy_texture_view(_storage_view);
        if (_rs) lrhi_destroy_residency_set(_rs);
        if (_tex) lrhi_destroy_texture(_tex);
        _pipeline = nullptr; _cs = nullptr; _render_view = nullptr; _storage_view = nullptr; _rs = nullptr; _tex = nullptr;
    }
};
REGISTER_TEST(compute_to_render_barrier_test);

class render_to_compute_barrier_test : public test
{
    LRHIDevice _device = nullptr;
    LRHITexture _render_tex = nullptr;
    LRHITextureView _render_view = nullptr;
    LRHITextureView _sampled_view = nullptr;
    LRHITexture _output_tex = nullptr;
    LRHITextureView _output_view = nullptr;
    LRHIShaderModule _cs = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    render_to_compute_barrier_test()
    {
        type = test_type::texture;
        name = "render_to_compute_barrier";
        source_path = "tests/golden/render_to_compute_barrier.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHIError err = {};

        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;

        ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        lrhi_create_texture(device, &ti, &_render_tex, &err);

        ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        lrhi_create_texture(device, &ti, &_output_tex, &err);

        _render_view = dh_make_view(device, _render_tex, LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D, 0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _sampled_view = dh_make_view(device, _render_tex, LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_2D, 0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D, 0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/render_to_compute_barrier.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _render_tex, nullptr);
        lrhi_residency_set_add_texture(_rs, _output_tex, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!begin_cmd(_device, &q, &f, &cmd, err_str)) return {false, err_str};

        LRHIError err = {};
        float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        LRHIRenderPass rp = nullptr;
        if (!run_clear_render_pass(cmd, _render_view, W, H, red, &rp, &err)) return {false, "render pass begin failed"};
        lrhi_render_pass_end(rp, &err);

        uint32_t input_idx = lrhi_texture_view_get_bindless_index(_sampled_view, &err);
        uint32_t out_idx = lrhi_texture_view_get_bindless_index(_output_view, &err);
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_FRAGMENT, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);

        struct C { uint32_t input_texture; uint32_t output_texture; } c = { input_idx, out_idx };
        lrhi_compute_pass_set_push_constants(cp, &c, sizeof(c), &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);
        lrhi_compute_pass_end(cp, &err);

        if (!end_cmd(q, f, cmd, _rs, err_str)) return {false, err_str};
        return texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) lrhi_destroy_compute_pipeline(_pipeline);
        if (_cs) lrhi_destroy_shader_module(_cs);
        if (_output_view) lrhi_destroy_texture_view(_output_view);
        if (_sampled_view) lrhi_destroy_texture_view(_sampled_view);
        if (_render_view) lrhi_destroy_texture_view(_render_view);
        if (_rs) lrhi_destroy_residency_set(_rs);
        if (_output_tex) lrhi_destroy_texture(_output_tex);
        if (_render_tex) lrhi_destroy_texture(_render_tex);
        _pipeline = nullptr; _cs = nullptr; _output_view = nullptr; _sampled_view = nullptr; _render_view = nullptr;
        _rs = nullptr; _output_tex = nullptr; _render_tex = nullptr;
    }
};
REGISTER_TEST(render_to_compute_barrier_test);

class compute_compute_barrier_test : public test
{
    LRHIDevice _device = nullptr;
    LRHITexture _tex = nullptr;
    LRHITextureView _storage_view = nullptr;
    LRHIShaderModule _cs = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    compute_compute_barrier_test()
    {
        type = test_type::texture;
        name = "compute_compute_barrier";
        source_path = "tests/golden/compute_compute_barrier.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHIError err = {};
        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &ti, &_tex, &err);

        _storage_view = dh_make_view(device, _tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_2D, 0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/compute_multi_dispatch_texture.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _tex, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!begin_cmd(_device, &q, &f, &cmd, err_str)) return {false, err_str};

        LRHIError err = {};
        uint32_t tex_idx = lrhi_texture_view_get_bindless_index(_storage_view, &err);
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);

        struct C { uint32_t texture; uint32_t pass_index; } c0 = { tex_idx, 0 };
        lrhi_compute_pass_set_push_constants(cp, &c0, sizeof(c0), &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);

        lrhi_compute_pass_barrier(cp, &err);

        struct C c1 = { tex_idx, 1 };
        lrhi_compute_pass_set_push_constants(cp, &c1, sizeof(c1), &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);

        lrhi_compute_pass_end(cp, &err);

        if (!end_cmd(q, f, cmd, _rs, err_str)) return {false, err_str};
        return texture_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) lrhi_destroy_compute_pipeline(_pipeline);
        if (_cs) lrhi_destroy_shader_module(_cs);
        if (_storage_view) lrhi_destroy_texture_view(_storage_view);
        if (_rs) lrhi_destroy_residency_set(_rs);
        if (_tex) lrhi_destroy_texture(_tex);
        _pipeline = nullptr; _cs = nullptr; _storage_view = nullptr; _rs = nullptr; _tex = nullptr;
    }
};
REGISTER_TEST(compute_compute_barrier_test);

class multi_dispatch_barriers_test : public test
{
    LRHIDevice _device = nullptr;
    LRHITexture _tex = nullptr;
    LRHITextureView _storage_view = nullptr;
    LRHIShaderModule _cs = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    multi_dispatch_barriers_test()
    {
        type = test_type::texture;
        name = "multi_dispatch_barriers";
        source_path = "tests/golden/multi_dispatch_barriers.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHIError err = {};
        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &ti, &_tex, &err);

        _storage_view = dh_make_view(device, _tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_2D, 0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/compute_multi_dispatch_texture.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _tex, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!begin_cmd(_device, &q, &f, &cmd, err_str)) return {false, err_str};

        LRHIError err = {};
        uint32_t tex_idx = lrhi_texture_view_get_bindless_index(_storage_view, &err);
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);

        struct C { uint32_t texture; uint32_t pass_index; } c0 = { tex_idx, 0 };
        lrhi_compute_pass_set_push_constants(cp, &c0, sizeof(c0), &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);

        lrhi_compute_pass_barrier(cp, &err);

        struct C c1 = { tex_idx, 1 };
        lrhi_compute_pass_set_push_constants(cp, &c1, sizeof(c1), &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);

        lrhi_compute_pass_barrier(cp, &err);

        struct C c2 = { tex_idx, 2 };
        lrhi_compute_pass_set_push_constants(cp, &c2, sizeof(c2), &err);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &err);

        lrhi_compute_pass_end(cp, &err);

        if (!end_cmd(q, f, cmd, _rs, err_str)) return {false, err_str};
        return texture_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) lrhi_destroy_compute_pipeline(_pipeline);
        if (_cs) lrhi_destroy_shader_module(_cs);
        if (_storage_view) lrhi_destroy_texture_view(_storage_view);
        if (_rs) lrhi_destroy_residency_set(_rs);
        if (_tex) lrhi_destroy_texture(_tex);
        _pipeline = nullptr; _cs = nullptr; _storage_view = nullptr; _rs = nullptr; _tex = nullptr;
    }
};
REGISTER_TEST(multi_dispatch_barriers_test);
