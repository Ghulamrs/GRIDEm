// =============================================================================
//  TerminalView.h - the Terminal pane (GRIDEd's TerminalPane in UI/Panes/OutputPanes.h)
//  GRIDEd runs cmd.exe on pipes; GRIDEm runs the user's shell ($SHELL -l, else
//  /bin/zsh) on a pseudo-terminal in the workspace folder. What it writes is
//  shown as plain text: line ends, carriage return, backspace, "erase line" and
//  "clear screen" are followed, colours and other escape sequences dropped.
//  Typing in the pane goes straight to the shell, as in a terminal.
//  The shell starts the first time the pane is shown.
// =============================================================================
#pragma once
#import "PaneHost.h"

@interface GRTerminalView : GRPaneController
- (instancetype)init;                                     // "Terminal", "Terminal"
@property (nonatomic, copy) NSString* workingDirectory;   // where the next shell starts
- (void)startShell;
- (void)stopShell;
- (void)restart;
- (void)clear;
// types text into the shell (Tools > Open in Terminal here: "cd <folder>\n")
- (void)send:(NSString*)text;
@property (nonatomic, readonly) BOOL running;
@end
