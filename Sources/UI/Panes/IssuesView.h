// =============================================================================
//  IssuesView.h - the Error List (GRIDEd's ErrorListPane in UI/Panes/OutputPanes.h)
//  Severity | Code | Description | Project | File | Line, with the three
//  toggles "N Errors", "N Warnings", "N Messages" filtering what is shown.
//  Double-click (or Return) goes to the place.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Services/BuildMessage.h"

@interface GRIssuesView : GRPaneController <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)init;                                     // "ErrorList", "Error List"
@property (nonatomic, copy) void (^navigate)(NSString* file, NSInteger line, NSInteger column);
// told whenever the counts change (the status line, the window's issue badge)
@property (nonatomic, copy) void (^countsChanged)(NSInteger errors, NSInteger warnings, NSInteger messages);
- (void)clear;
- (void)add:(const GRIDEm::Services::BuildMessagePtr&)message;     // main thread
@property (nonatomic, readonly) NSInteger errorCount;
@property (nonatomic, readonly) NSInteger warningCount;
@property (nonatomic, readonly) NSInteger messageCount;
// every message, for the editor's marks
- (std::vector<GRIDEm::Services::BuildMessagePtr>)messages;
@end
