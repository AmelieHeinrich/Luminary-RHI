#include "tests/test.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static uint32_t tex_bpp(LRHITextureFormat fmt)
{
    switch (fmt) {
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM:
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB:
        case LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM:
            return 4;
        default:
            return 4;
    }
}

// Build a simple RGBA8 gradient: R ramps left→right, G top→bottom, B=128, A=255.
static std::vector<uint8_t> make_gradient(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> px(w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i  = (y * w + x) * 4;
            px[i+0]     = (uint8_t)((x * 255) / (w > 1 ? w - 1 : 1));
            px[i+1]     = (uint8_t)((y * 255) / (h > 1 ? h - 1 : 1));
            px[i+2]     = 128;
            px[i+3]     = 255;
        }
    }
    return px;
}

// Upload pixel data into a texture (specific mip/layer) via staging buffer + copy pass.
static bool upload_texture(LRHIDevice device, LRHITexture texture,
                           const std::vector<uint8_t>& pixels,
                           uint32_t w, uint32_t h,
                           uint32_t mip_level, uint32_t array_layer,
                           std::string& err_out)
{
    LRHIBufferInfo buf_info = {};
    buf_info.size   = pixels.size();
    buf_info.stride = 1;
    buf_info.usage  = LUMINARY_RHI_BUFFER_USAGE_SHADER_READ;

    LRHIError err = {};
    LRHIBuffer staging = nullptr;
    lrhi_create_buffer(device, &buf_info, &staging, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("staging create: ") + err.message;
        return false;
    }

    err = {};
    void* ptr = lrhi_buffer_map(staging, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("staging map: ") + err.message;
        lrhi_destroy_buffer(staging);
        return false;
    }
    memcpy(ptr, pixels.data(), pixels.size());
    lrhi_buffer_unmap(staging);

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &err);
    LRHIFence fence = nullptr;
    err = {};
    lrhi_create_fence(device, 0, &fence, &err);
    LRHICommandList cmd = nullptr;
    err = {};
    lrhi_create_command_list(queue, &cmd, &err);

    err = {};
    lrhi_command_list_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd begin: ") + err.message;
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_buffer(staging);
        return false;
    }

    err = {};
    LRHICopyPass cp = lrhi_copy_pass_begin(cmd, &err);

    LRHIRegion dst_region = { 0, 0, 0, w, h, 1 };
    uint32_t   bpr        = w * tex_bpp(LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM);
    err = {};
    lrhi_copy_pass_copy_buffer_to_texture(cp, staging, 0, bpr, 0,
                                          texture, dst_region, mip_level, array_layer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("buf→tex copy: ") + err.message;
        lrhi_copy_pass_end(cp, nullptr);
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_buffer(staging);
        return false;
    }

    lrhi_copy_pass_end(cp, nullptr);
    lrhi_command_list_end(cmd, nullptr);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_buffer(staging);
    return true;
}

// Run a texture-to-texture copy pass.
static bool copy_tex_to_tex(LRHIDevice device,
                             LRHITexture src, LRHIRegion src_region, uint32_t src_mip, uint32_t src_layer,
                             LRHITexture dst, LRHIRegion dst_region, uint32_t dst_mip, uint32_t dst_layer,
                             std::string& err_out)
{
    LRHIError err = {};
    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &err);

    err = {};
    LRHIFence fence = nullptr;
    lrhi_create_fence(device, 0, &fence, &err);

    err = {};
    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &err);

    err = {};
    lrhi_command_list_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd begin: ") + err.message;
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
        return false;
    }

    err = {};
    LRHICopyPass cp = lrhi_copy_pass_begin(cmd, &err);

    err = {};
    lrhi_copy_pass_copy_texture_to_texture(cp,
        src, src_region, src_mip, src_layer,
        dst, dst_region, dst_mip, dst_layer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("tex→tex copy: ") + err.message;
        lrhi_copy_pass_end(cp, nullptr);
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
        return false;
    }

    lrhi_copy_pass_end(cp, nullptr);
    lrhi_command_list_end(cmd, nullptr);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr); lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    return true;
}

// ---------------------------------------------------------------------------
// copy_texture_to_texture_full
// ---------------------------------------------------------------------------

