// =============================================================================
//  BreakpointsNavigator.h - every breakpoint of the workspace, File | Line
//  (GRIDEd's BreakpointsPane in UI/Panes/DebugPanes.h). Double-click goes to
//  it, Delete takes it away.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Model/Workspace.h"

@interface GRBreakpointsNavigator : GRPaneController <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)init;                                     // "Breakpoints", "Breakpoints"
@property (nonatomic, copy) void (^navigate)(NSString* file, NSInteger line);
@property (nonatomic, copy) void (^removeRequested)(NSString* file, NSInteger line);
- (void)fill:(const GRIDEm::Model::BreakpointTable&)breakpoints;
@end
