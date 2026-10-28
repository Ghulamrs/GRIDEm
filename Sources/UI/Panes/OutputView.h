// =============================================================================
//  OutputView.h - the Output pane (GRIDEd's OutputPane in UI/Panes/OutputPanes.h)
//  Three sources - Build, Debug, Tools - each with its own lines (the newest
//  20,000 kept); the one shown is picked from the popup. Lines are coloured as
//  GRIDEd colours them (errors, warnings, the "------" summary); terminal
//  colour codes are taken out. While a program runs inside the IDE an input
//  line under the text hands what is typed to it. A double-click on a line that
//  names a place (a diagnostic, "file:line:col:", "file(line,col):") goes there.
//  Append may be called from any thread; it is moved to the main thread.
// =============================================================================
#pragma once
#import "PaneHost.h"

@interface GROutputView : GRPaneController <NSTextFieldDelegate>
- (instancetype)init;                                     // "Output", "Output"

@property (nonatomic, copy) void (^navigate)(NSString* file, NSInteger line, NSInteger column);
@property (nonatomic, copy) void (^inputEntered)(NSString* line);   // a line typed for the running program

@property (nonatomic, readonly) NSString* currentSource;           // "Build", "Debug", "Tools"
// shown while a program runs; it takes the focus when shown
@property (nonatomic, assign) BOOL inputEnabled;
// shown for a program under the debugger without the focus, so F10 and F11 go on working in the editor
- (void)showInput:(BOOL)on focus:(BOOL)focus;
// a line typed is shown as typed; off for a program on the debugger's console, which echoes it
@property (nonatomic, assign) BOOL echoInput;

- (void)append:(NSString*)text source:(NSString*)source;           // one line, any thread
// text added to the source's last line: the rest of a line the program had not finished
- (void)continueLine:(NSString*)text source:(NSString*)source;
- (void)clear:(NSString*)source;
- (void)showSource:(NSString*)source;
// the lines of a source, for Copy / a report
- (NSArray<NSString*>*)lines:(NSString*)source;
// takes ANSI escape sequences out of a line
+ (NSString*)plainText:(NSString*)line;
@end
