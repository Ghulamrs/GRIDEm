// =============================================================================
//  GoToLinePopover.h - Find > Go to Line ⌘L (GRIDEd's GoToLineDialog, as a popover)
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

@interface GRGoToLinePopover : NSViewController <NSPopoverDelegate>
// shows a popover under the top of view; done gets the line (1-based) on Return
+ (void)showFrom:(NSView*)view current:(NSInteger)current max:(NSInteger)max done:(void (^)(NSInteger line))done;
@end
