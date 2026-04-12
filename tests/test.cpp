#include "test.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

// STB (implementations compiled once in stb/stb.cpp)
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

// NVIDIA FLIP — single-header, CPU path only (do NOT define FLIP_ENABLE_CUDA)
#include "flip/flip.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static uint32_t format_bpp(LRHITextureFormat fmt)
{
    switch (fmt) {
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM:
        case LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB:
        case LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM:
            return 4;
        case LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT:
            return 8;
        case LUMINARY_RHI_TEXTURE_FORMAT_R32G32B32A32_FLOAT:
            return 16;
        default:
            return 4;
    }
}

// Ensure all parent directories for a path exist.
static void ensure_parent_dirs(const char* path)
{
    std::filesystem::path p(path);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());
}

// ---------------------------------------------------------------------------
// test_tools implementations
// ---------------------------------------------------------------------------

void test_tools::rhi_readback_texture(LRHIDevice device, LRHITexture texture, std::vector<uint8_t>& out_data,
                                      uint32_t mip_level, uint32_t array_layer)
{
    LRHITextureInfo info;
    lrhi_get_texture_info(texture, &info);

    uint32_t mip_w         = (info.width  >> mip_level) > 0 ? (info.width  >> mip_level) : 1;
    uint32_t mip_h         = (info.height >> mip_level) > 0 ? (info.height >> mip_level) : 1;
    uint32_t bpp           = format_bpp(info.format);
    uint32_t bytes_per_row = mip_w * bpp;
    uint32_t total_size    = bytes_per_row * mip_h;
    out_data.resize(total_size);

    LRHIRegion region = { 0, 0, 0, mip_w, mip_h, 1 };
    LRHIError err     = {};
    lrhi_texture_readback(device, texture, &region, mip_level, array_layer,
                          out_data.data(), total_size, bytes_per_row, 0, &err);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        fprintf(stderr, "rhi_readback_texture: %s\n", err.message);
}

void test_tools::rhi_readback_buffer(LRHIDevice /*device*/, LRHIBuffer buffer, std::vector<uint8_t>& out_data)
{
    LRHIBufferInfo info;
    lrhi_get_buffer_info(buffer, &info);
    out_data.resize((size_t)info.size);

    LRHIError err = {};
    const void* ptr = lrhi_buffer_map(buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        fprintf(stderr, "rhi_readback_buffer: %s\n", err.message);
        return;
    }
    memcpy(out_data.data(), ptr, (size_t)info.size);
    lrhi_buffer_unmap(buffer);
}

bool test_tools::validate_texture(const char* reference_path, const std::vector<uint8_t>& data,
                                  const LRHITextureInfo& info, bool /*hdr*/,
                                  float& out_mean_error, const char* flip_output_path)
{
    // Load reference image as RGBA8
    int ref_w, ref_h, ref_ch;
    uint8_t* ref_pixels = stbi_load(reference_path, &ref_w, &ref_h, &ref_ch, 4);
    if (!ref_pixels) {
        fprintf(stderr, "validate_texture: failed to load reference '%s'\n", reference_path);
        out_mean_error = 1.0f;
        return false;
    }

    if ((uint32_t)ref_w != info.width || (uint32_t)ref_h != info.height) {
        fprintf(stderr, "validate_texture: size mismatch (ref=%dx%d, test=%dx%d)\n",
                ref_w, ref_h, info.width, info.height);
        stbi_image_free(ref_pixels);
        out_mean_error = 1.0f;
        return false;
    }

    uint32_t n = info.width * info.height;

    // Convert RGBA8 → 3-channel float for FLIP (drop alpha, divide by 255)
    std::vector<float> ref_float(n * 3);
    std::vector<float> test_float(n * 3);
    for (uint32_t i = 0; i < n; i++) {
        ref_float[i*3+0]  = ref_pixels[i*4+0] / 255.0f;
        ref_float[i*3+1]  = ref_pixels[i*4+1] / 255.0f;
        ref_float[i*3+2]  = ref_pixels[i*4+2] / 255.0f;
        test_float[i*3+0] = data[i*4+0] / 255.0f;
        test_float[i*3+1] = data[i*4+1] / 255.0f;
        test_float[i*3+2] = data[i*4+2] / 255.0f;
    }
    stbi_image_free(ref_pixels);

    FLIP::Parameters params;
    float mean_error = 0.0f;
    float* error_map = nullptr;
    bool apply_magma = (flip_output_path != nullptr);

    FLIP::evaluate(ref_float.data(), test_float.data(),
                   (int)info.width, (int)info.height,
                   false, params,
                   apply_magma, true, mean_error,
                   apply_magma ? &error_map : nullptr);

    // Optionally save the Magma-colorized error map
    if (flip_output_path && error_map) {
        ensure_parent_dirs(flip_output_path);
        // error_map is 3-channel float (RGB, Magma-mapped) in [0,1]; convert to RGBA8
        std::vector<uint8_t> flip_rgba(n * 4);
        for (uint32_t i = 0; i < n; i++) {
            flip_rgba[i*4+0] = (uint8_t)(error_map[i*3+0] * 255.0f + 0.5f);
            flip_rgba[i*4+1] = (uint8_t)(error_map[i*3+1] * 255.0f + 0.5f);
            flip_rgba[i*4+2] = (uint8_t)(error_map[i*3+2] * 255.0f + 0.5f);
            flip_rgba[i*4+3] = 255;
        }
        stbi_write_png(flip_output_path, (int)info.width, (int)info.height, 4,
                       flip_rgba.data(), (int)info.width * 4);
        delete[] error_map;
    }

    out_mean_error = mean_error;
    return mean_error < 0.05f;
}

bool test_tools::validate_buffer(const char* reference_path, const std::vector<uint8_t>& data)
{
    FILE* f = fopen(reference_path, "rb");
    if (!f) {
        fprintf(stderr, "validate_buffer: failed to open reference '%s'\n", reference_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if ((size_t)size != data.size()) {
        fclose(f);
        return false;
    }

    std::vector<uint8_t> ref((size_t)size);
    fread(ref.data(), 1, (size_t)size, f);
    fclose(f);

    return memcmp(ref.data(), data.data(), (size_t)size) == 0;
}

void test_tools::save_texture(const char* output_path, const std::vector<uint8_t>& data,
                              const LRHITextureInfo& info, uint32_t mip_level)
{
    ensure_parent_dirs(output_path);
    uint32_t mip_w         = (info.width  >> mip_level) > 0 ? (info.width  >> mip_level) : 1;
    uint32_t mip_h         = (info.height >> mip_level) > 0 ? (info.height >> mip_level) : 1;
    uint32_t bytes_per_row = mip_w * format_bpp(info.format);
    stbi_write_png(output_path, (int)mip_w, (int)mip_h, 4,
                   data.data(), (int)bytes_per_row);
}

void test_tools::save_buffer(const char* output_path, const std::vector<uint8_t>& data)
{
    ensure_parent_dirs(output_path);
    FILE* f = fopen(output_path, "wb");
    if (!f) {
        fprintf(stderr, "save_buffer: failed to open output file '%s'\n", output_path);
        return;
    }
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}
