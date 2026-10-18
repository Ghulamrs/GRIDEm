// =============================================================================
//  FindResultsView.h - Find Results (GRIDEd's FindResultsPane in UI/Panes/OutputPanes.h)
//  "Find all "text"   -   N match(es) in M file(s)", then File | Line | Text;
//  double-click goes there.
// =============================================================================
#pragma once
#import "PaneHost.h"

@interface GRFindResultsView : GRPaneController <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)init;                                     // "FindResults", "Find Results"
@property (nonatomic, copy) void (^navigate)(NSString* file, NSInteger line);
- (void)begin:(NSString*)what;
- (void)addFile:(NSString*)file line:(NSInteger)line text:(NSString*)text;
- (void)end:(NSInteger)files;
@end
