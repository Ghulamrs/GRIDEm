// =============================================================================
//  PaneUtil.mm - see PaneUtil.h
// =============================================================================
#import "PaneUtil.h"
#import "../Theme.h"

@implementation GRPaneUtil

+ (NSFont*)monoFont
{
    NSFont* f = [NSFont fontWithName:@"Menlo" size:11];
    return f != nil ? f : [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
}

+ (NSFont*)boldMonoFont
{
    return [[NSFontManager sharedFontManager] convertFont:[self monoFont] toHaveTrait:NSBoldFontMask];
}

static void AddColumns(NSTableView* table, NSArray<NSString*>* titles, NSArray<NSNumber*>* widths)
{
    for (NSUInteger i = 0; i < titles.count; i++)
    {
        NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:titles[i]];
        c.title = titles[i];
        c.width = i < widths.count ? widths[i].doubleValue : 120;
        c.minWidth = 30;
        c.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
        [table addTableColumn:c];
    }
}

+ (NSTableView*)tableWithColumns:(NSArray<NSString*>*)titles widths:(NSArray<NSNumber*>*)widths
                          scroll:(NSScrollView**)scroll
{
    NSTableView* t = [[NSTableView alloc] initWithFrame:NSZeroRect];
    AddColumns(t, titles, widths);
    t.usesAlternatingRowBackgroundColors = NO;
    t.allowsMultipleSelection = NO;
    t.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    t.style = NSTableViewStyleFullWidth;
    t.rowSizeStyle = NSTableViewRowSizeStyleSmall;
    NSScrollView* s = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    s.documentView = t;
    s.hasVerticalScroller = YES;
    s.hasHorizontalScroller = YES;
    s.autohidesScrollers = YES;
    s.borderType = NSNoBorder;
    if (scroll != nullptr) *scroll = s;
    return t;
}

+ (NSOutlineView*)outlineWithColumns:(NSArray<NSString*>*)titles widths:(NSArray<NSNumber*>*)widths
                              scroll:(NSScrollView**)scroll
{
    NSOutlineView* o = [[NSOutlineView alloc] initWithFrame:NSZeroRect];
    AddColumns(o, titles, widths);
    if (o.tableColumns.count > 0) o.outlineTableColumn = o.tableColumns[0];
    o.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    o.rowSizeStyle = NSTableViewRowSizeStyleSmall;
    o.autoresizesOutlineColumn = NO;
    NSScrollView* s = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    s.documentView = o;
    s.hasVerticalScroller = YES;
    s.hasHorizontalScroller = YES;
    s.autohidesScrollers = YES;
    s.borderType = NSNoBorder;
    if (scroll != nullptr) *scroll = s;
    return o;
}

+ (NSTextView*)textViewWithScroll:(NSScrollView**)scroll
{
    NSScrollView* s = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
    s.hasVerticalScroller = YES;
    s.hasHorizontalScroller = YES;
    s.autohidesScrollers = YES;
    s.borderType = NSNoBorder;
    NSSize size = s.contentSize;
    NSTextView* tv = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    tv.editable = NO;
    tv.selectable = YES;
    tv.richText = YES;
    tv.font = [self monoFont];
    tv.textContainerInset = NSMakeSize(4, 4);
    // no wrapping: a long line scrolls sideways, as GRIDEd's WordWrap = false
    tv.horizontallyResizable = YES;
    tv.verticallyResizable = YES;
    tv.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    tv.minSize = NSMakeSize(0, size.height);
    tv.autoresizingMask = NSViewWidthSizable;
    tv.textContainer.widthTracksTextView = NO;
    tv.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    tv.automaticQuoteSubstitutionEnabled = NO;
    tv.automaticDashSubstitutionEnabled = NO;
    tv.automaticSpellingCorrectionEnabled = NO;
    s.documentView = tv;
    if (scroll != nullptr) *scroll = s;
    return tv;
}

+ (NSStackView*)barWithViews:(NSArray<NSView*>*)views
{
    NSStackView* bar = [NSStackView stackViewWithViews:views];
    bar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    bar.alignment = NSLayoutAttributeCenterY;
    bar.spacing = 6;
    bar.edgeInsets = NSEdgeInsetsMake(3, 6, 3, 6);
    [bar setHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
    return bar;
}

+ (NSTextField*)label:(NSString*)text
{
    NSTextField* l = [NSTextField labelWithString:text != nil ? text : @""];
    l.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    l.lineBreakMode = NSLineBreakByTruncatingTail;
    return l;
}

+ (NSTextField*)inputFieldWithPlaceholder:(NSString*)placeholder
{
    NSTextField* f = [NSTextField textFieldWithString:@""];
    f.placeholderString = placeholder;
    f.font = [self monoFont];
    f.bezelStyle = NSTextFieldSquareBezel;
    return f;
}

+ (NSTableCellView*)cellIn:(NSTableView*)table identifier:(NSString*)identifier
{
    NSTableCellView* cell = [table makeViewWithIdentifier:identifier owner:nil];
    if (cell != nil) return cell;
    cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 100, 17)];
    cell.identifier = identifier;
    NSImageView* iv = [[NSImageView alloc] initWithFrame:NSZeroRect];
    iv.translatesAutoresizingMaskIntoConstraints = NO;
    iv.imageScaling = NSImageScaleProportionallyDown;
    NSTextField* tf = [NSTextField labelWithString:@""];
    tf.translatesAutoresizingMaskIntoConstraints = NO;
    tf.lineBreakMode = NSLineBreakByTruncatingTail;
    tf.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [cell addSubview:iv];
    [cell addSubview:tf];
    cell.imageView = iv;
    cell.textField = tf;
    [NSLayoutConstraint activateConstraints:@[
        [iv.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:2],
        [iv.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        [iv.widthAnchor constraintEqualToConstant:16],
        [iv.heightAnchor constraintEqualToConstant:16],
        [tf.leadingAnchor constraintEqualToAnchor:iv.trailingAnchor constant:4],
        [tf.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-2],
        [tf.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor]
    ]];
    return cell;
}

+ (void)fill:(NSView*)container with:(NSView*)view
{
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [container addSubview:view];
    [NSLayoutConstraint activateConstraints:@[
        [view.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
        [view.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [view.topAnchor constraintEqualToAnchor:container.topAnchor],
        [view.bottomAnchor constraintEqualToAnchor:container.bottomAnchor]
    ]];
}

+ (void)stack:(NSArray<NSView*>*)views in:(NSView*)container grow:(NSView*)grow
{
    NSView* previous = nil;
    for (NSView* v in views)
    {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [container addSubview:v];
        [v.leadingAnchor constraintEqualToAnchor:container.leadingAnchor].active = YES;
        [v.trailingAnchor constraintEqualToAnchor:container.trailingAnchor].active = YES;
        // the safe area's top, not the view's: a pane in the sidebar sits under the window's full-size
        // title bar, and its header would otherwise lie beneath the close / minimise / zoom buttons
        if (previous == nil) [v.topAnchor constraintEqualToAnchor:container.safeAreaLayoutGuide.topAnchor].active = YES;
        else [v.topAnchor constraintEqualToAnchor:previous.bottomAnchor].active = YES;
        if (v != grow)
        {
            [v setContentHuggingPriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationVertical];
            [v setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];
        }
        else
        {
            [v setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationVertical];
            [v setContentCompressionResistancePriority:1 forOrientation:NSLayoutConstraintOrientationVertical];
        }
        previous = v;
    }
    if (previous != nil) [previous.bottomAnchor constraintEqualToAnchor:container.bottomAnchor].active = YES;
}
@end
