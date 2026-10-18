// =============================================================================
//  Sheet.mm - see Sheet.h
// =============================================================================
#import "Sheet.h"
#import "DialogKit.h"

// a sheet keeps itself alive while it is shown
static NSMutableSet* GRShownSheets()
{
    static NSMutableSet* set = nil;
    if (set == nil) set = [NSMutableSet set];
    return set;
}

@interface GRSheetButtonTarget : NSObject
@property (nonatomic, copy) void (^action)(void);
- (void)fire:(id)sender;
@end
@implementation GRSheetButtonTarget
- (void)fire:(id)sender { (void)sender; if (self.action) self.action(); }
@end

@implementation GRSheet
{
    NSView* _content;
    NSButton* _ok;
    NSButton* _cancel;
    NSStackView* _leading;
    NSStackView* _trailing;
    NSMutableArray<GRSheetButtonTarget*>* _targets;
    void (^_done)(BOOL);
    NSWindow* _parent;
    BOOL _accepted;
    BOOL _modal;
}

- (instancetype)initWithTitle:(NSString*)title size:(NSSize)size
{
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                  backing:NSBackingStoreBuffered
                                                    defer:YES];
    panel.title = title != nil ? title : @"";
    panel.releasedWhenClosed = NO;
    panel.minSize = NSMakeSize(MIN(size.width, 380), MIN(size.height, 200));
    if ((self = [super initWithWindow:panel]))
    {
        _targets = [NSMutableArray array];
        NSView* root = panel.contentView;
        _content = [[NSView alloc] initWithFrame:NSZeroRect];
        _content.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:_content];

        _ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(okPressed:)];
        _ok.keyEquivalent = @"\r";
        _cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelPressed:)];
        _cancel.keyEquivalent = @"\033";
        _leading = [NSStackView stackViewWithViews:@[]];
        _leading.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        _leading.spacing = 8;
        _trailing = [NSStackView stackViewWithViews:@[_cancel, _ok]];
        _trailing.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        _trailing.spacing = 8;
        NSStackView* bar = [NSStackView stackViewWithViews:@[]];
        bar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        [bar addView:_leading inGravity:NSStackViewGravityLeading];
        [bar addView:_trailing inGravity:NSStackViewGravityTrailing];
        bar.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:bar];

        [NSLayoutConstraint activateConstraints:@[
            [_content.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
            [_content.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
            [_content.topAnchor constraintEqualToAnchor:root.topAnchor],
            [bar.topAnchor constraintEqualToAnchor:_content.bottomAnchor constant:10],
            [bar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
            [bar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
            [bar.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-14],
        ]];
    }
    return self;
}

- (NSView*)content { return _content; }
- (NSButton*)okButton { return _ok; }
- (NSButton*)cancelButton { return _cancel; }
- (NSWindow*)parentWindow { return _parent; }
- (BOOL)accepted { return _accepted; }

- (void)setContentView:(NSView*)view
{
    for (NSView* v in [_content.subviews copy]) [v removeFromSuperview];
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [_content addSubview:view];
    [NSLayoutConstraint activateConstraints:@[
        [view.leadingAnchor constraintEqualToAnchor:_content.leadingAnchor],
        [view.trailingAnchor constraintEqualToAnchor:_content.trailingAnchor],
        [view.topAnchor constraintEqualToAnchor:_content.topAnchor],
        [view.bottomAnchor constraintEqualToAnchor:_content.bottomAnchor],
    ]];
}

- (void)setOkTitle:(NSString*)title { _ok.title = title; }
- (void)setCancelTitle:(NSString*)title { _cancel.title = title; }

- (void)removeOkButton
{
    if (_ok == nil) return;
    [_trailing removeView:_ok];
    _ok = nil;
    _cancel.title = @"Close";
    _cancel.keyEquivalent = @"\033";
}

- (NSButton*)makeButton:(NSString*)title action:(void (^)(void))action
{
    GRSheetButtonTarget* t = [[GRSheetButtonTarget alloc] init];
    t.action = action;
    [_targets addObject:t];
    return [NSButton buttonWithTitle:title target:t action:@selector(fire:)];
}

- (NSButton*)addButton:(NSString*)title action:(void (^)(void))action
{
    NSButton* b = [self makeButton:title action:action];
    [_leading addView:b inGravity:NSStackViewGravityTrailing];
    return b;
}

- (NSButton*)addTrailingButton:(NSString*)title action:(void (^)(void))action
{
    NSButton* b = [self makeButton:title action:action];
    [_trailing insertView:b atIndex:0 inGravity:NSStackViewGravityTrailing];
    return b;
}

- (void)beginOn:(NSWindow*)parent completion:(void (^)(BOOL ok))done
{
    _done = [done copy];
    _parent = parent;
    _accepted = NO;
    [GRShownSheets() addObject:self];
    if (parent != nil)
    {
        _modal = NO;
        [parent beginSheet:self.window completionHandler:nil];
    }
    else
    {
        _modal = YES;
        [self.window center];
        [NSApp runModalForWindow:self.window];
    }
}

- (void)finish:(BOOL)ok
{
    _accepted = ok;
    if (_modal)
    {
        [NSApp stopModal];
        [self.window orderOut:nil];
    }
    else if (_parent != nil) [_parent endSheet:self.window returnCode:ok ? NSModalResponseOK : NSModalResponseCancel];
    void (^done)(BOOL) = _done;
    _done = nil;
    if (done) done(ok);
    [GRShownSheets() removeObject:self];
}

- (void)okPressed:(id)sender
{
    (void)sender;
    // an edit still open in a field or a table is ended first, so -accept reads it
    [self.window makeFirstResponder:nil];
    if (![self accept]) return;
    [self finish:YES];
}

- (void)cancelPressed:(id)sender
{
    (void)sender;
    [self finish:NO];
}

- (BOOL)accept { return YES; }

- (void)showMessage:(NSString*)text
{
    GRShowMessage(self.window, self.window.title, text);
}
@end

// -----------------------------------------------------------------------------
@implementation GRInputSheet
{
    NSTextField* _field;
}

- (instancetype)initWithTitle:(NSString*)title prompt:(NSString*)prompt value:(NSString*)value
{
    if ((self = [super initWithTitle:title size:NSMakeSize(440, 130)]))
    {
        NSTextField* label = [NSTextField labelWithString:prompt != nil ? prompt : @""];
        _field = [NSTextField textFieldWithString:value != nil ? value : @""];
        NSStackView* stack = [NSStackView stackViewWithViews:@[label, _field]];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading;
        stack.edgeInsets = NSEdgeInsetsMake(16, 16, 0, 16);
        [_field.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-32].active = YES;
        [self setContentView:stack];
        self.window.initialFirstResponder = _field;
    }
    return self;
}

- (NSString*)value
{
    return [_field.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

- (BOOL)accept
{
    if (self.emptyMessage != nil && self.value.length == 0)
    {
        [self showMessage:self.emptyMessage];
        return NO;
    }
    return YES;
}
@end

// -----------------------------------------------------------------------------
@implementation GRTextSheet
{
    NSTextView* _text;
}

- (instancetype)initWithTitle:(NSString*)title text:(NSString*)text mono:(BOOL)mono
{
    if ((self = [super initWithTitle:title size:NSMakeSize(760, 520)]))
    {
        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 760, 460)];
        scroll.hasVerticalScroller = YES;
        scroll.hasHorizontalScroller = YES;
        scroll.borderType = NSBezelBorder;
        _text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 760, 460)];
        _text.editable = NO;
        _text.richText = NO;
        _text.font = mono ? [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]
                          : [NSFont systemFontOfSize:[NSFont systemFontSize]];
        _text.string = text != nil ? text : @"";
        _text.autoresizingMask = NSViewWidthSizable;
        _text.textContainerInset = NSMakeSize(6, 6);
        scroll.documentView = _text;
        NSView* host = [[NSView alloc] initWithFrame:NSZeroRect];
        scroll.translatesAutoresizingMaskIntoConstraints = NO;
        [host addSubview:scroll];
        [NSLayoutConstraint activateConstraints:@[
            [scroll.leadingAnchor constraintEqualToAnchor:host.leadingAnchor constant:12],
            [scroll.trailingAnchor constraintEqualToAnchor:host.trailingAnchor constant:-12],
            [scroll.topAnchor constraintEqualToAnchor:host.topAnchor constant:12],
            [scroll.bottomAnchor constraintEqualToAnchor:host.bottomAnchor],
        ]];
        [self setContentView:host];
        [self removeOkButton];
        __weak GRTextSheet* weakSelf = self;
        [self addButton:@"Copy" action:^{
            GRTextSheet* me = weakSelf;
            if (me == nil) return;
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:me->_text.string forType:NSPasteboardTypeString];
        }];
    }
    return self;
}
@end
