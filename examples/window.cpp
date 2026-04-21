#include "window.h"

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if defined(TARGET_OS_MAC)
        #include "platform/window_macos.h"
    #endif
#elif defined(_WIN32)
    #include "platform/window_win32.h"
#endif

Window* Window::create()
{
#if defined(__APPLE__)
#if defined(TARGET_OS_MAC)
    return new MacOSWindow();
#else
    #error "Unsupported Apple platform"
#endif
#elif defined(_WIN32)
    Window* window = new Win32Window();
    if (!window->get_native_view_handle()) {
        delete window;
        return nullptr;
    }
    return window;
#else
    return nullptr;
#endif
}
