#include "tests/test.h"

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a texture view for a specific mip/layer slice of a texture.
static LRHITextureView make_view(LRHIDevice device, LRHITexture tex,
                                 LRHITextureUsage usage,
                                 LRHITextureDimensions dimensions,
                                 uint32_t base_mip,   uint32_t mip_count,
                                 uint32_t base_layer, uint32_t layer_count)
{
    LRHITextureViewInfo vinfo = {};
    vinfo.texture           = tex;
    vinfo.base_mip_level    = base_mip;
    vinfo.mip_level_count   = mip_count;
    vinfo.base_array_layer  = base_layer;
    vinfo.array_layer_count = layer_count;
    vinfo.format            = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    vinfo.usage             = usage;
    vinfo.dimensions        = dimensions;
    LRHIError err = {};
    LRHITextureView view = nullptr;
    lrhi_create_texture_view(device, &vinfo, &view, &err);
    return view;
}

// Submit a command list and wait for GPU completion.
static bool rp_submit_and_wait(LRHIDevice device, LRHICommandQueue queue,
                                LRHICommandList cmd, LRHIFence fence,
                                std::string& err_out)
{
    LRHIError err = {};
    lrhi_command_list_end(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd end: ") + err.message;
        return false;
    }
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
    return true;
}

// Run a render pass (no draw calls — just clear) and wait for the GPU.
// textures[] / views[] / clear_colors[][4] are the N color attachments.
// depth_view may be nullptr if no depth attachment.
static bool run_render_pass_clear(LRHIDevice device,
                                  LRHITexture* textures, LRHITextureView* views,
                                  const float (*clear_colors)[4], uint32_t color_count,
                                  LRHITextureView depth_view, float clear_depth,
                                  uint32_t render_w, uint32_t render_h,
                                  std::string& err_out)
{
    LRHIResidencySet rs = nullptr;
    lrhi_create_residency_set(device, &rs, nullptr);
    for (uint32_t i = 0; i < color_count; ++i)
        lrhi_residency_set_add_texture(rs, textures[i], nullptr);
    lrhi_residency_set_update(rs, nullptr);

    LRHIError err = {};
    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &err);
    lrhi_command_queue_add_residency_set(queue, rs, nullptr);

    LRHIFence fence = nullptr;
    lrhi_create_fence(device, 0, &fence, &err);
    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &err);

    err = {};
    lrhi_command_list_begin(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd begin: ") + err.message;
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
        return false;
    }

    LRHIRenderPassInfo rp_info = {};
    for (uint32_t i = 0; i < color_count; ++i) {
        rp_info.color_attachments[i].texture_view  = views[i];
        rp_info.color_attachments[i].load_action   = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.color_attachments[i].store_action  = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.color_attachments[i].clear_color[0] = clear_colors[i][0];
        rp_info.color_attachments[i].clear_color[1] = clear_colors[i][1];
        rp_info.color_attachments[i].clear_color[2] = clear_colors[i][2];
        rp_info.color_attachments[i].clear_color[3] = clear_colors[i][3];
    }
    rp_info.color_attachment_count = color_count;

    if (depth_view) {
        rp_info.has_depth_stencil_attachment = 1;
        rp_info.depth_stencil_attachment.texture_view = depth_view;
        rp_info.depth_stencil_attachment.load_action  = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.depth_stencil_attachment.store_action = LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE;
        rp_info.depth_stencil_attachment.clear_depth  = clear_depth;
    }

    rp_info.render_width  = render_w;
    rp_info.render_height = render_h;

    err = {};
    LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("render_pass_begin: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
        return false;
    }

    err = {};
    lrhi_render_pass_end(rp, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("render_pass_end: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
        return false;
    }

    bool ok = rp_submit_and_wait(device, queue, cmd, fence, err_out);
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(rs);
    return ok;
}

