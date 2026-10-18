// =============================================================================
//  main.mm - GRIDEm's start (GRIDEd's Main.cpp): the application object, its
//  delegate, and the run loop. No nib: the menu bar and the window are made in code.
// =============================================================================
#import <AppKit/AppKit.h>
#import "AppDelegate.h"
#import "MainWindow.h"

int main(int argc, const char* argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        GRAppDelegate* delegate = [[GRAppDelegate alloc] init];   // held here for as long as the app runs
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
