// =============================================================================
//  SnippetsSheet.mm - see SnippetsSheet.h
// =============================================================================
#import "SnippetsSheet.h"
#import "DialogKit.h"
#include <algorithm>

using namespace GRIDEm;

@interface GRSnippetsSheet () <NSTextViewDelegate>
@end

@implementation GRSnippetsSheet
{
    Settings::OrderedStringMap _snippets;
    GRTable* _list;
    NSTextView* _editor;
    BOOL _loading;
    NSString* _insertText;
}

- (instancetype)initWithSnippets:(const Settings::OrderedStringMap&)snippets canInsert:(BOOL)canInsert
{
    if ((self = [super initWithTitle:@"Code Snippets Manager" size:NSMakeSize(780, 500)]))
    {
        _snippets = snippets;
        _list = [[GRTable alloc] initWithHeight:360];
        [_list addColumn:@"Snippet" width:220 kind:GRColumnText choices:nil];
        _list.table.allowsMultipleSelection = NO;
        __weak GRSnippetsSheet* weakSelf = self;
        _list.selectionChanged = ^{ [weakSelf snippetSelected]; };
        [_list.view.widthAnchor constraintEqualToConstant:240].active = YES;

        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 480, 360)];
        scroll.hasVerticalScroller = YES;
        scroll.hasHorizontalScroller = YES;
        scroll.borderType = NSBezelBorder;
        _editor = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 480, 360)];
        _editor.richText = NO;
        _editor.allowsUndo = YES;
        _editor.automaticQuoteSubstitutionEnabled = NO;
        _editor.automaticDashSubstitutionEnabled = NO;
        _editor.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
        _editor.delegate = self;
        // no wrapping: a long line scrolls, as GRIDEd's editor box does
        _editor.horizontallyResizable = YES;
        _editor.textContainer.widthTracksTextView = NO;
        _editor.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
        _editor.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
        scroll.documentView = _editor;

        NSStackView* row = [NSStackView stackViewWithViews:@[_list.view, scroll]];
        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        row.alignment = NSLayoutAttributeTop;
        row.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [_list.view.heightAnchor constraintEqualToAnchor:row.heightAnchor constant:-14].active = YES;
        [scroll.heightAnchor constraintEqualToAnchor:row.heightAnchor constant:-14].active = YES;
        [self setContentView:row];
        [self setOkTitle:@"Save"];
        [self addButton:@"New…" action:^{ [weakSelf newSnippet]; }];
        [self addButton:@"Delete" action:^{ [weakSelf deleteSnippet]; }];
        if (canInsert) [self addTrailingButton:@"Insert" action:^{ [weakSelf insertSnippet]; }];
        [self fill:nil];
    }
    return self;
}

- (Settings::OrderedStringMap)snippets { return _snippets; }
- (NSString*)insertText { return _insertText; }

// the names sorted, as GRIDEd lists them
- (void)fill:(NSString*)select
{
    std::vector<std::string> keys;
    for (const std::pair<std::string, std::string>& kv : _snippets.Items()) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    [_list clear];
    NSInteger chosen = keys.empty() ? -1 : 0;
    for (size_t i = 0; i < keys.size(); i++)
    {
        [_list addRow:@[GRNS(keys[i])] tag:nil];
        if (select != nil && GRNS(keys[i]).length > 0 && [GRNS(keys[i]) isEqualToString:select]) chosen = (NSInteger)i;
    }
    [_list reload];
    if (chosen >= 0) [_list.table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)chosen] byExtendingSelection:NO];
    else _editor.string = @"";
    [self snippetSelected];
}

- (NSString*)current
{
    NSInteger row = _list.selectedRow;
    if (row < 0 || row >= (NSInteger)_list.rows.count) return nil;
    return _list.rows[(NSUInteger)row][0];
}

- (void)snippetSelected
{
    NSString* k = [self current];
    if (k == nil) return;
    _loading = YES;
    _editor.string = GRNS(_snippets.Get(GRStd(k)));
    _loading = NO;
}

- (void)textDidChange:(NSNotification*)notification
{
    (void)notification;
    NSString* k = [self current];
    if (!_loading && k != nil) _snippets.Set(GRStd(k), GRStd(_editor.string));
}

- (void)newSnippet
{
    NSString* name = [NSString stringWithFormat:@"snippet %d", _snippets.Count() + 1];
    _snippets.Set(GRStd(name), "");
    [self fill:name];
    [self.window makeFirstResponder:_editor];
}

- (void)deleteSnippet
{
    NSString* k = [self current];
    if (k == nil) return;
    _snippets.Remove(GRStd(k));
    [self fill:nil];
}

- (void)insertSnippet
{
    NSString* k = [self current];
    if (k == nil) return;
    _insertText = GRNS(_snippets.Get(GRStd(k)));
    [self finish:YES];
}
@end
