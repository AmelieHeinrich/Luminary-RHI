#include "tests/draw_helpers.h"
#include <string>

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

static bool compute_begin_cmd(LRHIDevice device,
                               LRHICommandQueue* out_queue,
                               LRHIFence* out_fence,
                               LRHICommandList* out_cmd,
                               std::string& err_out)
{
    LRHIError err = {};
    lrhi_create_command_queue(device, out_queue, &err);
    lrhi_create_fence(device, 0, out_fence, &err);
    lrhi_create_command_list(*out_queue, out_cmd, &err);

    err = {};
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

static bool compute_end_cmd(LRHIDevice device,
                             LRHICommandQueue queue,
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

static test_result texture_test_result(LRHIDevice device, LRHITexture tex, const char* test_name, const char* golden_path, bool bake_mode)
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

// ---------------------------------------------------------------------------
// Test 1: compute_pipeline_size
// Validates that compute pipeline creation succeeds and pipeline is non-null
// ---------------------------------------------------------------------------

class compute_pipeline_size_test : public test
{
    LRHIDevice _device = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;

public:
    compute_pipeline_size_test()
    {
        type = test_type::validation;
        name = "compute_pipeline_size";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Compile a minimal compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_write.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        // Create compute pipeline
        if (_compute_shader)
        {
            LRHIComputePipelineInfo info = {};
            info.compute_shader = _compute_shader;
            info.supports_indirect_commands = 0;
            LRHIError err = {};
            lrhi_create_compute_pipeline(device, &info, &_pipeline, &err);
        }
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline) return {false, "compute pipeline creation failed"};
        return {true, ""};
    }

    void cleanup() override
    {
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);  _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
    }
};

REGISTER_TEST(compute_pipeline_size_test);

// ---------------------------------------------------------------------------
// Test 2: compute_texture_write_2d
// Dispatches compute to write a gradient pattern to a 128x128 2D texture
// ---------------------------------------------------------------------------

class compute_texture_write_2d_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;

    LRHIDevice _device = nullptr;
    LRHITexture _texture = nullptr;
    LRHITextureView _texture_view = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    compute_texture_write_2d_test()
    {
        type = test_type::texture;
        name = "compute_texture_write_2d";
        source_path = "tests/golden/compute_texture_write_2d.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Create 2D texture
        LRHITextureInfo tex_info = {};
        tex_info.width = W;
        tex_info.height = H;
        tex_info.depth = 1;
        tex_info.mip_levels = 1;
        tex_info.array_layers = 1;
        tex_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                           LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(device, &tex_info, &_texture, &err);

        // Create texture view for storage
        _texture_view = dh_make_view(device, _texture,
                                     LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Compile compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_write.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        // Create compute pipeline
        if (_compute_shader)
        {
            LRHIComputePipelineInfo info = {};
            info.compute_shader = _compute_shader;
            info.supports_indirect_commands = 0;
            err = {};
            lrhi_create_compute_pipeline(device, &info, &_pipeline, &err);
        }

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _texture, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_texture || !_texture_view) return {false, "init failed"};
        std::string err;

        // Get bindless index from texture view
        LRHIError lerr = {};
        uint32_t bindless_index = lrhi_texture_view_get_bindless_index(_texture_view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get bindless index"};

        // Create command infrastructure
        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        if (!compute_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        // Begin compute pass
        lerr = {};
        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            if (!compute_end_cmd(_device, queue, fence, cmd, _rs, err)) {}
            return {false, std::string("compute_pass_begin: ") + lerr.message};
        }

        // Set pipeline
        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            if (!compute_end_cmd(_device, queue, fence, cmd, _rs, err)) {}
            return {false, std::string("compute_pass_set_pipeline: ") + lerr.message};
        }

        // Set push constants with texture descriptor index
        struct PushConstants {
            uint32_t texture_descriptor;
        } push_data = {bindless_index};
        lrhi_compute_pass_set_push_constants(compute_pass, &push_data, sizeof(push_data), &lerr);

        // Dispatch: 16x16 thread groups, 8x8 threads per group
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            if (!compute_end_cmd(_device, queue, fence, cmd, _rs, err)) {}
            return {false, std::string("compute_pass_dispatch: ") + lerr.message};
        }

        // End compute pass
        lrhi_compute_pass_end(compute_pass, &lerr);

        // Submit and wait
        if (!compute_end_cmd(_device, queue, fence, cmd, _rs, err)) return {false, err};

        // Validate texture output
        return texture_test_result(_device, _texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_texture_view)    { lrhi_destroy_texture_view(_texture_view); _texture_view    = nullptr; }
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);  _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
        if (_rs)              { lrhi_destroy_residency_set(_rs);           _rs              = nullptr; }
        if (_texture)         { lrhi_destroy_texture(_texture);            _texture         = nullptr; }
    }
};

REGISTER_TEST(compute_texture_write_2d_test);

// ---------------------------------------------------------------------------
// Test 3: compute_texture_write_2d_array
// Dispatches compute to write a gradient pattern to a 128x128x4 2D array texture
// ---------------------------------------------------------------------------

