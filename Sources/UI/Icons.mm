// =============================================================================
//  Icons.mm - see Icons.h
// =============================================================================
#import "Icons.h"
#import "Common.h"

namespace {

    // GRIDEd icon name -> SF Symbol
    struct IconRow { const char* name; const char* symbol; };

    const IconRow kIcons[] = {
        { "new",          "doc.badge.plus" },
        { "workspace",    "square.stack.3d.up" },
        { "file",         "doc" },
        { "open",         "folder" },
        { "projectccs",   "cpu" },
        { "folder",       "folder.badge.plus" },
        { "folderopen",   "folder" },
        { "close",        "xmark" },
        { "save",         "square.and.arrow.down" },
        { "saveall",      "square.and.arrow.down.on.square" },
        { "undo",         "arrow.uturn.backward" },
        { "redo",         "arrow.uturn.forward" },
        { "cut",          "scissors" },
        { "copy",         "doc.on.doc" },
        { "paste",        "doc.on.clipboard" },
        { "find",         "magnifyingglass" },
        { "findfiles",    "doc.text.magnifyingglass" },
        { "comment",      "text.bubble" },
        { "uncomment",    "text.alignleft" },
        { "indent",       "increase.indent" },
        { "outdent",      "decrease.indent" },
        { "bookmark",     "bookmark" },
        { "nextbm",       "chevron.down.circle" },
        { "prevbm",       "chevron.up.circle" },
        { "navigator",    "sidebar.left" },
        { "inspector",    "sidebar.right" },
        { "bottomarea",   "rectangle.bottomthird.inset.filled" },
        { "explorer",     "folder" },
        { "classview",    "list.bullet.indent" },
        { "files",        "doc.on.doc" },
        { "properties",   "slider.horizontal.3" },
        { "output",       "text.alignleft" },
        { "errorlist",    "exclamationmark.triangle" },
        { "findresults",  "text.magnifyingglass" },
        { "terminal",     "terminal" },
        { "project",      "shippingbox" },
        { "refresh",      "arrow.clockwise" },
        { "info",         "info.circle" },
        { "settings",     "gearshape" },
        { "build",        "hammer" },
        { "rebuild",      "hammer.circle" },
        { "clean",        "trash" },
        { "stop",         "stop.fill" },
        { "debug",        "ladybug" },
        { "run",          "play.fill" },
        { "emulate",      "cpu" },
        { "restart",      "arrow.counterclockwise" },
        { "pause",        "pause.fill" },
        { "stepinto",     "arrow.down.to.line" },
        { "stepover",     "arrow.uturn.right" },
        { "stepout",      "arrow.up.to.line" },
        { "variable",     "x.squareroot" },
        { "breakpoint",   "arrowtriangle.right.fill" },
        { "watch",        "eye" },
        { "locals",       "list.bullet.rectangle" },
        { "callstack",    "square.stack" },
        { "registers",    "memorychip" },
        { "memory",       "square.grid.3x3" },
        { "disassembly",  "doc.plaintext" },
        { "convert",      "arrow.left.arrow.right" },
        { "help",         "questionmark.circle" },
        { "float",        "macwindow.on.rectangle" },
        { "split",        "rectangle.split.2x1" },
        { "pin",          "pin" },
    };
}

@implementation GRIcons

+ (NSString*)symbolForIconName:(NSString*)iconName
{
    if (iconName.length == 0) return nil;
    for (const IconRow& r : kIcons)
        if ([iconName isEqualToString:[NSString stringWithUTF8String:r.name]]) return [NSString stringWithUTF8String:r.symbol];
    return nil;
}

+ (NSImage*)image:(NSString*)iconName size:(CGFloat)pointSize
{
    if (iconName.length == 0) return nil;
    NSString* symbol = GROr([self symbolForIconName:iconName], iconName);   // an SF Symbol name passes through
    NSImage* img = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:iconName];
    if (img == nil) return nil;
    NSImageSymbolConfiguration* cfg = [NSImageSymbolConfiguration configurationWithPointSize:pointSize
                                                                                      weight:NSFontWeightRegular];
    NSImage* sized = [img imageWithSymbolConfiguration:cfg];
    sized.template = YES;
    return sized;
}

+ (NSImage*)imageForCommand:(NSString*)commandId symbol:(NSString*)iconName
{
    (void)commandId;
    return [self image:iconName size:15];
}

