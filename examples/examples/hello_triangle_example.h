#pragma once

#include "example.h"

class HelloTriangleExample : public Example
{
public:
    HelloTriangleExample(LRHIDevice device, LRHITextureFormat render_target_format);
    ~HelloTriangleExample() override;

    const char* name() const override;
    bool is_ready() const override;
    void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) override;
    void draw_ui() override;

private:
    LRHIDevice device;
    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;
    LRHIRenderPipeline pipeline;
};