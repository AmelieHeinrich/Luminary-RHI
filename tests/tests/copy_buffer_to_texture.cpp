#include "tests/test.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> cbt_make_gradient(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> px(w * h * 4);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = (y * w + x) * 4;
            px[i+0]    = (uint8_t)((x * 255) / (w > 1 ? w - 1 : 1));
            px[i+1]    = (uint8_t)((y * 255) / (h > 1 ? h - 1 : 1));
            px[i+2]    = 128;
            px[i+3]    = 255;
        }
    }
    return px;
}

// Copy buffer → texture subresource via a copy pass.
static bool cbt_copy_buf_to_tex(LRHIDevice device,
                                 LRHIBuffer src_buffer, uint64_t src_offset, uint32_t src_bpr,
                                 LRHITexture dst_texture, LRHIRegion dst_region,
                                 uint32_t dst_mip, uint32_t dst_layer,
                                 std::string& err_out)
{
    LRHICommandQueue queue = nullptr; lrhi_create_command_queue(device, &queue, nullptr);
    LRHIFence fence = nullptr;       lrhi_create_fence(device, 0, &fence, nullptr);
    LRHICommandList cmd = nullptr;   lrhi_create_command_list(queue, &cmd, nullptr);

    LRHIError err = {};
    lrhi_command_list_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd begin: ") + err.message;
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
        return false;
    }

    LRHICopyPass cp = lrhi_copy_pass_begin(cmd, nullptr);
    err = {};
    lrhi_copy_pass_copy_buffer_to_texture(cp,
        src_buffer, src_offset, src_bpr, 0,
        dst_texture, dst_region, dst_mip, dst_layer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("buf→tex copy: ") + err.message;
        lrhi_copy_pass_end(cp, nullptr);
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
        return false;
    }

    lrhi_copy_pass_end(cp, nullptr);
    lrhi_command_list_end(cmd, nullptr);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    return true;
}

// ---------------------------------------------------------------------------
// copy_buffer_to_texture_dst_mip
// Fills a buffer with a 16×16 gradient and copies it to mip 2 of a 64×64 texture.
// ---------------------------------------------------------------------------

class copy_buffer_to_texture_dst_mip_test : public test
{
    static constexpr uint32_t DST_W      = 64, DST_H = 64;
    static constexpr uint32_t MIP_W      = 16, MIP_H = 16; // mip 2
    static constexpr uint32_t DST_MIPS   = 3;
    static constexpr uint32_t TARGET_MIP = 2;

    LRHIDevice  _device  = nullptr;
    LRHIBuffer  _buffer  = nullptr;
    LRHITexture _texture = nullptr;

public:
    copy_buffer_to_texture_dst_mip_test()
    {
        type        = test_type::texture;
        name        = "copy_buffer_to_texture_dst_mip";
        source_path = "tests/golden/copy_buffer_to_texture_dst_mip.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo buf_info = {};
        buf_info.size   = MIP_W * MIP_H * 4;
        buf_info.stride = 1;
        buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        LRHIError err = {};
        lrhi_create_buffer(_device, &buf_info, &_buffer, &err);

        LRHITextureInfo tex_info = {};
        tex_info.width = DST_W; tex_info.height = DST_H; tex_info.depth = 1;
        tex_info.mip_levels = DST_MIPS; tex_info.array_layers = 1;
        tex_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        err = {};
        lrhi_create_texture(_device, &tex_info, &_texture, &err);
    }

    test_result run(bool bake_mode) override
    {
        // Write 16×16 gradient into buffer
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_buffer, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("map failed: ") + err.message };
            auto pixels = cbt_make_gradient(MIP_W, MIP_H);
            memcpy(ptr, pixels.data(), pixels.size());
            lrhi_buffer_unmap(_buffer);
        }

        std::string err_msg;
        LRHIRegion dst_region = { 0, 0, 0, MIP_W, MIP_H, 1 };
        uint32_t   bpr        = MIP_W * 4;
        if (!cbt_copy_buf_to_tex(_device, _buffer, 0, bpr, _texture, dst_region, TARGET_MIP, 0, err_msg))
            return { false, err_msg };

        LRHITextureInfo tex_info = {};
        lrhi_get_texture_info(_texture, &tex_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _texture, readback, TARGET_MIP, 0);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, tex_info, TARGET_MIP);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, tex_info, TARGET_MIP);

        LRHITextureInfo mip_info = tex_info;
        mip_info.width  = MIP_W;
        mip_info.height = MIP_H;

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
        if (_buffer)  { lrhi_destroy_buffer(_buffer);   _buffer  = nullptr; }
        if (_texture) { lrhi_destroy_texture(_texture); _texture = nullptr; }
    }
};

REGISTER_TEST(copy_buffer_to_texture_dst_mip_test);

// ---------------------------------------------------------------------------
// copy_buffer_to_texture_dst_face
// Fills a buffer with a 64×64 gradient and copies it to face 1 of a cube texture.
// ---------------------------------------------------------------------------

