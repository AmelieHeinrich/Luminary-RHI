#include "luminary_rhi.h"
#include "tests/draw_helpers.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Shared command helpers (same pattern as compute_barrier.cpp)
// ---------------------------------------------------------------------------

static bool bv_begin_cmd(LRHIDevice device,
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

static bool bv_end_cmd(LRHICommandQueue queue,
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

// Buffer test result helper
static test_result bv_buffer_result(LRHIDevice device,
                                    LRHIBuffer buffer,
                                    const char* test_name,
                                    const char* golden_path,
                                    bool bake_mode)
{
    std::vector<uint8_t> readback;
    test_tools::rhi_readback_buffer(device, buffer, readback);

    std::string output_buffer = std::string("tests/output/") + test_name + ".bin";

    if (bake_mode) {
        test_tools::save_buffer(golden_path, readback);
        test_result r;
        r.passed = true;
        r.message = "baked";
        r.golden_buffer = golden_path;
        return r;
    }

    test_tools::save_buffer(output_buffer.c_str(), readback);
    bool passed = test_tools::validate_buffer(golden_path, readback);

    test_result r;
    r.passed        = passed;
    r.message       = passed ? "" : "buffer data mismatch";
    r.output_buffer = output_buffer;
    r.golden_buffer = golden_path;
    return r;
}

// ===========================================================================
// 1. compute_buffer_atomics_test
//    Single compute pass: 8×8 threads perform atomics on a RWByteAddressBuffer.
//    Reuses shaders/tests/compute_buffer_atomics.hlsl.
// ===========================================================================

class compute_buffer_atomics_test : public test
{
    LRHIDevice           _device   = nullptr;
    LRHIBuffer           _buffer   = nullptr;
    LRHIBufferView       _view     = nullptr;
    LRHIShaderModule     _cs       = nullptr;
    LRHIComputePipeline  _pipeline = nullptr;
    LRHIResidencySet     _rs       = nullptr;

public:
    compute_buffer_atomics_test()
    {
        type        = test_type::buffer;
        name        = "compute_buffer_atomics";
        source_path = "tests/golden/compute_buffer_atomics.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        // 64 bytes: offset 0 (add), 4 (min), 8 (max), 12 (CAS) + padding
        LRHIBufferInfo bi = {};
        bi.size   = 64;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        LRHIError err = {};
        lrhi_create_buffer(device, &bi, &_buffer, &err);

        LRHIBufferViewInfo vi = {};
        vi.buffer    = _buffer;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_view, &err);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/compute_buffer_atomics.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader          = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_buffer(_rs, _buffer, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        // Zero-initialise buffer
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_buffer, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("buffer map: ") + err.message };
            memset(ptr, 0, 64);
            // Seed the CAS target at offset 12 with the expected sentinel
            reinterpret_cast<uint32_t*>(ptr)[3] = 0xDEADBEEFu;
            lrhi_buffer_unmap(_buffer);
        }

        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!bv_begin_cmd(_device, &q, &f, &cmd, err_str)) return { false, err_str };

        LRHIError err = {};
        uint32_t buf_idx = lrhi_buffer_view_get_bindless_index(_view, &err);

        struct C { uint32_t output_buffer; } c = { buf_idx };
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp, &c, sizeof(c), &err);
        lrhi_compute_pass_dispatch(cp, 1, 1, 1, 8, 8, 1, &err);
        lrhi_compute_pass_end(cp, &err);

        if (!bv_end_cmd(q, f, cmd, _rs, err_str)) return { false, err_str };
        return bv_buffer_result(_device, _buffer, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)  { lrhi_destroy_compute_pipeline(_pipeline); _pipeline = nullptr; }
        if (_cs)        { lrhi_destroy_shader_module(_cs);          _cs       = nullptr; }
        if (_view)      { lrhi_destroy_buffer_view(_view);          _view     = nullptr; }
        if (_rs)        { lrhi_destroy_residency_set(_rs);          _rs       = nullptr; }
        if (_buffer)    { lrhi_destroy_buffer(_buffer);             _buffer   = nullptr; }
    }
};
REGISTER_TEST(compute_buffer_atomics_test);

