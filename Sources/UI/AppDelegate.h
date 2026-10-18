// =============================================================================
//  AppDelegate.h - GRAppDelegate: the application (GRIDEd's Main.cpp and the
//  start / end of MainForm: OnShown, OnFormClosing)
//  It makes the session and the main window, builds the menu bar from
//  GRCommandRegistry, opens what Finder hands it (.grws, .pro, .sln, .vcxproj,
//  a CCS folder, sources), shows the Welcome window when nothing is open, and
//  asks the window before the app quits.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

@class GRMainWindowController;

@interface GRAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, readonly) GRMainWindowController* mainWindow;
@end
