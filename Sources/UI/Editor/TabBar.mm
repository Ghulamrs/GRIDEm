// =============================================================================
//  TabBar.mm - see TabBar.h
// =============================================================================
#import "TabBar.h"
#import "../Common.h"
#import "../Theme.h"
#import "../Icons.h"

static const CGFloat kTabMin = 90;
static const CGFloat kTabMax = 220;
static const CGFloat kChevron = 22;

@implementation GRTabItem
@end

@implementation GRTabBar
{
    NSMutableArray<GRTabItem*>* tabs;
    NSInteger hover;
    BOOL hoverClose;
    NSInteger pressed;
    NSPoint pressPoint;
    BOOL dragging;
    NSTrackingArea* tracking;
    NSInteger menuIndex;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        tabs = [NSMutableArray array];
        _selectedIndex = -1;
        hover = -1;
        pressed = -1;
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(themeChanged:)
                                                     name:GRThemeDidChangeNotification object:nil];
    }
    return self;
}

- (void)dealloc { [[NSNotificationCenter defaultCenter] removeObserver:self]; }

- (BOOL)isFlipped { return YES; }

- (NSSize)intrinsicContentSize { return NSMakeSize(NSViewNoIntrinsicMetric, 28); }

- (void)themeChanged:(NSNotification*)n { (void)n; [self setNeedsDisplay:YES]; }

- (void)viewDidChangeEffectiveAppearance { [super viewDidChangeEffectiveAppearance]; [self setNeedsDisplay:YES]; }

- (NSArray<GRTabItem*>*)items { return tabs; }

- (void)setItems:(NSArray<GRTabItem*>*)items selected:(NSInteger)selected
{
    tabs = GROr([items mutableCopy], [NSMutableArray array]);
    _selectedIndex = (selected >= 0 && selected < (NSInteger)tabs.count) ? selected : (tabs.count > 0 ? 0 : -1);
    [self setNeedsDisplay:YES];
}

- (void)setSelectedIndex:(NSInteger)index
{
    _selectedIndex = (index >= 0 && index < (NSInteger)tabs.count) ? index : -1;
    [self setNeedsDisplay:YES];
}

- (void)reload { [self setNeedsDisplay:YES]; }

- (NSInteger)indexOfObject:(id)represented
{
    for (NSUInteger i = 0; i < tabs.count; i++)
        if (tabs[i].representedObject == represented) return (NSInteger)i;
    return NSNotFound;
}

- (GRTheme*)theme { return [[GRTheme current] resolvedFor:self.effectiveAppearance]; }

- (NSDictionary*)titleAttributes:(BOOL)selected
{
    GRTheme* t = [self theme];
    return @{ NSFontAttributeName : [NSFont systemFontOfSize:12 weight:(selected ? NSFontWeightMedium : NSFontWeightRegular)],
              NSForegroundColorAttributeName : selected ? t.text : t.mutedText };
}

// ---- layout ---------------------------------------------------------------------
- (BOOL)overflows
{
    CGFloat total = 0;
    for (NSUInteger i = 0; i < tabs.count; i++) total += [self naturalWidth:i];
    return total > self.bounds.size.width;
}

- (CGFloat)naturalWidth:(NSUInteger)i
{
    GRTabItem* it = tabs[i];
    CGFloat w = [it.title sizeWithAttributes:[self titleAttributes:YES]].width + 44 + (it.pinned ? 14 : 0);
    return MAX(kTabMin, MIN(kTabMax, w));
}

- (NSRect)rectOfTab:(NSInteger)index
{
    CGFloat avail = self.bounds.size.width - ([self overflows] ? kChevron : 0);
    CGFloat x = 0;
    for (NSInteger i = 0; i < (NSInteger)tabs.count; i++)
    {
        CGFloat w = [self naturalWidth:(NSUInteger)i];
        if ([self overflows]) w = MAX(kTabMin, MIN(w, avail / MAX(1, (CGFloat)tabs.count)));
        if (i == index) return NSMakeRect(x, 0, w, self.bounds.size.height);
        x += w;
    }
    return NSZeroRect;
}

