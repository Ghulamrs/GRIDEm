// =============================================================================
//  AppDelegate.mm - see AppDelegate.h
// =============================================================================
#import "AppDelegate.h"
#import "Common.h"
#import "Commands.h"
#import "MainWindow.h"
#import "Session.h"
#import "Welcome.h"
#include "../Platform/Paths.h"

using namespace GRIDEm;

@implementation GRAppDelegate
{
    GRSession* _session;
    GRWelcomeWindow* _welcome;
    NSMutableArray<NSString*>* _pending;     // what was asked to open before the launch ended
    BOOL _launched;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil) _pending = [NSMutableArray array];
    return self;
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    _session = [[GRSession alloc] init];
    _mainWindow = [[GRMainWindowController alloc] initWithSession:_session];
    GRCommandRegistry* r = GRCommandRegistry.shared;
    NSApp.mainMenu = [r buildMainMenu];
    [_mainWindow applyKeySet];
    [_mainWindow hookMenus:NSApp.mainMenu];
    __weak GRMainWindowController* weakWindow = _mainWindow;
    // two-key chords ("Ctrl+K,C"): the status line says the first key was pressed, as GRIDEd's does
    [r startChordMonitor:^(NSString* status) { [weakWindow setStatus:status]; }];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    _mainWindow.window.restorable = NO;
    [_mainWindow showWindow:nil];
    [_mainWindow.window makeKeyAndOrderFront:nil];
    [_mainWindow reportToolchain];
    __weak GRAppDelegate* weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        GRAppDelegate* me = weakSelf;
        if (me == nil) return;
        [me.mainWindow restoreLayout];       // the split positions once the window has its size
        [me openAtLaunch];
    });
}

// what to open first: the files Finder or the command line named, else the last workspace, else the Welcome window
- (void)openAtLaunch
{
    _launched = YES;
    NSArray<NSString*>* args = [NSProcessInfo processInfo].arguments;
    for (NSUInteger i = 1; i < args.count; i++)
    {
        NSString* a = args[i];
        if ([a hasPrefix:@"-psn"]) continue;            // the process serial number an old launch passes
        if ([a hasPrefix:@"-"]) { i++; continue; }      // a flag (-NSDocumentRevisionsDebugMode YES ...) and its value
        if ([[NSFileManager defaultManager] fileExistsAtPath:a] && ![_pending containsObject:a]) [_pending addObject:a];
    }
    if (_pending.count > 0)
    {
        for (NSString* p in _pending) [_mainWindow openPath:p];
        [_pending removeAllObjects];
        return;
    }
    Settings::GrideSettingsPtr s = _session.settings;
    if (s->Env->ReopenLastWorkspace && !s->LastWorkspace.empty() && Platform::Paths::Exists(s->LastWorkspace))
    {
        [_mainWindow openPath:[NSString stringWithUTF8String:s->LastWorkspace.c_str()]];
        [_mainWindow setStatus:@"Ready"];
        return;
    }
    [self showWelcome];
}

- (void)showWelcome
{
    if (_welcome == nil)
    {
        _welcome = [[GRWelcomeWindow alloc] init];
        __weak GRAppDelegate* weakSelf = self;
        _welcome.commandRequested = ^(NSString* commandId) {
            GRAppDelegate* me = weakSelf;
            [me closeWelcome];
            [GRCommandRegistry.shared execute:commandId];
        };
        _welcome.openRequested = ^(NSString* path) {
            GRAppDelegate* me = weakSelf;
            [me closeWelcome];
            [me.mainWindow openPath:path];
        };
    }
    [_welcome showWithRecent:_session.settings->Recent];
}

- (void)closeWelcome { [_welcome close]; }

- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls
{
    (void)application;
    for (NSURL* u in urls)
    {
        if (u.path == nil) continue;
        if (!_launched) { [_pending addObject:u.path]; continue; }
        [self closeWelcome];
        [_mainWindow openPath:u.path];
    }
}

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication*)sender { (void)sender; return NO; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { (void)sender; return NO; }
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app { (void)app; return YES; }

// the dock icon clicked with no window on screen: the main window comes back
- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag
{
    (void)sender;
    if (!flag) [_mainWindow showWindow:nil];
    return YES;
}

// quitting asks the window: modified documents, a running build, the workspace and the settings saved
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    if (_mainWindow == nil) return NSTerminateNow;
    [_mainWindow reviewBeforeQuit:^(BOOL mayQuit) { [NSApp replyToApplicationShouldTerminate:mayQuit]; }];
    return NSTerminateLater;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    [GRCommandRegistry.shared stopChordMonitor];
}
@end
