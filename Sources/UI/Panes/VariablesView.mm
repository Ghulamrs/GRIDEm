// =============================================================================
//  VariablesView.mm - see VariablesView.h
// =============================================================================
#import "VariablesView.h"
#import "../Common.h"
#import "PaneUtil.h"
#import "../Theme.h"

using namespace GRIDEm;

// one row as shown
@interface GRVarRow : NSObject
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* value;
@property (nonatomic, copy) NSString* type;
@property (nonatomic, copy) NSString* address;
@property (nonatomic, assign) BOOL changed;    // since the last stop
@property (nonatomic, assign) BOOL muted;      // "(nothing in scope here)", a watch that could not be read, "add a watch..."
@property (nonatomic, assign) BOOL isVariable; // a real variable or watch (editable, double-clickable)
@end
@implementation GRVarRow
@end

@interface GRVarTable : NSTableView
@property (nonatomic, copy) void (^keyPressed)(unichar c);
@end
@implementation GRVarTable
- (void)keyDown:(NSEvent*)event
{
    unichar c = event.charactersIgnoringModifiers.length > 0 ? [event.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if ((c == NSCarriageReturnCharacter || c == NSEnterCharacter || c == NSDeleteCharacter || c == NSDeleteFunctionKey)
        && self.keyPressed) { self.keyPressed(c); return; }
    [super keyDown:event];
}
@end

static NSString* const kAddWatchRow = @"add a watch...";

// RIDE: a value with no place is "-", a missing value nothing
static NSString* AddressText(const std::string& address, const std::string& value)
{
    if (!address.empty()) return GRNS(address);
    return value.empty() ? @"" : @"-";
}

@implementation GRVariablesView
{
    GRVariablesKind _kind;
    GRVarTable* _table;
    NSTextField* _stopLabel;
    NSTextField* _keysLabel;
    NSTextField* _line;              // "name = value" (Locals), an expression (Watch)
    NSMutableArray<GRVarRow*>* _rows;
    NSMutableArray<NSString*>* _expressions;
    NSInteger _editing;              // Watch: the watch the line changes, -1 to add
    // ValueMarks: key -> value at the last stop, and what this stop has shown
    NSMutableDictionary<NSString*, NSString*>* _before;
    NSMutableDictionary<NSString*, NSString*>* _seen;
}

- (instancetype)initWithKind:(GRVariablesKind)kind
{
    self = kind == GRVariablesLocals
        ? [super initWithIdentifier:@"Locals" title:@"Locals" symbol:@"list.bullet.rectangle"]
        : [super initWithIdentifier:@"Watch" title:@"Watch" symbol:@"eye"];
    if (self != nil)
    {
        _kind = kind;
        _rows = [NSMutableArray array];
        _expressions = [NSMutableArray array];
        _editing = -1;
        _before = [NSMutableDictionary dictionary];
        _seen = [NSMutableDictionary dictionary];
    }
    return self;
}

- (GRVariablesKind)kind { return _kind; }

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    _table = [[GRVarTable alloc] initWithFrame:NSZeroRect];
    NSArray* titles = @[ @"Name", @"Value", @"Type", @"Address" ];
    NSArray* widths = _kind == GRVariablesLocals ? @[ @160, @300, @140, @150 ] : @[ @180, @300, @130, @150 ];
    for (NSUInteger i = 0; i < titles.count; i++)
    {
        NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:titles[i]];
        c.title = titles[i];
        c.width = [widths[i] doubleValue];
        [_table addTableColumn:c];
    }
    _table.rowSizeStyle = NSTableViewRowSizeStyleSmall;
    _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    _table.usesAlternatingRowBackgroundColors = YES;
    _table.dataSource = self;
    _table.delegate = self;
    _table.target = self;
    _table.doubleAction = @selector(doubleClicked:);
    __weak GRVariablesView* weakSelf = self;
    _table.keyPressed = ^(unichar c) { [weakSelf tableKey:c]; };
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.documentView = _table;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;

    _line = [GRPaneUtil inputFieldWithPlaceholder:_kind == GRVariablesLocals
                 ? @"name = value sets a variable (Return)" : @"an expression to watch - Return adds it"];
    _line.delegate = self;
    if (_kind == GRVariablesLocals)
    {
        _stopLabel = [GRPaneUtil label:@""];
        _stopLabel.font = [GRPaneUtil boldMonoFont];
        _keysLabel = [GRPaneUtil label:@""];
        _keysLabel.textColor = [NSColor secondaryLabelColor];
        NSStackView* top = [GRPaneUtil barWithViews:@[ _stopLabel ]];
        NSStackView* bottom = [GRPaneUtil barWithViews:@[ _keysLabel ]];
        [GRPaneUtil stack:@[ top, scroll, _line, bottom ] in:root grow:scroll];
    }
    else [GRPaneUtil stack:@[ scroll, _line ] in:root grow:scroll];
    self.view = root;
    if (_kind == GRVariablesWatch) [self showWatchRowsOnly];
}

