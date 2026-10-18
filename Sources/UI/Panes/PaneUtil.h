// =============================================================================
//  PaneUtil.h - what every pane uses (GRIDEd's PaneUtil in UI/Panes/OutputPanes.h):
//  UTF-8 <-> NSString, the fixed-width font, a table with columns, a bar
//  along the top of a pane, the "muted" text colour.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include <string>

#import "../Common.h"      // GRNS / GRStd

@interface GRPaneUtil : NSObject
+ (NSFont*)monoFont;                                       // the editor's font family at 11 pt
+ (NSFont*)boldMonoFont;
// a view-based table in a scroll view; columns: identifier -> title, widths in points
+ (NSTableView*)tableWithColumns:(NSArray<NSString*>*)titles widths:(NSArray<NSNumber*>*)widths
                          scroll:(NSScrollView**)scroll;
+ (NSOutlineView*)outlineWithColumns:(NSArray<NSString*>*)titles widths:(NSArray<NSNumber*>*)widths
                              scroll:(NSScrollView**)scroll;
// a read-only, non-wrapping text view in a scroll view
+ (NSTextView*)textViewWithScroll:(NSScrollView**)scroll;
// a thin bar of controls along the top of a pane
+ (NSStackView*)barWithViews:(NSArray<NSView*>*)views;
+ (NSTextField*)label:(NSString*)text;
+ (NSTextField*)inputFieldWithPlaceholder:(NSString*)placeholder;
// a cell text field for a view-based table, reused by identifier
+ (NSTableCellView*)cellIn:(NSTableView*)table identifier:(NSString*)identifier;
// lays a view to fill its container
+ (void)fill:(NSView*)container with:(NSView*)view;
// a stack of views top to bottom filling the container (the last given the spare height
// unless another is named)
+ (void)stack:(NSArray<NSView*>*)views in:(NSView*)container grow:(NSView*)grow;
@end