+ (NSImage*)imageForFile:(NSString*)path
{
    NSString* ext = path.pathExtension.lowercaseString;
    if ([ext isEqualToString:@"shl"] || [ext isEqualToString:@"shm"]) return [self shalimarImage:16];
    NSString* symbol = @"doc";
    if ([ext isEqualToString:@"c"] || [ext isEqualToString:@"cpp"] || [ext isEqualToString:@"cc"]
        || [ext isEqualToString:@"cxx"])
        symbol = @"doc.text";
    else if ([ext isEqualToString:@"h"] || [ext isEqualToString:@"hpp"] || [ext isEqualToString:@"inc"]
             || [ext isEqualToString:@"inl"])
        symbol = @"h.square";
    else if ([ext isEqualToString:@"asm"] || [ext isEqualToString:@"s"] || [ext isEqualToString:@"sa"])
        symbol = @"cpu";
    else if ([ext isEqualToString:@"cmd"] || [ext isEqualToString:@"lds"]) symbol = @"link";
    else if ([ext isEqualToString:@"pro"] || [ext isEqualToString:@"vcxproj"] || [ext isEqualToString:@"project"])
        symbol = @"shippingbox";
    else if ([ext isEqualToString:@"grws"] || [ext isEqualToString:@"sln"]) symbol = @"square.stack.3d.up";
    else if ([ext isEqualToString:@"txt"] || [ext isEqualToString:@"md"]) symbol = @"doc.plaintext";
    return [self image:symbol size:13];
}

// a square chip with pins on four sides and "C6x" on it
+ (NSImage*)chipImage:(CGFloat)size
{
    NSImage* img = [NSImage imageWithSize:NSMakeSize(size, size) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        CGFloat inset = size * 0.22;
        NSRect body = NSInsetRect(rect, inset, inset);
        [[NSColor blackColor] set];
        NSBezierPath* box = [NSBezierPath bezierPathWithRoundedRect:body xRadius:size * 0.06 yRadius:size * 0.06];
        box.lineWidth = MAX(1.0, size / 16.0);
        [box stroke];
        CGFloat pin = size * 0.14;
        for (int i = 0; i < 3; i++)
        {
            CGFloat t = body.origin.x + body.size.width * (0.25 + 0.25 * i);
            NSRectFill(NSMakeRect(t - size * 0.03, NSMinY(rect) + size * 0.06, size * 0.06, pin));
            NSRectFill(NSMakeRect(t - size * 0.03, NSMaxY(rect) - size * 0.06 - pin, size * 0.06, pin));
            CGFloat u = body.origin.y + body.size.height * (0.25 + 0.25 * i);
            NSRectFill(NSMakeRect(NSMinX(rect) + size * 0.06, u - size * 0.03, pin, size * 0.06));
            NSRectFill(NSMakeRect(NSMaxX(rect) - size * 0.06 - pin, u - size * 0.03, pin, size * 0.06));
        }
        NSDictionary* attrs = @{ NSFontAttributeName : [NSFont boldSystemFontOfSize:size * 0.22],
                                 NSForegroundColorAttributeName : [NSColor blackColor] };
        NSString* label = @"C6x";
        NSSize ls = [label sizeWithAttributes:attrs];
        [label drawAtPoint:NSMakePoint(NSMidX(body) - ls.width / 2, NSMidY(body) - ls.height / 2) withAttributes:attrs];
        return YES;
    }];
    img.template = YES;
    return img;
}

// a page with an ogee arch (the Shalimar gardens' gate) on it
+ (NSImage*)shalimarImage:(CGFloat)size
{
    NSImage* img = [NSImage imageWithSize:NSMakeSize(size, size) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        [[NSColor blackColor] set];
        NSRect page = NSInsetRect(rect, size * 0.16, size * 0.06);
        NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:page xRadius:size * 0.08 yRadius:size * 0.08];
        p.lineWidth = MAX(1.0, size / 16.0);
        [p stroke];
        NSBezierPath* arch = [NSBezierPath bezierPath];
        CGFloat l = NSMinX(page) + page.size.width * 0.22, r = NSMaxX(page) - page.size.width * 0.22;
        CGFloat bottom = NSMinY(page) + page.size.height * 0.18;
        CGFloat spring = NSMinY(page) + page.size.height * 0.50;
        CGFloat apex = NSMinY(page) + page.size.height * 0.82;
        CGFloat mid = (l + r) / 2;
        [arch moveToPoint:NSMakePoint(l, bottom)];
        [arch lineToPoint:NSMakePoint(l, spring)];
        [arch curveToPoint:NSMakePoint(mid, apex) controlPoint1:NSMakePoint(l, spring + (apex - spring) * 0.6)
             controlPoint2:NSMakePoint(mid, spring + (apex - spring) * 0.4)];
        [arch curveToPoint:NSMakePoint(r, spring) controlPoint1:NSMakePoint(mid, spring + (apex - spring) * 0.4)
             controlPoint2:NSMakePoint(r, spring + (apex - spring) * 0.6)];
        [arch lineToPoint:NSMakePoint(r, bottom)];
        arch.lineWidth = MAX(1.0, size / 16.0);
        [arch stroke];
        return YES;
    }];
    img.template = YES;
    return img;
}

+ (NSImage*)dot:(NSColor*)color size:(CGFloat)size
{
    return [NSImage imageWithSize:NSMakeSize(size, size) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        [color set];
        [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect, 1, 1)] fill];
        return YES;
    }];
}
@end
