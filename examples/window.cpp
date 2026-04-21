#include "window.h"

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if defined(TARGET_OS_MAC)
        #include "platform/window_macos.h"
    #endif
#endif

Window* Window::create()
{
#if defined(__APPLE__)
#if defined(TARGET_OS_MAC)
    return new MacOSWindow();
#else
    #error "Unsupported Apple platform"
#endif
#else
    return nullptr;
#endif
}