// ===========================================================================
// 2. buffer_view_constant_test
//    CPU writes known data to a constant buffer; a compute shader reads it via
//    a ConstantBuffer<uint4> CBV and copies the result to an output buffer.
// ===========================================================================

class buffer_view_constant_test : public test
{
    LRHIDevice           _device     = nullptr;
    LRHIBuffer           _input      = nullptr;
    LRHIBufferView       _cbv        = nullptr;
    LRHIBuffer           _output     = nullptr;
    LRHIBufferView       _out_view   = nullptr;
    LRHIShaderModule     _cs         = nullptr;
    LRHIComputePipeline  _pipeline   = nullptr;
    LRHIResidencySet     _rs         = nullptr;

public:
    buffer_view_constant_test()
    {
        type        = test_type::buffer;
        name        = "buffer_view_constant";
        source_path = "tests/golden/buffer_view_constant.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        // D3D12 CBVs must be 256-byte aligned; use 256 bytes to be safe.
        LRHIBufferInfo bi = {};
        bi.size   = 256;
        bi.stride = 16;
        bi.usage  = LUMINARY_RHI_BUFFER_USAGE_CONSTANT;
        LRHIError err = {};
        lrhi_create_buffer(device, &bi, &_input, &err);

        LRHIBufferViewInfo vi = {};
        vi.buffer    = _input;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_CONSTANT;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_cbv, &err);

        bi.size   = 16;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(device, &bi, &_output, &err);

        vi.buffer    = _output;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_out_view, &err);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/buffer_view_constant.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader          = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_buffer(_rs, _input,  nullptr);
        lrhi_residency_set_add_buffer(_rs, _output, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        // Write known pattern into constant buffer
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_input, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("input map: ") + err.message };
            auto* words = reinterpret_cast<uint32_t*>(ptr);
            words[0] = 0x11223344u;
            words[1] = 0xAABBCCDDu;
            words[2] = 0xDEADBEEFu;
            words[3] = 0xCAFEBABEu;
            lrhi_buffer_unmap(_input);
        }

        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!bv_begin_cmd(_device, &q, &f, &cmd, err_str)) return { false, err_str };

        LRHIError err = {};
        uint32_t cbv_idx = lrhi_buffer_view_get_bindless_index(_cbv,      &err);
        uint32_t out_idx = lrhi_buffer_view_get_bindless_index(_out_view, &err);

        struct C { uint32_t input_buffer; uint32_t output_buffer; } c = { cbv_idx, out_idx };
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp, &c, sizeof(c), &err);
        lrhi_compute_pass_dispatch(cp, 1, 1, 1, 1, 1, 1, &err);
        lrhi_compute_pass_end(cp, &err);

        if (!bv_end_cmd(q, f, cmd, _rs, err_str)) return { false, err_str };
        return bv_buffer_result(_device, _output, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)  { lrhi_destroy_compute_pipeline(_pipeline); _pipeline = nullptr; }
        if (_cs)        { lrhi_destroy_shader_module(_cs);          _cs       = nullptr; }
        if (_out_view)  { lrhi_destroy_buffer_view(_out_view);      _out_view = nullptr; }
        if (_cbv)       { lrhi_destroy_buffer_view(_cbv);           _cbv      = nullptr; }
        if (_rs)        { lrhi_destroy_residency_set(_rs);          _rs       = nullptr; }
        if (_output)    { lrhi_destroy_buffer(_output);             _output   = nullptr; }
        if (_input)     { lrhi_destroy_buffer(_input);              _input    = nullptr; }
    }
};
REGISTER_TEST(buffer_view_constant_test);

// ===========================================================================
// 3. buffer_view_structured_test
//    CPU populates a StructuredBuffer<uint2>; a compute shader reads it and
//    writes each element into a RWByteAddressBuffer output.
// ===========================================================================

class buffer_view_structured_test : public test
{
    static constexpr uint32_t ELEM_COUNT = 4;

