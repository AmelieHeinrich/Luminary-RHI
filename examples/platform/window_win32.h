#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "examples/window.h"

#include <windows.h>

class Win32Window : public Window
{
public:
	Win32Window();
	~Win32Window() override;

	static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool should_close() const override;
	void poll_events() override;
	bool consume_escape_pressed() override;
	void get_width_and_height(int* width, int* height) const override;
	void* get_swap_chain_handle() const override { return hwnd; }
	void* get_native_view_handle() const override { return hwnd; }
	void configure_swap_chain_info(LRHISwapChainInfo* info) const override;
	bool init_imgui() override;
	void new_imgui_frame() override;
	void shutdown_imgui() override;

private:
	HWND hwnd;
	bool escape_pressed;
	bool close_requested;
};
