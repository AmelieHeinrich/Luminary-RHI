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
    bool consume_escape_pressed() override;
    void get_width_and_height(int* width, int* height) const override;
    void* get_swap_chain_handle() const override { return (__bridge void*)metal_layer; }
    void* get_native_view_handle() const override { return (__bridge void*)window.contentView; }
    void configure_swap_chain_info(LRHISwapChainInfo* info) const override;
    bool init_imgui() override;
    void new_imgui_frame() override;
    void shutdown_imgui() override;

private:
    void update_drawable_size() const;

    NSWindow* window;
    CAMetalLayer* metal_layer;
    bool escape_pressed;
};