// Standard texture-test boilerplate: readback, save, compare.
static test_result texture_test_result(LRHIDevice device, LRHITexture tex,
                                       const char* test_name,
                                       const char* golden_path,
                                       bool bake_mode,
                                       uint32_t mip_level = 0,
                                       uint32_t array_layer = 0)
{
    LRHITextureInfo info = {};
    lrhi_get_texture_info(tex, &info);

    std::vector<uint8_t> readback;
    test_tools::rhi_readback_texture(device, tex, readback, mip_level, array_layer);

    std::string output_image = std::string("tests/output/") + test_name + ".png";
    std::string flip_image   = std::string("tests/output/") + test_name + "_flip.png";

    if (bake_mode) {
        test_tools::save_texture(golden_path, readback, info, mip_level);
        test_result r; r.passed = true; r.message = "baked"; r.golden_image = golden_path;
        return r;
    }

    test_tools::save_texture(output_image.c_str(), readback, info, mip_level);
    
    LRHITextureInfo mip_adj_info = info;
    mip_adj_info.width = (info.width >> mip_level) > 0 ? (info.width >> mip_level) : 1;
    mip_adj_info.height = (info.height >> mip_level) > 0 ? (info.height >> mip_level) : 1;

    float mean_error = 0.0f;
    bool  passed     = test_tools::validate_texture(golden_path, readback, mip_adj_info, false, mean_error, flip_image.c_str());
    test_result r;
    r.passed         = passed;
    r.message        = passed ? "" : "FLIP mean error too high";
    r.flip_mean_error = mean_error;
    r.output_image   = output_image;
    r.golden_image   = golden_path;
    r.flip_image     = flip_image;
    return r;
}

// ---------------------------------------------------------------------------
// 1. render_pass_clear_texture
//    Single 64×64 RGBA8 render target cleared to {1, 0.5, 0.25, 1}.
// ---------------------------------------------------------------------------

class render_pass_clear_texture_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;

public:
    render_pass_clear_texture_test()
    {
        type        = test_type::texture;
        name        = "render_pass_clear_texture";
        source_path = "tests/golden/render_pass_clear_texture.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        _view = make_view(_device, _tex,
                          LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                          LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                          0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                          0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    }

    test_result run(bool bake_mode) override
    {
        static const float clear[1][4] = {{ 1.0f, 0.5f, 0.25f, 1.0f }};
        std::string err_msg;
        LRHITexture textures[1] = { _tex };
        LRHITextureView views[1] = { _view };
        if (!run_render_pass_clear(_device, textures, views, clear, 1,
                                   nullptr, 0.0f, W, H, err_msg))
            return { false, err_msg };
        return texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_view) { lrhi_destroy_texture_view(_view); _view = nullptr; }
        if (_tex)  { lrhi_destroy_texture(_tex);       _tex  = nullptr; }
    }
};

REGISTER_TEST(render_pass_clear_texture_test);

// ---------------------------------------------------------------------------
// 2. render_pass_clear_depth
//    Depth-only pass — clears a 64×64 D32_FLOAT_S8_UINT texture.
//    Validation test only (depth is not directly readable).
// ---------------------------------------------------------------------------

class render_pass_clear_depth_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;

public:
    render_pass_clear_depth_test()
    {
        type        = test_type::validation;
        name        = "render_pass_clear_depth";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT;
        info.usage        = LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        _view = make_view(_device, _tex,
                          LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL,
                          LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                          0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                          0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    }

    test_result run(bool /*bake_mode*/) override
    {
        LRHIResidencySet rs = nullptr;
        lrhi_create_residency_set(_device, &rs, nullptr);
        lrhi_residency_set_add_texture(rs, _tex, nullptr);
        lrhi_residency_set_update(rs, nullptr);

        LRHIError err = {};
        LRHICommandQueue queue = nullptr;
        lrhi_create_command_queue(_device, &queue, &err);
        lrhi_command_queue_add_residency_set(queue, rs, nullptr);

        LRHIFence fence = nullptr;
        lrhi_create_fence(_device, 0, &fence, &err);
        LRHICommandList cmd = nullptr;
        lrhi_create_command_list(queue, &cmd, &err);

        err = {};
        lrhi_command_list_begin(cmd, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
            return { false, std::string("cmd begin: ") + err.message };
        }

        LRHIRenderPassInfo rp_info = {};
        rp_info.color_attachment_count              = 0;
        rp_info.has_depth_stencil_attachment        = 1;
        rp_info.depth_stencil_attachment.texture_view = _view;
        rp_info.depth_stencil_attachment.load_action  = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.depth_stencil_attachment.store_action = LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE;
        rp_info.depth_stencil_attachment.clear_depth  = 1.0f;
        rp_info.depth_stencil_attachment.clear_stencil = 0;
        rp_info.render_width  = W;
        rp_info.render_height = H;

        err = {};
        LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_command_list_end(cmd, nullptr);
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
            return { false, std::string("render_pass_begin: ") + err.message };
        }

        err = {};
        lrhi_render_pass_end(rp, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_command_list_end(cmd, nullptr);
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
            return { false, std::string("render_pass_end: ") + err.message };
        }

        std::string err_msg;
        bool ok = rp_submit_and_wait(_device, queue, cmd, fence, err_msg);
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
        if (!ok) return { false, err_msg };
        return { true, "" };
    }

    void cleanup() override
    {
        if (_view) { lrhi_destroy_texture_view(_view); _view = nullptr; }
        if (_tex)  { lrhi_destroy_texture(_tex);       _tex  = nullptr; }
    }
};