class copy_buffer_to_texture_dst_face_test : public test
{
    static constexpr uint32_t W           = 64, H = 64;
    static constexpr uint32_t TARGET_FACE = 1;

    LRHIDevice  _device  = nullptr;
    LRHIBuffer  _buffer  = nullptr;
    LRHITexture _texture = nullptr;

public:
    copy_buffer_to_texture_dst_face_test()
    {
        type        = test_type::texture;
        name        = "copy_buffer_to_texture_dst_face";
        source_path = "tests/golden/copy_buffer_to_texture_dst_face.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo buf_info = {};
        buf_info.size   = W * H * 4;
        buf_info.stride = 1;
        buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        LRHIError err = {};
        lrhi_create_buffer(_device, &buf_info, &_buffer, &err);

        LRHITextureInfo tex_info = {};
        tex_info.width = W; tex_info.height = H; tex_info.depth = 1;
        tex_info.mip_levels = 1;
        tex_info.array_layers = 1;
        tex_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
        err = {};
        lrhi_create_texture(_device, &tex_info, &_texture, &err);
    }

    test_result run(bool bake_mode) override
    {
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_buffer, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("map failed: ") + err.message };
            auto pixels = cbt_make_gradient(W, H);
            memcpy(ptr, pixels.data(), pixels.size());
            lrhi_buffer_unmap(_buffer);
        }

        std::string err_msg;
        LRHIRegion dst_region = { 0, 0, 0, W, H, 1 };
        uint32_t   bpr        = W * 4;
        if (!cbt_copy_buf_to_tex(_device, _buffer, 0, bpr, _texture, dst_region, 0, TARGET_FACE, err_msg))
            return { false, err_msg };

        LRHITextureInfo tex_info = {};
        lrhi_get_texture_info(_texture, &tex_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _texture, readback, 0, TARGET_FACE);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, tex_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, tex_info);

        LRHITextureInfo face_info = tex_info;
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
        if (_buffer)  { lrhi_destroy_buffer(_buffer);   _buffer  = nullptr; }
        if (_texture) { lrhi_destroy_texture(_texture); _texture = nullptr; }
    }
};

REGISTER_TEST(copy_buffer_to_texture_dst_face_test);

// ---------------------------------------------------------------------------
// copy_buffer_to_texture_dst_layer
// Fills a buffer with a 64×64 gradient and copies it to layer 2 of a 2D array texture.
// ---------------------------------------------------------------------------

class copy_buffer_to_texture_dst_layer_test : public test
{
    static constexpr uint32_t W            = 64, H = 64;
    static constexpr uint32_t TARGET_LAYER = 2;

    LRHIDevice  _device  = nullptr;
    LRHIBuffer  _buffer  = nullptr;
    LRHITexture _texture = nullptr;

public:
    copy_buffer_to_texture_dst_layer_test()
    {
        type        = test_type::texture;
        name        = "copy_buffer_to_texture_dst_layer";
        source_path = "tests/golden/copy_buffer_to_texture_dst_layer.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIBufferInfo buf_info = {};
        buf_info.size   = W * H * 4;
        buf_info.stride = 1;
        buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        LRHIError err = {};
        lrhi_create_buffer(_device, &buf_info, &_buffer, &err);

        LRHITextureInfo tex_info = {};
        tex_info.width = W; tex_info.height = H; tex_info.depth = 1;
        tex_info.mip_levels = 1; tex_info.array_layers = 3;
        tex_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        err = {};
        lrhi_create_texture(_device, &tex_info, &_texture, &err);
    }

    test_result run(bool bake_mode) override
    {
        {
            LRHIError err = {};
            void* ptr = lrhi_buffer_map(_buffer, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
                return { false, std::string("map failed: ") + err.message };
            auto pixels = cbt_make_gradient(W, H);
            memcpy(ptr, pixels.data(), pixels.size());
            lrhi_buffer_unmap(_buffer);
        }

        std::string err_msg;
        LRHIRegion dst_region = { 0, 0, 0, W, H, 1 };
        uint32_t   bpr        = W * 4;
        if (!cbt_copy_buf_to_tex(_device, _buffer, 0, bpr, _texture, dst_region, 0, TARGET_LAYER, err_msg))
            return { false, err_msg };

        LRHITextureInfo tex_info = {};
        lrhi_get_texture_info(_texture, &tex_info);
        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _texture, readback, 0, TARGET_LAYER);

        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, tex_info);
            test_result r; r.passed = true; r.message = "baked"; r.golden_image = source_path;
            return r;
        }

        test_tools::save_texture(output_image.c_str(), readback, tex_info);

        LRHITextureInfo layer_info = tex_info;
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
        if (_buffer)  { lrhi_destroy_buffer(_buffer);   _buffer  = nullptr; }
        if (_texture) { lrhi_destroy_texture(_texture); _texture = nullptr; }
    }
};

REGISTER_TEST(copy_buffer_to_texture_dst_layer_test);
