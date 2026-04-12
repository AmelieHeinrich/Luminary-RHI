#include "tests/test.h"

#ifdef LRHI_MACOS

#include <cstdio>
#include <vector>

class replace_texture_region_test : public test
{
    static constexpr uint32_t W = 64;
    static constexpr uint32_t H = 64;

    LRHIDevice  _device  = nullptr;
    LRHITexture _texture = nullptr;

public:
    replace_texture_region_test()
    {
        type        = test_type::texture;
        name        = "replace_texture_region";
        source_path = "tests/golden/replace_texture_region.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHITextureInfo info = {};
        info.width           = W;
        info.height          = H;
        info.depth           = 1;
        info.mip_levels      = 1;
        info.array_layers    = 1;
        info.format          = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        info.usage           = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        info.dimensions      = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;

        LRHIError err = {};
        lrhi_create_texture(_device, &info, &_texture, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            fprintf(stderr, "replace_texture_region_test::init: %s\n", err.message);
    }

    test_result run(bool bake_mode) override
    {
        // Build an RGBA8 gradient pattern — easy to verify visually.
        std::vector<uint8_t> pixels(W * H * 4);
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                uint32_t i   = (y * W + x) * 4;
                pixels[i+0]  = (uint8_t)(x * 4);   // R ramps left → right
                pixels[i+1]  = (uint8_t)(y * 4);   // G ramps top  → bottom
                pixels[i+2]  = 128;                 // B constant
                pixels[i+3]  = 255;                 // A opaque
            }
        }

        // Upload via replace_region
        LRHIRegion region = { 0, 0, 0, W, H, 1 };
        LRHIError  err    = {};
        lrhi_texture_replace_region(_texture, &region, 0, 0,
                                    pixels.data(), (uint32_t)pixels.size(),
                                    W * 4, 0, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return { false, std::string("replace_region failed: ") + err.message };

        // Read back via test_tools
        LRHITextureInfo tex_info = {};
        lrhi_get_texture_info(_texture, &tex_info);

        std::vector<uint8_t> readback;
        test_tools::rhi_readback_texture(_device, _texture, readback);

        // Build output / FLIP paths derived from test name
        std::string output_image = std::string("tests/output/") + name + ".png";
        std::string flip_image   = std::string("tests/output/") + name + "_flip.png";

        if (bake_mode) {
            test_tools::save_texture(source_path, readback, tex_info);
            test_result r;
            r.passed       = true;
            r.message      = "baked";
            r.golden_image = source_path;
            return r;
        }

        // Save output image for the viewer regardless of pass/fail
        test_tools::save_texture(output_image.c_str(), readback, tex_info);

        float mean_error = 0.0f;
        bool  passed     = test_tools::validate_texture(source_path, readback, tex_info,
                                                        false, mean_error, flip_image.c_str());

        test_result r;
        r.passed          = passed;
        r.message         = passed ? "" : "FLIP mean error too high";
        r.flip_mean_error = mean_error;
        r.output_image    = output_image;
        r.golden_image    = source_path;
        r.flip_image      = flip_image;
        return r;
    }

    void cleanup() override
    {
        if (_texture) {
            lrhi_destroy_texture(_texture);
            _texture = nullptr;
        }
    }
};

REGISTER_TEST(replace_texture_region_test);

#endif // LRHI_MACOS