REGISTER_TEST(render_pass_clear_depth_test);

// ---------------------------------------------------------------------------
// 3. render_pass_clear_multiple_textures
//    Three 64×64 render targets cleared to red, green, blue.
//    Reads back attachment 0 (red) for comparison.
// ---------------------------------------------------------------------------

class render_pass_clear_multiple_textures_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice      _device   = nullptr;
    LRHITexture     _tex[3]   = {};
    LRHITextureView _views[3] = {};

public:
    render_pass_clear_multiple_textures_test()
    {
        type        = test_type::texture;
        name        = "render_pass_clear_multiple_textures";
        source_path = "tests/golden/render_pass_clear_multiple_textures.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        for (int i = 0; i < 3; ++i) {
            LRHITextureInfo info = {};
            info.width        = W; info.height = H; info.depth = 1;
            info.mip_levels   = 1; info.array_layers = 1;
            info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
            LRHIError err = {};
            lrhi_create_texture(_device, &info, &_tex[i], &err);
            _views[i] = make_view(_device, _tex[i],
                                  LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                  LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                  0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                  0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        }
    }

    test_result run(bool bake_mode) override
    {
        static const float clears[3][4] = {
            { 1.0f, 0.0f, 0.0f, 1.0f },  // red
            { 0.0f, 1.0f, 0.0f, 1.0f },  // green
            { 0.0f, 0.0f, 1.0f, 1.0f }   // blue
        };
        std::string err_msg;
        if (!run_render_pass_clear(_device, _tex, _views, clears, 3,
                                   nullptr, 0.0f, W, H, err_msg))
            return { false, err_msg };
        return texture_test_result(_device, _tex[0], name, source_path, bake_mode);
    }

    void cleanup() override
    {
        for (int i = 2; i >= 0; --i) {
            if (_views[i]) { lrhi_destroy_texture_view(_views[i]); _views[i] = nullptr; }
            if (_tex[i])   { lrhi_destroy_texture(_tex[i]);        _tex[i]   = nullptr; }
        }
    }
};

REGISTER_TEST(render_pass_clear_multiple_textures_test);

// ---------------------------------------------------------------------------
// 4. render_pass_clear_texture_face
//    64×64 cube texture (6 layers); clears face 2 to {0.5, 0.0, 1.0, 1.0}.
// ---------------------------------------------------------------------------

class render_pass_clear_texture_face_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    static constexpr uint32_t FACE = 2;
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;  // view of face 2 only

public:
    render_pass_clear_texture_face_test()
    {
        type        = test_type::texture;
        name        = "render_pass_clear_texture_face";
        source_path = "tests/golden/render_pass_clear_texture_face.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1;
        info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        // View targeting face 2 only
        _view = make_view(_device, _tex,
                          LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                          LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                          0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                          FACE, 1);
    }

    test_result run(bool bake_mode) override
    {
        static const float clear[1][4] = {{ 0.5f, 0.0f, 1.0f, 1.0f }};
        std::string err_msg;
        LRHITexture textures[1] = { _tex };
        LRHITextureView views[1] = { _view };
        if (!run_render_pass_clear(_device, textures, views, clear, 1,
                                   nullptr, 0.0f, W, H, err_msg))
            return { false, err_msg };
        return texture_test_result(_device, _tex, name, source_path, bake_mode, 0, FACE);
    }

    void cleanup() override
    {
        if (_view) { lrhi_destroy_texture_view(_view); _view = nullptr; }
        if (_tex)  { lrhi_destroy_texture(_tex);       _tex  = nullptr; }
    }
};

