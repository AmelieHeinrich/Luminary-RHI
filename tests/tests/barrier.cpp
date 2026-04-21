#include "luminary_rhi.h"
#include "tests/test.h"
#include <vector>

#include <string>

// ---------------------------------------------------------------------------
// Shared helpers

static test_result buffer_test_result(LRHIDevice device, LRHIBuffer buf, const char* test_name, const char* golden_path, bool bake_mode)
{
    std::vector<uint8_t> rb;
    test_tools::rhi_readback_buffer(device, buf, rb);
    if (bake_mode) {
        test_tools::save_buffer(golden_path, rb);
        test_result r; r.passed = true; r.message = "baked"; r.golden_buffer = golden_path;
        return r;
    }
    std::string out = std::string("tests/output/") + test_name + ".bin";
    test_tools::save_buffer(out.c_str(), rb);
    bool passed = test_tools::validate_buffer(golden_path, rb);
    test_result r; r.passed = passed; r.message = passed ? "" : "Binary mismatch";
    r.golden_buffer = golden_path; r.output_buffer = out; return r;
}

static test_result texture_test_result_b(LRHIDevice device, LRHITexture tex, const char* test_name, const char* golden_path, bool bake_mode)
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

// Minimal setup: create queue, fence, command list, begin command list.
// On success, returns true and populates out_queue/fence/cmd.
static bool barrier_begin_cmd(LRHIDevice device,
                               LRHICommandQueue* out_queue,
                               LRHIFence* out_fence,
                               LRHICommandList* out_cmd,
                               std::string& err_out)
{
    LRHIError err = {};
    lrhi_create_command_queue(device, out_queue, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create queue: ") + err.message;
        return false;
    }
    lrhi_create_fence(device, 0, out_fence, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create fence: ") + err.message;
        lrhi_destroy_command_queue(*out_queue); *out_queue = nullptr;
        return false;
    }
    lrhi_create_command_list(*out_queue, out_cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create cmd: ") + err.message;
        lrhi_destroy_fence(*out_fence); *out_fence = nullptr;
        lrhi_destroy_command_queue(*out_queue); *out_queue = nullptr;
        return false;
    }

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

// Submit and wait, then tear down queue/fence/cmd.
static bool barrier_end_cmd(LRHIDevice device,
                             LRHICommandQueue queue,
                             LRHIFence fence,
                             LRHICommandList cmd,
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
    err = {};
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd submit: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        return false;
    }
    err = {};
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd queue wait: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        return false;
    }
    err = {};
    lrhi_fence_wait(fence, 1, 5000000000ULL, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("fence wait: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        return false;
    }
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    return true;
}

// ---------------------------------------------------------------------------
// 1. barrier_copy_intra_pass
//    lrhi_copy_pass_intra_barrier() within a single copy pass.
// ---------------------------------------------------------------------------

class barrier_copy_intra_pass_test : public test
{
    static constexpr uint64_t SIZE = 256;
    LRHIDevice _device = nullptr;
    LRHIBuffer _bufA = nullptr;
    LRHIBuffer _bufB = nullptr;
    LRHIBuffer _bufC = nullptr;
    LRHIResidencySet _rs = nullptr;
    bool _init_success = false;

public:
    barrier_copy_intra_pass_test() {
        type = test_type::buffer;
        name = "barrier_copy_intra_pass";
        source_path = "tests/golden/barrier_copy_intra_pass.bin";
    }

