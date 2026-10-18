// =============================================================================
//  Icons.h - GRIcons: every icon GRIDEm shows (GRIDEd's UI/IconFactory.h)
//  GRIDEd draws all its icons in code; on the Mac they are SF Symbols, which
//  follow the appearance and the accent colour, and only what SF Symbols has no
//  picture for is drawn here: the C6747 chip and the Shalimar mark.
//  GRIDEd's icon names ("build", "stepinto", "projectccs" ...) are kept as keys.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

@interface GRIcons : NSObject

// the SF Symbol for one of GRIDEd's icon names; nil when there is none
+ (NSString*)symbolForIconName:(NSString*)iconName;
// a command's image: its GRIDEd icon name (or SF Symbol name) as a template image
+ (NSImage*)imageForCommand:(NSString*)commandId symbol:(NSString*)iconName;
// an icon by GRIDEd name, at a point size (template image, tinted by the control)
+ (NSImage*)image:(NSString*)iconName size:(CGFloat)pointSize;
// a file in the navigator, by extension (.c, .cpp, .shl, .asm, .cmd, .h, .pro, .grws ...)
+ (NSImage*)imageForFile:(NSString*)path;
// drawn in code
+ (NSImage*)chipImage:(CGFloat)size;          // TMS320C6747
+ (NSImage*)shalimarImage:(CGFloat)size;      // a Shalimar source
// a filled dot in a colour (breakpoint, dirty mark, status)
+ (NSImage*)dot:(NSColor*)color size:(CGFloat)size;
@end