    LRHIDevice           _device   = nullptr;
    LRHIBuffer           _input    = nullptr;
    LRHIBufferView       _srv      = nullptr;
    LRHIBuffer           _output   = nullptr;
    LRHIBufferView       _out_view = nullptr;
    LRHIShaderModule     _cs       = nullptr;
    LRHIComputePipeline  _pipeline = nullptr;
    LRHIResidencySet     _rs       = nullptr;

public:
    buffer_view_structured_test()
    {
        type        = test_type::buffer;
        name        = "buffer_view_structured";
        source_path = "tests/golden/buffer_view_structured.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        // Input: 4 × uint2 = 32 bytes, stride = 8
        LRHIBufferInfo bi = {};
        bi.size   = ELEM_COUNT * 8;
        bi.stride = 8;
        bi.usage  = LUMINARY_RHI_BUFFER_USAGE_SHADER_READ;
        LRHIError err = {};
        lrhi_create_buffer(device, &bi, &_input, &err);

        LRHIBufferViewInfo vi = {};
        vi.buffer    = _input;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_STRUCTURED;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_srv, &err);

        bi.size   = ELEM_COUNT * 8;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(device, &bi, &_output, &err);

        vi.buffer    = _output;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_out_view, &err);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/buffer_view_structured.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader          = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_buffer(_rs, _input,  nullptr);
        lrhi_residency_set_add_buffer(_rs, _output, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        // Write pattern: element[i] = { i*2, i*2+1 }
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_input, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("input map: ") + err.message };
            auto* words = reinterpret_cast<uint32_t*>(ptr);
            for (uint32_t i = 0; i < ELEM_COUNT; ++i) {
                words[i * 2 + 0] = i * 2u;
                words[i * 2 + 1] = i * 2u + 1u;
            }
            lrhi_buffer_unmap(_input);
        }

        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!bv_begin_cmd(_device, &q, &f, &cmd, err_str)) return { false, err_str };

        LRHIError err = {};
        uint32_t srv_idx = lrhi_buffer_view_get_bindless_index(_srv,      &err);
        uint32_t out_idx = lrhi_buffer_view_get_bindless_index(_out_view, &err);

        struct C { uint32_t input_buffer; uint32_t output_buffer; } c = { srv_idx, out_idx };
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp, &c, sizeof(c), &err);
        lrhi_compute_pass_dispatch(cp, 1, 1, 1, 4, 1, 1, &err);
        lrhi_compute_pass_end(cp, &err);

        if (!bv_end_cmd(q, f, cmd, _rs, err_str)) return { false, err_str };
        return bv_buffer_result(_device, _output, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)  { lrhi_destroy_compute_pipeline(_pipeline); _pipeline = nullptr; }
        if (_cs)        { lrhi_destroy_shader_module(_cs);          _cs       = nullptr; }
        if (_out_view)  { lrhi_destroy_buffer_view(_out_view);      _out_view = nullptr; }
        if (_srv)       { lrhi_destroy_buffer_view(_srv);           _srv      = nullptr; }
        if (_rs)        { lrhi_destroy_residency_set(_rs);          _rs       = nullptr; }
        if (_output)    { lrhi_destroy_buffer(_output);             _output   = nullptr; }
        if (_input)     { lrhi_destroy_buffer(_input);              _input    = nullptr; }
    }
};
REGISTER_TEST(buffer_view_structured_test);

// ===========================================================================
// 4. buffer_view_rwstructured_test
//    Pass A: compute writes a pattern into an RWStructuredBuffer<uint2>.
//    Pass B: encoder barrier on COMPUTE stage, then reads the RWStructuredBuffer
//            and copies elements to an output RWByteAddressBuffer.
// ===========================================================================

class buffer_view_rwstructured_test : public test
{
    static constexpr uint32_t ELEM_COUNT = 4;

