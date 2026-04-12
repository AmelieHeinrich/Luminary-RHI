#include "tests/test.h"

#include <cstdio>
#include <cstring>
#include <vector>

class replace_buffer_region_test : public test
{
    // 256 uint32_t values — each word equals its own index.
    static constexpr uint64_t ELEMENT_COUNT = 256;
    static constexpr uint64_t BUFFER_SIZE   = ELEMENT_COUNT * sizeof(uint32_t);

    LRHIDevice _device = nullptr;
    LRHIBuffer _buffer = nullptr;

public:
    replace_buffer_region_test()
    {
        type        = test_type::buffer;
        name        = "replace_buffer_region";
        source_path = "tests/golden/replace_buffer_region.bin";
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
        lrhi_create_buffer(_device, &info, &_buffer, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            fprintf(stderr, "replace_buffer_region_test::init: %s\n", err.message);
    }

    test_result run(bool bake_mode) override
    {
        // Write pattern: word[i] = i
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_buffer, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("map failed: ") + err.message };

            auto* words = reinterpret_cast<uint32_t*>(ptr);
            for (uint32_t i = 0; i < ELEMENT_COUNT; i++)
                words[i] = i;

            lrhi_buffer_unmap(_buffer);
        }

        // Read back
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_buffer(_device, _buffer, readback);

        // Build output / golden paths
        std::string output_buffer = std::string("tests/output/") + name + ".bin";
        std::string golden_buffer = source_path;

        if (bake_mode) {
            test_tools::save_buffer(source_path, readback);
            test_result r;
            r.passed        = true;
            r.message       = "baked";
            r.golden_buffer = golden_buffer;
            return r;
        }

        test_tools::save_buffer(output_buffer.c_str(), readback);

        bool passed = test_tools::validate_buffer(source_path, readback);

        test_result r;
        r.passed        = passed;
        r.message       = passed ? "" : "buffer data mismatch";
        r.output_buffer = output_buffer;
        r.golden_buffer = golden_buffer;
        return r;
    }

    void cleanup() override
    {
        if (_buffer) {
            lrhi_destroy_buffer(_buffer);
            _buffer = nullptr;
        }
    }
};

REGISTER_TEST(replace_buffer_region_test);