// ---- marks (GRIDEd's ValueMarks) ---------------------------------------------
- (void)newStop
{
    _before = _seen;
    _seen = [NSMutableDictionary dictionary];
}

- (BOOL)mark:(NSString*)key value:(NSString*)value atStop:(BOOL)atStop
{
    NSString* was = _before[key];
    BOOL changed = was != nil && ![was isEqualToString:value];
    if (atStop) _seen[key] = value != nil ? value : @"";
    return changed;
}

- (void)clearView
{
    _before = [NSMutableDictionary dictionary];
    _seen = [NSMutableDictionary dictionary];
    _editing = -1;
    [_rows removeAllObjects];
    if (self.isViewLoaded)
    {
        _stopLabel.stringValue = @"";
        if (_kind == GRVariablesWatch) [self showWatchRowsOnly];
        else [_table reloadData];
    }
}

// ---- Locals --------------------------------------------------------------------
- (void)setStopLine:(NSString*)stopLine
{
    if (!self.isViewLoaded) [self loadView];
    _stopLabel.stringValue = stopLine != nil ? stopLine : @"";
}

- (NSString*)keysLine { return _keysLabel.stringValue; }

- (void)setKeysLine:(NSString*)keysLine
{
    if (!self.isViewLoaded) [self loadView];
    _keysLabel.stringValue = keysLine != nil ? keysLine : @"";
}

- (void)setVariables:(const std::vector<Services::DebugVariable>&)variables
           frameName:(NSString*)frameName atStop:(BOOL)atStop whyNone:(NSString*)whyNone
{
    if (!self.isViewLoaded) [self loadView];
    [_rows removeAllObjects];
    for (const Services::DebugVariable& v : variables)
    {
        GRVarRow* r = [[GRVarRow alloc] init];
        r.name = GRNS(v.Name);
        r.value = GRNS(v.Value);
        r.type = GRNS(v.Type);
        r.address = AddressText(v.Address, v.Value);
        r.isVariable = YES;
        r.changed = [self mark:[NSString stringWithFormat:@"%@|%@", GROr(frameName, @""), r.name] value:r.value atStop:atStop];
        [_rows addObject:r];
    }
    if (variables.empty())
    {
        GRVarRow* none = [[GRVarRow alloc] init];
        none.name = [NSString stringWithFormat:@"(%@)", whyNone.length > 0 ? whyNone : @"nothing in scope here"];
        none.value = none.type = none.address = @"";
        none.muted = YES;
        [_rows addObject:none];
    }
    [_table reloadData];
}

- (void)focusSet { [_line.window makeFirstResponder:_line]; }

// ---- Watch ---------------------------------------------------------------------
- (NSArray<NSString*>*)expressions { return [_expressions copy]; }

- (void)setExpressions:(NSArray<NSString*>*)expressions
{
    _expressions = GROr([expressions mutableCopy], [NSMutableArray array]);
    if (self.isViewLoaded) [self showWatchRowsOnly];
}

// no session: the expressions alone, with nothing read
- (void)showWatchRowsOnly
{
    [_rows removeAllObjects];
    for (NSString* x in _expressions)
    {
        GRVarRow* r = [[GRVarRow alloc] init];
        r.name = x;
        r.value = r.type = r.address = @"";
        r.isVariable = YES;
        [_rows addObject:r];
    }
    [self addWatchRow];
    [_table reloadData];
}

- (void)addWatchRow
{
    GRVarRow* add = [[GRVarRow alloc] init];
    add.name = kAddWatchRow;
    add.value = add.type = add.address = @"";
    add.muted = YES;
    [_rows addObject:add];
}

- (void)setWatches:(const std::vector<Services::DebugWatch>&)watches paused:(BOOL)paused atStop:(BOOL)atStop
{
    if (!self.isViewLoaded) [self loadView];
    [_rows removeAllObjects];
    for (const Services::DebugWatch& w : watches)
    {
        GRVarRow* r = [[GRVarRow alloc] init];
        r.name = GRNS(w.Expression);
        r.value = GRNS(w.Value);
        r.type = GRNS(w.Type);
        r.address = AddressText(w.Address, w.Value);
        r.isVariable = YES;
        if (!w.Ok) r.muted = YES;
        else if (paused) r.changed = [self mark:[@"watch|" stringByAppendingString:r.name] value:r.value atStop:atStop];
        [_rows addObject:r];
    }
    [self addWatchRow];
    [_table reloadData];
}

- (void)focusAdd { [_line.window makeFirstResponder:_line]; }

