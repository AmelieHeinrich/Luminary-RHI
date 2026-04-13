#pragma once

class Window
{
public:
    static Window* create();

    virtual ~Window() = default;

    virtual bool should_close() const = 0;
    virtual void poll_events() = 0;
    virtual void get_width_and_height(int* width, int* height) const = 0;
    virtual void* get_swap_chain_handle() const = 0;
};
