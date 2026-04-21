#include "window_linux.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(LRHI_LINUX)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#endif

#include "../ext/imgui/imgui.h"
#include "../ext/imgui/backends/imgui_impl_glfw.h"

namespace {
	constexpr int kInitialClientWidth = 1280;
	constexpr int kInitialClientHeight = 720;
	constexpr const char* kWindowTitle = "Luminary RHI Example";

	void glfw_error_callback(int error, const char* description)
	{
		(void)error;
		(void)description;
	}
}

LinuxWindow::LinuxWindow()
	: window(nullptr)
	, escape_pressed(false)
{
	static bool glfw_initialized = false;
	if (!glfw_initialized) {
		if (!glfwInit()) {
			return;
		}
		glfw_initialized = true;
	}

	glfwSetErrorCallback(glfw_error_callback);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);

	window = glfwCreateWindow(kInitialClientWidth, kInitialClientHeight, kWindowTitle, nullptr, nullptr);
	if (!window) {
		return;
	}

	glfwSetWindowUserPointer(window, this);
	glfwSetKeyCallback(window, LinuxWindow::key_callback);

	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}
}

LinuxWindow::~LinuxWindow()
{
	if (window) {
		glfwDestroyWindow(window);
		window = nullptr;
	}
}

bool LinuxWindow::should_close() const
{
	return !window || glfwWindowShouldClose(window);
}

void LinuxWindow::poll_events()
{
	glfwPollEvents();
}

bool LinuxWindow::consume_escape_pressed()
{
	const bool was_pressed = escape_pressed;
	escape_pressed = false;
	return was_pressed;
}

void LinuxWindow::get_width_and_height(int* width, int* height) const
{
	if (!window) {
		*width = 0;
		*height = 0;
		return;
	}
	glfwGetFramebufferSize(window, width, height);
}

void* LinuxWindow::get_swap_chain_handle() const
{
#if defined(LRHI_LINUX)
	return (void*)(uintptr_t)glfwGetWaylandWindow(window);
#else
	return nullptr;
#endif
}

void* LinuxWindow::get_native_view_handle() const
{
	return (void*)window;
}

void LinuxWindow::configure_swap_chain_info(LRHISwapChainInfo* info) const
{
	info->handle_type = LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_WAYLAND;
	info->handle.wayland_surface = get_swap_chain_handle();
}

bool LinuxWindow::init_imgui()
{
	return ImGui_ImplGlfw_InitForVulkan(window, true);
}

void LinuxWindow::new_imgui_frame()
{
	ImGui_ImplGlfw_NewFrame();
}

void LinuxWindow::shutdown_imgui()
{
	ImGui_ImplGlfw_Shutdown();
}

void LinuxWindow::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	(void)scancode;
	(void)mods;
	LinuxWindow* self = static_cast<LinuxWindow*>(glfwGetWindowUserPointer(window));
	if (self && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		self->escape_pressed = true;
	}
}
