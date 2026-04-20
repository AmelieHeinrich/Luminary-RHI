#include "window_macos.h"

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
    NSView* contentView = window.contentView;
    NSRect backingRect = [contentView convertRectToBacking:contentView.bounds];
    CGSize drawableSize = CGSizeMake(backingRect.size.width, backingRect.size.height);
    metal_layer.contentsScale = window.backingScaleFactor;
    metal_layer.drawableSize = drawableSize;
}

MacOSWindow::~MacOSWindow()
{
    [window close];
}

bool MacOSWindow::should_close() const
{
    return [window isVisible] == NO;
}

void MacOSWindow::poll_events()
{
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
    update_drawable_size();
    CGSize drawableSize = metal_layer.drawableSize;
    *width = static_cast<int>(drawableSize.width);
    *height = static_cast<int>(drawableSize.height);
}
