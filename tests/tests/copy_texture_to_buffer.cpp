#include "tests/test.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Shared helpers (duplicated locally to keep files self-contained)
// ---------------------------------------------------------------------------

static uint32_t ctb_bpp(LRHITextureFormat /*fmt*/) { return 4; }

static std::vector<uint8_t> ctb_make_gradient(uint32_t w, uint32_t h)
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

// Upload pixel data → texture subresource via staging buffer + copy pass.
static bool ctb_upload_texture(LRHIDevice device, LRHITexture texture,
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

    LRHICommandQueue queue = nullptr; lrhi_create_command_queue(device, &queue, nullptr);
    LRHIFence fence = nullptr;       lrhi_create_fence(device, 0, &fence, nullptr);
    LRHICommandList cmd = nullptr;   lrhi_create_command_list(queue, &cmd, nullptr);

    lrhi_command_list_begin(cmd, nullptr);
    LRHICopyPass cp = lrhi_copy_pass_begin(cmd, nullptr);
    LRHIRegion dst_region = { 0, 0, 0, w, h, 1 };
    uint32_t   bpr        = w * ctb_bpp(LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM);
    err = {};
    lrhi_copy_pass_copy_buffer_to_texture(cp, staging, 0, bpr, 0,
                                          texture, dst_region, mip_level, array_layer, &err);
    lrhi_copy_pass_end(cp, nullptr);
    lrhi_command_list_end(cmd, nullptr);
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_buffer(staging);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("buf→tex copy: ") + err.message;
        return false;
    }
    return true;
}