class compute_texture_write_2d_array_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;
    static constexpr uint32_t LAYERS = 4;

    LRHIDevice _device = nullptr;
    LRHITexture _texture = nullptr;
    LRHITextureView _texture_view = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    compute_texture_write_2d_array_test()
    {
        type = test_type::texture;
        name = "compute_texture_write_2d_array";
        source_path = "tests/golden/compute_texture_write_2d_array.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Create 2D array texture
        LRHITextureInfo tex_info = {};
        tex_info.width = W;
        tex_info.height = H;
        tex_info.depth = 1;
        tex_info.mip_levels = 1;
        tex_info.array_layers = LAYERS;
        tex_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                           LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        LRHIError err = {};
        lrhi_create_texture(device, &tex_info, &_texture, &err);

        // Create texture view for storage
        _texture_view = dh_make_view(device, _texture,
                                     LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Compile compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_write_2d_array.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        // Create compute pipeline
        if (_compute_shader)
        {
            LRHIComputePipelineInfo info = {};
            info.compute_shader = _compute_shader;
            info.supports_indirect_commands = 0;
            err = {};
            lrhi_create_compute_pipeline(device, &info, &_pipeline, &err);
        }

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _texture, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_texture || !_texture_view) return {false, "init failed"};
        std::string err;

        // Get bindless index from texture view
        LRHIError lerr = {};
        uint32_t bindless_index = lrhi_texture_view_get_bindless_index(_texture_view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get bindless index"};

        // Create command infrastructure
        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        if (!compute_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        // Dispatch compute for each layer
        for (uint32_t layer = 0; layer < LAYERS; ++layer)
        {
            LRHIError lerr = {};
            LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return {false, std::string("compute_pass_begin: ") + lerr.message};

            lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                lrhi_compute_pass_end(compute_pass, nullptr);
                return {false, std::string("compute_pass_set_pipeline: ") + lerr.message};
            }

            struct PushConstants {
                uint32_t texture_descriptor;
                uint32_t layer;
            } push_data = {bindless_index, layer};
            lrhi_compute_pass_set_push_constants(compute_pass, &push_data, sizeof(push_data), &lerr);

            lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                lrhi_compute_pass_end(compute_pass, nullptr);
                return {false, std::string("compute_pass_dispatch: ") + lerr.message};
            }

            lrhi_compute_pass_end(compute_pass, &lerr);
        }

        // Submit and wait
        if (!compute_end_cmd(_device, queue, fence, cmd, _rs, err)) return {false, err};

        // For verification, we'll readback the first layer
        return texture_test_result(_device, _texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_texture_view)    { lrhi_destroy_texture_view(_texture_view); _texture_view    = nullptr; }
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);  _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
        if (_rs)              { lrhi_destroy_residency_set(_rs);           _rs              = nullptr; }
        if (_texture)         { lrhi_destroy_texture(_texture);            _texture         = nullptr; }
    }
};

REGISTER_TEST(compute_texture_write_2d_array_test);

// ---------------------------------------------------------------------------
// Test 4: compute_texture_write_3d
// Dispatches compute to write a gradient pattern to a 64x64x64 3D texture
// ---------------------------------------------------------------------------

class compute_texture_write_3d_test : public test
{
    static constexpr uint32_t W = 64;
    static constexpr uint32_t H = 64;
    static constexpr uint32_t D = 64;

    LRHIDevice _device = nullptr;
    LRHITexture _texture = nullptr;
    LRHITextureView _texture_view = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    LRHIResidencySet _rs = nullptr;

public:
    compute_texture_write_3d_test()
    {
        type = test_type::texture;
        name = "compute_texture_write_3d";
        source_path = "tests/golden/compute_texture_write_3d.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Create 3D texture
        LRHITextureInfo tex_info = {};
        tex_info.width = W;
        tex_info.height = H;
        tex_info.depth = D;
        tex_info.mip_levels = 1;
        tex_info.array_layers = 1;
        tex_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                           LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_3D;
        LRHIError err = {};
        lrhi_create_texture(device, &tex_info, &_texture, &err);

        // Create texture view for storage
        _texture_view = dh_make_view(device, _texture,
                                     LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                     LUMINARY_RHI_TEXTURE_DIMENSIONS_3D,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                     0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Compile compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_write_3d.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        // Create compute pipeline
        if (_compute_shader)
        {
            LRHIComputePipelineInfo info = {};
            info.compute_shader = _compute_shader;
            info.supports_indirect_commands = 0;
            err = {};
            lrhi_create_compute_pipeline(device, &info, &_pipeline, &err);
        }

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _texture, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_texture || !_texture_view) return {false, "init failed"};
        std::string err;

        // Get bindless index from texture view
        LRHIError lerr = {};
        uint32_t bindless_index = lrhi_texture_view_get_bindless_index(_texture_view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get bindless index"};

        // Create command infrastructure
        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        if (!compute_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        // Begin compute pass
        lerr = {};
        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, std::string("compute_pass_begin: ") + lerr.message};

        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            return {false, std::string("compute_pass_set_pipeline: ") + lerr.message};
        }

        struct PushConstants {
            uint32_t texture_descriptor;
        } push_data = {bindless_index};
        lrhi_compute_pass_set_push_constants(compute_pass, &push_data, sizeof(push_data), &lerr);

        // Dispatch: 16x16x16 thread groups, 4x4x4 threads per group
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 16, 4, 4, 4, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            return {false, std::string("compute_pass_dispatch: ") + lerr.message};
        }

        lrhi_compute_pass_end(compute_pass, &lerr);

        // Submit and wait
        if (!compute_end_cmd(_device, queue, fence, cmd, _rs, err)) return {false, err};

        // Validate texture output (will readback one slice for comparison)
        return texture_test_result(_device, _texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_texture_view)    { lrhi_destroy_texture_view(_texture_view); _texture_view    = nullptr; }
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);  _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
        if (_rs)              { lrhi_destroy_residency_set(_rs);           _rs              = nullptr; }
        if (_texture)         { lrhi_destroy_texture(_texture);            _texture         = nullptr; }
    }
};

REGISTER_TEST(compute_texture_write_3d_test);
