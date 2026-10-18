// =============================================================================
//  StackView.h - the Call Stack (GRIDEd's CallStackPane in UI/Panes/DebugPanes.h)
//  Function | File | Line, the frame looked at in bold on its own colour; a
//  double-click (or Return) looks at that frame, the top line comes back to the stop.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Services/Debugger.h"
#include <vector>

@interface GRStackView : GRPaneController <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)init;                                     // "CallStack", "Call Stack"
@property (nonatomic, copy) void (^frameChosen)(NSInteger index);
// empty when the program is not paused
- (void)setFrames:(const std::vector<GRIDEm::Services::DebugFrame>&)frames current:(NSInteger)current;
- (void)clearView;
@end
