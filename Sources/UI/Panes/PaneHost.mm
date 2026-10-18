// =============================================================================
//  PaneHost.mm - see PaneHost.h
// =============================================================================
#import "PaneHost.h"
#import "PaneUtil.h"
#import "../Theme.h"

// =============================================================================
@implementation GRPaneController
{
    NSString* _identifier;
    NSString* _title;
    NSString* _symbol;
}

- (instancetype)initWithIdentifier:(NSString*)identifier title:(NSString*)title symbol:(NSString*)symbol
{
    self = [super initWithNibName:nil bundle:nil];
    if (self != nil)
    {
        _identifier = [identifier copy];
        _title = [title copy];
        _symbol = [symbol copy];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(themeChanged:)
                                                     name:GRThemeDidChangeNotification object:nil];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)loadView
{
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
}

- (NSString*)paneIdentifier { return _identifier; }
- (NSString*)paneTitle { return _title; }
- (NSString*)paneSymbol { return _symbol; }
- (NSViewController*)paneViewController { return self; }

- (void)themeChanged:(NSNotification*)note
{
    (void)note;
    if (self.isViewLoaded) [self applyTheme];
}

- (void)applyTheme
{
    self.view.appearance = [[GRTheme current] appearance];
}
@end

// =============================================================================
@implementation GRPaneWindow
{
    id<GRPane> _pane;
    __weak GRPaneHost* _host;
}

- (instancetype)initWithPane:(id<GRPane>)pane host:(GRPaneHost*)host
{
    NSRect frame = NSMakeRect(0, 0, 520, 320);
    self = [super initWithContentRect:frame
                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                      | NSWindowStyleMaskUtilityWindow
                              backing:NSBackingStoreBuffered defer:NO];
    if (self != nil)
    {
        _pane = pane;
        _host = host;
        self.title = pane.paneTitle;
        self.releasedWhenClosed = NO;
        self.hidesOnDeactivate = NO;
        self.floatingPanel = NO;
        self.delegate = self;
        [self setFrameAutosaveName:[@"GRIDEm.Pane." stringByAppendingString:pane.paneIdentifier]];
        [[GRTheme current] applyToWindow:self];
    }
    return self;
}

- (id<GRPane>)pane { return _pane; }
- (GRPaneHost*)host { return _host; }

// closing the window docks the pane back, as closing GRIDEd's FloatWindow does
- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    GRPaneHost* host = _host;
    if (host != nil) [host dockPane:_pane.paneIdentifier];
}
@end

// =============================================================================
@interface GRPaneHost ()
@end

@implementation GRPaneHost
{
    NSString* _name;
    BOOL _iconsOnly;
    NSMutableArray<id<GRPane>>* _panes;
    NSMutableSet<NSString*>* _hidden;
    NSMutableDictionary<NSString*, GRPaneWindow*>* _floating;
    NSString* _active;
    NSSegmentedControl* _switcher;
    NSButton* _menuButton;
    NSView* _content;
    NSView* _header;
    NSTextField* _emptyLabel;
}

- (instancetype)initWithName:(NSString*)name iconsOnly:(BOOL)iconsOnly
{
    self = [super initWithNibName:nil bundle:nil];
    if (self != nil)
    {
        _name = [name copy];
        _iconsOnly = iconsOnly;
        _panes = [NSMutableArray array];
        _hidden = [NSMutableSet set];
        _floating = [NSMutableDictionary dictionary];
    }
    return self;
}

- (NSString*)name { return _name; }
- (NSArray<id<GRPane>>*)panes { return [_panes copy]; }

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 300)];
    _switcher = [NSSegmentedControl segmentedControlWithLabels:@[] trackingMode:NSSegmentSwitchTrackingSelectOne
                                                        target:self action:@selector(switcherChanged:)];
    _switcher.segmentStyle = NSSegmentStyleAutomatic;
    _switcher.controlSize = NSControlSizeSmall;
    _switcher.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];

    _menuButton = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"ellipsis.circle"
                                                      accessibilityDescription:@"Pane"]
                                     target:self action:@selector(showPaneMenu:)];
    _menuButton.bordered = NO;
    _menuButton.toolTip = @"Open in Separate Window, or Hide";

    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ _switcher, spacer, _menuButton ]];
    _header = bar;

    _content = [[NSView alloc] initWithFrame:NSZeroRect];
    _emptyLabel = [GRPaneUtil label:@"No pane shown - choose one from the View menu."];
    _emptyLabel.textColor = [NSColor secondaryLabelColor];
    _emptyLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_content addSubview:_emptyLabel];
    [NSLayoutConstraint activateConstraints:@[
        [_emptyLabel.centerXAnchor constraintEqualToAnchor:_content.centerXAnchor],
        [_emptyLabel.centerYAnchor constraintEqualToAnchor:_content.centerYAnchor]
    ]];

    NSBox* line = [[NSBox alloc] initWithFrame:NSZeroRect];
    line.boxType = NSBoxSeparator;
    [GRPaneUtil stack:@[ bar, line, _content ] in:root grow:_content];
    self.view = root;
    [self rebuildSwitcher];
}