    LRHIDevice           _device    = nullptr;
    LRHIBuffer           _rw_struct = nullptr;
    LRHIBufferView       _rw_view   = nullptr;
    LRHIBuffer           _output    = nullptr;
    LRHIBufferView       _out_view  = nullptr;
    LRHIShaderModule     _cs        = nullptr;
    LRHIComputePipeline  _pipeline  = nullptr;
    LRHIResidencySet     _rs        = nullptr;

public:
    buffer_view_rwstructured_test()
    {
        type        = test_type::buffer;
        name        = "buffer_view_rwstructured";
        source_path = "tests/golden/buffer_view_rwstructured.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo bi = {};
        bi.size   = ELEM_COUNT * 8;
        bi.stride = 8;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        LRHIError err = {};
        lrhi_create_buffer(device, &bi, &_rw_struct, &err);

        LRHIBufferViewInfo vi = {};
        vi.buffer    = _rw_struct;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_STRUCTURED;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_rw_view, &err);

        bi.size   = ELEM_COUNT * 8;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(device, &bi, &_output, &err);

        vi.buffer    = _output;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_out_view, &err);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/buffer_view_rwstructured.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader          = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_buffer(_rs, _rw_struct, nullptr);
        lrhi_residency_set_add_buffer(_rs, _output,    nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!bv_begin_cmd(_device, &q, &f, &cmd, err_str)) return { false, err_str };

        LRHIError err = {};
        uint32_t rw_idx  = lrhi_buffer_view_get_bindless_index(_rw_view,  &err);
        uint32_t out_idx = lrhi_buffer_view_get_bindless_index(_out_view, &err);

        // Pass A: write pattern into RWStructuredBuffer
        struct C { uint32_t rw_struct_buffer; uint32_t output_buffer; uint32_t pass_index; };
        C c_write = { rw_idx, out_idx, 0 };
        LRHIComputePass cp_a = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp_a, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp_a, &c_write, sizeof(c_write), &err);
        lrhi_compute_pass_dispatch(cp_a, 1, 1, 1, 4, 1, 1, &err);
        lrhi_compute_pass_end(cp_a, &err);

        // Pass B: barrier on compute stage, then read and copy to output
        C c_read = { rw_idx, out_idx, 1 };
        LRHIComputePass cp_b = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_encoder_barrier(cp_b, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &err);
        lrhi_compute_pass_set_pipeline(cp_b, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp_b, &c_read, sizeof(c_read), &err);
        lrhi_compute_pass_dispatch(cp_b, 1, 1, 1, 4, 1, 1, &err);
        lrhi_compute_pass_end(cp_b, &err);

        if (!bv_end_cmd(q, f, cmd, _rs, err_str)) return { false, err_str };
        return bv_buffer_result(_device, _output, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)  { lrhi_destroy_compute_pipeline(_pipeline); _pipeline = nullptr; }
        if (_cs)        { lrhi_destroy_shader_module(_cs);          _cs       = nullptr; }
        if (_out_view)  { lrhi_destroy_buffer_view(_out_view);      _out_view = nullptr; }
        if (_rw_view)   { lrhi_destroy_buffer_view(_rw_view);       _rw_view  = nullptr; }
        if (_rs)        { lrhi_destroy_residency_set(_rs);          _rs       = nullptr; }
        if (_output)    { lrhi_destroy_buffer(_output);             _output   = nullptr; }
        if (_rw_struct) { lrhi_destroy_buffer(_rw_struct);          _rw_struct = nullptr; }
    }
};
REGISTER_TEST(buffer_view_rwstructured_test);

// ===========================================================================
// 5. buffer_view_byteaddress_test
//    CPU writes a known byte pattern; a compute shader reads via
//    LuminaryByteAddressBuffer (Load/Load2/Load4) and copies to output.
// ===========================================================================

class buffer_view_byteaddress_test : public test
{
    // 28 bytes: Store(0,4B) + Store2(4,8B) + Store4(12,16B)
    static constexpr uint32_t BUF_SIZE = 28;