- (NSRect)closeRectOf:(NSRect)tab { return NSMakeRect(NSMaxX(tab) - 22, NSMidY(tab) - 8, 16, 16); }

- (NSRect)chevronRect
{
    return NSMakeRect(NSMaxX(self.bounds) - kChevron, 0, kChevron, self.bounds.size.height);
}

- (NSInteger)tabAt:(NSPoint)p
{
    if ([self overflows] && NSPointInRect(p, [self chevronRect])) return -2;
    for (NSInteger i = 0; i < (NSInteger)tabs.count; i++)
    {
        NSRect r = [self rectOfTab:i];
        if (NSMaxX(r) > self.bounds.size.width - ([self overflows] ? kChevron : 0)) break;
        if (NSPointInRect(p, r)) return i;
    }
    return -1;
}

// ---- drawing ----------------------------------------------------------------------
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    GRTheme* t = [self theme];
    [t.header set];
    NSRectFill(self.bounds);
    CGFloat limit = self.bounds.size.width - ([self overflows] ? kChevron : 0);
    for (NSInteger i = 0; i < (NSInteger)tabs.count; i++)
    {
        NSRect r = [self rectOfTab:i];
        if (NSMinX(r) >= limit) break;
        if (NSMaxX(r) > limit) r.size.width = limit - NSMinX(r);
        GRTabItem* it = tabs[(NSUInteger)i];
        BOOL sel = i == self.selectedIndex;
        [(sel ? t.window : (i == hover ? t.hover : t.header)) set];
        NSRectFill(r);
        if (sel) { [t.accent set]; NSRectFill(NSMakeRect(NSMinX(r), NSMaxY(r) - 2, r.size.width, 2)); }
        [t.border set];
        NSRectFill(NSMakeRect(NSMaxX(r) - 1, NSMinY(r) + 5, 1, r.size.height - 10));

        CGFloat x = NSMinX(r) + 10;
        if (it.pinned)
        {
            NSImage* pin = [GRIcons image:@"pin" size:10];
            [pin drawInRect:NSMakeRect(x, NSMidY(r) - 6, 12, 12)];
            x += 14;
        }
        NSDictionary* a = [self titleAttributes:sel];
        NSString* title = it.readOnly ? [it.title stringByAppendingString:@" — read only"] : it.title;
        NSSize ts = [title sizeWithAttributes:a];
        NSRect textRect = NSMakeRect(x, NSMidY(r) - ts.height / 2, NSMaxX(r) - 26 - x, ts.height);
        NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
        ps.lineBreakMode = NSLineBreakByTruncatingMiddle;
        NSMutableDictionary* ad = [a mutableCopy];
        ad[NSParagraphStyleAttributeName] = ps;
        [title drawInRect:textRect withAttributes:ad];

        // the close box on the selected or hovered tab, else the modified dot
        NSRect cr = [self closeRectOf:r];
        if (sel || i == hover)
        {
            if (i == hover && hoverClose) { [t.hover set]; [[NSBezierPath bezierPathWithRoundedRect:cr xRadius:3 yRadius:3] fill]; }
            if (it.modified && !(i == hover && hoverClose))
            {
                [t.text set];
                [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(cr, 4.5, 4.5)] fill];
            }
            else
            {
                NSBezierPath* x2 = [NSBezierPath bezierPath];
                [x2 moveToPoint:NSMakePoint(NSMinX(cr) + 4.5, NSMinY(cr) + 4.5)];
                [x2 lineToPoint:NSMakePoint(NSMaxX(cr) - 4.5, NSMaxY(cr) - 4.5)];
                [x2 moveToPoint:NSMakePoint(NSMaxX(cr) - 4.5, NSMinY(cr) + 4.5)];
                [x2 lineToPoint:NSMakePoint(NSMinX(cr) + 4.5, NSMaxY(cr) - 4.5)];
                x2.lineWidth = 1.3;
                [t.mutedText set];
                [x2 stroke];
            }
        }
        else if (it.modified)
        {
            [t.mutedText set];
            [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(cr, 4.5, 4.5)] fill];
        }
    }
    if ([self overflows])
    {
        NSImage* chev = [NSImage imageWithSystemSymbolName:@"chevron.down" accessibilityDescription:@"All tabs"];
        NSRect cr = [self chevronRect];
        [chev drawInRect:NSMakeRect(NSMidX(cr) - 6, NSMidY(cr) - 6, 12, 12)];
    }
    [t.border set];
    NSRectFill(NSMakeRect(0, NSMaxY(self.bounds) - 1, self.bounds.size.width, 1));
}