// ---- the panes ---------------------------------------------------------------
- (id<GRPane>)pane:(NSString*)identifier
{
    for (id<GRPane> p in _panes)
        if ([p.paneIdentifier isEqualToString:identifier]) return p;
    return nil;
}

- (BOOL)hasPane:(NSString*)identifier { return [self pane:identifier] != nil; }

- (void)addPane:(id<GRPane>)pane hidden:(BOOL)hidden
{
    if (pane == nil || [self hasPane:pane.paneIdentifier]) return;
    [_panes addObject:pane];
    if (hidden) [_hidden addObject:pane.paneIdentifier];
    if (_active == nil && !hidden) _active = pane.paneIdentifier;
    if (self.isViewLoaded) [self rebuildSwitcher];
}

- (id<GRPane>)activePane { return _active != nil ? [self pane:_active] : nil; }

// the panes the header lists: neither hidden nor floating
- (NSArray<id<GRPane>>*)docked
{
    NSMutableArray* r = [NSMutableArray array];
    for (id<GRPane> p in _panes)
        if (![_hidden containsObject:p.paneIdentifier] && _floating[p.paneIdentifier] == nil) [r addObject:p];
    return r;
}

- (void)rebuildSwitcher
{
    if (!self.isViewLoaded) return;
    NSArray<id<GRPane>>* docked = [self docked];
    _switcher.segmentCount = (NSInteger)docked.count;
    NSInteger selected = -1;
    for (NSUInteger i = 0; i < docked.count; i++)
    {
        id<GRPane> p = docked[i];
        NSImage* img = p.paneSymbol.length > 0
            ? [NSImage imageWithSystemSymbolName:p.paneSymbol accessibilityDescription:p.paneTitle] : nil;
        if (_iconsOnly && img != nil)
        {
            [_switcher setImage:img forSegment:(NSInteger)i];
            [_switcher setLabel:@"" forSegment:(NSInteger)i];
            [_switcher setWidth:28 forSegment:(NSInteger)i];
        }
        else
        {
            [_switcher setImage:nil forSegment:(NSInteger)i];
            [_switcher setLabel:p.paneTitle forSegment:(NSInteger)i];
            [_switcher setWidth:0 forSegment:(NSInteger)i];
        }
        [_switcher setToolTip:p.paneTitle forSegment:(NSInteger)i];
        if ([p.paneIdentifier isEqualToString:_active]) selected = (NSInteger)i;
    }
    if (selected < 0 && docked.count > 0) { selected = 0; _active = docked[0].paneIdentifier; }
    if (docked.count == 0) _active = nil;
    if (selected >= 0) _switcher.selectedSegment = selected;
    _menuButton.enabled = docked.count > 0;
    [self showActive];
}

- (void)showActive
{
    for (NSView* v in [_content.subviews copy])
        if (v != _emptyLabel) [v removeFromSuperview];
    id<GRPane> p = self.activePane;
    _emptyLabel.hidden = p != nil;
    if (p == nil) return;
    NSViewController* vc = [p paneViewController];
    if (vc.parentViewController != self && vc.parentViewController == nil) [self addChildViewController:vc];
    [GRPaneUtil fill:_content with:vc.view];
    if ([p respondsToSelector:@selector(paneDidShow)]) [p paneDidShow];
}

- (void)switcherChanged:(NSSegmentedControl*)sender
{
    NSArray<id<GRPane>>* docked = [self docked];
    NSInteger i = sender.selectedSegment;
    if (i < 0 || i >= (NSInteger)docked.count) return;
    _active = docked[(NSUInteger)i].paneIdentifier;
    [self showActive];
}

- (void)changed
{
    [self rebuildSwitcher];
    if (self.layoutChanged) self.layoutChanged(self);
}

