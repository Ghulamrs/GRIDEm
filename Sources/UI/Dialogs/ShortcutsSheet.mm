// =============================================================================
//  ShortcutsSheet.mm - see ShortcutsSheet.h
// =============================================================================
#import "ShortcutsSheet.h"
#import "DialogKit.h"
#import "../Commands.h"

@interface GRShortcutsSheet () <NSSearchFieldDelegate>
@end

@implementation GRShortcutsSheet
{
    NSSearchField* _filter;
    GRTable* _list;
}

- (instancetype)init
{
    if ((self = [super initWithTitle:@"Keyboard Shortcuts" size:NSMakeSize(660, 560)]))
    {
        _filter = [[NSSearchField alloc] initWithFrame:NSZeroRect];
        _filter.placeholderString = @"Type to filter";
        _filter.delegate = self;
        _list = [[GRTable alloc] initWithHeight:420];
        [_list addColumn:@"Menu" width:90 kind:GRColumnText choices:nil];
        [_list addColumn:@"Command" width:300 kind:GRColumnText choices:nil];
        [_list addColumn:@"Shortcut" width:120 kind:GRColumnText choices:nil];
        [_list addColumn:@"Keys" width:100 kind:GRColumnText choices:nil];
        NSStackView* all = [NSStackView stackViewWithViews:@[_filter, _list.view]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.alignment = NSLayoutAttributeLeading;
        all.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [_filter.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [_list.view.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [self setContentView:all];
        [self removeOkButton];
        self.window.initialFirstResponder = _filter;
        [self fill];
    }
    return self;
}

- (void)fill
{
    NSString* f = [_filter.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]].lowercaseString;
    [_list clear];
    for (GRCommand* c in [GRCommandRegistry shared].all)
    {
        if (c.shortcutText.length == 0) continue;
        NSString* category = c.category != nil ? c.category : @"";
        NSString* title = c.title != nil ? c.title : c.commandId;
        NSString* hay = [[NSString stringWithFormat:@"%@ %@ %@", category, title, c.shortcutText] lowercaseString];
        if (f.length > 0 && ![hay containsString:f]) continue;
        [_list addRow:@[category, title, c.shortcutText, [GRCommandRegistry displayText:c.shortcutText]] tag:nil];
    }
    [_list reload];
}

- (void)controlTextDidChange:(NSNotification*)note { (void)note; [self fill]; }
@end
