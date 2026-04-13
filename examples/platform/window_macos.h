#pragma once

#include "examples/window.h"

#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <Cocoa/Cocoa.h>
#include <Foundation/Foundation.h>

class MacOSWindow : public Window
{
public:
    MacOSWindow();
    ~MacOSWindow() override;

    bool should_close() const override;
    void poll_events() override;
    void get_width_and_height(int* width, int* height) const override;
    void* get_swap_chain_handle() const override { return (__bridge void*)metal_layer; }

private:
    NSWindow* window;
    CAMetalLayer* metal_layer;
};