REGISTER_TEST(render_pass_clear_texture_face_test);

// ---------------------------------------------------------------------------
// 5. render_pass_clear_texture_layer
//    64×64 2D_ARRAY texture (4 layers); clears layer 1 to {0.0, 1.0, 0.5, 1.0}.
// ---------------------------------------------------------------------------

class render_pass_clear_texture_layer_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    static constexpr uint32_t LAYER = 1;
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;

public:
    render_pass_clear_texture_layer_test()
    {
        type        = test_type::texture;
        name        = "render_pass_clear_texture_layer";
        source_path = "tests/golden/render_pass_clear_texture_layer.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 4;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        _view = make_view(_device, _tex,
                          LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                          LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                          0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                          LAYER, 1);
    }

    test_result run(bool bake_mode) override
    {
        static const float clear[1][4] = {{ 0.0f, 1.0f, 0.5f, 1.0f }};
        std::string err_msg;
        LRHITexture textures[1] = { _tex };
        LRHITextureView views[1] = { _view };
        if (!run_render_pass_clear(_device, textures, views, clear, 1,
                                   nullptr, 0.0f, W, H, err_msg))
            return { false, err_msg };
        return texture_test_result(_device, _tex, name, source_path, bake_mode, 0, LAYER);
    }

    void cleanup() override
    {
        if (_view) { lrhi_destroy_texture_view(_view); _view = nullptr; }
        if (_tex)  { lrhi_destroy_texture(_tex);       _tex  = nullptr; }
    }
};

REGISTER_TEST(render_pass_clear_texture_layer_test);

// ---------------------------------------------------------------------------
// 6. render_pass_clear_texture_mip
//    64×64 4-mip texture; clears mip 2 (16×16) to {0.0, 0.75, 1.0, 1.0}.
// ---------------------------------------------------------------------------

class render_pass_clear_texture_mip_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    static constexpr uint32_t TARGET_MIP = 2;
    static constexpr uint32_t MIP_W = W >> TARGET_MIP;  // 16
    static constexpr uint32_t MIP_H = H >> TARGET_MIP;  // 16
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;

public:
    render_pass_clear_texture_mip_test()
    {
        type        = test_type::texture;
        name        = "render_pass_clear_texture_mip";
        source_path = "tests/golden/render_pass_clear_texture_mip.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 4; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        // View targeting mip 2 only
        _view = make_view(_device, _tex,
                          LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                          LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                          TARGET_MIP, 1,
                          0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    }

    test_result run(bool bake_mode) override
    {
        static const float clear[1][4] = {{ 0.0f, 0.75f, 1.0f, 1.0f }};
        std::string err_msg;
        LRHITexture textures[1] = { _tex };
        LRHITextureView views[1] = { _view };
        if (!run_render_pass_clear(_device, textures, views, clear, 1,
                                   nullptr, 0.0f, MIP_W, MIP_H, err_msg))
            return { false, err_msg };
        return texture_test_result(_device, _tex, name, source_path, bake_mode, TARGET_MIP, 0);
    }

    void cleanup() override
    {
        if (_view) { lrhi_destroy_texture_view(_view); _view = nullptr; }
        if (_tex)  { lrhi_destroy_texture(_tex);       _tex  = nullptr; }
    }
};

REGISTER_TEST(render_pass_clear_texture_mip_test);

// ---------------------------------------------------------------------------
// 7. render_pass_clear_multiple_complex
//    4 color attachments + 1 depth, each targeting different subresources.
//    All render targets share a 64×64 render area.
//
//    tex0: full clear {1,0,0,1}
//    tex1: cube, view = face 2, clear {0,1,0,1}
//    tex2: 2d_array (4 layers), view = layer 2, clear {0,0,1,1}
//    tex3: 2d_array (4 layers), view = layer 3, clear {1,1,0,1}
//    depth: cube (6 faces), view = face 3, clear depth 1.0
//
//    Verification: readback tex0 mip0 layer0 (should be solid red).
// ---------------------------------------------------------------------------