// ---- mouse -------------------------------------------------------------------------
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (tracking != nil) [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                            options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
                                                    | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                              owner:self userInfo:nil];
    [self addTrackingArea:tracking];
}

- (void)mouseMoved:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger h = [self tabAt:p];
    BOOL hc = h >= 0 && NSPointInRect(p, [self closeRectOf:[self rectOfTab:h]]);
    if (h != hover || hc != hoverClose)
    {
        hover = h;
        hoverClose = hc;
        [self setNeedsDisplay:YES];
    }
    if (h >= 0) self.toolTip = GROr(tabs[(NSUInteger)h].path, tabs[(NSUInteger)h].title);
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    hover = -1;
    hoverClose = NO;
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger i = [self tabAt:p];
    if (i == -2) { [self showOverflowMenu:event]; return; }
    pressed = i;
    pressPoint = p;
    dragging = NO;
    if (i >= 0 && !NSPointInRect(p, [self closeRectOf:[self rectOfTab:i]]))
    {
        self.selectedIndex = i;
        [self.delegate tabBar:self didSelect:i];
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (pressed < 0) return;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (!dragging && fabs(p.x - pressPoint.x) < 6) return;
    dragging = YES;
    NSInteger over = [self tabAt:NSMakePoint(p.x, NSMidY(self.bounds))];
    if (over >= 0 && over != pressed)
    {
        GRTabItem* it = tabs[(NSUInteger)pressed];
        [tabs removeObjectAtIndex:(NSUInteger)pressed];
        [tabs insertObject:it atIndex:(NSUInteger)over];
        if ([self.delegate respondsToSelector:@selector(tabBar:moveFrom:to:)])
            [self.delegate tabBar:self moveFrom:pressed to:over];
        _selectedIndex = over;
        pressed = over;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger i = pressed;
    pressed = -1;
    if (dragging) { dragging = NO; return; }
    if (i >= 0 && i == [self tabAt:p] && NSPointInRect(p, [self closeRectOf:[self rectOfTab:i]]))
        [self.delegate tabBar:self close:i];
}

- (void)otherMouseUp:(NSEvent*)event
{
    // a middle click closes the tab, as in GRIDEd
    if (event.buttonNumber != 2) return;
    NSInteger i = [self tabAt:[self convertPoint:event.locationInWindow fromView:nil]];
    if (i >= 0) [self.delegate tabBar:self close:i];
}

- (void)showOverflowMenu:(NSEvent*)event
{
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"Tabs"];
    for (NSUInteger i = 0; i < tabs.count; i++)
    {
        NSMenuItem* it = [m addItemWithTitle:tabs[i].title action:@selector(pickFromOverflow:) keyEquivalent:@""];
        it.target = self;
        it.tag = (NSInteger)i;
        it.state = (NSInteger)i == self.selectedIndex ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [NSMenu popUpContextMenu:m withEvent:event forView:self];
}

- (void)pickFromOverflow:(NSMenuItem*)item
{
    NSInteger i = item.tag;
    // brought to the front so it shows
    if (i > 0 && i < (NSInteger)tabs.count)
    {
        NSInteger front = 0;
        while (front < (NSInteger)tabs.count && tabs[(NSUInteger)front].pinned) front++;
        if (i > front && [self.delegate respondsToSelector:@selector(tabBar:moveFrom:to:)])
        {
            GRTabItem* t = tabs[(NSUInteger)i];
            [tabs removeObjectAtIndex:(NSUInteger)i];
            [tabs insertObject:t atIndex:(NSUInteger)front];
            [self.delegate tabBar:self moveFrom:i to:front];
            i = front;
        }
    }
    self.selectedIndex = i;
    [self.delegate tabBar:self didSelect:i];
}

// ---- the context menu ------------------------------------------------------------------
- (NSMenu*)menuForEvent:(NSEvent*)event
{
    NSInteger i = [self tabAt:[self convertPoint:event.locationInWindow fromView:nil]];
    if (i < 0) return nil;
    menuIndex = i;
    GRTabItem* it = tabs[(NSUInteger)i];
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"Tab"];
    [[m addItemWithTitle:@"Save" action:@selector(menuSave:) keyEquivalent:@""] setTarget:self];
    [m addItem:[NSMenuItem separatorItem]];
    [[m addItemWithTitle:@"Close" action:@selector(menuClose:) keyEquivalent:@""] setTarget:self];
    [[m addItemWithTitle:@"Close Other Tabs" action:@selector(menuCloseOthers:) keyEquivalent:@""] setTarget:self];
    [[m addItemWithTitle:@"Close All Tabs" action:@selector(menuCloseAll:) keyEquivalent:@""] setTarget:self];
    [m addItem:[NSMenuItem separatorItem]];
    [[m addItemWithTitle:(it.pinned ? @"Unpin Tab" : @"Pin Tab") action:@selector(menuPin:) keyEquivalent:@""] setTarget:self];
    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* copy = [m addItemWithTitle:@"Copy Path" action:@selector(menuCopyPath:) keyEquivalent:@""];
    copy.target = self;
    copy.enabled = it.path.length > 0;
    NSMenuItem* finder = [m addItemWithTitle:@"Show in Finder" action:@selector(menuShowInFinder:) keyEquivalent:@""];
    finder.target = self;
    m.autoenablesItems = NO;
    return m;
}

