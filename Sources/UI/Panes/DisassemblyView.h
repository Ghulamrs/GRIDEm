// =============================================================================
//  DisassemblyView.h - the assembly the compiler wrote for a source
//  (GRIDEd's DisassemblyPane in UI/Panes/DebugPanes.h), with the current
//  statement's line marked with an arrow when the window knows it.
// =============================================================================
#pragma once
#import "PaneHost.h"

@interface GRDisassemblyView : GRPaneController
- (instancetype)init;                                     // "Disassembly", "Disassembly"
// asmPath: the generated assembly of the source (may not exist yet)
- (void)showFile:(NSString*)source assembly:(NSString*)asmPath;
// the 1-based line of the assembly to mark (0: none), scrolled to
- (void)setCurrentLine:(NSInteger)line;
- (void)clearView;
@end