class render_pass_clear_multiple_complex_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice      _device        = nullptr;
    LRHITexture     _tex[4]        = {};
    LRHITextureView _color_views[4]= {};
    LRHITexture     _depth_tex     = nullptr;
    LRHITextureView _depth_view    = nullptr;

public:
    render_pass_clear_multiple_complex_test()
    {
        type        = test_type::texture;
        name        = "render_pass_clear_multiple_complex";
        source_path = "tests/golden/render_pass_clear_multiple_complex.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHIError err = {};

        // tex0: plain 64×64
        {
            LRHITextureInfo info = {};
            info.width = W; info.height = H; info.depth = 1;
            info.mip_levels = 1; info.array_layers = 1;
            info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
            lrhi_create_texture(_device, &info, &_tex[0], &err);
            _color_views[0] = make_view(_device, _tex[0],
                                        LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                        LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                        0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                        0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
        }

        // tex1: cube (6 faces), view = face 2
        {
            LRHITextureInfo info = {};
            info.width = W; info.height = H; info.depth = 1;
            info.mip_levels = 1; info.array_layers = 1;
            info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
            lrhi_create_texture(_device, &info, &_tex[1], &err);
            _color_views[1] = make_view(_device, _tex[1],
                                        LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                        LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                        0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                        2, 1);
        }

        // tex2: 2d_array (4 layers), view = layer 2
        {
            LRHITextureInfo info = {};
            info.width = W; info.height = H; info.depth = 1;
            info.mip_levels = 1; info.array_layers = 4;
            info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
            lrhi_create_texture(_device, &info, &_tex[2], &err);
            _color_views[2] = make_view(_device, _tex[2],
                                        LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                        LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                        0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                        2, 1);
        }

        // tex3: 2d_array (4 layers), view = layer 3
        {
            LRHITextureInfo info = {};
            info.width = W; info.height = H; info.depth = 1;
            info.mip_levels = 1; info.array_layers = 4;
            info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D_ARRAY;
            lrhi_create_texture(_device, &info, &_tex[3], &err);
            _color_views[3] = make_view(_device, _tex[3],
                                        LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                                        LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                        0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                        3, 1);
        }

        // depth: cube (6 faces), view = face 3
        {
            LRHITextureInfo info = {};
            info.width = W; info.height = H; info.depth = 1;
            info.mip_levels = 1; info.array_layers = 1;
            info.format = LUMINARY_RHI_TEXTURE_FORMAT_D32_FLOAT_S8_UINT;
            info.usage  = LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
            info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_CUBE;
            lrhi_create_texture(_device, &info, &_depth_tex, &err);
            _depth_view = make_view(_device, _depth_tex,
                                    LUMINARY_RHI_TEXTURE_USAGE_DEPTH_STENCIL,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    3, 1);
        }
    }

    test_result run(bool bake_mode) override
    {
        static const float clears[4][4] = {
            { 1.0f, 0.0f, 0.0f, 1.0f },  // tex0: red
            { 0.0f, 1.0f, 0.0f, 1.0f },  // tex1 face 2: green
            { 0.0f, 0.0f, 1.0f, 1.0f },  // tex2 layer 2: blue
            { 1.0f, 1.0f, 0.0f, 1.0f }   // tex3 layer 3: yellow
        };
        std::string err_msg;
        if (!run_render_pass_clear(_device, _tex, _color_views, clears, 4,
                                   _depth_view, 1.0f, W, H, err_msg))
            return { false, err_msg };
        return texture_test_result(_device, _tex[0], name, source_path, bake_mode, 0, 0);
    }

    void cleanup() override
    {
        if (_depth_view) { lrhi_destroy_texture_view(_depth_view); _depth_view = nullptr; }
        if (_depth_tex)  { lrhi_destroy_texture(_depth_tex);       _depth_tex  = nullptr; }
        for (int i = 3; i >= 0; --i) {
            if (_color_views[i]) { lrhi_destroy_texture_view(_color_views[i]); _color_views[i] = nullptr; }
            if (_tex[i])         { lrhi_destroy_texture(_tex[i]);               _tex[i]         = nullptr; }
        }
    }
};

