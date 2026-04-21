#include "window_macos.h"

#include "../ext/imgui/backends/imgui_impl_osx.h"

bool MacOSWindow::init_imgui()
{
    return ImGui_ImplOSX_Init((NSView*)window.contentView);
}

void MacOSWindow::new_imgui_frame()
{
    ImGui_ImplOSX_NewFrame((NSView*)window.contentView);
}

void MacOSWindow::shutdown_imgui()
{
    ImGui_ImplOSX_Shutdown();
}

void MacOSWindow::configure_swap_chain_info(LRHISwapChainInfo* info) const
{
    info->handle_type = LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER;
    info->handle.metal_layer = get_swap_chain_handle();
}

MacOSWindow::MacOSWindow()
    : escape_pressed(false)
{
    // 1. Initialize the Application
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    
    // 2. Set up a minimal Menu Bar to handle Cmd+Q
    NSMenu* menubar = [[NSMenu alloc] init];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [menubar addItem:appMenuItem];
    [app setMainMenu:menubar];
    
    NSMenu* appMenu = [[NSMenu alloc] init];
    NSString* appName = [[NSProcessInfo processInfo] processName];
    NSString* quitTitle = [@"Quit " stringByAppendingString:appName];
    
    // Wire up standard terminate command to Cmd+Q
    NSMenuItem* quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                          action:@selector(terminate:)
                                                   keyEquivalent:@"q"];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];

    // 3. Initialize the Window
    NSRect frame = NSMakeRect(0, 0, 1280, 720);
    window = [[NSWindow alloc] initWithContentRect:frame
                                   styleMask:(NSWindowStyleMaskTitled | 
                                              NSWindowStyleMaskClosable | 
                                              NSWindowStyleMaskResizable |
                                              NSWindowStyleMaskMiniaturizable)
                                     backing:NSBackingStoreBuffered
                                       defer:NO];
    [window setTitle:@"Luminary RHI Example"];
    
    // 4. Set up the display layer for Metal
    metal_layer = [CAMetalLayer layer];
    NSView* contentView = window.contentView;
    contentView.wantsLayer = YES;
    contentView.layer = metal_layer;
    metal_layer.contentsScale = window.backingScaleFactor;
    update_drawable_size();

    // 5. Present the Window
    [window makeKeyAndOrderFront:nil];
    [window center];
    [app activateIgnoringOtherApps:YES];
    
    // Mark the app as finished launching explicitly since we aren't using the standard NSApp.run
    [app finishLaunching];
}

void MacOSWindow::update_drawable_size() const
{
    if (!window || ![window isVisible])
        return;

    NSView* contentView = window.contentView;
    if (!contentView)
        return;

    NSRect backingRect = [contentView convertRectToBacking:contentView.bounds];
    CGSize drawableSize = CGSizeMake(backingRect.size.width, backingRect.size.height);
    metal_layer.contentsScale = window.backingScaleFactor;
    metal_layer.drawableSize = drawableSize;
}

MacOSWindow::~MacOSWindow()
{
    if (window) {
        [window close];
        window = nil;
    }
}

bool MacOSWindow::should_close() const
{
    if (!window)
        return true;

    return [window isVisible] == NO;
}

void MacOSWindow::poll_events()
{
    if (!window || ![window isVisible])
        return;

    // Proper non-blocking event pump
    NSEvent* event;
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES]))
    {
        if (event.type == NSEventTypeKeyDown && event.keyCode == 53) {
            escape_pressed = true;
        }
        [NSApp sendEvent:event];
    }
    
    [NSApp updateWindows];
    update_drawable_size();
}

bool MacOSWindow::consume_escape_pressed()
{
    bool was_pressed = escape_pressed;
    escape_pressed = false;
    return was_pressed;
}

void MacOSWindow::get_width_and_height(int* width, int* height) const
{
    if (!window || ![window isVisible]) {
        *width = 0;
        *height = 0;
        return;
    }

    update_drawable_size();
    CGSize drawableSize = metal_layer.drawableSize;
    *width = static_cast<int>(drawableSize.width);
    *height = static_cast<int>(drawableSize.height);
}