    void init(LRHIDevice device) override {
        _device = device;
        _init_success = false;
        LRHIBufferUsage u = LUMINARY_RHI_BUFFER_USAGE_STAGING;
        LRHIBufferInfo info = {}; info.size = SIZE; info.stride = 4; info.usage = u;
        LRHIError err = {};
        
        lrhi_create_buffer(_device, &info, &_bufA, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create buffer A: %s\n", err.message);
            return;
        }
        
        err = {};
        lrhi_create_buffer(_device, &info, &_bufB, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create buffer B: %s\n", err.message);
            lrhi_destroy_buffer(_bufA);
            _bufA = nullptr;
            return;
        }
        
        err = {};
        lrhi_create_buffer(_device, &info, &_bufC, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create buffer C: %s\n", err.message);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            _bufA = nullptr;
            _bufB = nullptr;
            return;
        }
        
        err = {};
        lrhi_create_residency_set(_device, &_rs, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create residency set: %s\n", err.message);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _bufA, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer A to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _bufB, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer B to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _bufC, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer C to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_update(_rs, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to update residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        _init_success = true;
    }
    
    test_result run(bool bake_mode) override {
        if (!_init_success) return {false, "Initialization failed"};
        if (!_bufA || !_bufB || !_bufC) return {false, "Buffers are null (creation likely failed)"};
        
        LRHIError map_err = {};
        void* ptr = lrhi_buffer_map(_bufA, &map_err);
        if (map_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !ptr) {
            return {false, std::string("map buffer A failed: ") + map_err.message};
        }
        for(uint32_t i=0; i<SIZE/4; ++i) ((uint32_t*)ptr)[i] = i + 1;
        
        map_err = {};
        lrhi_buffer_unmap(_bufA);
        
        LRHICommandQueue q=nullptr; LRHIFence f=nullptr; LRHICommandList cmd=nullptr; std::string err;
        if (!barrier_begin_cmd(_device, &q, &f, &cmd, err)) return {false, err};
        
        LRHIError res_err = {};
        lrhi_command_queue_add_residency_set(q, _rs, &res_err);
        if (res_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("add residency set failed: ") + res_err.message};
        }

        LRHICopyPass cp = lrhi_copy_pass_begin(cmd, nullptr);
        if (!cp) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, "copy pass begin failed"};
        }
        
        LRHIError cp_err = {};
        lrhi_copy_pass_copy_buffer_to_buffer(cp, _bufA, 0, _bufB, 0, SIZE, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy buffer A->B failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_intra_barrier(cp, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass intra barrier failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_copy_buffer_to_buffer(cp, _bufB, 0, _bufC, 0, SIZE, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy buffer B->C failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_end(cp, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass end failed: ") + cp_err.message};
        }

        if (!barrier_end_cmd(_device, q, f, cmd, err)) return {false, err};
        return buffer_test_result(_device, _bufC, name, source_path, bake_mode);
    }

    void cleanup() override {
        if (_bufA) lrhi_destroy_buffer(_bufA);
        if (_bufB) lrhi_destroy_buffer(_bufB);
        if (_bufC) lrhi_destroy_buffer(_bufC);
        if (_rs) lrhi_destroy_residency_set(_rs);
    }
};

REGISTER_TEST(barrier_copy_intra_pass_test);

// ---------------------------------------------------------------------------
// 2. barrier_copy_to_copy_encoder
//    lrhi_copy_pass_encoder_barrier() with afterStage = COPY.
//    Models synchronization between two sequential copy passes.
// ---------------------------------------------------------------------------

class barrier_copy_to_copy_encoder_test : public test
{
    static constexpr uint64_t SIZE = 256;
    LRHIDevice _device = nullptr;
    LRHIBuffer _bufA = nullptr;
    LRHIBuffer _bufB = nullptr;
    LRHIBuffer _bufC = nullptr;
    LRHIResidencySet _rs = nullptr;
    bool _init_success = false;

public:
    barrier_copy_to_copy_encoder_test() {
        type = test_type::buffer;
        name = "barrier_copy_to_copy_encoder";
        source_path = "tests/golden/barrier_copy_to_copy_encoder.bin";
    }

    void init(LRHIDevice device) override {
        _device = device;
        _init_success = false;
        LRHIBufferUsage u = LUMINARY_RHI_BUFFER_USAGE_STAGING;
        LRHIBufferInfo info = {}; info.size = SIZE; info.stride = 4; info.usage = u;
        LRHIError err = {};
        
        lrhi_create_buffer(_device, &info, &_bufA, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create buffer A: %s\n", err.message);
            return;
        }
        
        err = {};
        lrhi_create_buffer(_device, &info, &_bufB, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create buffer B: %s\n", err.message);
            lrhi_destroy_buffer(_bufA);
            _bufA = nullptr;
            return;
        }
        
        err = {};
        lrhi_create_buffer(_device, &info, &_bufC, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create buffer C: %s\n", err.message);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            _bufA = nullptr;
            _bufB = nullptr;
            return;
        }
        
        err = {};
        lrhi_create_residency_set(_device, &_rs, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create residency set: %s\n", err.message);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _bufA, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer A to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _bufB, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer B to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _bufC, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer C to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_update(_rs, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to update residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_buffer(_bufA);
            lrhi_destroy_buffer(_bufB);
            lrhi_destroy_buffer(_bufC);
            _rs = nullptr;
            _bufA = nullptr;
            _bufB = nullptr;
            _bufC = nullptr;
            return;
        }
        
        _init_success = true;
    }
    
    test_result run(bool bake_mode) override {
        if (!_init_success) return {false, "Initialization failed"};
        if (!_bufA || !_bufB || !_bufC) return {false, "Buffers are null (creation likely failed)"};
        
        LRHIError map_err = {};
        void* ptr = lrhi_buffer_map(_bufA, &map_err);
        if (map_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !ptr) {
            return {false, std::string("map buffer A failed: ") + map_err.message};
        }
        for(uint32_t i=0; i<SIZE/4; ++i) ((uint32_t*)ptr)[i] = i + 2;
        
        map_err = {};
        lrhi_buffer_unmap(_bufA);
        
        LRHICommandQueue q=nullptr; LRHIFence f=nullptr; LRHICommandList cmd=nullptr; std::string err;
        if (!barrier_begin_cmd(_device, &q, &f, &cmd, err)) return {false, err};
        
        LRHIError res_err = {};
        lrhi_command_queue_add_residency_set(q, _rs, &res_err);
        if (res_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("add residency set failed: ") + res_err.message};
        }

        LRHICopyPass cp1 = lrhi_copy_pass_begin(cmd, nullptr);
        if (!cp1) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, "copy pass 1 begin failed"};
        }
        
        LRHIError cp_err = {};
        lrhi_copy_pass_copy_buffer_to_buffer(cp1, _bufA, 0, _bufB, 0, SIZE, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass 1 buffer copy failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_end(cp1, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass 1 end failed: ") + cp_err.message};
        }

        LRHICopyPass cp2 = lrhi_copy_pass_begin(cmd, nullptr);
        if (!cp2) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, "copy pass 2 begin failed"};
        }
        
        cp_err = {};
        lrhi_copy_pass_encoder_barrier(cp2, LUMINARY_RHI_RENDER_STAGE_COPY, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass 2 encoder barrier failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_copy_buffer_to_buffer(cp2, _bufB, 0, _bufC, 0, SIZE, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass 2 buffer copy failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_end(cp2, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass 2 end failed: ") + cp_err.message};
        }

        if (!barrier_end_cmd(_device, q, f, cmd, err)) return {false, err};
        return buffer_test_result(_device, _bufC, name, source_path, bake_mode);
    }

    void cleanup() override {
        if (_bufA) lrhi_destroy_buffer(_bufA);
        if (_bufB) lrhi_destroy_buffer(_bufB);
        if (_bufC) lrhi_destroy_buffer(_bufC);
        if (_rs) lrhi_destroy_residency_set(_rs);
    }
};

