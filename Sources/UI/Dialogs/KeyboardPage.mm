// =============================================================================
//  KeyboardPage.mm - Settings > Keyboard (GRIDEd's OptionsDialog KeyboardPage)
//  The key set the bindings go over, every command with its shortcut, a key
//  recorder, Assign / Remove / Reset All. A binding of "" removes a key.
// =============================================================================
#import "SettingsWindow.h"
#import "DialogKit.h"
#import "../Commands.h"

using namespace GRIDEm;

// ---- a field that records the next key pressed ------------------------------------
@interface GRKeyRecorder : NSTextField
@end

@implementation GRKeyRecorder
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
- (void)mouseDown:(NSEvent*)event { (void)event; [self.window makeFirstResponder:self]; }

- (BOOL)becomeFirstResponder
{
    self.placeholderString = @"Press the keys…";
    return [super becomeFirstResponder];
}

- (void)record:(NSEvent*)event
{
    NSString* text = [GRCommandRegistry keyTextOfEvent:event];
    if (text.length > 0) self.stringValue = text;
}

- (void)keyDown:(NSEvent*)event { [self record:event]; }

// ⌘-keys come here first; taken only while the recorder has the focus
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if (self.window.firstResponder != self) return [super performKeyEquivalent:event];
    [self record:event];
    return YES;
}
@end

// -----------------------------------------------------------------------------
@implementation GRKeyboardPage
{
    NSMutableDictionary<NSString*, NSString*>* _bindings;     // the user's: id -> key text
    NSMutableArray<NSString*>* _bindingOrder;                 // the order they were in, then new ones
    NSPopUpButton* _set;
    NSTextField* _setNote;
    GRTable* _list;
    GRKeyRecorder* _press;
}

- (NSView*)makeContent
{
    _bindings = [NSMutableDictionary dictionary];
    _bindingOrder = [NSMutableArray array];
    for (const std::pair<std::string, std::string>& kv : self.work->KeyBindings.Items())
    {
        _bindings[GRNS(kv.first)] = GRNS(kv.second);
        [_bindingOrder addObject:GRNS(kv.first)];
    }
    GRForm* form = [[GRForm alloc] init];
    form.labelWidth = 140;
    NSArray<NSString*>* sets = [GRCommandRegistry keySetNames];
    _set = [form addPopup:@"Key set" items:sets selected:GRNS(self.work->KeySet)];
    if (_set.indexOfSelectedItem < 0) [_set selectItemWithTitle:@"macos"];
    _set.target = self;
    _set.action = @selector(setChosen:);
    _setNote = [form addNote:@""];
    _list = [[GRTable alloc] initWithHeight:300];
    [_list addColumn:@"Menu" width:90 kind:GRColumnText choices:nil];
    [_list addColumn:@"Command" width:300 kind:GRColumnText choices:nil];
    [_list addColumn:@"Shortcut" width:160 kind:GRColumnText choices:nil];
    _list.table.allowsMultipleSelection = NO;
    __weak GRKeyboardPage* weakSelf = self;
    _list.selectionChanged = ^{ [weakSelf commandSelected]; };
    [form addRow:@"Commands" view:_list.view];
    _press = [[GRKeyRecorder alloc] initWithFrame:NSMakeRect(0, 0, 200, 22)];
    _press.editable = NO;
    _press.selectable = NO;
    _press.bezeled = YES;
    _press.drawsBackground = YES;
    [_press.widthAnchor constraintEqualToConstant:220].active = YES;
    [form addRow:@"Press new shortcut" view:_press];
    NSButton* assign = [NSButton buttonWithTitle:@"Assign" target:self action:@selector(assign:)];
    NSButton* remove = [NSButton buttonWithTitle:@"Remove" target:self action:@selector(remove:)];
    NSButton* reset = [NSButton buttonWithTitle:@"Reset All" target:self action:@selector(resetAll:)];
    NSStackView* buttons = [NSStackView stackViewWithViews:@[assign, remove, reset]];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [form addRow:@"" view:buttons];
    [form addNote:@"Ctrl is the Control key, Cmd the Command key, Alt the Option key. A binding written on Windows "
                   "(\"Ctrl+Shift+B\") means Control on the Mac. Two-key chords are written \"Ctrl+K,C\"."];
    [self fill];
    return form.view;
}