- (void)menuSave:(id)s
{
    (void)s;
    if ([self.delegate respondsToSelector:@selector(tabBar:save:)]) [self.delegate tabBar:self save:menuIndex];
}

- (void)menuClose:(id)s { (void)s; [self.delegate tabBar:self close:menuIndex]; }

- (void)menuCloseOthers:(id)s
{
    (void)s;
    if ([self.delegate respondsToSelector:@selector(tabBar:closeOthers:)]) [self.delegate tabBar:self closeOthers:menuIndex];
}

- (void)menuCloseAll:(id)s
{
    (void)s;
    if ([self.delegate respondsToSelector:@selector(tabBarCloseAll:)]) [self.delegate tabBarCloseAll:self];
}

- (void)menuPin:(id)s
{
    (void)s;
    if ([self.delegate respondsToSelector:@selector(tabBar:togglePin:)]) [self.delegate tabBar:self togglePin:menuIndex];
}

- (void)menuCopyPath:(id)s
{
    (void)s;
    if (menuIndex < 0 || menuIndex >= (NSInteger)tabs.count) return;
    NSString* p = tabs[(NSUInteger)menuIndex].path;
    if (p.length == 0) return;
    [[NSPasteboard generalPasteboard] clearContents];
    [[NSPasteboard generalPasteboard] setString:p forType:NSPasteboardTypeString];
}

- (void)menuShowInFinder:(id)s
{
    (void)s;
    if (menuIndex < 0 || menuIndex >= (NSInteger)tabs.count) return;
    NSString* p = tabs[(NSUInteger)menuIndex].path;
    if (p.length > 0) [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:p] ]];
}
@end