// ---- the table -------------------------------------------------------------------
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return (NSInteger)_rows.count;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    GRVarRow* r = _rows[(NSUInteger)row];
    NSString* column = tableColumn.identifier;
    NSTableCellView* cell = [GRPaneUtil cellIn:tableView identifier:column];
    cell.imageView.image = nil;
    NSTextField* f = cell.textField;
    f.font = [GRPaneUtil monoFont];
    f.textColor = r.muted ? [NSColor secondaryLabelColor] : [NSColor labelColor];
    f.editable = NO;
    f.delegate = nil;
    if ([column isEqualToString:@"Name"]) f.stringValue = GROr(r.name, @"");
    else if ([column isEqualToString:@"Type"]) f.stringValue = GROr(r.type, @"");
    else if ([column isEqualToString:@"Address"]) f.stringValue = GROr(r.address, @"");
    else
    {
        f.stringValue = GROr(r.value, @"");
        if (r.changed)
        {
            f.textColor = [[GRTheme current] resolvedFor:tableView.effectiveAppearance].errorText;
            f.font = [GRPaneUtil boldMonoFont];
        }
        // a Locals value is edited in place: the set-variable request
        if (_kind == GRVariablesLocals && r.isVariable)
        {
            f.editable = YES;
            f.delegate = self;
        }
    }
    f.toolTip = f.stringValue.length > 40 ? f.stringValue : nil;
    return cell;
}

- (void)doubleClicked:(id)sender
{
    (void)sender;
    NSInteger row = _table.clickedRow;
    if (row < 0) return;
    [self chooseRow:row];
}

- (void)chooseRow:(NSInteger)row
{
    if (row < 0 || row >= (NSInteger)_rows.count) return;
    GRVarRow* r = _rows[(NSUInteger)row];
    if (_kind == GRVariablesLocals)
    {
        if (!r.isVariable) return;
        _line.stringValue = [NSString stringWithFormat:@"%@ = %@", r.name, r.value];
        [self focusSet];
        return;
    }
    if (row >= (NSInteger)_expressions.count) { _editing = -1; _line.stringValue = @""; [self focusAdd]; return; }
    _editing = row;
    _line.stringValue = _expressions[(NSUInteger)row];
    [self focusAdd];
}

- (void)tableKey:(unichar)c
{
    NSInteger row = _table.selectedRow;
    if (c == NSCarriageReturnCharacter || c == NSEnterCharacter) { [self chooseRow:row]; return; }
    // Delete takes a watch away
    if (_kind != GRVariablesWatch || row < 0 || row >= (NSInteger)_expressions.count) return;
    [_expressions removeObjectAtIndex:(NSUInteger)row];
    if (self.watchRemoved) self.watchRemoved(row);
    else [self showWatchRowsOnly];
}

// ---- the line under the table ----------------------------------------------------
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (control != _line) return NO;
    if (commandSelector == @selector(cancelOperation:)) { _editing = -1; _line.stringValue = @""; return YES; }
    if (commandSelector != @selector(insertNewline:)) return NO;
    NSString* text = [_line.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (_kind == GRVariablesLocals)
    {
        NSRange eq = [text rangeOfString:@"="];
        if (eq.location == NSNotFound || eq.location == 0) return YES;
        NSString* name = [[text substringToIndex:eq.location] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        NSString* value = [[text substringFromIndex:eq.location + 1] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (name.length == 0 || value.length == 0) return YES;
        _line.stringValue = @"";
        if (self.setRequested) self.setRequested(name, value);
        return YES;
    }
    if (_editing >= 0 && _editing < (NSInteger)_expressions.count)
    {
        NSInteger i = _editing;
        _editing = -1;
        if (text.length == 0)
        {
            [_expressions removeObjectAtIndex:(NSUInteger)i];
            if (self.watchRemoved) self.watchRemoved(i);
        }
        else
        {
            _expressions[(NSUInteger)i] = text;
            if (self.watchChanged) self.watchChanged(i, text);
        }
    }
    else if (text.length > 0)
    {
        [_expressions addObject:text];
        if (self.watchAdded) self.watchAdded(text);
    }
    _line.stringValue = @"";
    if (!self.watchAdded && !self.watchChanged && !self.watchRemoved) [self showWatchRowsOnly];
    return YES;
}

// a Locals value edited in its cell
- (void)controlTextDidEndEditing:(NSNotification*)note
{
    NSTextField* f = note.object;
    if (f == _line) return;
    NSInteger row = [_table rowForView:f];
    if (row < 0 || row >= (NSInteger)_rows.count) return;
    GRVarRow* r = _rows[(NSUInteger)row];
    NSString* value = [f.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (!r.isVariable || value.length == 0 || [value isEqualToString:r.value]) { f.stringValue = GROr(r.value, @""); return; }
    if (self.setRequested) self.setRequested(r.name, value);
}
@end
