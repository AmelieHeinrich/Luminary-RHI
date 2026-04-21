#pragma once

#include "luminary_rhi.h"

class Window
{
public:
    static Window* create();

    virtual ~Window() = default;

    virtual bool should_close() const = 0;
    virtual void poll_events() = 0;
    virtual bool consume_escape_pressed() = 0;
    virtual void get_width_and_height(int* width, int* height) const = 0;
    virtual void* get_swap_chain_handle() const = 0;
    virtual void* get_native_view_handle() const = 0;
    virtual void configure_swap_chain_info(LRHISwapChainInfo* info) const = 0;
    virtual bool init_imgui() = 0;
    virtual void new_imgui_frame() = 0;
    virtual void shutdown_imgui() = 0;
};