// Run a texture-to-buffer copy pass.
static bool ctb_copy_tex_to_buf(LRHIDevice device,
                                 LRHITexture src_texture, LRHIRegion src_region,
                                 uint32_t src_mip, uint32_t src_layer,
                                 LRHIBuffer dst_buffer, uint64_t dst_offset,
                                 uint32_t dst_bpr,
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
    lrhi_copy_pass_copy_texture_to_buffer(cp,
        src_texture, src_region, src_mip, src_layer,
        dst_buffer, dst_offset, dst_bpr, 0, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("tex→buf copy: ") + err.message;
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
// copy_texture_to_buffer_src_mip
// Uploads 16×16 gradient to mip 2 of a 64×64 texture, copies it to a buffer.
// ---------------------------------------------------------------------------

class copy_texture_to_buffer_src_mip_test : public test
{
    static constexpr uint32_t SRC_W   = 64, SRC_H = 64;
    static constexpr uint32_t MIP_W   = 16, MIP_H = 16; // mip 2
    static constexpr uint32_t SRC_MIP = 2;

    LRHIDevice  _device  = nullptr;
    LRHITexture _texture = nullptr;
    LRHIBuffer  _buffer  = nullptr;

public:
    copy_texture_to_buffer_src_mip_test()
    {
        type        = test_type::buffer;
        name        = "copy_texture_to_buffer_src_mip";
        source_path = "tests/golden/copy_texture_to_buffer_src_mip.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo tex_info = {};
        tex_info.width = SRC_W; tex_info.height = SRC_H; tex_info.depth = 1;
        tex_info.mip_levels = 3; tex_info.array_layers = 1;
        tex_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &tex_info, &_texture, &err);

        LRHIBufferInfo buf_info = {};
        buf_info.size   = MIP_W * MIP_H * 4;
        buf_info.stride = 1;
        buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(_device, &buf_info, &_buffer, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = ctb_make_gradient(MIP_W, MIP_H);
        std::string err_msg;
        if (!ctb_upload_texture(_device, _texture, pixels, MIP_W, MIP_H, SRC_MIP, 0, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion src_region = { 0, 0, 0, MIP_W, MIP_H, 1 };
        uint32_t   bpr        = MIP_W * 4;
        if (!ctb_copy_tex_to_buf(_device, _texture, src_region, SRC_MIP, 0, _buffer, 0, bpr, err_msg))
            return { false, err_msg };

        std::vector<uint8_t> readback;
        test_tools::rhi_readback_buffer(_device, _buffer, readback);

        std::string output_buffer = std::string("tests/output/") + name + ".bin";

        if (bake_mode) {
            test_tools::save_buffer(source_path, readback);
            test_result r; r.passed = true; r.message = "baked"; r.golden_buffer = source_path;
            return r;
        }

        test_tools::save_buffer(output_buffer.c_str(), readback);
        bool passed = test_tools::validate_buffer(source_path, readback);

        test_result r;
        r.passed = passed; r.message = passed ? "" : "buffer data mismatch";
        r.output_buffer = output_buffer; r.golden_buffer = source_path;
        return r;
    }

    void cleanup() override
    {
        if (_texture) { lrhi_destroy_texture(_texture); _texture = nullptr; }
        if (_buffer)  { lrhi_destroy_buffer(_buffer);   _buffer  = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_buffer_src_mip_test);

// ---------------------------------------------------------------------------
// copy_texture_to_buffer_src_face
// Uploads gradient to face 4 of a cube texture, copies that face to a buffer.
// ---------------------------------------------------------------------------

class copy_texture_to_buffer_src_face_test : public test
{
    static constexpr uint32_t W           = 64, H = 64;
    static constexpr uint32_t SOURCE_FACE = 4;

    LRHIDevice  _device  = nullptr;
    LRHITexture _texture = nullptr;
    LRHIBuffer  _buffer  = nullptr;

public:
    copy_texture_to_buffer_src_face_test()
    {
        type        = test_type::buffer;
        name        = "copy_texture_to_buffer_src_face";
        source_path = "tests/golden/copy_texture_to_buffer_src_face.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo tex_info = {};
        tex_info.width = W; tex_info.height = H; tex_info.depth = 1;
        tex_info.mip_levels = 1;
        tex_info.array_layers = 1;
        tex_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
        LRHIError err = {};
        lrhi_create_texture(_device, &tex_info, &_texture, &err);

        LRHIBufferInfo buf_info = {};
        buf_info.size   = W * H * 4;
        buf_info.stride = 1;
        buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(_device, &buf_info, &_buffer, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = ctb_make_gradient(W, H);
        std::string err_msg;
        if (!ctb_upload_texture(_device, _texture, pixels, W, H, 0, SOURCE_FACE, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion src_region = { 0, 0, 0, W, H, 1 };
        uint32_t   bpr        = W * 4;
        if (!ctb_copy_tex_to_buf(_device, _texture, src_region, 0, SOURCE_FACE, _buffer, 0, bpr, err_msg))
            return { false, err_msg };

        std::vector<uint8_t> readback;
        test_tools::rhi_readback_buffer(_device, _buffer, readback);

        std::string output_buffer = std::string("tests/output/") + name + ".bin";

        if (bake_mode) {
            test_tools::save_buffer(source_path, readback);
            test_result r; r.passed = true; r.message = "baked"; r.golden_buffer = source_path;
            return r;
        }

        test_tools::save_buffer(output_buffer.c_str(), readback);
        bool passed = test_tools::validate_buffer(source_path, readback);

        test_result r;
        r.passed = passed; r.message = passed ? "" : "buffer data mismatch";
        r.output_buffer = output_buffer; r.golden_buffer = source_path;
        return r;
    }

    void cleanup() override
    {
        if (_texture) { lrhi_destroy_texture(_texture); _texture = nullptr; }
        if (_buffer)  { lrhi_destroy_buffer(_buffer);   _buffer  = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_buffer_src_face_test);

// ---------------------------------------------------------------------------
// copy_texture_to_buffer_src_layer
// Uploads gradient to layer 3 of a 2D array texture, copies that layer to a buffer.
// ---------------------------------------------------------------------------

class copy_texture_to_buffer_src_layer_test : public test
{
    static constexpr uint32_t W            = 64, H = 64;
    static constexpr uint32_t SOURCE_LAYER = 3;

    LRHIDevice  _device  = nullptr;
    LRHITexture _texture = nullptr;
    LRHIBuffer  _buffer  = nullptr;

public:
    copy_texture_to_buffer_src_layer_test()
    {
        type        = test_type::buffer;
        name        = "copy_texture_to_buffer_src_layer";
        source_path = "tests/golden/copy_texture_to_buffer_src_layer.bin";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo tex_info = {};
        tex_info.width = W; tex_info.height = H; tex_info.depth = 1;
        tex_info.mip_levels = 1; tex_info.array_layers = 4;
        tex_info.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        tex_info.usage      = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        tex_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        LRHIError err = {};
        lrhi_create_texture(_device, &tex_info, &_texture, &err);

        LRHIBufferInfo buf_info = {};
        buf_info.size   = W * H * 4;
        buf_info.stride = 1;
        buf_info.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ |
                                            LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
        err = {};
        lrhi_create_buffer(_device, &buf_info, &_buffer, &err);
    }

    test_result run(bool bake_mode) override
    {
        auto pixels = ctb_make_gradient(W, H);
        std::string err_msg;
        if (!ctb_upload_texture(_device, _texture, pixels, W, H, 0, SOURCE_LAYER, err_msg))
            return { false, "upload: " + err_msg };

        LRHIRegion src_region = { 0, 0, 0, W, H, 1 };
        uint32_t   bpr        = W * 4;
        if (!ctb_copy_tex_to_buf(_device, _texture, src_region, 0, SOURCE_LAYER, _buffer, 0, bpr, err_msg))
            return { false, err_msg };

        std::vector<uint8_t> readback;
        test_tools::rhi_readback_buffer(_device, _buffer, readback);

        std::string output_buffer = std::string("tests/output/") + name + ".bin";

        if (bake_mode) {
            test_tools::save_buffer(source_path, readback);
            test_result r; r.passed = true; r.message = "baked"; r.golden_buffer = source_path;
            return r;
        }

        test_tools::save_buffer(output_buffer.c_str(), readback);
        bool passed = test_tools::validate_buffer(source_path, readback);

        test_result r;
        r.passed = passed; r.message = passed ? "" : "buffer data mismatch";
        r.output_buffer = output_buffer; r.golden_buffer = source_path;
        return r;
    }

    void cleanup() override
    {
        if (_texture) { lrhi_destroy_texture(_texture); _texture = nullptr; }
        if (_buffer)  { lrhi_destroy_buffer(_buffer);   _buffer  = nullptr; }
    }
};

REGISTER_TEST(copy_texture_to_buffer_src_layer_test);