- (NSString*)keySet
{
    NSString* s = _set.titleOfSelectedItem;
    return s != nil ? s : @"macos";
}

// the user's binding, else the set's
- (NSString*)shortcutOf:(GRCommand*)c
{
    NSString* v = _bindings[c.commandId];
    if (v != nil) return v;
    NSString* d = [[GRCommandRegistry shared] defaultShortcut:c.commandId inKeySet:self.keySet];
    return d != nil ? d : @"";
}

- (void)fill
{
    _setNote.stringValue = [GRCommandRegistry describeKeySet:self.keySet];
    NSInteger keep = _list.selectedRow;
    [_list clear];
    for (GRCommand* c in [GRCommandRegistry shared].all)
        [_list addRow:@[c.category != nil ? c.category : @"", c.title != nil ? c.title : c.commandId, [self shortcutOf:c]] tag:c];
    [_list reload];
    if (keep >= 0 && keep < (NSInteger)_list.rows.count)
        [_list.table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)keep] byExtendingSelection:NO];
}

- (GRCommand*)current
{
    NSInteger row = _list.selectedRow;
    if (row < 0 || row >= (NSInteger)_list.tags.count) return nil;
    id tag = _list.tags[(NSUInteger)row];
    return [tag isKindOfClass:[GRCommand class]] ? tag : nil;
}

- (void)commandSelected
{
    GRCommand* c = [self current];
    if (c != nil) _press.stringValue = [self shortcutOf:c];
}

- (void)setChosen:(id)sender { (void)sender; [self fill]; }

- (void)bind:(NSString*)commandId to:(NSString*)keys
{
    if (_bindings[commandId] == nil) [_bindingOrder addObject:commandId];
    _bindings[commandId] = keys;
}

- (void)assign:(id)sender
{
    (void)sender;
    GRCommand* c = [self current];
    NSString* keys = _press.stringValue;
    if (c == nil || keys.length == 0) return;
    GRCommand* clash = nil;
    for (GRCommand* o in [GRCommandRegistry shared].all)
        if (o != c && [[self shortcutOf:o] caseInsensitiveCompare:keys] == NSOrderedSame) { clash = o; break; }
    if (clash == nil)
    {
        [self bind:c.commandId to:keys];
        [self fill];
        return;
    }
    NSString* text = [NSString stringWithFormat:@"%@ is used by '%@'. Assign it anyway?", keys, clash.title];
    GRAskYesNo(self.view.window, @"Keyboard", text, ^(BOOL yes) {
        if (!yes) return;
        for (GRCommand* o in [GRCommandRegistry shared].all)
            if (o != c && [[self shortcutOf:o] caseInsensitiveCompare:keys] == NSOrderedSame) [self bind:o.commandId to:@""];
        [self bind:c.commandId to:keys];
        [self fill];
    });
}

- (void)remove:(id)sender
{
    (void)sender;
    GRCommand* c = [self current];
    if (c == nil) return;
    [self bind:c.commandId to:@""];
    _press.stringValue = @"";
    [self fill];
}

- (void)resetAll:(id)sender
{
    (void)sender;
    [_bindings removeAllObjects];
    [_bindingOrder removeAllObjects];
    [self fill];
}

- (void)commit
{
    Settings::OrderedStringMap bindings;
    for (NSString* key in _bindingOrder)
    {
        NSString* v = _bindings[key];
        if (v != nil) bindings.Set(GRStd(key), GRStd(v));
    }
    self.work->KeyBindings = bindings;
    self.work->KeySet = GRStd(self.keySet);
}
@end
