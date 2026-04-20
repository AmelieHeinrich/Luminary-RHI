#pragma once

#include "luminary_rhi.h"

class Example
{
public:
    virtual ~Example() = default;

    virtual const char* name() const = 0;
    virtual bool is_ready() const = 0;
    virtual void record(LRHICommandList command_list, LRHITextureView target_view, int width, int height, LRHIResidencySet residency_set) = 0;
    virtual void draw_ui() = 0;
};