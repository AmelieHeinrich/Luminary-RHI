#include "tests/test.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: submit a one-shot copy pass and wait for it to complete.
// ---------------------------------------------------------------------------

struct copy_cmd_ctx
{
    LRHICommandQueue queue        = nullptr;
    LRHIFence        fence        = nullptr;
    LRHICommandList  command_list = nullptr;
    LRHICopyPass     copy_pass    = nullptr;
    bool             ok           = false;
};

static copy_cmd_ctx begin_copy(LRHIDevice device, std::string& err_out)
{
    copy_cmd_ctx ctx;
    LRHIError err = {};

    lrhi_create_command_queue(device, &ctx.queue, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return ctx; }

    err = {};
    lrhi_create_fence(device, 0, &ctx.fence, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return ctx; }

    err = {};
    lrhi_create_command_list(ctx.queue, &ctx.command_list, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return ctx; }

    err = {};
    lrhi_command_list_begin(ctx.command_list, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return ctx; }

    err = {};
    ctx.copy_pass = lrhi_copy_pass_begin(ctx.command_list, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return ctx; }

    ctx.ok = true;
    return ctx;
}

static bool end_copy(copy_cmd_ctx& ctx, std::string& err_out)
{
    LRHIError err = {};
    lrhi_copy_pass_end(ctx.copy_pass, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return false; }

    err = {};
    lrhi_command_list_end(ctx.command_list, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return false; }

    err = {};
    lrhi_command_queue_submit(ctx.queue, &ctx.command_list, 1, ctx.fence, 1, nullptr, 0, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return false; }

    err = {};
    lrhi_fence_wait(ctx.fence, 1, 5000000000ULL, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) { err_out = err.message; return false; }

    lrhi_destroy_command_list(ctx.command_list);
    lrhi_destroy_fence(ctx.fence);
    lrhi_destroy_command_queue(ctx.queue);
    ctx = {};
    return true;
}

// ---------------------------------------------------------------------------
// copy_buffer_to_buffer_full
// ---------------------------------------------------------------------------

class copy_buffer_to_buffer_full_test : public test
{
    static constexpr uint64_t ELEMENT_COUNT = 256;
    static constexpr uint64_t BUFFER_SIZE   = ELEMENT_COUNT * sizeof(uint32_t);

    LRHIDevice _device = nullptr;
    LRHIBuffer _src    = nullptr;
    LRHIBuffer _dst    = nullptr;

public:
    copy_buffer_to_buffer_full_test()
    {
        type        = test_type::buffer;
        name        = "copy_buffer_to_buffer_full";
        source_path = "tests/golden/copy_buffer_to_buffer_full.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo info = {};
        info.size   = BUFFER_SIZE;
        info.stride = sizeof(uint32_t);
        info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                        LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);

        LRHIError err = {};
        lrhi_create_buffer(_device, &info, &_src, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            fprintf(stderr, "%s::init (src): %s\n", name, err.message);

        err = {};
        lrhi_create_buffer(_device, &info, &_dst, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            fprintf(stderr, "%s::init (dst): %s\n", name, err.message);
    }

    test_result run(bool bake_mode) override
    {
        // Fill source: word[i] = i
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_src, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("src map failed: ") + err.message };
            auto* words = reinterpret_cast<uint32_t*>(ptr);
            for (uint32_t i = 0; i < ELEMENT_COUNT; i++) words[i] = i;
            lrhi_buffer_unmap(_src);
        }

        // Copy full src → dst
        std::string err_msg;
        auto ctx = begin_copy(_device, err_msg);
        if (!ctx.ok) return { false, "begin_copy: " + err_msg };