- (void)showPane:(NSString*)identifier
{
    if (![self hasPane:identifier]) return;
    GRPaneWindow* w = _floating[identifier];
    if (w != nil) { [w makeKeyAndOrderFront:nil]; return; }
    [_hidden removeObject:identifier];
    _active = identifier;
    [self changed];
}

- (void)hidePane:(NSString*)identifier
{
    if (![self hasPane:identifier]) return;
    GRPaneWindow* w = _floating[identifier];
    if (w != nil)
    {
        [_floating removeObjectForKey:identifier];
        w.delegate = nil;
        [w close];
        NSViewController* vc = [w.pane paneViewController];
        [vc.view removeFromSuperview];
        if (vc.parentViewController == nil) [self addChildViewController:vc];
    }
    [_hidden addObject:identifier];
    if ([_active isEqualToString:identifier]) _active = nil;
    [self changed];
}

- (void)togglePane:(NSString*)identifier
{
    if ([self isShown:identifier] && ([self isFloating:identifier] || [_active isEqualToString:identifier]))
        [self hidePane:identifier];
    else [self showPane:identifier];
}

- (BOOL)isShown:(NSString*)identifier
{
    return [self hasPane:identifier] && ![_hidden containsObject:identifier];
}

- (BOOL)isFloating:(NSString*)identifier { return _floating[identifier] != nil; }

- (void)floatPane:(NSString*)identifier
{
    id<GRPane> p = [self pane:identifier];
    if (p == nil || _floating[identifier] != nil) return;
    [_hidden removeObject:identifier];
    NSViewController* vc = [p paneViewController];
    [vc.view removeFromSuperview];
    [vc removeFromParentViewController];
    GRPaneWindow* w = [[GRPaneWindow alloc] initWithPane:p host:self];
    w.contentViewController = vc;
    _floating[identifier] = w;
    if ([_active isEqualToString:identifier]) _active = nil;
    if (self.view.window != nil)
    {
        NSRect r = self.view.window.frame;
        [w setFrameTopLeftPoint:NSMakePoint(NSMidX(r) - 260, NSMidY(r) + 160)];
    }
    [w makeKeyAndOrderFront:nil];
    [self changed];
}

- (void)dockPane:(NSString*)identifier
{
    GRPaneWindow* w = _floating[identifier];
    if (w == nil) return;
    [_floating removeObjectForKey:identifier];
    NSViewController* vc = w.contentViewController;
    w.contentViewController = nil;
    w.delegate = nil;
    if (w.isVisible) [w orderOut:nil];
    if (vc != nil && vc.parentViewController == nil) [self addChildViewController:vc];
    _active = identifier;
    [self changed];
}

- (void)dockAll
{
    for (NSString* identifier in [_floating allKeys]) [self dockPane:identifier];
}

// ---- the pane menu: Open in Separate Window, Hide ------------------------------
- (void)showPaneMenu:(NSButton*)sender
{
    id<GRPane> p = self.activePane;
    if (p == nil) return;
    NSMenu* m = [[NSMenu alloc] initWithTitle:p.paneTitle];
    NSMenuItem* f = [m addItemWithTitle:@"Open in Separate Window" action:@selector(floatActive:) keyEquivalent:@""];
    f.target = self;
    NSMenuItem* h = [m addItemWithTitle:[@"Hide " stringByAppendingString:p.paneTitle]
                                 action:@selector(hideActive:) keyEquivalent:@""];
    h.target = self;
    [m popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, NSHeight(sender.bounds)) inView:sender];
}

- (void)floatActive:(id)sender { (void)sender; if (_active != nil) [self floatPane:_active]; }
- (void)hideActive:(id)sender { (void)sender; if (_active != nil) [self hidePane:_active]; }

// ---- layout -------------------------------------------------------------------
- (void)restoreHidden:(const std::vector<std::string>&)hiddenPanes
{
    for (id<GRPane> p in _panes)
    {
        std::string id8 = GRStd(p.paneIdentifier);
        bool hide = false;
        for (const std::string& h : hiddenPanes)
            if (h == id8) { hide = true; break; }
        if (hide) [_hidden addObject:p.paneIdentifier];
        else [_hidden removeObject:p.paneIdentifier];
    }
    if (_active != nil && [_hidden containsObject:_active]) _active = nil;
    [self rebuildSwitcher];
}

- (void)collectHidden:(std::vector<std::string>&)hiddenPanes
{
    for (id<GRPane> p in _panes)
        if ([_hidden containsObject:p.paneIdentifier]) hiddenPanes.push_back(GRStd(p.paneIdentifier));
}
@end
