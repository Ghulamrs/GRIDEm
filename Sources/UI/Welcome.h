// =============================================================================
//  Welcome.h - the window shown when nothing is open (GRIDEd's StartPage in
//  UI/Controls/StartPage.h, as a window of its own, like Xcode's welcome)
//  "GRIDEm" with its languages and targets; Start: New Project, Open Workspace /
//  Project, Open File, Import CCS Project; Recent: the recent workspaces;
//  Samples: the workspaces shipped in the app's Resources/samples.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../Settings/GrideSettings.h"

@interface GRWelcomeWindow : NSWindowController
- (instancetype)init;
// a command id from the Start list ("File.NewProject" ...)
@property (nonatomic, copy) void (^commandRequested)(NSString* commandId);
// a workspace or project to open: a recent one or a sample
@property (nonatomic, copy) void (^openRequested)(NSString* path);
// fills the lists and shows the window
- (void)showWithRecent:(const GRIDEm::Settings::RecentListPtr&)recent;
// the sample workspaces (.grws) under Resources/samples, newest names first
+ (NSArray<NSString*>*)samples;
@end