        LRHIError err = {};
        lrhi_copy_pass_copy_buffer_to_buffer(ctx.copy_pass, _src, 0, _dst, 0, BUFFER_SIZE, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return { false, std::string("copy_buffer_to_buffer failed: ") + err.message };

        if (!end_copy(ctx, err_msg)) return { false, "end_copy: " + err_msg };

        // Readback dst
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_buffer(_device, _dst, readback);

        std::string output_buffer = std::string("tests/output/") + name + ".bin";

        if (bake_mode) {
            test_tools::save_buffer(source_path, readback);
            test_result r; r.passed = true; r.message = "baked"; r.golden_buffer = source_path;
            return r;
        }

        test_tools::save_buffer(output_buffer.c_str(), readback);
        bool passed = test_tools::validate_buffer(source_path, readback);

        test_result r;
        r.passed        = passed;
        r.message       = passed ? "" : "buffer data mismatch";
        r.output_buffer = output_buffer;
        r.golden_buffer = source_path;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_buffer(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_buffer(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_buffer_to_buffer_full_test);

// ---------------------------------------------------------------------------
// copy_buffer_to_buffer_offset
// Copies words 128–255 (bytes 512–1023) from src to the first 512 bytes of dst.
// ---------------------------------------------------------------------------

class copy_buffer_to_buffer_offset_test : public test
{
    static constexpr uint64_t SRC_ELEMENTS = 256;
    static constexpr uint64_t SRC_SIZE     = SRC_ELEMENTS * sizeof(uint32_t); // 1024 bytes
    static constexpr uint64_t DST_SIZE     = 512;                              // 128 words

    LRHIDevice _device = nullptr;
    LRHIBuffer _src    = nullptr;
    LRHIBuffer _dst    = nullptr;

public:
    copy_buffer_to_buffer_offset_test()
    {
        type        = test_type::buffer;
        name        = "copy_buffer_to_buffer_offset";
        source_path = "tests/golden/copy_buffer_to_buffer_offset.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo src_info = {};
        src_info.size   = SRC_SIZE;
        src_info.stride = sizeof(uint32_t);
        src_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);

        LRHIError err = {};
        lrhi_create_buffer(_device, &src_info, &_src, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            fprintf(stderr, "%s::init (src): %s\n", name, err.message);

        LRHIBufferInfo dst_info = src_info;
        dst_info.size = DST_SIZE;
        err = {};
        lrhi_create_buffer(_device, &dst_info, &_dst, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            fprintf(stderr, "%s::init (dst): %s\n", name, err.message);
    }

    test_result run(bool bake_mode) override
    {
        // Fill source: word[i] = i
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_src, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("src map failed: ") + err.message };
            auto* words = reinterpret_cast<uint32_t*>(ptr);
            for (uint32_t i = 0; i < SRC_ELEMENTS; i++) words[i] = i;
            lrhi_buffer_unmap(_src);
        }

        // Copy bytes [512, 1024) of src → [0, 512) of dst  (words 128–255)
        std::string err_msg;
        auto ctx = begin_copy(_device, err_msg);
        if (!ctx.ok) return { false, "begin_copy: " + err_msg };

        LRHIError err = {};
        lrhi_copy_pass_copy_buffer_to_buffer(ctx.copy_pass, _src, 512, _dst, 0, DST_SIZE, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return { false, std::string("copy_buffer_to_buffer failed: ") + err.message };

        if (!end_copy(ctx, err_msg)) return { false, "end_copy: " + err_msg };

        // Readback dst
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_buffer(_device, _dst, readback);

        std::string output_buffer = std::string("tests/output/") + name + ".bin";

        if (bake_mode) {
            test_tools::save_buffer(source_path, readback);
            test_result r; r.passed = true; r.message = "baked"; r.golden_buffer = source_path;
            return r;
        }

        test_tools::save_buffer(output_buffer.c_str(), readback);
        bool passed = test_tools::validate_buffer(source_path, readback);

        test_result r;
        r.passed        = passed;
        r.message       = passed ? "" : "buffer data mismatch";
        r.output_buffer = output_buffer;
        r.golden_buffer = source_path;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_buffer(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_buffer(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_buffer_to_buffer_offset_test);
