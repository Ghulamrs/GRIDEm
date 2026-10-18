// =============================================================================
//  PaneHost.h - the panes and what holds them (GRIDEd's UI/Docking/DockArea.h:
//  ToolPane, DockArea, FloatWindow)
//
//  GRPane          a pane: an identifier ("Output", "ErrorList" - GRIDEd's names),
//                  a title, an SF Symbol and its view
//  GRPaneController the base most panes derive from
//  GRPaneHost      one area of the window (the navigator, the inspector, the two
//                  halves of the bottom area): a segmented header picks the pane
//                  shown, a menu button opens it in a window of its own or hides it
//  GRPaneWindow    "Open in Separate Window": a panel holding the pane; closing it
//                  puts the pane back where it was (GRIDEd's float / dock back)
//
//  Which panes are hidden is kept in LayoutSettings::HiddenPanes, as GRIDEd keeps it.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../../Settings/GrideSettings.h"

@protocol GRPane <NSObject>
@property (nonatomic, readonly, copy) NSString* paneIdentifier;
@property (nonatomic, readonly, copy) NSString* paneTitle;
@property (nonatomic, readonly, copy) NSString* paneSymbol;     // SF Symbol name
- (NSViewController*)paneViewController;
@optional
- (void)paneDidShow;                 // it became the visible pane of its host
- (void)applyTheme;                  // GRThemeDidChangeNotification
@end

// -----------------------------------------------------------------------------
@interface GRPaneController : NSViewController <GRPane>
- (instancetype)initWithIdentifier:(NSString*)identifier title:(NSString*)title symbol:(NSString*)symbol;
@property (nonatomic, readonly, copy) NSString* paneIdentifier;
@property (nonatomic, readonly, copy) NSString* paneTitle;
@property (nonatomic, readonly, copy) NSString* paneSymbol;
// called on GRThemeDidChangeNotification; the base sets the view's appearance
- (void)applyTheme;
@end

@class GRPaneHost;

// -----------------------------------------------------------------------------
@interface GRPaneWindow : NSPanel <NSWindowDelegate>
@property (nonatomic, strong, readonly) id<GRPane> pane;
@property (nonatomic, weak, readonly) GRPaneHost* host;
- (instancetype)initWithPane:(id<GRPane>)pane host:(GRPaneHost*)host;
@end

// -----------------------------------------------------------------------------
@interface GRPaneHost : NSViewController

// headerStyle: YES shows the segmented header with icons only (the navigator's
// switcher); NO shows titles (the bottom area)
- (instancetype)initWithName:(NSString*)name iconsOnly:(BOOL)iconsOnly;

@property (nonatomic, readonly, copy) NSString* name;                // "Navigator", "Inspector", "Bottom", "Debug"
@property (nonatomic, readonly) NSArray<id<GRPane>>* panes;            // in the order added
@property (nonatomic, readonly) id<GRPane> activePane;
// the host's panes changed (shown, hidden, floated, docked); the window keeps the
// layout and the View menu's checks from it
@property (nonatomic, copy) void (^layoutChanged)(GRPaneHost* host);

- (void)addPane:(id<GRPane>)pane hidden:(BOOL)hidden;
- (id<GRPane>)pane:(NSString*)identifier;                              // nil when not here
- (BOOL)hasPane:(NSString*)identifier;
// shows the pane (brings back a hidden one, brings its window forward when it floats)
- (void)showPane:(NSString*)identifier;
- (void)hidePane:(NSString*)identifier;
- (void)togglePane:(NSString*)identifier;
- (BOOL)isShown:(NSString*)identifier;                                 // floating counts as shown
- (BOOL)isFloating:(NSString*)identifier;
- (void)floatPane:(NSString*)identifier;
- (void)dockPane:(NSString*)identifier;
- (void)dockAll;                                                       // Window > Dock / Reset Layout

// LayoutSettings::HiddenPanes - the identifiers of this host's hidden panes
- (void)restoreHidden:(const std::vector<std::string>&)hiddenPanes;
- (void)collectHidden:(std::vector<std::string>&)hiddenPanes;
@end
