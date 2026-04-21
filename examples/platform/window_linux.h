#pragma once

#include "examples/window.h"

struct GLFWwindow;

class LinuxWindow : public Window
{
public:
	LinuxWindow();
	~LinuxWindow() override;

	bool should_close() const override;
	void poll_events() override;
	bool consume_escape_pressed() override;
	void get_width_and_height(int* width, int* height) const override;
	void* get_swap_chain_handle() const override;
	void* get_native_view_handle() const override;
	void configure_swap_chain_info(LRHISwapChainInfo* info) const override;
	bool init_imgui() override;
	void new_imgui_frame() override;
	void shutdown_imgui() override;

private:
	GLFWwindow* window;
	bool escape_pressed;

    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
};
