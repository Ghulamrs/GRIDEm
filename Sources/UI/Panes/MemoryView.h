// =============================================================================
//  MemoryView.h - Memory (GRIDEd's MemoryPane in UI/Panes/DebugPanes.h)
//  256 bytes from an address, 16 to a row, hex then ASCII. Until an address is
//  typed it shows where the stop's first variable lives; a typed address is
//  kept from stop to stop.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Services/Debugger.h"
#include <vector>

@interface GRMemoryView : GRPaneController <NSTextFieldDelegate>
- (instancetype)init;                                     // "Memory", "Memory"
// reads memory from the session; nil (or empty) when there is none there
@property (nonatomic, copy) NSData* (^readMemory)(unsigned long long address, NSInteger length);
// the program paused: locals give the first address when none was typed, backend
// names the engine for "(no memory at ... from X)"
- (void)refreshPaused:(BOOL)paused locals:(const std::vector<GRIDEm::Services::DebugVariable>&)locals
              backend:(NSString*)backend;
- (void)clearView;
@end
