// =============================================================================
//  GoToLinePopover.mm - see GoToLinePopover.h
// =============================================================================
#import "GoToLinePopover.h"

static GRGoToLinePopover* shownGoTo = nil;

@implementation GRGoToLinePopover
{
    NSInteger _current;
    NSInteger _max;
    NSTextField* _number;
    NSPopover* _popover;
    void (^_done)(NSInteger);
}

+ (void)showFrom:(NSView*)view current:(NSInteger)current max:(NSInteger)max done:(void (^)(NSInteger line))done
{
    if (view == nil) return;
    GRGoToLinePopover* c = [[GRGoToLinePopover alloc] initWithNibName:nil bundle:nil];
    c->_current = current;
    c->_max = max < 1 ? 1 : max;
    c->_done = [done copy];
    NSPopover* p = [[NSPopover alloc] init];
    p.behavior = NSPopoverBehaviorTransient;
    p.contentViewController = c;
    p.delegate = c;
    c->_popover = p;
    shownGoTo = c;
    NSRect anchor = NSMakeRect(NSMidX(view.visibleRect) - 1, NSMinY(view.visibleRect), 2, 2);
    [p showRelativeToRect:anchor ofView:view preferredEdge:NSRectEdgeMinY];
}

- (void)loadView
{
    NSTextField* label = [NSTextField labelWithString:
                          [NSString stringWithFormat:@"Line number (1 - %ld)", (long)_max]];
    NSNumberFormatter* fmt = [[NSNumberFormatter alloc] init];
    fmt.numberStyle = NSNumberFormatterNoStyle;
    fmt.minimum = @1;
    fmt.maximum = @(_max);
    fmt.allowsFloats = NO;
    _number = [NSTextField textFieldWithString:@""];
    _number.formatter = fmt;
    NSInteger start = _current < 1 ? 1 : (_current > _max ? _max : _current);
    _number.integerValue = start;
    _number.target = self;
    _number.action = @selector(go:);
    [_number.widthAnchor constraintEqualToConstant:90].active = YES;
    NSButton* go = [NSButton buttonWithTitle:@"Go" target:self action:@selector(go:)];
    go.keyEquivalent = @"\r";
    NSStackView* stack = [NSStackView stackViewWithViews:@[label, _number, go]];
    stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    stack.edgeInsets = NSEdgeInsetsMake(10, 12, 10, 12);
    self.view = stack;
}

- (void)viewDidAppear
{
    [super viewDidAppear];
    [self.view.window makeFirstResponder:_number];
    [_number selectText:nil];
}

- (void)go:(id)sender
{
    (void)sender;
    NSInteger line = _number.integerValue;
    if (line < 1) line = 1;
    if (line > _max) line = _max;
    void (^done)(NSInteger) = _done;
    _done = nil;
    [_popover close];
    if (done) done(line);
}

- (void)popoverDidClose:(NSNotification*)notification
{
    (void)notification;
    if (shownGoTo == self) shownGoTo = nil;
}
@end