    LRHIDevice           _device   = nullptr;
    LRHIBuffer           _input    = nullptr;
    LRHIBufferView       _srv      = nullptr;
    LRHIBuffer           _output   = nullptr;
    LRHIBufferView       _out_view = nullptr;
    LRHIShaderModule     _cs       = nullptr;
    LRHIComputePipeline  _pipeline = nullptr;
    LRHIResidencySet     _rs       = nullptr;

public:
    buffer_view_byteaddress_test()
    {
        type        = test_type::buffer;
        name        = "buffer_view_byteaddress";
        source_path = "tests/golden/buffer_view_byteaddress.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo bi = {};
        bi.size   = BUF_SIZE;
        bi.stride = 4;
        bi.usage  = LUMINARY_RHI_BUFFER_USAGE_SHADER_READ;
        LRHIError err = {};
        lrhi_create_buffer(device, &bi, &_input, &err);

        LRHIBufferViewInfo vi = {};
        vi.buffer    = _input;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_srv, &err);

        bi.size   = BUF_SIZE;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(device, &bi, &_output, &err);

        vi.buffer    = _output;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_out_view, &err);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/buffer_view_byteaddress.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader          = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_buffer(_rs, _input,  nullptr);
        lrhi_residency_set_add_buffer(_rs, _output, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        // Write known pattern matching what the shader reads with Load/Load2/Load4
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_input, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("input map: ") + err.message };
            auto* words = reinterpret_cast<uint32_t*>(ptr);
            words[0] = 0xDEADBEEFu;            // Load at byte 0
            words[1] = 0x11223344u;            // Load2 at byte 4 (x)
            words[2] = 0xAABBCCDDu;            // Load2 at byte 4 (y)
            words[3] = 0x10203040u;            // Load4 at byte 12 (x)
            words[4] = 0x50607080u;            // Load4 at byte 12 (y)
            words[5] = 0x90A0B0C0u;            // Load4 at byte 12 (z)
            words[6] = 0xD0E0F000u;            // Load4 at byte 12 (w)
            lrhi_buffer_unmap(_input);
        }

        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!bv_begin_cmd(_device, &q, &f, &cmd, err_str)) return { false, err_str };

        LRHIError err = {};
        uint32_t srv_idx = lrhi_buffer_view_get_bindless_index(_srv,      &err);
        uint32_t out_idx = lrhi_buffer_view_get_bindless_index(_out_view, &err);

        struct C { uint32_t input_buffer; uint32_t output_buffer; } c = { srv_idx, out_idx };
        LRHIComputePass cp = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp, &c, sizeof(c), &err);
        lrhi_compute_pass_dispatch(cp, 1, 1, 1, 1, 1, 1, &err);
        lrhi_compute_pass_end(cp, &err);

        if (!bv_end_cmd(q, f, cmd, _rs, err_str)) return { false, err_str };
        return bv_buffer_result(_device, _output, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline)  { lrhi_destroy_compute_pipeline(_pipeline); _pipeline = nullptr; }
        if (_cs)        { lrhi_destroy_shader_module(_cs);          _cs       = nullptr; }
        if (_out_view)  { lrhi_destroy_buffer_view(_out_view);      _out_view = nullptr; }
        if (_srv)       { lrhi_destroy_buffer_view(_srv);           _srv      = nullptr; }
        if (_rs)        { lrhi_destroy_residency_set(_rs);          _rs       = nullptr; }
        if (_output)    { lrhi_destroy_buffer(_output);             _output   = nullptr; }
        if (_input)     { lrhi_destroy_buffer(_input);              _input    = nullptr; }
    }
};
REGISTER_TEST(buffer_view_byteaddress_test);

// ===========================================================================
// 6. buffer_view_rwbyteaddress_test
//    Pass A: compute writes a pattern via Store/Store2/Store4 to an
//            RWByteAddressBuffer.
//    Pass B: encoder barrier on COMPUTE stage, then reads via Load/Load2/Load4
//            and copies to an output RWByteAddressBuffer.
// ===========================================================================

class buffer_view_rwbyteaddress_test : public test
{
    // 28 bytes: matches Store(0) + Store2(4) + Store4(12)
    static constexpr uint32_t BUF_SIZE = 28;