REGISTER_TEST(barrier_copy_to_copy_encoder_test);

// ---------------------------------------------------------------------------
// 4. barrier_copy_to_render_encoder
//    lrhi_copy_pass_encoder_barrier() with afterStage = FRAGMENT.
//    Models synchronization from a copy pass to a subsequent render pass.
// ---------------------------------------------------------------------------

class barrier_copy_to_render_encoder_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;
    LRHIBuffer      _buf    = nullptr;
    LRHIResidencySet _rs    = nullptr;
    bool _init_success = false;

public:
    barrier_copy_to_render_encoder_test() {
        type = test_type::texture;
        name = "barrier_copy_to_render_encoder";
        source_path = "tests/golden/barrier_copy_to_render_encoder.png";
    }

    void init(LRHIDevice device) override {
        _device = device;
        _init_success = false;
        LRHIBufferInfo binfo = {}; binfo.size = W * H * 4; binfo.stride = 4;
        binfo.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_STAGING);
        LRHIError err = {};
        
        lrhi_create_buffer(_device, &binfo, &_buf, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create upload buffer: %s\n", err.message);
            return;
        }

        LRHITextureInfo info = {}; info.width = W; info.height = H; info.depth = 1;
        info.mip_levels = 1; info.array_layers = 1;
        info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage  = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        
        err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create texture: %s\n", err.message);
            lrhi_destroy_buffer(_buf);
            _buf = nullptr;
            return;
        }

        LRHITextureViewInfo vinfo = {}; vinfo.texture = _tex; vinfo.base_mip_level = 0; vinfo.mip_level_count = 1;
        vinfo.base_array_layer = 0; vinfo.array_layer_count = 1; vinfo.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
        vinfo.usage = LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET; vinfo.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        
        err = {};
        lrhi_create_texture_view(_device, &vinfo, &_view, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create texture view: %s\n", err.message);
            lrhi_destroy_texture(_tex);
            lrhi_destroy_buffer(_buf);
            _tex = nullptr;
            _buf = nullptr;
            return;
        }

        err = {};
        lrhi_create_residency_set(_device, &_rs, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to create residency set: %s\n", err.message);
            lrhi_destroy_texture_view(_view);
            lrhi_destroy_texture(_tex);
            lrhi_destroy_buffer(_buf);
            _view = nullptr;
            _tex = nullptr;
            _buf = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_texture(_rs, _tex, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add texture to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_texture_view(_view);
            lrhi_destroy_texture(_tex);
            lrhi_destroy_buffer(_buf);
            _rs = nullptr;
            _view = nullptr;
            _tex = nullptr;
            _buf = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_add_buffer(_rs, _buf, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to add buffer to residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_texture_view(_view);
            lrhi_destroy_texture(_tex);
            lrhi_destroy_buffer(_buf);
            _rs = nullptr;
            _view = nullptr;
            _tex = nullptr;
            _buf = nullptr;
            return;
        }
        
        err = {};
        lrhi_residency_set_update(_rs, &err);
        if (err.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
            fprintf(stderr, "Failed to update residency set: %s\n", err.message);
            lrhi_destroy_residency_set(_rs);
            lrhi_destroy_texture_view(_view);
            lrhi_destroy_texture(_tex);
            lrhi_destroy_buffer(_buf);
            _rs = nullptr;
            _view = nullptr;
            _tex = nullptr;
            _buf = nullptr;
            return;
        }
        
        _init_success = true;
    }
    
    test_result run(bool bake_mode) override {
        if (!_init_success) return {false, "Initialization failed"};
        if (!_buf || !_tex || !_view) return {false, "Resources are null (creation likely failed)"};
        
        LRHIError map_err = {};
        void* ptr = lrhi_buffer_map(_buf, &map_err);
        if (map_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !ptr) {
            return {false, std::string("map upload buffer failed: ") + map_err.message};
        }
        for(uint32_t i=0; i<W*H; ++i) ((uint32_t*)ptr)[i] = 0xFF00FF00; // green 
        
        map_err = {};
        lrhi_buffer_unmap(_buf);

        LRHICommandQueue q=nullptr; LRHIFence f=nullptr; LRHICommandList cmd=nullptr; std::string err;
        if (!barrier_begin_cmd(_device, &q, &f, &cmd, err)) return {false, err};
        
        LRHIError res_err = {};
        lrhi_command_queue_add_residency_set(q, _rs, &res_err);
        if (res_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("add residency set failed: ") + res_err.message};
        }

        LRHICopyPass cp = lrhi_copy_pass_begin(cmd, nullptr);
        if (!cp) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, "copy pass begin failed"};
        }
        
        LRHIRegion reg = {0, 0, 0, W, H, 1};
        LRHIError cp_err = {};
        lrhi_copy_pass_copy_buffer_to_texture(cp, _buf, 0, W * 4, W * H * 4, _tex, reg, 0, 0, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy buffer to texture failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_encoder_barrier(cp, LUMINARY_RHI_RENDER_STAGE_FRAGMENT, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass encoder barrier failed: ") + cp_err.message};
        }
        
        cp_err = {};
        lrhi_copy_pass_end(cp, &cp_err);
        if (cp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("copy pass end failed: ") + cp_err.message};
        }

        // Then just load the texture to confirm its contents in the render pass. Since we don't have pipeline, 
        // we'll just do a LOAD and STORE using render pass.
        LRHIRenderPassInfo rp_info = {};
        rp_info.color_attachments[0].texture_view = _view;
        rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_LOAD;
        rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
        rp_info.color_attachments[0].clear_color[0] = 0.0f;
        rp_info.color_attachments[0].clear_color[1] = 0.0f;
        rp_info.color_attachments[0].clear_color[2] = 0.0f;
        rp_info.color_attachments[0].clear_color[3] = 1.0f;
        rp_info.color_attachment_count = 1;
        rp_info.render_width = W;
        rp_info.render_height = H;
        
        LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, nullptr);
        if (!rp) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, "render pass begin failed"};
        }
        
        LRHIError rp_err = {};
        lrhi_render_pass_end(rp, &rp_err);
        if (rp_err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            std::string barrier_err;
            barrier_end_cmd(_device, q, f, cmd, barrier_err);
            return {false, std::string("render pass end failed: ") + rp_err.message};
        }

        if (!barrier_end_cmd(_device, q, f, cmd, err)) return {false, err};
        return texture_test_result_b(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override {
        if (_view) lrhi_destroy_texture_view(_view);
        if (_tex) lrhi_destroy_texture(_tex);
        if (_buf) lrhi_destroy_buffer(_buf);
        if (_rs) lrhi_destroy_residency_set(_rs);
    }
};

REGISTER_TEST(barrier_copy_to_render_encoder_test);
