# Swap chain for Luminary RHI

Luminary RHI is an RHI that targets D3D12/Vulkan/Metal3/Metal4.

Here is how each API handles swapchain:
- D3D12 swapchains are automatically synchronized with command queue, resize does not require recreation
- Vulkan swapchains are synchronized using binary semaphores only (fence system uses timeline semaphore so maybe try and have a work around)
- Metal has no frame in flight system and is automatically synchronized
- Metal4 has frame in flight system and synchronizes with cmd_queue.signalDrawable, cmd_queue.waitDrawable, drawable.present.

Knowing that, please design a comprehensive swapchain handle for the RHI that can take the following window handles:
- HWND
- X11
- xcb
- wayland
- metal layer

And implement the Metal3 and Metal4 backends.

Files to modify:
- src/luminary_rhi.h
- src/luminary_rhi_internal.h
- src/luminary_rhi.c
- src/luminary_rhi_metal3.m
- src/luminary_rhi_metal4.m
