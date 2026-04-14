#pragma once

#include "luminary_rhi.h"
#include "tests/test.h"
#include <luminary_shader_compiler.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

static std::string dh_read_shader_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string src(size, '\0');
    fread(src.data(), 1, size, f);
    fclose(f);
    return src;
}

// ---------------------------------------------------------------------------
// Shader compilation
// ---------------------------------------------------------------------------

// Compile HLSL source to METALLIB. Returns {bytecode, size} or {nullptr, 0}.
// defines[] is an array of C-strings (e.g. "USE_RED_COLOR").
// use_point_topology must be true when the pipeline uses POINT_LIST topology.
static std::pair<uint8_t*, uint64_t> dh_compile_stage(
    const std::string& source,
    LuminaryShaderStage stage,
    const char* entry_point,
    const char* const* defines      = nullptr,
    uint32_t           defines_count = 0,
    bool               use_point_topology = false)
{
    LuminaryShaderCompilerOptions opts = {};
    opts.shading_language   = LUMINARY_SHADING_LANGUAGE_HLSL;
    opts.bytecode           = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage       = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code        = const_cast<char*>(source.data());
    opts.source_code_size   = source.size();
    opts.defines_count      = defines_count;
    opts.use_point_topology = use_point_topology ? 1 : 0;
    for (uint32_t i = 0; i < defines_count && i < 256; ++i)
        opts.defines[i] = defines[i];

    uint64_t size     = 0;
    uint8_t* bytecode = luminary_compile_shader(&opts, &size);
    return {bytecode, size};
}

// Create a shader module from compiled bytecode. Frees bytecode on success or failure.
static LRHIShaderModule dh_make_module(LRHIDevice device,
                                       uint8_t* bytecode, uint64_t size,
                                       LRHIShaderStage stage,
                                       const char* entry_point,
                                       std::string& err_out)
{
    LRHIShaderModuleInfo info = {};
    info.stage       = stage;
    info.entry_point = entry_point;
    info.code        = reinterpret_cast<const uint32_t*>(bytecode);
    info.code_size   = static_cast<uint32_t>(size);

    LRHIShaderModule module = nullptr;
    LRHIError err = {};
    lrhi_create_shader_module(device, &info, &module, &err);
    free(bytecode);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("create_shader_module: ") + err.message;
        return nullptr;
    }
    if (!module)
        err_out = "create_shader_module returned null";
    return module;
}

// ---------------------------------------------------------------------------
// Texture view helper
// ---------------------------------------------------------------------------

static LRHITextureView dh_make_view(LRHIDevice device, LRHITexture tex,
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

// ---------------------------------------------------------------------------
// Submit + wait
// ---------------------------------------------------------------------------

static bool dh_submit_and_wait(LRHIDevice device,
                                LRHICommandQueue queue,
                                LRHICommandList cmd,
                                LRHIFence fence,
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

// ---------------------------------------------------------------------------
// run_draw_pass
//
// Sets up the full GPU submission machinery (residency set, queue, fence,
// cmd), opens a render pass with CLEAR/STORE for color and CLEAR/DONT_CARE
// for depth (when depth_view != nullptr), calls body(rp, err) for draw calls,
// ends the pass, submits, and waits.
//
// extra_buffers[] are added to the residency set (e.g. index buffers).
// ---------------------------------------------------------------------------

template<typename F>
static bool dh_run_draw_pass(LRHIDevice device,
                              LRHITexture       color_tex,
                              LRHITextureView   color_view,
                              LRHITextureView   depth_view,
                              LRHIBuffer*       extra_buffers,
                              uint32_t          extra_buffer_count,
                              uint32_t          render_w,
                              uint32_t          render_h,
                              const float       clear_color[4],
                              std::string&      err_out,
                              F&&               body)
{
    // Residency set
    LRHIResidencySet rs = nullptr;
    lrhi_create_residency_set(device, &rs, nullptr);
    lrhi_residency_set_add_texture(rs, color_tex, nullptr);
    for (uint32_t i = 0; i < extra_buffer_count; ++i)
        lrhi_residency_set_add_buffer(rs, extra_buffers[i], nullptr);
    lrhi_residency_set_update(rs, nullptr);

    // Command objects
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
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    // Render pass info
    LRHIRenderPassInfo rp_info = {};
    rp_info.color_attachments[0].texture_view     = color_view;
    rp_info.color_attachments[0].load_action      = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].store_action     = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].clear_color[0]   = clear_color[0];
    rp_info.color_attachments[0].clear_color[1]   = clear_color[1];
    rp_info.color_attachments[0].clear_color[2]   = clear_color[2];
    rp_info.color_attachments[0].clear_color[3]   = clear_color[3];
    rp_info.color_attachment_count                = 1;

    if (depth_view) {
        rp_info.has_depth_stencil_attachment              = 1;
        rp_info.depth_stencil_attachment.texture_view     = depth_view;
        rp_info.depth_stencil_attachment.load_action      = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
        rp_info.depth_stencil_attachment.store_action     = LUMINARY_RHI_RENDER_PASS_ACTION_DONT_CARE;
        rp_info.depth_stencil_attachment.clear_depth      = 1.0f;
    }

    rp_info.render_width  = render_w;
    rp_info.render_height = render_h;

    err = {};
    LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("render_pass_begin: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    // Issue draw calls via caller-provided lambda
    err = {};
    body(rp, err);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("draw: ") + err.message;
        lrhi_render_pass_end(rp, nullptr);
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    err = {};
    lrhi_render_pass_end(rp, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("render_pass_end: ") + err.message;
        lrhi_command_list_end(cmd, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_residency_set(rs);
        return false;
    }

    bool ok = dh_submit_and_wait(device, queue, cmd, fence, err_out);
    lrhi_destroy_command_list(cmd);
    lrhi_destroy_fence(fence);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_residency_set(rs);
    return ok;
}

// ---------------------------------------------------------------------------
// texture_test_result — readback, save PNG, compare against golden
// ---------------------------------------------------------------------------

static test_result dh_texture_test_result(LRHIDevice device,
                                          LRHITexture tex,
                                          const char* test_name,
                                          const char* golden_path,
                                          bool        bake_mode,
                                          uint32_t    mip_level   = 0,
                                          uint32_t    array_layer = 0)
{
    LRHITextureInfo info = {};
    lrhi_get_texture_info(tex, &info);

    std::vector<uint8_t> readback;
    test_tools::rhi_readback_texture(device, tex, readback, mip_level, array_layer);

    std::string output_image = std::string("tests/output/") + test_name + ".png";
    std::string flip_image   = std::string("tests/output/") + test_name + "_flip.png";

    if (bake_mode) {
        test_tools::save_texture(golden_path, readback, info, mip_level);
        test_result r;
        r.passed       = true;
        r.message      = "baked";
        r.golden_image = golden_path;
        return r;
    }

    test_tools::save_texture(output_image.c_str(), readback, info, mip_level);

    LRHITextureInfo adj = info;
    adj.width  = (info.width  >> mip_level) > 0 ? (info.width  >> mip_level) : 1;
    adj.height = (info.height >> mip_level) > 0 ? (info.height >> mip_level) : 1;

    float mean_error = 0.0f;
    bool  passed     = test_tools::validate_texture(golden_path, readback, adj, false, mean_error,
                                                     flip_image.c_str());
    test_result r;
    r.passed          = passed;
    r.message         = passed ? "" : "FLIP mean error too high";
    r.flip_mean_error = mean_error;
    r.output_image    = output_image;
    r.golden_image    = golden_path;
    r.flip_image      = flip_image;
    return r;
}
