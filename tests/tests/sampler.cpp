#include "luminary_rhi.h"
#include "tests/draw_helpers.h"
#include <cstring>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// Shared helpers (mirrored from compute_texture_load.cpp)
// ---------------------------------------------------------------------------

static bool sampler_begin_cmd(LRHIDevice device,
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

static bool sampler_end_cmd(LRHIDevice device,
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

static bool upload_texture_layer(LRHIDevice device,
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
    buf_info.size   = data_size;
    buf_info.stride = 1;
    buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                        LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
    lrhi_create_buffer(device, &buf_info, &staging, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create staging buffer: ") + err.message;
        return false;
    }

    void* mapped = lrhi_buffer_map(staging, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !mapped) {
        err_out = std::string("map staging buffer: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }
    memcpy(mapped, data, data_size);
    lrhi_buffer_unmap(staging);

    lrhi_residency_set_add_buffer(rs, staging, &err);
    lrhi_residency_set_update(rs, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("residency add staging: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }

    LRHICopyPass cp = lrhi_copy_pass_begin(cmd, &err);
    lrhi_copy_pass_copy_buffer_to_texture(cp,
        staging, 0, bytes_per_row, bytes_per_image,
        dst_texture, dst_region, dst_mip, dst_layer, &err);
    lrhi_copy_pass_end(cp, &err);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("copy pass: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }

    *out_staging = staging;
    return true;
}

static test_result sampler_texture_result(LRHIDevice device,
                                           LRHITexture tex,
                                           const char* test_name,
                                           const char* golden_path,
                                           bool bake_mode)
{
    LRHITextureInfo info = {};
    lrhi_get_texture_info(tex, &info);
    std::vector<uint8_t> rb;
    test_tools::rhi_readback_texture(device, tex, rb, 0, 0);
    std::string out  = std::string("tests/output/") + test_name + ".png";
    std::string flip = std::string("tests/output/") + test_name + "_flip.png";
    if (bake_mode) {
        test_tools::save_texture(golden_path, rb, info, 0);
        test_result r; r.passed = true; r.message = "baked"; r.golden_image = golden_path;
        return r;
    }
    test_tools::save_texture(out.c_str(), rb, info, 0);
    float mean_err = 0.0f;
    bool  passed   = test_tools::validate_texture(golden_path, rb, info, false, mean_err, flip.c_str());
    test_result r;
    r.passed       = passed;
    r.message      = passed ? "" : "FLIP mean error too high";
    r.golden_image = golden_path;
    r.output_image = out;
    r.flip_image   = flip;
    r.flip_mean_error = mean_err;
    return r;
}

// Helper: dispatch a compute shader with 3 push-constant handles over a given output size.
struct SamplerPushConstants {
    uint32_t input_texture;
    uint32_t sampler;
    uint32_t output_texture;
};

static bool run_sampler_compute(LRHIDevice device,
                                 LRHIComputePipeline pipeline,
                                 LRHITexture input_tex,
                                 LRHITexture output_tex,
                                 LRHITextureView input_view,
                                 LRHITextureView output_view,
                                 LRHISampler sampler,
                                 const std::vector<uint8_t>& init_data,
                                 uint32_t input_w, uint32_t input_h,
                                 uint32_t output_w, uint32_t output_h,
                                 uint32_t upload_bpr, uint32_t upload_bpi,
                                 uint32_t dispatch_x, uint32_t dispatch_y,
                                 const void* push_data, uint32_t push_size,
                                 std::string& err_out)
{
    LRHICommandQueue queue = nullptr;
    LRHIFence        fence = nullptr;
    LRHICommandList  cmd   = nullptr;
    if (!sampler_begin_cmd(device, &queue, &fence, &cmd, err_out)) return false;

    LRHIResidencySet rs = nullptr;
    LRHIError rs_err    = {};
    lrhi_create_residency_set(device, &rs, &rs_err);
    lrhi_residency_set_add_texture(rs, input_tex,  &rs_err);
    lrhi_residency_set_add_texture(rs, output_tex, &rs_err);
    lrhi_residency_set_update(rs, &rs_err);
    if (rs_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        lrhi_destroy_residency_set(rs);
        sampler_end_cmd(device, queue, fence, cmd, nullptr, err_out);
        err_out = std::string("residency: ") + rs_err.message;
        return false;
    }

    LRHIBuffer staging = nullptr;
    LRHIRegion upload_region = {0, 0, 0, input_w, input_h, 1};
    if (!upload_texture_layer(device, cmd, rs, input_tex,
                              init_data.data(), (uint32_t)init_data.size(),
                              upload_bpr, upload_bpi,
                              upload_region, 0, 0, &staging, err_out)) {
        lrhi_destroy_residency_set(rs);
        sampler_end_cmd(device, queue, fence, cmd, nullptr, err_out);
        return false;
    }

    LRHIError lerr = {};
    LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &lerr);
    lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
    lrhi_compute_pass_set_pipeline(cp, pipeline, &lerr);
    lrhi_compute_pass_set_push_constants(cp, push_data, push_size, &lerr);
    lrhi_compute_pass_dispatch(cp, dispatch_x, dispatch_y, 1, 8, 8, 1, &lerr);
    lrhi_compute_pass_end(cp, &lerr);

    if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        lrhi_destroy_buffer(staging);
        lrhi_destroy_residency_set(rs);
        sampler_end_cmd(device, queue, fence, cmd, nullptr, err_out);
        err_out = std::string("compute pass: ") + lerr.message;
        return false;
    }

    if (!sampler_end_cmd(device, queue, fence, cmd, rs, err_out)) {
        lrhi_destroy_buffer(staging);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    lrhi_destroy_buffer(staging);
    lrhi_destroy_residency_set(rs);
    return true;
}

// ---------------------------------------------------------------------------
// Test 1: sampler_sample_2d
// Samples a 2D gradient texture and writes result to output.
// ---------------------------------------------------------------------------

class sampler_sample_2d_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;

    LRHIDevice              _device         = nullptr;
    LRHITexture             _input_tex      = nullptr;
    LRHITextureView         _input_view     = nullptr;
    LRHITexture             _output_tex     = nullptr;
    LRHITextureView         _output_view    = nullptr;
    LRHISampler             _sampler        = nullptr;
    LRHIShaderModule        _shader         = nullptr;
    LRHIComputePipeline     _pipeline       = nullptr;
    std::vector<uint8_t>    _init_data;

public:
    sampler_sample_2d_test()
    {
        type        = test_type::texture;
        name        = "sampler_sample_2d";
        source_path = "tests/golden/sampler_sample_2d.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        LRHIError err = {};

        // Input texture
        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &ti, &_input_tex, &err);
        lrhi_texture_set_name(_input_tex, "sampler_2d_input");

        // Output texture
        LRHITextureInfo to = ti;
        to.usage = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
        lrhi_create_texture(device, &to, &_output_tex, &err);
        lrhi_texture_set_name(_output_tex, "sampler_2d_output");

        _input_view  = dh_make_view(device, _input_tex,  LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Gradient pattern
        _init_data.resize((size_t)W * H * 4);
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                uint32_t i = (y * W + x) * 4;
                float u = x / (float)W, v = y / (float)H;
                _init_data[i+0] = (uint8_t)(u * 255.0f);
                _init_data[i+1] = (uint8_t)(v * 255.0f);
                _init_data[i+2] = (uint8_t)((0.5f + 0.5f*(u+v)/2.0f) * 255.0f);
                _init_data[i+3] = 255;
            }

        // Sampler: linear, clamp-to-edge
        LRHISamplerInfo si = {};
        si.min_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mag_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mipmap_filter  = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.min_lod = 0.0f; si.max_lod = 1000.0f;
        lrhi_create_sampler(device, &si, &_sampler, &err);

        std::string src = dh_read_shader_file("shaders/tests/sampler_sample_2d.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader            = _shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_sampler) return {false, "init failed"};
        LRHIError lerr = {};
        SamplerPushConstants push = {
            lrhi_texture_view_get_bindless_index(_input_view,  &lerr),
            lrhi_sampler_get_bindless_index(_sampler, &lerr),
            lrhi_texture_view_get_bindless_index(_output_view, &lerr)
        };
        std::string err;
        if (!run_sampler_compute(_device, _pipeline,
                                 _input_tex, _output_tex,
                                 _input_view, _output_view, _sampler,
                                 _init_data, W, H, W, H,
                                 W * 4, W * H * 4,
                                 W / 8, H / 8,
                                 &push, sizeof(push), err))
            return {false, err};
        return sampler_texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view) { lrhi_destroy_texture_view(_output_view); _output_view = nullptr; }
        if (_input_view)  { lrhi_destroy_texture_view(_input_view);  _input_view  = nullptr; }
        if (_pipeline)    { lrhi_destroy_compute_pipeline(_pipeline); _pipeline   = nullptr; }
        if (_shader)      { lrhi_destroy_shader_module(_shader);      _shader     = nullptr; }
        if (_sampler)     { lrhi_destroy_sampler(_sampler);           _sampler    = nullptr; }
        if (_output_tex)  { lrhi_destroy_texture(_output_tex);        _output_tex = nullptr; }
        if (_input_tex)   { lrhi_destroy_texture(_input_tex);         _input_tex  = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(sampler_sample_2d_test);

// ---------------------------------------------------------------------------
// Test 2: sampler_sample_2d_array
// Samples all layers of a 2D array texture, outputs stacked vertically.
// ---------------------------------------------------------------------------

class sampler_sample_2d_array_test : public test
{
    static constexpr uint32_t W      = 128;
    static constexpr uint32_t H      = 128;
    static constexpr uint32_t LAYERS = 4;

    LRHIDevice           _device      = nullptr;
    LRHITexture          _input_tex   = nullptr;
    LRHITextureView      _input_view  = nullptr;
    LRHITexture          _output_tex  = nullptr;
    LRHITextureView      _output_view = nullptr;
    LRHISampler          _sampler     = nullptr;
    LRHIShaderModule     _shader      = nullptr;
    LRHIComputePipeline  _pipeline    = nullptr;
    std::vector<uint8_t> _init_data;

public:
    sampler_sample_2d_array_test()
    {
        type        = test_type::texture;
        name        = "sampler_sample_2d_array";
        source_path = "tests/golden/sampler_sample_2d_array.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        LRHIError err = {};

        // Input: 2D array, solid-color per layer
        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = LAYERS;
        ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        lrhi_create_texture(device, &ti, &_input_tex, &err);
        lrhi_texture_set_name(_input_tex, "sampler_2d_array_input");

        // Output: plain 2D, W x (H * LAYERS)
        LRHITextureInfo to = {};
        to.width = W; to.height = H * LAYERS; to.depth = 1;
        to.mip_levels = 1; to.array_layers = 1;
        to.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        to.usage      = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
        to.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &to, &_output_tex, &err);
        lrhi_texture_set_name(_output_tex, "sampler_2d_array_output");

        _input_view  = dh_make_view(device, _input_tex,  LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Each layer gets a distinct solid color
        const uint8_t layer_colors[LAYERS][4] = {
            {255,  64,  64, 255},
            { 64, 255,  64, 255},
            { 64,  64, 255, 255},
            {255, 255,  64, 255},
        };
        _init_data.resize((size_t)W * H * LAYERS * 4);
        for (uint32_t l = 0; l < LAYERS; ++l)
            for (uint32_t y = 0; y < H; ++y)
                for (uint32_t x = 0; x < W; ++x) {
                    uint32_t i = ((l * H + y) * W + x) * 4;
                    _init_data[i+0] = layer_colors[l][0];
                    _init_data[i+1] = layer_colors[l][1];
                    _init_data[i+2] = layer_colors[l][2];
                    _init_data[i+3] = layer_colors[l][3];
                }

        // Sampler
        LRHISamplerInfo si = {};
        si.min_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mag_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mipmap_filter  = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.min_lod = 0.0f; si.max_lod = 1000.0f;
        lrhi_create_sampler(device, &si, &_sampler, &err);

        std::string src = dh_read_shader_file("shaders/tests/sampler_sample_2d_array.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader            = _shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_sampler) return {false, "init failed"};
        std::string err;

        LRHICommandQueue queue = nullptr;
        LRHIFence        fence = nullptr;
        LRHICommandList  cmd   = nullptr;
        if (!sampler_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        LRHIResidencySet rs   = nullptr;
        LRHIError        lerr = {};
        lrhi_create_residency_set(_device, &rs, &lerr);
        lrhi_residency_set_add_texture(rs, _input_tex,  &lerr);
        lrhi_residency_set_add_texture(rs, _output_tex, &lerr);
        lrhi_residency_set_update(rs, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_residency_set(rs);
            sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
            return {false, std::string("residency: ") + lerr.message};
        }

        // Upload each layer separately
        std::vector<LRHIBuffer> stagings;
        const size_t layer_bytes = (size_t)W * H * 4;
        LRHIRegion region = {0, 0, 0, W, H, 1};
        for (uint32_t l = 0; l < LAYERS; ++l) {
            LRHIBuffer staging = nullptr;
            if (!upload_texture_layer(_device, cmd, rs, _input_tex,
                                      _init_data.data() + l * layer_bytes,
                                      (uint32_t)layer_bytes, W * 4, (uint32_t)layer_bytes,
                                      region, 0, l, &staging, err)) {
                for (auto b : stagings) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(rs);
                sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
                return {false, err};
            }
            stagings.push_back(staging);
        }

        struct PushConstants {
            uint32_t input_texture;
            uint32_t sampler;
            uint32_t output_texture;
            uint32_t layer_count;
        } push = {
            lrhi_texture_view_get_bindless_index(_input_view,  &lerr),
            lrhi_sampler_get_bindless_index(_sampler, &lerr),
            lrhi_texture_view_get_bindless_index(_output_view, &lerr),
            LAYERS
        };

        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &lerr);
        lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &lerr);
        lrhi_compute_pass_set_push_constants(cp, &push, sizeof(push), &lerr);
        lrhi_compute_pass_dispatch(cp, W / 8, (H * LAYERS) / 8, 1, 8, 8, 1, &lerr);
        lrhi_compute_pass_end(cp, &lerr);

        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            for (auto b : stagings) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(rs);
            sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
            return {false, std::string("compute pass: ") + lerr.message};
        }

        if (!sampler_end_cmd(_device, queue, fence, cmd, rs, err)) {
            for (auto b : stagings) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(rs);
            return {false, err};
        }

        for (auto b : stagings) lrhi_destroy_buffer(b);
        lrhi_destroy_residency_set(rs);
        return sampler_texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view) { lrhi_destroy_texture_view(_output_view); _output_view = nullptr; }
        if (_input_view)  { lrhi_destroy_texture_view(_input_view);  _input_view  = nullptr; }
        if (_pipeline)    { lrhi_destroy_compute_pipeline(_pipeline); _pipeline   = nullptr; }
        if (_shader)      { lrhi_destroy_shader_module(_shader);      _shader     = nullptr; }
        if (_sampler)     { lrhi_destroy_sampler(_sampler);           _sampler    = nullptr; }
        if (_output_tex)  { lrhi_destroy_texture(_output_tex);        _output_tex = nullptr; }
        if (_input_tex)   { lrhi_destroy_texture(_input_tex);         _input_tex  = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(sampler_sample_2d_array_test);

// ---------------------------------------------------------------------------
// Test 3: sampler_sample_3d
// Samples the mid-depth slice of a 3D texture.
// ---------------------------------------------------------------------------

class sampler_sample_3d_test : public test
{
    static constexpr uint32_t W = 32;
    static constexpr uint32_t H = 32;
    static constexpr uint32_t D = 32;

    LRHIDevice           _device      = nullptr;
    LRHITexture          _input_tex   = nullptr;
    LRHITextureView      _input_view  = nullptr;
    LRHITexture          _output_tex  = nullptr;
    LRHITextureView      _output_view = nullptr;
    LRHISampler          _sampler     = nullptr;
    LRHIShaderModule     _shader      = nullptr;
    LRHIComputePipeline  _pipeline    = nullptr;
    std::vector<uint8_t> _init_data;

public:
    sampler_sample_3d_test()
    {
        type        = test_type::texture;
        name        = "sampler_sample_3d";
        source_path = "tests/golden/sampler_sample_3d.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        LRHIError err = {};

        // Input: 3D texture
        LRHITextureInfo ti = {};
        ti.width = W; ti.height = H; ti.depth = D;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_3D;
        lrhi_create_texture(device, &ti, &_input_tex, &err);
        lrhi_texture_set_name(_input_tex, "sampler_3d_input");

        // Output: 2D slice
        LRHITextureInfo to = {};
        to.width = W; to.height = H; to.depth = 1;
        to.mip_levels = 1; to.array_layers = 1;
        to.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        to.usage      = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
        to.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &to, &_output_tex, &err);
        lrhi_texture_set_name(_output_tex, "sampler_3d_output");

        _input_view  = dh_make_view(device, _input_tex,  LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_3D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // 3D gradient: R=x, G=y, B=z
        _init_data.resize((size_t)W * H * D * 4);
        for (uint32_t z = 0; z < D; ++z)
            for (uint32_t y = 0; y < H; ++y)
                for (uint32_t x = 0; x < W; ++x) {
                    uint32_t i = ((z * H + y) * W + x) * 4;
                    _init_data[i+0] = (uint8_t)(x / (float)W * 255.0f);
                    _init_data[i+1] = (uint8_t)(y / (float)H * 255.0f);
                    _init_data[i+2] = (uint8_t)(z / (float)D * 255.0f);
                    _init_data[i+3] = 255;
                }

        // Sampler
        LRHISamplerInfo si = {};
        si.min_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mag_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mipmap_filter  = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.min_lod = 0.0f; si.max_lod = 1000.0f;
        lrhi_create_sampler(device, &si, &_sampler, &err);

        std::string src = dh_read_shader_file("shaders/tests/sampler_sample_3d.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader            = _shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_sampler) return {false, "init failed"};
        std::string err;

        LRHICommandQueue queue = nullptr;
        LRHIFence        fence = nullptr;
        LRHICommandList  cmd   = nullptr;
        if (!sampler_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        LRHIResidencySet rs   = nullptr;
        LRHIError        lerr = {};
        lrhi_create_residency_set(_device, &rs, &lerr);
        lrhi_residency_set_add_texture(rs, _input_tex,  &lerr);
        lrhi_residency_set_add_texture(rs, _output_tex, &lerr);
        lrhi_residency_set_update(rs, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_residency_set(rs);
            sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
            return {false, std::string("residency: ") + lerr.message};
        }

        // Upload each depth slice as a separate layer
        std::vector<LRHIBuffer> stagings;
        const size_t slice_bytes = (size_t)W * H * 4;
        for (uint32_t z = 0; z < D; ++z) {
            LRHIBuffer staging = nullptr;
            LRHIRegion region = {0, 0, z, W, H, 1};
            if (!upload_texture_layer(_device, cmd, rs, _input_tex,
                                      _init_data.data() + z * slice_bytes,
                                      (uint32_t)slice_bytes, W * 4, (uint32_t)slice_bytes,
                                      region, 0, 0, &staging, err)) {
                for (auto b : stagings) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(rs);
                sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
                return {false, err};
            }
            stagings.push_back(staging);
        }

        SamplerPushConstants push = {
            lrhi_texture_view_get_bindless_index(_input_view,  &lerr),
            lrhi_sampler_get_bindless_index(_sampler, &lerr),
            lrhi_texture_view_get_bindless_index(_output_view, &lerr)
        };

        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &lerr);
        lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &lerr);
        lrhi_compute_pass_set_push_constants(cp, &push, sizeof(push), &lerr);
        lrhi_compute_pass_dispatch(cp, W / 8, H / 8, 1, 8, 8, 1, &lerr);
        lrhi_compute_pass_end(cp, &lerr);

        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            for (auto b : stagings) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(rs);
            sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
            return {false, std::string("compute pass: ") + lerr.message};
        }

        if (!sampler_end_cmd(_device, queue, fence, cmd, rs, err)) {
            for (auto b : stagings) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(rs);
            return {false, err};
        }

        for (auto b : stagings) lrhi_destroy_buffer(b);
        lrhi_destroy_residency_set(rs);
        return sampler_texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view) { lrhi_destroy_texture_view(_output_view); _output_view = nullptr; }
        if (_input_view)  { lrhi_destroy_texture_view(_input_view);  _input_view  = nullptr; }
        if (_pipeline)    { lrhi_destroy_compute_pipeline(_pipeline); _pipeline   = nullptr; }
        if (_shader)      { lrhi_destroy_shader_module(_shader);      _shader     = nullptr; }
        if (_sampler)     { lrhi_destroy_sampler(_sampler);           _sampler    = nullptr; }
        if (_output_tex)  { lrhi_destroy_texture(_output_tex);        _output_tex = nullptr; }
        if (_input_tex)   { lrhi_destroy_texture(_input_tex);         _input_tex  = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(sampler_sample_3d_test);

// ---------------------------------------------------------------------------
// Test 4: sampler_sample_cube
// Samples each face of a cube texture, outputs a 6-face horizontal strip.
// ---------------------------------------------------------------------------

class sampler_sample_cube_test : public test
{
    static constexpr uint32_t FACE_SIZE = 32;

    LRHIDevice           _device      = nullptr;
    LRHITexture          _input_tex   = nullptr;
    LRHITextureView      _input_view  = nullptr;
    LRHITexture          _output_tex  = nullptr;
    LRHITextureView      _output_view = nullptr;
    LRHISampler          _sampler     = nullptr;
    LRHIShaderModule     _shader      = nullptr;
    LRHIComputePipeline  _pipeline    = nullptr;
    std::vector<uint8_t> _init_data;

public:
    sampler_sample_cube_test()
    {
        type        = test_type::texture;
        name        = "sampler_sample_cube";
        source_path = "tests/golden/sampler_sample_cube.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        LRHIError err = {};

        // Input: cube texture (6 array layers = 6 faces)
        LRHITextureInfo ti = {};
        ti.width = FACE_SIZE; ti.height = FACE_SIZE; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
        lrhi_create_texture(device, &ti, &_input_tex, &err);
        lrhi_texture_set_name(_input_tex, "sampler_cube_input");

        // Output: 2D, 6 faces wide
        LRHITextureInfo to = {};
        to.width = FACE_SIZE * 6; to.height = FACE_SIZE; to.depth = 1;
        to.mip_levels = 1; to.array_layers = 1;
        to.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        to.usage      = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
        to.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &to, &_output_tex, &err);
        lrhi_texture_set_name(_output_tex, "sampler_cube_output");

        _input_view  = dh_make_view(device, _input_tex,  LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Each face gets a distinct solid color: +X red, -X cyan, +Y green, -Y magenta, +Z blue, -Z yellow
        const uint8_t face_colors[6][4] = {
            {255,   0,   0, 255}, // +X
            {  0, 255, 255, 255}, // -X
            {  0, 255,   0, 255}, // +Y
            {255,   0, 255, 255}, // -Y
            {  0,   0, 255, 255}, // +Z
            {255, 255,   0, 255}, // -Z
        };
        const size_t face_bytes = (size_t)FACE_SIZE * FACE_SIZE * 4;
        _init_data.resize(face_bytes * 6);
        for (uint32_t f = 0; f < 6; ++f)
            for (size_t i = 0; i < (size_t)FACE_SIZE * FACE_SIZE; ++i) {
                _init_data[f * face_bytes + i * 4 + 0] = face_colors[f][0];
                _init_data[f * face_bytes + i * 4 + 1] = face_colors[f][1];
                _init_data[f * face_bytes + i * 4 + 2] = face_colors[f][2];
                _init_data[f * face_bytes + i * 4 + 3] = face_colors[f][3];
            }

        // Sampler
        LRHISamplerInfo si = {};
        si.min_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mag_filter     = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.mipmap_filter  = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
        si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.min_lod = 0.0f; si.max_lod = 1000.0f;
        lrhi_create_sampler(device, &si, &_sampler, &err);

        std::string src = dh_read_shader_file("shaders/tests/sampler_sample_cube.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader            = _shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_sampler) return {false, "init failed"};
        std::string err;

        LRHICommandQueue queue = nullptr;
        LRHIFence        fence = nullptr;
        LRHICommandList  cmd   = nullptr;
        if (!sampler_begin_cmd(_device, &queue, &fence, &cmd, err)) return {false, err};

        LRHIResidencySet rs   = nullptr;
        LRHIError        lerr = {};
        lrhi_create_residency_set(_device, &rs, &lerr);
        lrhi_residency_set_add_texture(rs, _input_tex,  &lerr);
        lrhi_residency_set_add_texture(rs, _output_tex, &lerr);
        lrhi_residency_set_update(rs, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_residency_set(rs);
            sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
            return {false, std::string("residency: ") + lerr.message};
        }

        // Upload 6 faces
        const size_t face_bytes = (size_t)FACE_SIZE * FACE_SIZE * 4;
        std::vector<LRHIBuffer> stagings;
        LRHIRegion region = {0, 0, 0, FACE_SIZE, FACE_SIZE, 1};
        for (uint32_t f = 0; f < 6; ++f) {
            LRHIBuffer staging = nullptr;
            if (!upload_texture_layer(_device, cmd, rs, _input_tex,
                                      _init_data.data() + f * face_bytes,
                                      (uint32_t)face_bytes, FACE_SIZE * 4, (uint32_t)face_bytes,
                                      region, 0, f, &staging, err)) {
                for (auto b : stagings) lrhi_destroy_buffer(b);
                lrhi_destroy_residency_set(rs);
                sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
                return {false, err};
            }
            stagings.push_back(staging);
        }

        SamplerPushConstants push = {
            lrhi_texture_view_get_bindless_index(_input_view,  &lerr),
            lrhi_sampler_get_bindless_index(_sampler, &lerr),
            lrhi_texture_view_get_bindless_index(_output_view, &lerr)
        };

        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &lerr);
        lrhi_compute_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_COPY, &lerr);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &lerr);
        lrhi_compute_pass_set_push_constants(cp, &push, sizeof(push), &lerr);
        // Dispatch over the 6-face-wide output
        lrhi_compute_pass_dispatch(cp, (FACE_SIZE * 6) / 8, FACE_SIZE / 8, 1, 8, 8, 1, &lerr);
        lrhi_compute_pass_end(cp, &lerr);

        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            for (auto b : stagings) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(rs);
            sampler_end_cmd(_device, queue, fence, cmd, nullptr, err);
            return {false, std::string("compute pass: ") + lerr.message};
        }

        if (!sampler_end_cmd(_device, queue, fence, cmd, rs, err)) {
            for (auto b : stagings) lrhi_destroy_buffer(b);
            lrhi_destroy_residency_set(rs);
            return {false, err};
        }

        for (auto b : stagings) lrhi_destroy_buffer(b);
        lrhi_destroy_residency_set(rs);
        return sampler_texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view) { lrhi_destroy_texture_view(_output_view); _output_view = nullptr; }
        if (_input_view)  { lrhi_destroy_texture_view(_input_view);  _input_view  = nullptr; }
        if (_pipeline)    { lrhi_destroy_compute_pipeline(_pipeline); _pipeline   = nullptr; }
        if (_shader)      { lrhi_destroy_shader_module(_shader);      _shader     = nullptr; }
        if (_sampler)     { lrhi_destroy_sampler(_sampler);           _sampler    = nullptr; }
        if (_output_tex)  { lrhi_destroy_texture(_output_tex);        _output_tex = nullptr; }
        if (_input_tex)   { lrhi_destroy_texture(_input_tex);         _input_tex  = nullptr; }
        _init_data.clear();
    }
};

REGISTER_TEST(sampler_sample_cube_test);

// ---------------------------------------------------------------------------
// Tests 5–8: Address mode tests
// Shared base class; subclasses only set the sampler address mode.
// ---------------------------------------------------------------------------

class sampler_address_mode_base : public test
{
protected:
    static constexpr uint32_t IN_SIZE  = 4;   // 4x4 checkerboard input
    static constexpr uint32_t OUT_SIZE = 32;  // 32x32 output (UVs span [-0.5, 1.5])

    LRHIDevice           _device      = nullptr;
    LRHITexture          _input_tex   = nullptr;
    LRHITextureView      _input_view  = nullptr;
    LRHITexture          _output_tex  = nullptr;
    LRHITextureView      _output_view = nullptr;
    LRHISampler          _sampler     = nullptr;
    LRHIShaderModule     _shader      = nullptr;
    LRHIComputePipeline  _pipeline    = nullptr;
    std::vector<uint8_t> _init_data;

    virtual LRHISamplerAddressMode address_mode() const = 0;

public:
    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        LRHIError err = {};

        LRHITextureInfo ti = {};
        ti.width = IN_SIZE; ti.height = IN_SIZE; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &ti, &_input_tex, &err);
        lrhi_texture_set_name(_input_tex, "sampler_addr_input");

        LRHITextureInfo to = {};
        to.width = OUT_SIZE; to.height = OUT_SIZE; to.depth = 1;
        to.mip_levels = 1; to.array_layers = 1;
        to.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        to.usage      = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
        to.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &to, &_output_tex, &err);
        lrhi_texture_set_name(_output_tex, "sampler_addr_output");

        _input_view  = dh_make_view(device, _input_tex,  LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // Checkerboard: white/black on even/odd (x+y) cells
        _init_data.resize((size_t)IN_SIZE * IN_SIZE * 4);
        for (uint32_t y = 0; y < IN_SIZE; ++y)
            for (uint32_t x = 0; x < IN_SIZE; ++x) {
                uint32_t i = (y * IN_SIZE + x) * 4;
                uint8_t  v = ((x + y) & 1) ? 255 : 0;
                _init_data[i+0] = v;
                _init_data[i+1] = v;
                _init_data[i+2] = v;
                _init_data[i+3] = 255;
            }

        LRHISamplerAddressMode mode = address_mode();
        LRHISamplerInfo si = {};
        si.min_filter     = LUMINARY_RHI_SAMPLER_FILTER_NEAREST;
        si.mag_filter     = LUMINARY_RHI_SAMPLER_FILTER_NEAREST;
        si.mipmap_filter  = LUMINARY_RHI_SAMPLER_FILTER_NEAREST;
        si.address_mode_u = mode;
        si.address_mode_v = mode;
        si.address_mode_w = mode;
        si.min_lod = 0.0f; si.max_lod = 1000.0f;
        lrhi_create_sampler(device, &si, &_sampler, &err);

        std::string src = dh_read_shader_file("shaders/tests/sampler_address_mode.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader            = _shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_sampler) return {false, "init failed"};
        LRHIError lerr = {};
        SamplerPushConstants push = {
            lrhi_texture_view_get_bindless_index(_input_view,  &lerr),
            lrhi_sampler_get_bindless_index(_sampler, &lerr),
            lrhi_texture_view_get_bindless_index(_output_view, &lerr)
        };
        std::string err;
        if (!run_sampler_compute(_device, _pipeline,
                                 _input_tex, _output_tex,
                                 _input_view, _output_view, _sampler,
                                 _init_data, IN_SIZE, IN_SIZE, OUT_SIZE, OUT_SIZE,
                                 IN_SIZE * 4, IN_SIZE * IN_SIZE * 4,
                                 OUT_SIZE / 8, OUT_SIZE / 8,
                                 &push, sizeof(push), err))
            return {false, err};
        return sampler_texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view) { lrhi_destroy_texture_view(_output_view); _output_view = nullptr; }
        if (_input_view)  { lrhi_destroy_texture_view(_input_view);  _input_view  = nullptr; }
        if (_pipeline)    { lrhi_destroy_compute_pipeline(_pipeline); _pipeline   = nullptr; }
        if (_shader)      { lrhi_destroy_shader_module(_shader);      _shader     = nullptr; }
        if (_sampler)     { lrhi_destroy_sampler(_sampler);           _sampler    = nullptr; }
        if (_output_tex)  { lrhi_destroy_texture(_output_tex);        _output_tex = nullptr; }
        if (_input_tex)   { lrhi_destroy_texture(_input_tex);         _input_tex  = nullptr; }
        _init_data.clear();
    }
};

class sampler_address_repeat_test : public sampler_address_mode_base
{
protected:
    LRHISamplerAddressMode address_mode() const override { return LUMINARY_RHI_SAMPLER_ADDRESS_MODE_REPEAT; }
public:
    sampler_address_repeat_test()
    {
        type        = test_type::texture;
        name        = "sampler_address_repeat";
        source_path = "tests/golden/sampler_address_repeat.png";
    }
};

REGISTER_TEST(sampler_address_repeat_test);

class sampler_address_mirrored_repeat_test : public sampler_address_mode_base
{
protected:
    LRHISamplerAddressMode address_mode() const override { return LUMINARY_RHI_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; }
public:
    sampler_address_mirrored_repeat_test()
    {
        type        = test_type::texture;
        name        = "sampler_address_mirrored_repeat";
        source_path = "tests/golden/sampler_address_mirrored_repeat.png";
    }
};

REGISTER_TEST(sampler_address_mirrored_repeat_test);

class sampler_address_clamp_to_edge_test : public sampler_address_mode_base
{
protected:
    LRHISamplerAddressMode address_mode() const override { return LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; }
public:
    sampler_address_clamp_to_edge_test()
    {
        type        = test_type::texture;
        name        = "sampler_address_clamp_to_edge";
        source_path = "tests/golden/sampler_address_clamp_to_edge.png";
    }
};

REGISTER_TEST(sampler_address_clamp_to_edge_test);

class sampler_address_clamp_to_border_test : public sampler_address_mode_base
{
protected:
    LRHISamplerAddressMode address_mode() const override { return LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; }
public:
    sampler_address_clamp_to_border_test()
    {
        type        = test_type::texture;
        name        = "sampler_address_clamp_to_border";
        source_path = "tests/golden/sampler_address_clamp_to_border.png";
    }
};

REGISTER_TEST(sampler_address_clamp_to_border_test);

// ---------------------------------------------------------------------------
// Tests 10–12: Filter tests
// Shared base: magnify a small texture to a larger output.
// ---------------------------------------------------------------------------

class sampler_filter_base : public test
{
protected:
    static constexpr uint32_t IN_SIZE  = 4;   // 4x4 input
    static constexpr uint32_t OUT_SIZE = 64;  // 64x64 output (magnification)

    LRHIDevice           _device      = nullptr;
    LRHITexture          _input_tex   = nullptr;
    LRHITextureView      _input_view  = nullptr;
    LRHITexture          _output_tex  = nullptr;
    LRHITextureView      _output_view = nullptr;
    LRHISampler          _sampler     = nullptr;
    LRHIShaderModule     _shader      = nullptr;
    LRHIComputePipeline  _pipeline    = nullptr;
    std::vector<uint8_t> _init_data;

    virtual LRHISamplerFilter filter_mode() const = 0;

public:
    void init(LRHIDevice device) override
    {
        _device = device;
        std::string err_str;
        LRHIError err = {};

        LRHITextureInfo ti = {};
        ti.width = IN_SIZE; ti.height = IN_SIZE; ti.depth = 1;
        ti.mip_levels = 1; ti.array_layers = 1;
        ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        ti.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &ti, &_input_tex, &err);
        lrhi_texture_set_name(_input_tex, "sampler_filter_input");

        LRHITextureInfo to = {};
        to.width = OUT_SIZE; to.height = OUT_SIZE; to.depth = 1;
        to.mip_levels = 1; to.array_layers = 1;
        to.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        to.usage      = LUMINARY_RHI_TEXTURE_USAGE_STORAGE;
        to.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        lrhi_create_texture(device, &to, &_output_tex, &err);
        lrhi_texture_set_name(_output_tex, "sampler_filter_output");

        _input_view  = dh_make_view(device, _input_tex,  LUMINARY_RHI_TEXTURE_USAGE_SAMPLED,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        _output_view = dh_make_view(device, _output_tex, LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        // 4x4 gradient: each texel has a distinct color (R=x/W, G=y/H)
        _init_data.resize((size_t)IN_SIZE * IN_SIZE * 4);
        for (uint32_t y = 0; y < IN_SIZE; ++y)
            for (uint32_t x = 0; x < IN_SIZE; ++x) {
                uint32_t i = (y * IN_SIZE + x) * 4;
                _init_data[i+0] = (uint8_t)(x * 255 / (IN_SIZE - 1));
                _init_data[i+1] = (uint8_t)(y * 255 / (IN_SIZE - 1));
                _init_data[i+2] = (uint8_t)((x + y) * 255 / (2 * (IN_SIZE - 1)));
                _init_data[i+3] = 255;
            }

        LRHISamplerFilter f = filter_mode();
        LRHISamplerInfo si = {};
        si.min_filter     = f;
        si.mag_filter     = f;
        si.mipmap_filter  = f;
        si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.min_lod = 0.0f; si.max_lod = 1000.0f;
        if (f == LUMINARY_RHI_SAMPLER_FILTER_ANISOTROPIC)
            si.anisotropy_enable = 1;
        lrhi_create_sampler(device, &si, &_sampler, &err);

        std::string src = dh_read_shader_file("shaders/tests/sampler_filter.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader            = _shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);
    }

    test_result run(bool bake_mode) override
    {
        if (!_pipeline || !_sampler) return {false, "init failed"};
        LRHIError lerr = {};
        SamplerPushConstants push = {
            lrhi_texture_view_get_bindless_index(_input_view,  &lerr),
            lrhi_sampler_get_bindless_index(_sampler, &lerr),
            lrhi_texture_view_get_bindless_index(_output_view, &lerr)
        };
        std::string err;
        if (!run_sampler_compute(_device, _pipeline,
                                 _input_tex, _output_tex,
                                 _input_view, _output_view, _sampler,
                                 _init_data, IN_SIZE, IN_SIZE, OUT_SIZE, OUT_SIZE,
                                 IN_SIZE * 4, IN_SIZE * IN_SIZE * 4,
                                 OUT_SIZE / 8, OUT_SIZE / 8,
                                 &push, sizeof(push), err))
            return {false, err};
        return sampler_texture_result(_device, _output_tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view) { lrhi_destroy_texture_view(_output_view); _output_view = nullptr; }
        if (_input_view)  { lrhi_destroy_texture_view(_input_view);  _input_view  = nullptr; }
        if (_pipeline)    { lrhi_destroy_compute_pipeline(_pipeline); _pipeline   = nullptr; }
        if (_shader)      { lrhi_destroy_shader_module(_shader);      _shader     = nullptr; }
        if (_sampler)     { lrhi_destroy_sampler(_sampler);           _sampler    = nullptr; }
        if (_output_tex)  { lrhi_destroy_texture(_output_tex);        _output_tex = nullptr; }
        if (_input_tex)   { lrhi_destroy_texture(_input_tex);         _input_tex  = nullptr; }
        _init_data.clear();
    }
};

class sampler_filter_nearest_test : public sampler_filter_base
{
protected:
    LRHISamplerFilter filter_mode() const override { return LUMINARY_RHI_SAMPLER_FILTER_NEAREST; }
public:
    sampler_filter_nearest_test()
    {
        type        = test_type::texture;
        name        = "sampler_filter_nearest";
        source_path = "tests/golden/sampler_filter_nearest.png";
    }
};

REGISTER_TEST(sampler_filter_nearest_test);

class sampler_filter_linear_test : public sampler_filter_base
{
protected:
    LRHISamplerFilter filter_mode() const override { return LUMINARY_RHI_SAMPLER_FILTER_LINEAR; }
public:
    sampler_filter_linear_test()
    {
        type        = test_type::texture;
        name        = "sampler_filter_linear";
        source_path = "tests/golden/sampler_filter_linear.png";
    }
};

REGISTER_TEST(sampler_filter_linear_test);

class sampler_filter_anisotropic_test : public sampler_filter_base
{
protected:
    LRHISamplerFilter filter_mode() const override { return LUMINARY_RHI_SAMPLER_FILTER_ANISOTROPIC; }
public:
    sampler_filter_anisotropic_test()
    {
        type        = test_type::texture;
        name        = "sampler_filter_anisotropic";
        source_path = "tests/golden/sampler_filter_anisotropic.png";
    }
};

REGISTER_TEST(sampler_filter_anisotropic_test);
