#include "window_win32.h"

#include "../ext/imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
	constexpr const wchar_t* kWindowClassName = L"LuminaryRHIExampleWindowClass";
	constexpr const wchar_t* kWindowTitle = L"Luminary RHI Example";
	constexpr int kInitialClientWidth = 1280;
	constexpr int kInitialClientHeight = 720;

	void register_window_class()
	{
		static bool registered = false;
		if (registered)
			return;

		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = Win32Window::wnd_proc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = kWindowClassName;

		if (!RegisterClassExW(&wc)) {
			const DWORD error = GetLastError();
			if (error != ERROR_CLASS_ALREADY_EXISTS)
				return;
		}

		registered = true;
	}
}

Win32Window::Win32Window()
	: hwnd(nullptr)
	, escape_pressed(false)
	, close_requested(false)
{
	register_window_class();

	DWORD style = WS_OVERLAPPEDWINDOW;
	RECT rect = { 0, 0, kInitialClientWidth, kInitialClientHeight };
	AdjustWindowRectEx(&rect, style, FALSE, 0);

	const int window_width = rect.right - rect.left;
	const int window_height = rect.bottom - rect.top;
	const int screen_width = GetSystemMetrics(SM_CXSCREEN);
	const int screen_height = GetSystemMetrics(SM_CYSCREEN);
	const int window_x = (screen_width - window_width) / 2;
	const int window_y = (screen_height - window_height) / 2;

	hwnd = CreateWindowExW(
		0,
		kWindowClassName,
		kWindowTitle,
		style,
		window_x,
		window_y,
		window_width,
		window_height,
		nullptr,
		nullptr,
		GetModuleHandleW(nullptr),
		this);

	if (!hwnd)
		return;

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
}

Win32Window::~Win32Window()
{
	if (hwnd) {
		DestroyWindow(hwnd);
		hwnd = nullptr;
	}
}

bool Win32Window::should_close() const
{
	if (close_requested || !hwnd || !IsWindow(hwnd))
		return true;

	return IsWindowVisible(hwnd) == FALSE;
}

void Win32Window::poll_events()
{
	MSG msg = {};
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			close_requested = true;
			continue;
		}

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

bool Win32Window::consume_escape_pressed()
{
	const bool was_pressed = escape_pressed;
	escape_pressed = false;
	return was_pressed;
}

void Win32Window::get_width_and_height(int* width, int* height) const
{
	if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
		*width = 0;
		*height = 0;
		return;
	}

	RECT rect = {};
	if (!GetClientRect(hwnd, &rect)) {
		*width = 0;
		*height = 0;
		return;
	}

	*width = rect.right - rect.left;
	*height = rect.bottom - rect.top;
}

void Win32Window::configure_swap_chain_info(LRHISwapChainInfo* info) const
{
	info->handle_type = LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_HWND;
	info->handle.hwnd = get_swap_chain_handle();
}

bool Win32Window::init_imgui()
{
	return ImGui_ImplWin32_Init(hwnd);
}

void Win32Window::new_imgui_frame()
{
	ImGui_ImplWin32_NewFrame();
}

void Win32Window::shutdown_imgui()
{
	ImGui_ImplWin32_Shutdown();
}

LRESULT CALLBACK Win32Window::wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Win32Window* window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	if (msg == WM_NCCREATE) {
		const CREATESTRUCTW* create_struct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
		window = static_cast<Win32Window*>(create_struct->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
		if (window)
			window->hwnd = hwnd;
	}

	if (window && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_ESCAPE)
		window->escape_pressed = true;

	if (ImGui::GetCurrentContext() != nullptr) {
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
			return 1;
	}

	if (window) {
		switch (msg) {
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			break;

		case WM_CLOSE:
			window->close_requested = true;
			DestroyWindow(hwnd);
			return 0;

		case WM_DESTROY:
			window->close_requested = true;
			window->hwnd = nullptr;
			PostQuitMessage(0);
			return 0;
		}
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}
