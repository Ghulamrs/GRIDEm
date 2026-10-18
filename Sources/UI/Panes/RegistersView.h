// =============================================================================
//  RegistersView.h - Registers (GRIDEd's RegistersPane in UI/Panes/DebugPanes.h)
//  Two registers to a row; a value changed since the last stop in the error colour.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Services/Debugger.h"
#include <vector>

@interface GRRegistersView : GRPaneController <NSTableViewDataSource, NSTableViewDelegate>
- (instancetype)init;                                     // "Registers", "Registers"
// backend names the program behind the engine, for "(no registers from X)"
- (void)setRegisters:(const std::vector<GRIDEm::Services::RegisterValue>&)registers backend:(NSString*)backend;
- (void)clearView;
@end
