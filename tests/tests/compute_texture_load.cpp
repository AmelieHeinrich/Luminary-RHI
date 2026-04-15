#include "luminary_rhi.h"
#include "tests/draw_helpers.h"
#include <cstring>
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
    if (rs) {
        lrhi_command_queue_add_residency_set(queue, rs, nullptr);
    }
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    return true;
}

static bool record_upload_to_texture_with_staging(LRHIDevice device,
                                                  LRHICommandList cmd,
                                                  LRHIResidencySet rs,
                                                  LRHITexture dst_texture,
                                                  const void* data,
                                                  uint32_t data_size,
                                                  uint32_t bytes_per_row,
                                                  uint32_t bytes_per_image,
                                                  LRHIRegion dst_region,
                                                  uint32_t dst_mip,
                                                  uint32_t dst_layer,
                                                  LRHIBuffer* out_staging,
                                                  std::string& err_out)
{
    LRHIError err = {};

    LRHIBuffer staging = nullptr;
    LRHIBufferInfo buf_info = {};
    buf_info.size = data_size;
    buf_info.stride = 1;
    buf_info.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                       LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
    lrhi_create_buffer(device, &buf_info, &staging, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create staging buffer failed: ") + err.message;
        return false;
    }

    void* mapped = lrhi_buffer_map(staging, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
        err_out = std::string("map staging buffer failed: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }
    memcpy(mapped, data, data_size);
    lrhi_buffer_unmap(staging);

    lrhi_residency_set_add_buffer(rs, staging, &err);
    lrhi_residency_set_update(rs, &err);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("add upload residency failed: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }

    LRHICopyPass cp = lrhi_copy_pass_begin(cmd, &err);
    lrhi_copy_pass_copy_buffer_to_texture(cp,
        staging, 0, bytes_per_row, bytes_per_image,
        dst_texture, dst_region, dst_mip, dst_layer, &err);
    lrhi_copy_pass_end(cp, &err);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("record upload copy failed: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }

    *out_staging = staging;
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
// Test 1: compute_texture_load_2d
// Computes texture read from input 2D texture, writes to output 2D texture
// ---------------------------------------------------------------------------

class compute_texture_load_2d_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;

    LRHIDevice _device = nullptr;
    LRHITexture _input_texture = nullptr;
    LRHITextureView _input_view = nullptr;
    LRHITexture _output_texture = nullptr;
    LRHITextureView _output_view = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    std::vector<uint8_t> _init_data;

public:
    compute_texture_load_2d_test()
    {
        type = test_type::texture;
        name = "compute_texture_load_2d";
        source_path = "tests/golden/compute_texture_load_2d.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Create input 2D texture with gradient pattern
        LRHITextureInfo input_info = {};
        input_info.width = W;
        input_info.height = H;
        input_info.depth = 1;
        input_info.mip_levels = 1;
        input_info.array_layers = 1;
        input_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        input_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        input_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(device, &input_info, &_input_texture, &err);
        lrhi_texture_set_name(_input_texture, "InputTexture");

        // Create output 2D texture (storage)
        LRHITextureInfo output_info = input_info;
        output_info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE);
        lrhi_create_texture(device, &output_info, &_output_texture, &err);
        lrhi_texture_set_name(_output_texture, "OutputTexture");

        // Create texture views
        _input_view = dh_make_view(device, _input_texture,
                                   LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                   LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                   0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                   0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_texture,
                                    LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Initialize input texture with gradient pattern
        _init_data.resize((size_t)W * H * 4);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                uint32_t idx = (y * W + x) * 4;
                float u = x / (float)W;
                float v = y / (float)H;
                _init_data[idx + 0] = (uint8_t)(u * 255.0f);
                _init_data[idx + 1] = (uint8_t)(v * 255.0f);
                _init_data[idx + 2] = (uint8_t)((0.5f + 0.5f * (u + v) / 2.0f) * 255.0f);
                _init_data[idx + 3] = 255;
            }
        }

        // Compile compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_load_2d.hlsl");
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

    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_input_texture || !_output_texture || _init_data.empty()) return {false, "init failed"};
        std::string err;

        // Get bindless indices
        LRHIError lerr = {};
        uint32_t input_index = lrhi_texture_view_get_bindless_index(_input_view, &lerr);
        uint32_t output_index = lrhi_texture_view_get_bindless_index(_output_view, &lerr);

        // Create command infrastructure
        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        if (!compute_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        LRHIResidencySet run_rs = nullptr;
        LRHIError rs_err = {};
        lrhi_create_residency_set(_device, &run_rs, &rs_err);
        lrhi_residency_set_add_texture(run_rs, _input_texture, &rs_err);
        lrhi_residency_set_add_texture(run_rs, _output_texture, &rs_err);
        lrhi_residency_set_update(run_rs, &rs_err);
        if (rs_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("run residency setup failed: ") + rs_err.message};
        }

        LRHIBuffer upload_staging = nullptr;
        LRHIRegion upload_region = {0, 0, 0, W, H, 1};
        if (!record_upload_to_texture_with_staging(_device,
                                                   cmd,
                                                   run_rs,
                                                   _input_texture,
                                                   _init_data.data(),
                                                   (uint32_t)_init_data.size(),
                                                   W * 4,
                                                   W * H * 4,
                                                   upload_region,
                                                   0,
                                                   0,
                                                   &upload_staging,
                                                   err)) {
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, err};
        }

        // Begin compute pass
        lerr = {};
        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("compute_pass_begin: ") + lerr.message};
        }

        // Set pipeline
        lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("compute_pass_set_pipeline: ") + lerr.message};
        }

        // Set push constants
        struct PushConstants {
            uint32_t input_texture_descriptor;
            uint32_t output_texture_descriptor;
        } push_data = {input_index, output_index};
        lrhi_compute_pass_set_push_constants(compute_pass, &push_data, sizeof(push_data), &lerr);

        // Dispatch: 16x16 thread groups, 8x8 threads per group
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("compute_pass_dispatch: ") + lerr.message};
        }

        // End compute pass
        lrhi_compute_pass_end(compute_pass, &lerr);

        // Submit and wait
        if (!compute_end_cmd(_device, queue, fence, cmd, run_rs, err)) {
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            return {false, err};
        }

        lrhi_destroy_buffer(upload_staging);
        lrhi_destroy_residency_set(run_rs);

        // Validate output texture
        return texture_test_result(_device, _output_texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view)     { lrhi_destroy_texture_view(_output_view);    _output_view     = nullptr; }
        if (_input_view)      { lrhi_destroy_texture_view(_input_view);     _input_view      = nullptr; }
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);   _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
        if (_output_texture)  { lrhi_destroy_texture(_output_texture);      _output_texture  = nullptr; }
        if (_input_texture)   { lrhi_destroy_texture(_input_texture);       _input_texture   = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(compute_texture_load_2d_test);

// ---------------------------------------------------------------------------
// Test 2: compute_texture_load_2d_array
// Loads from 2D array texture, computes, writes to output 2D array
// ---------------------------------------------------------------------------

class compute_texture_load_2d_array_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;
    static constexpr uint32_t LAYERS = 4;

    LRHIDevice _device = nullptr;
    LRHITexture _input_texture = nullptr;
    LRHITextureView _input_view = nullptr;
    LRHITexture _output_texture = nullptr;
    LRHITextureView _output_view = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    std::vector<uint8_t> _init_data;

public:
    compute_texture_load_2d_array_test()
    {
        type = test_type::texture;
        name = "compute_texture_load_2d_array";
        source_path = "tests/golden/compute_texture_load_2d_array.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Create input 2D array texture
        LRHITextureInfo input_info = {};
        input_info.width = W;
        input_info.height = H;
        input_info.depth = 1;
        input_info.mip_levels = 1;
        input_info.array_layers = LAYERS;
        input_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        input_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        input_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        LRHIError err = {};
        lrhi_create_texture(device, &input_info, &_input_texture, &err);
        lrhi_texture_set_name(_input_texture, "InputTexture");

        // Create output 2D array texture
        LRHITextureInfo output_info = input_info;
        output_info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                              LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        lrhi_create_texture(device, &output_info, &_output_texture, &err);
        lrhi_texture_set_name(_output_texture, "OutputTexture");

        // Create texture views
        _input_view = dh_make_view(device, _input_texture,
                                   LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                   LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY,
                                   0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                   0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_texture,
                                    LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Initialize input with layer-specific patterns
        _init_data.resize((size_t)W * H * LAYERS * 4);
        for (uint32_t layer = 0; layer < LAYERS; ++layer) {
            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    uint32_t idx = ((layer * H + y) * W + x) * 4;
                    float u = x / (float)W;
                    float v = y / (float)H;
                    float l = layer / (float)LAYERS;
                    _init_data[idx + 0] = (uint8_t)(u * 255.0f);
                    _init_data[idx + 1] = (uint8_t)(v * 255.0f);
                    _init_data[idx + 2] = (uint8_t)(l * 255.0f);
                    _init_data[idx + 3] = 255;
                }
            }
        }

        // Compile compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_load_2d_array.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        if (_compute_shader)
        {
            LRHIComputePipelineInfo info = {};
            info.compute_shader = _compute_shader;
            info.supports_indirect_commands = 0;
            err = {};
            lrhi_create_compute_pipeline(device, &info, &_pipeline, &err);
        }

    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_input_texture || !_output_texture || _init_data.empty()) return {false, "init failed"};
        std::string err;

        LRHIError lerr = {};
        uint32_t input_index = lrhi_texture_view_get_bindless_index(_input_view, &lerr);
        uint32_t output_index = lrhi_texture_view_get_bindless_index(_output_view, &lerr);

        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        if (!compute_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        LRHIResidencySet run_rs = nullptr;
        LRHIError rs_err = {};
        lrhi_create_residency_set(_device, &run_rs, &rs_err);
        lrhi_residency_set_add_texture(run_rs, _input_texture, &rs_err);
        lrhi_residency_set_add_texture(run_rs, _output_texture, &rs_err);
        lrhi_residency_set_update(run_rs, &rs_err);
        if (rs_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("run residency setup failed: ") + rs_err.message};
        }

        std::vector<LRHIBuffer> upload_staging_buffers;
        upload_staging_buffers.reserve(LAYERS);
        LRHIRegion upload_region = {0, 0, 0, W, H, 1};
        const size_t layer_stride = (size_t)W * H * 4;
        for (uint32_t layer = 0; layer < LAYERS; ++layer) {
            LRHIBuffer upload_staging = nullptr;
            if (!record_upload_to_texture_with_staging(_device,
                                                       cmd,
                                                       run_rs,
                                                       _input_texture,
                                                       _init_data.data() + layer * layer_stride,
                                                       (uint32_t)layer_stride,
                                                       W * 4,
                                                       W * H * 4,
                                                       upload_region,
                                                       0,
                                                       layer,
                                                       &upload_staging,
                                                       err)) {
                for (LRHIBuffer b : upload_staging_buffers) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(run_rs);
                if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
                return {false, err};
            }
            upload_staging_buffers.push_back(upload_staging);
        }

        for (uint32_t layer = 0; layer < LAYERS; ++layer)
        {
            lerr = {};
            LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                for (LRHIBuffer b : upload_staging_buffers) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(run_rs);
                if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
                return {false, std::string("compute_pass_begin: ") + lerr.message};
            }

            lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
            lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                lrhi_compute_pass_end(compute_pass, nullptr);
                for (LRHIBuffer b : upload_staging_buffers) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(run_rs);
                if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
                return {false, std::string("compute_pass_set_pipeline: ") + lerr.message};
            }

            struct PushConstants {
                uint32_t input_texture_descriptor;
                uint32_t output_texture_descriptor;
                uint32_t layer;
            } push_data = {input_index, output_index, layer};
            lrhi_compute_pass_set_push_constants(compute_pass, &push_data, sizeof(push_data), &lerr);

            lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
            if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                lrhi_compute_pass_end(compute_pass, nullptr);
                for (LRHIBuffer b : upload_staging_buffers) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(run_rs);
                if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
                return {false, std::string("compute_pass_dispatch: ") + lerr.message};
            }

            lrhi_compute_pass_end(compute_pass, &lerr);
        }

        if (!compute_end_cmd(_device, queue, fence, cmd, run_rs, err)) {
            for (LRHIBuffer b : upload_staging_buffers) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(run_rs);
            return {false, err};
        }

        for (LRHIBuffer b : upload_staging_buffers) lrhi_destroy_buffer(b);
        lrhi_destroy_residency_set(run_rs);
        return texture_test_result(_device, _output_texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view)     { lrhi_destroy_texture_view(_output_view);    _output_view     = nullptr; }
        if (_input_view)      { lrhi_destroy_texture_view(_input_view);     _input_view      = nullptr; }
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);   _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
        if (_output_texture)  { lrhi_destroy_texture(_output_texture);      _output_texture  = nullptr; }
        if (_input_texture)   { lrhi_destroy_texture(_input_texture);       _input_texture   = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(compute_texture_load_2d_array_test);

// ---------------------------------------------------------------------------
// Test 3: compute_texture_load_3d
// Loads from 3D texture, computes, writes to output 3D texture
// ---------------------------------------------------------------------------

class compute_texture_load_3d_test : public test
{
    static constexpr uint32_t W = 64;
    static constexpr uint32_t H = 64;
    static constexpr uint32_t D = 64;

    LRHIDevice _device = nullptr;
    LRHITexture _input_texture = nullptr;
    LRHITextureView _input_view = nullptr;
    LRHITexture _output_texture = nullptr;
    LRHITextureView _output_view = nullptr;
    LRHIShaderModule _compute_shader = nullptr;
    LRHIComputePipeline _pipeline = nullptr;
    std::vector<uint8_t> _init_data;

public:
    compute_texture_load_3d_test()
    {
        type = test_type::texture;
        name = "compute_texture_load_3d";
        source_path = "tests/golden/compute_texture_load_3d.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;

        // Create input 3D texture
        LRHITextureInfo input_info = {};
        input_info.width = W;
        input_info.height = H;
        input_info.depth = D;
        input_info.mip_levels = 1;
        input_info.array_layers = 1;
        input_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        input_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        input_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_3D;
        LRHIError err = {};
        lrhi_create_texture(device, &input_info, &_input_texture, &err);
        lrhi_texture_set_name(_input_texture, "InputTexture");

        // Create output 3D texture
        LRHITextureInfo output_info = input_info;
        output_info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE |
                                              LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        lrhi_create_texture(device, &output_info, &_output_texture, &err);
        lrhi_texture_set_name(_output_texture, "OutputTexture");

        // Create texture views
        _input_view = dh_make_view(device, _input_texture,
                                   LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                   LUMINARY_RHI_TEXTURE_DIMENSIONS_3D,
                                   0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                   0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_texture,
                                    LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_3D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Initialize input with 3D gradient
        _init_data.resize((size_t)W * H * D * 4);
        for (uint32_t z = 0; z < D; ++z) {
            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    uint32_t idx = ((z * H + y) * W + x) * 4;
                    float u = x / (float)W;
                    float v = y / (float)H;
                    float w = z / (float)D;
                    _init_data[idx + 0] = (uint8_t)(u * 255.0f);
                    _init_data[idx + 1] = (uint8_t)(v * 255.0f);
                    _init_data[idx + 2] = (uint8_t)(w * 255.0f);
                    _init_data[idx + 3] = 255;
                }
            }
        }

        // Compile compute shader
        std::string src = dh_read_shader_file("shaders/tests/compute_texture_load_3d.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        if (_compute_shader)
        {
            LRHIComputePipelineInfo info = {};
            info.compute_shader = _compute_shader;
            info.supports_indirect_commands = 0;
            err = {};
            lrhi_create_compute_pipeline(device, &info, &_pipeline, &err);
        }

    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_input_texture || !_output_texture || _init_data.empty()) return {false, "init failed"};
        std::string err;

        LRHIError lerr = {};
        uint32_t input_index = lrhi_texture_view_get_bindless_index(_input_view, &lerr);
        uint32_t output_index = lrhi_texture_view_get_bindless_index(_output_view, &lerr);

        LRHICommandQueue queue = nullptr;
        LRHIFence fence = nullptr;
        LRHICommandList cmd = nullptr;
        if (!compute_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        LRHIResidencySet run_rs = nullptr;
        LRHIError rs_err = {};
        lrhi_create_residency_set(_device, &run_rs, &rs_err);
        lrhi_residency_set_add_texture(run_rs, _input_texture, &rs_err);
        lrhi_residency_set_add_texture(run_rs, _output_texture, &rs_err);
        lrhi_residency_set_update(run_rs, &rs_err);
        if (rs_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("run residency setup failed: ") + rs_err.message};
        }

        LRHIBuffer upload_staging = nullptr;
        LRHIRegion upload_region = {0, 0, 0, W, H, D};
        if (!record_upload_to_texture_with_staging(_device,
                                                   cmd,
                                                   run_rs,
                                                   _input_texture,
                                                   _init_data.data(),
                                                   (uint32_t)_init_data.size(),
                                                   W * 4,
                                                   W * H * 4,
                                                   upload_region,
                                                   0,
                                                   0,
                                                   &upload_staging,
                                                   err)) {
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, err};
        }

        lerr = {};
        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("compute_pass_begin: ") + lerr.message};
        }

        lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("compute_pass_set_pipeline: ") + lerr.message};
        }

        struct PushConstants {
            uint32_t input_texture_descriptor;
            uint32_t output_texture_descriptor;
        } push_data = {input_index, output_index};
        lrhi_compute_pass_set_push_constants(compute_pass, &push_data, sizeof(push_data), &lerr);

        // Dispatch: 16x16x16 thread groups, 4x4x4 threads per group
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 16, 4, 4, 4, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_compute_pass_end(compute_pass, nullptr);
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            if (!compute_end_cmd(_device, queue, fence, cmd, nullptr, err)) {}
            return {false, std::string("compute_pass_dispatch: ") + lerr.message};
        }

        lrhi_compute_pass_end(compute_pass, &lerr);

        if (!compute_end_cmd(_device, queue, fence, cmd, run_rs, err)) {
            lrhi_destroy_buffer(upload_staging);
            lrhi_destroy_residency_set(run_rs);
            return {false, err};
        }

        lrhi_destroy_buffer(upload_staging);
        lrhi_destroy_residency_set(run_rs);
        return texture_test_result(_device, _output_texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view)     { lrhi_destroy_texture_view(_output_view);    _output_view     = nullptr; }
        if (_input_view)      { lrhi_destroy_texture_view(_input_view);     _input_view      = nullptr; }
        if (_pipeline)        { lrhi_destroy_compute_pipeline(_pipeline);   _pipeline        = nullptr; }
        if (_compute_shader)  { lrhi_destroy_shader_module(_compute_shader); _compute_shader = nullptr; }
        if (_output_texture)  { lrhi_destroy_texture(_output_texture);      _output_texture  = nullptr; }
        if (_input_texture)   { lrhi_destroy_texture(_input_texture);       _input_texture   = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(compute_texture_load_3d_test);
