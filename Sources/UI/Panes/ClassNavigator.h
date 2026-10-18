// =============================================================================
//  ClassNavigator.h - Class View: each project's functions, types and macros,
//  found by a simple scan of its sources, as GRIDEd's ClassViewPane finds them
//  (UI/Panes/ExplorerPanes.h). A rescan keeps what was expanded; a group with
//  nothing in it is left out.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Model/Workspace.h"

@interface GRClassNavigator : GRPaneController <NSOutlineViewDataSource, NSOutlineViewDelegate>
- (instancetype)init;                                     // "ClassView", "Class View"
// double-click on a symbol: its file and 1-based line
@property (nonatomic, copy) void (^navigate)(NSString* file, NSInteger line);
// scans the workspace's sources (off the main thread) and shows what it found
- (void)scan:(const GRIDEm::Model::WorkspacePtr&)workspace;
@end
