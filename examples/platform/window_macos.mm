#include "window_macos.h"

MacOSWindow::MacOSWindow()
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
    
    // 5. Present the Window
    [window makeKeyAndOrderFront:nil];
    [window center];
    [app activateIgnoringOtherApps:YES];
    
    // Mark the app as finished launching explicitly since we aren't using the standard NSApp.run
    [app finishLaunching];
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
        [NSApp sendEvent:event];
    }
    
    [NSApp updateWindows];
}

void MacOSWindow::get_width_and_height(int* width, int* height) const
{
    NSRect bounds = [window.contentView bounds];
    *width = (int)bounds.size.width;
    *height = (int)bounds.size.height;
}