REGISTER_TEST(render_pass_clear_multiple_complex_test);

// ---------------------------------------------------------------------------
// 8. render_pass_load_store
//    Two render passes on the same texture.
//    Pass 1: CLEAR to green, STORE.
//    Pass 2: LOAD, STORE (clear color red, should be ignored).
//    Result should be green.
// ---------------------------------------------------------------------------

class render_pass_load_store_test : public test
{
    static constexpr uint32_t W = 64, H = 64;
    LRHIDevice      _device = nullptr;
    LRHITexture     _tex    = nullptr;
    LRHITextureView _view   = nullptr;

public:
    render_pass_load_store_test()
    {
        type        = test_type::texture;
        name        = "render_pass_load_store";
        source_path = "tests/golden/render_pass_load_store.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;
        LRHITextureInfo info = {};
        info.width        = W; info.height = H; info.depth = 1;
        info.mip_levels   = 1; info.array_layers = 1;
        info.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        info.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_tex, &err);
        _view = make_view(_device, _tex,
                          LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                          LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                          0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                          0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);
    }

    test_result run(bool bake_mode) override
    {
        LRHIResidencySet rs = nullptr;
        lrhi_create_residency_set(_device, &rs, nullptr);
        lrhi_residency_set_add_texture(rs, _tex, nullptr);
        lrhi_residency_set_update(rs, nullptr);

        LRHIError err = {};
        LRHICommandQueue queue = nullptr;
        lrhi_create_command_queue(_device, &queue, &err);
        lrhi_command_queue_add_residency_set(queue, rs, nullptr);

        LRHIFence fence = nullptr;
        lrhi_create_fence(_device, 0, &fence, &err);
        LRHICommandList cmd = nullptr;
        lrhi_create_command_list(queue, &cmd, &err);

        err = {};
        lrhi_command_list_begin(cmd, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);
            return { false, std::string("cmd begin: ") + err.message };
        }

        // Pass 1
        LRHIRenderPassInfo rp1_info = {};
        rp1_info.color_attachments[0].texture_view  = _view;
        rp1_info.color_attachments[0].load_action   = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp1_info.color_attachments[0].store_action  = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp1_info.color_attachments[0].clear_color[0] = 0.0f; // green
        rp1_info.color_attachments[0].clear_color[1] = 1.0f;
        rp1_info.color_attachments[0].clear_color[2] = 0.0f;
        rp1_info.color_attachments[0].clear_color[3] = 1.0f;
        rp1_info.color_attachment_count = 1;
        rp1_info.render_width  = W;
        rp1_info.render_height = H;

        err = {};
        LRHIRenderPass rp1 = lrhi_render_pass_begin(cmd, &rp1_info, &err);
        lrhi_render_pass_end(rp1, nullptr);

        // Pass 2
        LRHIRenderPassInfo rp2_info = {};
        rp2_info.color_attachments[0].texture_view  = _view;
        rp2_info.color_attachments[0].load_action   = LUMINARY_RHI_RENDER_PASS_ACTION_LOAD;
        rp2_info.color_attachments[0].store_action  = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp2_info.color_attachments[0].clear_color[0] = 1.0f; // red (should be ignored!)
        rp2_info.color_attachments[0].clear_color[1] = 0.0f;
        rp2_info.color_attachments[0].clear_color[2] = 0.0f;
        rp2_info.color_attachments[0].clear_color[3] = 1.0f;
        rp2_info.color_attachment_count = 1;
        rp2_info.render_width  = W;
        rp2_info.render_height = H;

        err = {};
        LRHIRenderPass rp2 = lrhi_render_pass_begin(cmd, &rp2_info, &err);
        lrhi_render_pass_end(rp2, nullptr);

        std::string err_msg;
        bool ok = rp_submit_and_wait(_device, queue, cmd, fence, err_msg);
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue); lrhi_destroy_residency_set(rs);

        if (!ok) return { false, err_msg };

        return texture_test_result(_device, _tex, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_view) { lrhi_destroy_texture_view(_view); _view = nullptr; }
        if (_tex)  { lrhi_destroy_texture(_tex);       _tex  = nullptr; }
    }
};

REGISTER_TEST(render_pass_load_store_test);
