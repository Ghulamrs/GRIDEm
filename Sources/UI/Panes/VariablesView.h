// =============================================================================
//  VariablesView.h - Locals and Watch (GRIDEd's LocalsPane and WatchPane in
//  UI/Panes/DebugPanes.h), RIDE's Name | Value | Type | Address.
//
//  Locals  the stop line on top ("stopped at f.c:11 in main", and what frame is
//          looked at), the variables, the engine's reason in brackets when there
//          are none, the keys line at the bottom. A value can be edited in place,
//          or "name = value" typed in the line above the keys (Return); a
//          double-click fills that line.
//  Watch   the expressions are kept here, so a new session gets them again; the
//          line at the bottom adds one (Return); a double-click brings one there
//          to be changed, and an empty answer takes it away; Delete takes one away.
//  A value changed since the last stop is shown in the error colour, in bold.
//  The panes take their data through the setters; what the user asks goes out
//  through the blocks.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Services/Debugger.h"
#include <vector>

typedef NS_ENUM(NSInteger, GRVariablesKind) { GRVariablesLocals = 0, GRVariablesWatch };

@interface GRVariablesView : GRPaneController <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>

- (instancetype)initWithKind:(GRVariablesKind)kind;       // "Locals" / "Watch"
@property (nonatomic, readonly) GRVariablesKind kind;

// ---- Locals --------------------------------------------------------------------
// variables at a stop: frameName keys the changed-value marks; atStop is true when the
// top frame is looked at (only then are values remembered for the next stop)
- (void)setStopLine:(NSString*)stopLine;
- (void)setVariables:(const std::vector<GRIDEm::Services::DebugVariable>&)variables
           frameName:(NSString*)frameName atStop:(BOOL)atStop whyNone:(NSString*)whyNone;
@property (nonatomic, copy) NSString* keysLine;
@property (nonatomic, copy) void (^setRequested)(NSString* name, NSString* value);
- (void)focusSet;

// ---- Watch ---------------------------------------------------------------------
@property (nonatomic, readonly) NSArray<NSString*>* expressions;
- (void)setExpressions:(NSArray<NSString*>*)expressions;
// the engine's watches; marked when the program is paused at the top frame
- (void)setWatches:(const std::vector<GRIDEm::Services::DebugWatch>&)watches paused:(BOOL)paused atStop:(BOOL)atStop;
@property (nonatomic, copy) void (^watchAdded)(NSString* expression);
@property (nonatomic, copy) void (^watchChanged)(NSInteger index, NSString* expression);
@property (nonatomic, copy) void (^watchRemoved)(NSInteger index);
- (void)focusAdd;

// ---- both ----------------------------------------------------------------------
- (void)newStop;              // the values seen become "before" for the next stop
- (void)clearView;            // the session ended
@end
