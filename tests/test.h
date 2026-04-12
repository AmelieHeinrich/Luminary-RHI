#pragma once

#include "luminary_rhi.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class test_type
{
    validation,
    texture,
    buffer
};

struct test_result
{
    bool passed = false;
    std::string message;
    float flip_mean_error = 0.0f;
    // Paths written to results.json for the HTML viewer
    std::string output_image;
    std::string golden_image;
    std::string flip_image;
    std::string output_buffer;
    std::string golden_buffer;
};

struct test
{
    test_type type;
    const char* name;
    const char* source_path; // path to golden file (texture/buffer), or nullptr for validation

    virtual ~test() = default;
    virtual void init(LRHIDevice device) = 0;
    virtual test_result run(bool bake_mode) = 0;
    virtual void cleanup() = 0;
};

// ---------------------------------------------------------------------------
// Test registry — each test .cpp file self-registers via REGISTER_TEST().
// main.cpp calls test_registry::create_all() to get every registered test.
// ---------------------------------------------------------------------------

struct test_registry
{
    using factory_fn = std::function<std::unique_ptr<test>()>;

    static std::vector<factory_fn>& factories()
    {
        static std::vector<factory_fn> s_factories;
        return s_factories;
    }

    static void register_test(factory_fn fn)
    {
        factories().push_back(std::move(fn));
    }

    static std::vector<std::unique_ptr<test>> create_all()
    {
        std::vector<std::unique_ptr<test>> out;
        for (auto& fn : factories())
            out.push_back(fn());
        return out;
    }
};

// Place REGISTER_TEST(ClassName) at file scope in each test's .cpp file.
#define REGISTER_TEST(ClassName)                                            \
    static bool _##ClassName##_registered = []() {                         \
        test_registry::register_test(                                       \
            []() { return std::unique_ptr<test>(new ClassName()); });       \
        return true;                                                        \
    }()

// ---------------------------------------------------------------------------

struct test_tools
{
    // Read back the full mip 0 / layer 0 of a GPU texture into a byte vector.
    static void rhi_readback_texture(LRHIDevice device, LRHITexture texture, std::vector<uint8_t>& out_data);

    // Stub: buffer API not yet implemented.
    static void rhi_readback_buffer(LRHIDevice device, LRHIBuffer buffer, std::vector<uint8_t>& out_data);

    // Compare texture data against a reference PNG using NVIDIA FLIP.
    // out_mean_error receives the mean FLIP error value (0..1).
    // If flip_output_path is non-null, also saves the Magma-colorized error map as a PNG.
    // Returns true if mean error < 0.05.
    static bool validate_texture(const char* reference_path, const std::vector<uint8_t>& data,
                                 const LRHITextureInfo& info, bool hdr, float& out_mean_error,
                                 const char* flip_output_path = nullptr);

    // Binary comparison of buffer data against a reference file.
    static bool validate_buffer(const char* reference_path, const std::vector<uint8_t>& data);

    // Save RGBA8 texture data as PNG.
    static void save_texture(const char* output_path, const std::vector<uint8_t>& data, const LRHITextureInfo& info);

    // Save raw buffer bytes to disk.
    static void save_buffer(const char* output_path, const std::vector<uint8_t>& data);
};