class copy_texture_to_texture_full_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_full_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_full";
        source_path = "tests/golden/copy_texture_to_texture_full.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage        = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_src, &err);
        err = {};
        lrhi_create_texture(_device, &info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(W, H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, W, H, 0, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, W, H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, 0, _dst, region, 0, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo info = {};
        lrhi_get_texture_info(_dst, &info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, info);
        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_full_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_region
// Copies a 32×32 sub-region from src to dst; rest of dst stays black.
// ---------------------------------------------------------------------------

class copy_texture_to_texture_region_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_region_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_region";
        source_path = "tests/golden/copy_texture_to_texture_region.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width = W; info.height = H; info.depth = 1;
        info.mip_levels = 1; info.array_layers = 1;
        info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_src, &err);
        err = {};
        lrhi_create_texture(_device, &info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(W, H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, W, H, 0, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 16, 16, 0, 32, 32, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, 0, _dst, region, 0, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo info = {};
        lrhi_get_texture_info(_dst, &info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, info);
        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_region_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_dst_mip
// Copies a 16×16 src → mip 2 of a 64×64 dst (mip 2 = 16×16).
// ---------------------------------------------------------------------------

class copy_texture_to_texture_dst_mip_test : public test
{
    static constexpr uint32_t SRC_W = 16, SRC_H = 16;
    static constexpr uint32_t DST_W = 64, DST_H = 64;
    static constexpr uint32_t DST_MIPS = 3; // mip 2 → 16×16
    static constexpr uint32_t TARGET_MIP = 2;

    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_dst_mip_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_dst_mip";
        source_path = "tests/golden/copy_texture_to_texture_dst_mip.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo src_info = {};
        src_info.width = SRC_W; src_info.height = SRC_H; src_info.depth = 1;
        src_info.mip_levels = 1; src_info.array_layers = 1;
        src_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        src_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        src_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &src_info, &_src, &err);

        LRHITextureInfo dst_info = src_info;
        dst_info.width = DST_W; dst_info.height = DST_H;
        dst_info.mip_levels = DST_MIPS;
        err = {};
        lrhi_create_texture(_device, &dst_info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(SRC_W, SRC_H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, SRC_W, SRC_H, 0, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, SRC_W, SRC_H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, 0, _dst, region, TARGET_MIP, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo dst_info = {};
        lrhi_get_texture_info(_dst, &dst_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback, TARGET_MIP, 0);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, dst_info, TARGET_MIP);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, dst_info, TARGET_MIP);

        // Build a synthetic info with the mip-adjusted size for validate_texture.
        LRHITextureInfo mip_info = dst_info;
        mip_info.width  = SRC_W;
        mip_info.height = SRC_H;

        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, mip_info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_dst_mip_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_dst_face
// Copies a 64×64 src → face 3 of a cube dst.
// ---------------------------------------------------------------------------

class copy_texture_to_texture_dst_face_test : public test
{
    static constexpr uint32_t W           = 64, H = 64;
    static constexpr uint32_t TARGET_FACE = 3;

    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_dst_face_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_dst_face";
        source_path = "tests/golden/copy_texture_to_texture_dst_face.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo src_info = {};
        src_info.width = W; src_info.height = H; src_info.depth = 1;
        src_info.mip_levels = 1; src_info.array_layers = 1;
        src_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        src_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        src_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &src_info, &_src, &err);

        LRHITextureInfo dst_info = src_info;
        dst_info.array_layers = 1;
        dst_info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
        err = {};
        lrhi_create_texture(_device, &dst_info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(W, H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, W, H, 0, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, W, H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, 0, _dst, region, 0, TARGET_FACE, err_msg))
            return { false, err_msg };

        LRHITextureInfo dst_info = {};
        lrhi_get_texture_info(_dst, &dst_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback, 0, TARGET_FACE);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, dst_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, dst_info);

        LRHITextureInfo face_info = dst_info;
        face_info.array_layers = 1;

        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, face_info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_dst_face_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_dst_layer
// Copies a 64×64 src → layer 2 of a 2D array dst with 4 layers.
// ---------------------------------------------------------------------------

class copy_texture_to_texture_dst_layer_test : public test
{
    static constexpr uint32_t W            = 64, H = 64;
    static constexpr uint32_t TARGET_LAYER = 2;

    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_dst_layer_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_dst_layer";
        source_path = "tests/golden/copy_texture_to_texture_dst_layer.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo src_info = {};
        src_info.width = W; src_info.height = H; src_info.depth = 1;
        src_info.mip_levels = 1; src_info.array_layers = 1;
        src_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        src_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        src_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &src_info, &_src, &err);

        LRHITextureInfo dst_info = src_info;
        dst_info.array_layers = 4;
        dst_info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        err = {};
        lrhi_create_texture(_device, &dst_info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(W, H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, W, H, 0, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, W, H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, 0, _dst, region, 0, TARGET_LAYER, err_msg))
            return { false, err_msg };

        LRHITextureInfo dst_info = {};
        lrhi_get_texture_info(_dst, &dst_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback, 0, TARGET_LAYER);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, dst_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, dst_info);

        LRHITextureInfo layer_info = dst_info;
        layer_info.array_layers = 1;

        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, layer_info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_dst_layer_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_src_mip
// Copies from mip 2 (16×16) of a 64×64 src → a 16×16 dst.
// ---------------------------------------------------------------------------

class copy_texture_to_texture_src_mip_test : public test
{
    static constexpr uint32_t SRC_W      = 64, SRC_H = 64;
    static constexpr uint32_t SRC_MIPS   = 3;
    static constexpr uint32_t SRC_MIP    = 2; // → 16×16
    static constexpr uint32_t DST_W      = 16, DST_H = 16;

    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_src_mip_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_src_mip";
        source_path = "tests/golden/copy_texture_to_texture_src_mip.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo src_info = {};
        src_info.width = SRC_W; src_info.height = SRC_H; src_info.depth = 1;
        src_info.mip_levels = SRC_MIPS; src_info.array_layers = 1;
        src_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        src_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        src_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &src_info, &_src, &err);

        LRHITextureInfo dst_info = src_info;
        dst_info.width = DST_W; dst_info.height = DST_H;
        dst_info.mip_levels = 1;
        err = {};
        lrhi_create_texture(_device, &dst_info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        // Upload 16×16 gradient into src mip 2
        auto pixels = make_gradient(DST_W, DST_H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, DST_W, DST_H, SRC_MIP, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, DST_W, DST_H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, SRC_MIP, 0, _dst, region, 0, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo dst_info = {};
        lrhi_get_texture_info(_dst, &dst_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback, 0, 0);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, dst_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, dst_info);
        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, dst_info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_src_mip_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_src_face
// Copies face 2 of a cube src → a regular 2D dst.
// ---------------------------------------------------------------------------

class copy_texture_to_texture_src_face_test : public test
{
    static constexpr uint32_t W           = 64, H = 64;
    static constexpr uint32_t SOURCE_FACE = 2;

    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_src_face_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_src_face";
        source_path = "tests/golden/copy_texture_to_texture_src_face.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo src_info = {};
        src_info.width = W; src_info.height = H; src_info.depth = 1;
        src_info.mip_levels = 1; src_info.array_layers = 1;
        src_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        src_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        src_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
        LRHIError err = {};
        lrhi_create_texture(_device, &src_info, &_src, &err);

        LRHITextureInfo dst_info = src_info;
        dst_info.array_layers = 1;
        dst_info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        err = {};
        lrhi_create_texture(_device, &dst_info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(W, H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, W, H, 0, SOURCE_FACE, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, W, H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, SOURCE_FACE, _dst, region, 0, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo dst_info = {};
        lrhi_get_texture_info(_dst, &dst_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, dst_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, dst_info);
        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, dst_info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_src_face_test);

// ---------------------------------------------------------------------------
// copy_texture_to_texture_src_layer
// Copies layer 1 of a 2D array src → a regular 2D dst.
// ---------------------------------------------------------------------------

class copy_texture_to_texture_src_layer_test : public test
{
    static constexpr uint32_t W            = 64, H = 64;
    static constexpr uint32_t SOURCE_LAYER = 1;

    LRHIDevice  _device = nullptr;
    LRHITexture _src    = nullptr;
    LRHITexture _dst    = nullptr;

public:
    copy_texture_to_texture_src_layer_test()
    {
        type        = test_type::texture;
        name        = "copy_texture_to_texture_src_layer";
        source_path = "tests/golden/copy_texture_to_texture_src_layer.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo src_info = {};
        src_info.width = W; src_info.height = H; src_info.depth = 1;
        src_info.mip_levels = 1; src_info.array_layers = 4;
        src_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        src_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        src_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        LRHIError err = {};
        lrhi_create_texture(_device, &src_info, &_src, &err);

        LRHITextureInfo dst_info = src_info;
        dst_info.array_layers = 1;
        dst_info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        err = {};
        lrhi_create_texture(_device, &dst_info, &_dst, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = make_gradient(W, H);
        std::string err_msg;
        if (!upload_texture(_device, _src, pixels, W, H, 0, SOURCE_LAYER, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion region = { 0, 0, 0, W, H, 1 };
        if (!copy_tex_to_tex(_device, _src, region, 0, SOURCE_LAYER, _dst, region, 0, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo dst_info = {};
        lrhi_get_texture_info(_dst, &dst_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _dst, readback);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, dst_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, dst_info);
        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, dst_info, false, mean_error, flip_image.c_str());

        test_result r;
        r.passed = passed; r.message = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image = output_image; r.golden_image = source_path; r.flip_image = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_src) { lrhi_destroy_texture(_src); _src = nullptr; }
        if (_dst) { lrhi_destroy_texture(_dst); _dst = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_texture_src_layer_test);
