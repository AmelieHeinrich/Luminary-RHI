#include "luminary_rhi.h"
#include "window.h"
#include <cstdio>

int main(void)
{
    LRHIError error = {};

    LRHIDevice device;
    lrhi_create_device(LUMINARY_RHI_BACKEND_METAL4, &device, 0, nullptr);
    if (error.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
        printf("Error: %s", error.message);
        return 1;
    }

    LRHICommandQueue queue;
    lrhi_create_command_queue(device, &queue, &error);
    if (error.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
        printf("Error: %s", error.message);
        lrhi_destroy_device(device);
        return 1;
    }

    Window* window = Window::create();
    int width, height;
    window->get_width_and_height(&width, &height);

    LRHISwapChainInfo swap_chain_info = {};
    swap_chain_info.width = width;
    swap_chain_info.height = height;
    swap_chain_info.format = LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM;
    swap_chain_info.max_frames_in_flight = 3;
    swap_chain_info.handle_type = LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER;
    swap_chain_info.handle.metal_layer = window->get_swap_chain_handle();

    LRHISwapChain swap_chain;
    lrhi_create_swap_chain(device, queue, &swap_chain_info, &swap_chain, &error);
    if (error.severity != LUMINARY_RHI_ERROR_SEVERITY_SUCCESS) {
        printf("Error: %s", error.message);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    while (!window->should_close()) {
        window->poll_events();
    }
    delete window;
    
    lrhi_destroy_swap_chain(swap_chain);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_device(device);
}