    LRHIDevice           _device    = nullptr;
    LRHIBuffer           _rw_buf    = nullptr;
    LRHIBufferView       _rw_view   = nullptr;
    LRHIBuffer           _output    = nullptr;
    LRHIBufferView       _out_view  = nullptr;
    LRHIShaderModule     _cs        = nullptr;
    LRHIComputePipeline  _pipeline  = nullptr;
    LRHIResidencySet     _rs        = nullptr;

public:
    buffer_view_rwbyteaddress_test()
    {
        type        = test_type::buffer;
        name        = "buffer_view_rwbyteaddress";
        source_path = "tests/golden/buffer_view_rwbyteaddress.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo bi = {};
        bi.size   = BUF_SIZE;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        LRHIError err = {};
        lrhi_create_buffer(device, &bi, &_rw_buf, &err);

        LRHIBufferViewInfo vi = {};
        vi.buffer    = _rw_buf;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_rw_view, &err);

        bi.size   = BUF_SIZE;
        bi.stride = 4;
        bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                      LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(device, &bi, &_output, &err);

        vi.buffer    = _output;
        vi.offset    = 0;
        vi.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_READWRITE_RAW;
        err = {};
        lrhi_create_buffer_view(device, &vi, &_out_view, &err);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/buffer_view_rwbyteaddress.hlsl");
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        _cs = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader          = _cs;
        pi.supports_indirect_commands = 0;
        err = {};
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        err = {};
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_buffer(_rs, _rw_buf, nullptr);
        lrhi_residency_set_add_buffer(_rs, _output, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        std::string err_str;
        LRHICommandQueue q = nullptr; LRHIFence f = nullptr; LRHICommandList cmd = nullptr;
        if (!bv_begin_cmd(_device, &q, &f, &cmd, err_str)) return { false, err_str };

        LRHIError err = {};
        uint32_t rw_idx  = lrhi_buffer_view_get_bindless_index(_rw_view,  &err);
        uint32_t out_idx = lrhi_buffer_view_get_bindless_index(_out_view, &err);

        // Pass A: write pattern via Store/Store2/Store4
        struct C { uint32_t rw_buffer; uint32_t output_buffer; uint32_t pass_index; };
        C c_write = { rw_idx, out_idx, 0 };
        LRHIComputePass cp_a = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_set_pipeline(cp_a, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp_a, &c_write, sizeof(c_write), &err);
        lrhi_compute_pass_dispatch(cp_a, 1, 1, 1, 1, 1, 1, &err);
        lrhi_compute_pass_end(cp_a, &err);

        // Pass B: encoder barrier, then read via Load/Load2/Load4 and copy to output
        C c_read = { rw_idx, out_idx, 1 };
        LRHIComputePass cp_b = lrhi_compute_pass_begin(cmd, &err);
        lrhi_compute_pass_encoder_barrier(cp_b, LUMINARY_RHI_RENDER_STAGE_COMPUTE, &err);
        lrhi_compute_pass_set_pipeline(cp_b, _pipeline, &err);
        lrhi_compute_pass_set_push_constants(cp_b, &c_read, sizeof(c_read), &err);
        lrhi_compute_pass_dispatch(cp_b, 1, 1, 1, 1, 1, 1, &err);
        lrhi_compute_pass_end(cp_b, &err);

        if (!bv_end_cmd(q, f, cmd, _rs, err_str)) return { false, err_str };
        return bv_buffer_result(_device, _output, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_pipeline) { lrhi_destroy_compute_pipeline(_pipeline); _pipeline = nullptr; }
        if (_cs)       { lrhi_destroy_shader_module(_cs);          _cs       = nullptr; }
        if (_out_view) { lrhi_destroy_buffer_view(_out_view);      _out_view = nullptr; }
        if (_rw_view)  { lrhi_destroy_buffer_view(_rw_view);       _rw_view  = nullptr; }
        if (_rs)       { lrhi_destroy_residency_set(_rs);          _rs       = nullptr; }
        if (_output)   { lrhi_destroy_buffer(_output);             _output   = nullptr; }
        if (_rw_buf)   { lrhi_destroy_buffer(_rw_buf);             _rw_buf   = nullptr; }
    }
};
REGISTER_TEST(buffer_view_rwbyteaddress_test);
