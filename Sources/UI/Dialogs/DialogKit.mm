// =============================================================================
//  DialogKit.mm - see DialogKit.h
// =============================================================================
#import "DialogKit.h"
#include "../../Core/StringUtil.h"
#include "../../Platform/Environment.h"
#include "../../Platform/Paths.h"

using GRIDEm::Core::StringUtil;

// ---- strings -----------------------------------------------------------------
NSString* GRJoinLines(const std::vector<std::string>& items)
{
    return GRNS(StringUtil::Join("\n", items));
}

std::vector<std::string> GRSplitLines(NSString* text)
{
    std::vector<std::string> r;
    for (const std::string& line : StringUtil::SplitLines(GRStd(text)))
    {
        std::string t = StringUtil::Trim(line);
        if (!t.empty()) r.push_back(t);
    }
    return r;
}

std::vector<std::string> GRSplitSemicolons(NSString* text)
{
    std::vector<std::string> r;
    for (const std::string& part : StringUtil::Split(GRStd(text), ';'))
    {
        std::string t = StringUtil::Trim(part);
        if (!t.empty()) r.push_back(t);
    }
    return r;
}

NSString* GRJoinSemicolons(const std::vector<std::string>& items)
{
    return GRNS(StringUtil::Join("; ", items));
}

NSArray<NSString*>* GRArray(const std::vector<std::string>& items)
{
    NSMutableArray<NSString*>* r = [NSMutableArray array];
    for (const std::string& s : items) [r addObject:GRNS(s)];
    return r;
}

std::vector<std::string> GRVector(NSArray<NSString*>* items)
{
    std::vector<std::string> r;
    for (NSString* s in items) r.push_back(GRStd(s));
    return r;
}

void GRShowMessage(NSWindow* window, NSString* title, NSString* text)
{
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = title != nil ? title : @"GRIDEm";
    alert.informativeText = text != nil ? text : @"";
    [alert addButtonWithTitle:@"OK"];
    if (window != nil) [alert beginSheetModalForWindow:window completionHandler:nil];
    else [alert runModal];
}

void GRAskYesNo(NSWindow* window, NSString* title, NSString* text, void (^done)(BOOL yes))
{
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = title != nil ? title : @"GRIDEm";
    alert.informativeText = text != nil ? text : @"";
    [alert addButtonWithTitle:@"Yes"];
    [alert addButtonWithTitle:@"No"];
    if (window != nil)
        [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse r) {
            if (done) done(r == NSAlertFirstButtonReturn);
        }];
    else
    {
        NSModalResponse r = [alert runModal];
        if (done) done(r == NSAlertFirstButtonReturn);
    }
}

// ---- the form ------------------------------------------------------------------
@interface GRFlippedView : NSView
@end
@implementation GRFlippedView
- (BOOL)isFlipped { return YES; }
@end

@implementation GRForm
{
    NSScrollView* _view;
    NSGridView* _grid;
}

- (instancetype)init
{
    if ((self = [super init]))
    {
        _labelWidth = 170;
        _grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
        _grid.translatesAutoresizingMaskIntoConstraints = NO;
        _grid.rowSpacing = 8;
        _grid.columnSpacing = 10;
        _grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
        [_grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        [_grid columnAtIndex:0].width = _labelWidth;
        [_grid columnAtIndex:1].xPlacement = NSGridCellPlacementFill;

        GRFlippedView* doc = [[GRFlippedView alloc] initWithFrame:NSMakeRect(0, 0, 600, 400)];
        doc.translatesAutoresizingMaskIntoConstraints = NO;
        [doc addSubview:_grid];
        [NSLayoutConstraint activateConstraints:@[
            [_grid.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor constant:16],
            [_grid.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor constant:-16],
            [_grid.topAnchor constraintEqualToAnchor:doc.topAnchor constant:14],
            [_grid.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor constant:-14],
        ]];

        _view = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 600, 400)];
        _view.hasVerticalScroller = YES;
        _view.hasHorizontalScroller = NO;
        _view.drawsBackground = NO;
        _view.borderType = NSNoBorder;
        _view.documentView = doc;
        NSClipView* clip = _view.contentView;
        [NSLayoutConstraint activateConstraints:@[
            [doc.leadingAnchor constraintEqualToAnchor:clip.leadingAnchor],
            [doc.topAnchor constraintEqualToAnchor:clip.topAnchor],
            [doc.widthAnchor constraintEqualToAnchor:clip.widthAnchor],
        ]];
    }
    return self;
}

- (NSScrollView*)view { return _view; }
- (NSGridView*)grid { return _grid; }

- (void)setLabelWidth:(CGFloat)labelWidth
{
    _labelWidth = labelWidth;
    [_grid columnAtIndex:0].width = labelWidth;
}

- (void)addSection:(NSString*)title
{
    NSTextField* label = [NSTextField labelWithString:title];
    label.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
    NSGridRow* row = [_grid addRowWithViews:@[label, [NSGridCell emptyContentView]]];
    [row mergeCellsInRange:NSMakeRange(0, 2)];
    [row cellAtIndex:0].xPlacement = NSGridCellPlacementLeading;
    if (_grid.numberOfRows > 1) row.topPadding = 12;
}

- (void)addRow:(NSString*)label view:(NSView*)view
{
    NSTextField* l = [NSTextField labelWithString:label != nil ? label : @""];
    l.alignment = NSTextAlignmentRight;
    l.lineBreakMode = NSLineBreakByWordWrapping;
    l.preferredMaxLayoutWidth = _labelWidth;
    NSGridRow* row = [_grid addRowWithViews:@[l, view]];
    // a list, a table or a text box has no baseline: its label sits at its top
    if (![view isKindOfClass:[NSControl class]] && ![view isKindOfClass:[GRPathField class]])
    {
        row.rowAlignment = NSGridRowAlignmentNone;
        row.yPlacement = NSGridCellPlacementTop;
    }
}

- (NSTextField*)addNote:(NSString*)text
{
    NSTextField* note = [NSTextField wrappingLabelWithString:text != nil ? text : @""];
    note.textColor = [NSColor secondaryLabelColor];
    note.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    note.preferredMaxLayoutWidth = 520;
    [self addRow:@"" view:note];
    return note;
}

- (NSTextField*)addText:(NSString*)label value:(NSString*)value
{
    NSTextField* f = [NSTextField textFieldWithString:value != nil ? value : @""];
    [f.widthAnchor constraintGreaterThanOrEqualToConstant:300].active = YES;
    [self addRow:label view:f];
    return f;
}

- (NSTextField*)addMono:(NSString*)label value:(NSString*)value
{
    NSTextField* f = [self addText:label value:value];
    f.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    return f;
}

- (NSButton*)addCheck:(NSString*)title on:(BOOL)on
{
    NSButton* b = [NSButton checkboxWithTitle:title target:nil action:nil];
    b.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    [self addRow:@"" view:b];
    return b;
}

- (NSPopUpButton*)addPopup:(NSString*)label items:(NSArray<NSString*>*)items selected:(NSString*)selected
{
    NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [p addItemsWithTitles:items];
    if (selected != nil) [p selectItemWithTitle:selected];
    if (p.indexOfSelectedItem < 0 && p.numberOfItems > 0) [p selectItemAtIndex:0];
    [self addRow:label view:p];
    return p;
}

- (NSTextField*)addNumber:(NSString*)label value:(double)value min:(double)min max:(double)max decimals:(int)decimals
{
    NSNumberFormatter* fmt = [[NSNumberFormatter alloc] init];
    fmt.numberStyle = NSNumberFormatterDecimalStyle;
    fmt.usesGroupingSeparator = NO;
    fmt.minimum = @(min);
    fmt.maximum = @(max);
    fmt.maximumFractionDigits = (NSUInteger)decimals;
    fmt.minimumFractionDigits = 0;
    NSTextField* f = [NSTextField textFieldWithString:@""];
    f.formatter = fmt;
    f.doubleValue = value < min ? min : (value > max ? max : value);
    [f.widthAnchor constraintEqualToConstant:90].active = YES;
    [self addRow:label view:f];
    return f;
}

- (NSTextView*)addLines:(NSString*)label text:(NSString*)text height:(CGFloat)height mono:(BOOL)mono
{
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, height)];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextView* tv = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 400, height)];
    tv.richText = NO;
    tv.allowsUndo = YES;
    tv.automaticQuoteSubstitutionEnabled = NO;
    tv.automaticDashSubstitutionEnabled = NO;
    tv.automaticTextReplacementEnabled = NO;
    tv.automaticSpellingCorrectionEnabled = NO;
    tv.verticallyResizable = YES;
    tv.horizontallyResizable = NO;
    tv.autoresizingMask = NSViewWidthSizable;
    tv.textContainer.widthTracksTextView = YES;
    tv.font = mono ? [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular]
                   : [NSFont systemFontOfSize:[NSFont systemFontSize]];
    tv.string = text != nil ? text : @"";
    scroll.documentView = tv;
    [scroll.heightAnchor constraintEqualToConstant:height].active = YES;
    [scroll.widthAnchor constraintGreaterThanOrEqualToConstant:300].active = YES;
    [self addRow:label view:scroll];
    return tv;
}
@end

// ---- a path with its status ------------------------------------------------------
@implementation GRPathField
{
    NSTextField* _field;
    NSButton* _choose;
    NSTextField* _status;
}

- (instancetype)initFolder:(BOOL)folder status:(BOOL)status
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, 420, 24)]))
    {
        _folder = folder;
        _showStatus = status;
        _baseDir = @"";
        _field = [NSTextField textFieldWithString:@""];
        _field.target = self;
        _field.action = @selector(fieldChanged:);
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(textChanged:)
                                                     name:NSControlTextDidChangeNotification object:_field];
        _choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(choose:)];
        _status = [NSTextField labelWithString:@""];
        _status.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
        [_status.widthAnchor constraintEqualToConstant:status ? 110 : 0].active = YES;
        NSStackView* stack = [NSStackView stackViewWithViews:@[_field, _choose, _status]];
        stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        stack.spacing = 6;
        stack.translatesAutoresizingMaskIntoConstraints = NO;
        [_field setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [_field.widthAnchor constraintGreaterThanOrEqualToConstant:220].active = YES;
        [self addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[
            [stack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [stack.topAnchor constraintEqualToAnchor:self.topAnchor],
            [stack.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (NSTextField*)field { return _field; }

- (NSString*)value
{
    return [_field.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

- (void)setValue:(NSString*)value
{
    _field.stringValue = value != nil ? value : @"";
    [self updateStatus];
}

- (void)setBaseDir:(NSString*)baseDir
{
    _baseDir = [baseDir copy];
    [self updateStatus];
}

- (NSView*)viewForLastBaselineLayout { return _field; }
- (NSView*)viewForFirstBaselineLayout { return _field; }

- (void)fieldChanged:(id)sender { (void)sender; [self textChanged:nil]; }

- (void)textChanged:(NSNotification*)note
{
    (void)note;
    [self updateStatus];
    if (self.changed) self.changed();
}

- (void)updateStatus
{
    if (!_showStatus) { _status.stringValue = @""; return; }
    NSString* v = self.value;
    if (v.length == 0)
    {
        _status.stringValue = @"(not set)";
        _status.textColor = [NSColor secondaryLabelColor];
        return;
    }
    BOOL ok;
    if (self.check) ok = self.check(v);
    else
    {
        std::string full = GRIDEm::Platform::Environment::Expand(GRStd(v));
        if (!GRIDEm::Platform::Paths::IsRooted(full) && _baseDir.length > 0)
            full = GRIDEm::Platform::Paths::Combine(GRIDEm::Platform::Environment::Expand(GRStd(_baseDir)), full);
        ok = _folder ? GRIDEm::Platform::Paths::IsDirectory(full) : GRIDEm::Platform::Paths::IsFile(full);
    }
    _status.stringValue = ok ? @"✓ Found" : @"! Not installed";
    _status.textColor = ok ? [NSColor systemGreenColor] : [NSColor systemOrangeColor];
}

- (void)choose:(id)sender
{
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = !_folder;
    panel.canChooseDirectories = _folder;
    panel.allowsMultipleSelection = NO;
    panel.canCreateDirectories = _folder;
    NSString* current = self.value;
    if (current.length > 0)
    {
        NSString* dir = _folder ? current : current.stringByDeletingLastPathComponent;
        if (dir.length > 0) panel.directoryURL = [NSURL fileURLWithPath:dir.stringByExpandingTildeInPath];
    }
    __weak GRPathField* weakSelf = self;
    void (^finish)(NSModalResponse) = ^(NSModalResponse r) {
        GRPathField* me = weakSelf;
        if (me == nil || r != NSModalResponseOK || panel.URL == nil) return;
        NSString* chosen = panel.URL.path;
        // kept short when the file is inside the base folder, as GRIDEd keeps it
        if (!me.folder && me.baseDir.length > 0
            && [chosen.stringByDeletingLastPathComponent isEqualToString:me.baseDir.stringByStandardizingPath])
            chosen = chosen.lastPathComponent;
        me.value = chosen;
        if (me.changed) me.changed();
    };
    if (self.window != nil) [panel beginSheetModalForWindow:self.window completionHandler:finish];
    else finish([panel runModal]);
}
@end

// ---- check boxes in a list ---------------------------------------------------------
@implementation GRCheckList
{
    NSScrollView* _view;
    NSTableView* _table;
}

- (instancetype)initWithHeight:(CGFloat)height
{
    if ((self = [super init]))
    {
        _titles = [NSMutableArray array];
        _checks = [NSMutableArray array];
        _enabled = YES;
        _table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 300, height)];
        NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"check"];
        col.resizingMask = NSTableColumnAutoresizingMask;
        [_table addTableColumn:col];
        _table.headerView = nil;
        _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
        _table.rowHeight = 20;
        _table.dataSource = self;
        _table.delegate = self;
        _view = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 300, height)];
        _view.documentView = _table;
        _view.hasVerticalScroller = YES;
        _view.borderType = NSBezelBorder;
        _view.translatesAutoresizingMaskIntoConstraints = NO;
        [_view.heightAnchor constraintGreaterThanOrEqualToConstant:height].active = YES;
        [_view.widthAnchor constraintGreaterThanOrEqualToConstant:220].active = YES;
    }
    return self;
}

- (NSScrollView*)view { return _view; }
- (NSTableView*)table { return _table; }

- (void)setItems:(NSArray<NSString*>*)titles checked:(NSArray<NSNumber*>*)checks
{
    _titles = [titles mutableCopy];
    _checks = [NSMutableArray array];
    for (NSUInteger i = 0; i < titles.count; i++)
        [_checks addObject:(i < checks.count ? checks[i] : @NO)];
    [_table reloadData];
}

- (BOOL)isChecked:(NSInteger)row
{
    return row >= 0 && row < (NSInteger)_checks.count && _checks[(NSUInteger)row].boolValue;
}

- (void)setAll:(BOOL)on
{
    for (NSUInteger i = 0; i < _checks.count; i++) _checks[i] = @(on);
    [_table reloadData];
}

- (void)setEnabled:(BOOL)enabled
{
    _enabled = enabled;
    [_table reloadData];
}

- (NSArray<NSString*>*)checkedTitles
{
    NSMutableArray<NSString*>* r = [NSMutableArray array];
    for (NSUInteger i = 0; i < _titles.count; i++)
        if (_checks[i].boolValue) [r addObject:_titles[i]];
    return r;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView { (void)tableView; return (NSInteger)_titles.count; }

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row
{
    (void)column;
    NSButton* box = [tableView makeViewWithIdentifier:@"checkCell" owner:self];
    if (box == nil)
    {
        box = [NSButton checkboxWithTitle:@"" target:self action:@selector(toggle:)];
        box.identifier = @"checkCell";
    }
    box.title = _titles[(NSUInteger)row];
    box.state = _checks[(NSUInteger)row].boolValue ? NSControlStateValueOn : NSControlStateValueOff;
    box.enabled = _enabled;
    return box;
}

- (void)toggle:(NSButton*)sender
{
    NSInteger row = [_table rowForView:sender];
    if (row < 0) return;
    BOOL on = sender.state == NSControlStateValueOn;
    _checks[(NSUInteger)row] = @(on);
    if (self.toggled) self.toggled(row, on);
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    if (self.selected) self.selected(_table.selectedRow);
}
@end

// ---- a table ---------------------------------------------------------------------
@implementation GRTable
{
    NSScrollView* _view;
    NSTableView* _table;
    NSMutableArray<NSNumber*>* _kinds;
    NSMutableArray* _choices;
    NSMutableDictionary<NSString*, NSColor*>* _colors;
}

- (instancetype)initWithHeight:(CGFloat)height
{
    if ((self = [super init]))
    {
        _rows = [NSMutableArray array];
        _tags = [NSMutableArray array];
        _kinds = [NSMutableArray array];
        _choices = [NSMutableArray array];
        _colors = [NSMutableDictionary dictionary];
        _table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 500, height)];
        _table.usesAlternatingRowBackgroundColors = YES;
        _table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
        _table.allowsMultipleSelection = YES;
        _table.rowHeight = 20;
        _table.dataSource = self;
        _table.delegate = self;
        _table.target = self;
        _table.doubleAction = @selector(doubleClick:);
        _view = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 500, height)];
        _view.documentView = _table;
        _view.hasVerticalScroller = YES;
        _view.hasHorizontalScroller = YES;
        _view.borderType = NSBezelBorder;
        _view.translatesAutoresizingMaskIntoConstraints = NO;
        [_view.heightAnchor constraintGreaterThanOrEqualToConstant:height].active = YES;
        [_view.widthAnchor constraintGreaterThanOrEqualToConstant:300].active = YES;
    }
    return self;
}

- (NSScrollView*)view { return _view; }
- (NSTableView*)table { return _table; }

- (void)addColumn:(NSString*)title width:(CGFloat)width kind:(GRColumnKind)kind choices:(NSArray<NSString*>*)choices
{
    NSString* ident = [NSString stringWithFormat:@"c%lu", (unsigned long)_kinds.count];
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:ident];
    col.title = title;
    col.width = width;
    col.minWidth = 40;
    col.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
    [_table addTableColumn:col];
    [_kinds addObject:@(kind)];
    [_choices addObject:choices != nil ? choices : @[]];
}

- (void)addRow:(NSArray<NSString*>*)cells tag:(id)tag
{
    NSMutableArray<NSString*>* row = [NSMutableArray array];
    for (NSUInteger i = 0; i < _kinds.count; i++) [row addObject:(i < cells.count ? cells[i] : @"")];
    [_rows addObject:row];
    [_tags addObject:tag != nil ? tag : [NSNull null]];
}

- (void)clear
{
    [_rows removeAllObjects];
    [_tags removeAllObjects];
    [_colors removeAllObjects];
}

- (void)reload { [_table reloadData]; }
- (NSInteger)selectedRow { return _table.selectedRow; }
- (NSIndexSet*)selectedRows { return _table.selectedRowIndexes; }

- (void)setColor:(NSColor*)color row:(NSInteger)row column:(NSInteger)column
{
    NSString* key = [NSString stringWithFormat:@"%ld:%ld", (long)row, (long)column];
    if (color != nil) _colors[key] = color;
    else [_colors removeObjectForKey:key];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView { (void)tableView; return (NSInteger)_rows.count; }

- (NSInteger)columnOf:(NSTableColumn*)column
{
    return [[column.identifier substringFromIndex:1] integerValue];
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    (void)tableView;
    NSInteger c = [self columnOf:tableColumn];
    GRColumnKind kind = (GRColumnKind)[_kinds[(NSUInteger)c] integerValue];
    NSString* value = _rows[(NSUInteger)row][(NSUInteger)c];
    if (kind == GRColumnPopup && !_readOnly)
    {
        NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, tableColumn.width, 20) pullsDown:NO];
        p.controlSize = NSControlSizeSmall;
        p.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
        p.bordered = NO;
        [p addItemsWithTitles:_choices[(NSUInteger)c]];
        [p selectItemWithTitle:value];
        if (p.indexOfSelectedItem < 0 && p.numberOfItems > 0) [p selectItemAtIndex:0];
        p.tag = c;
        p.target = self;
        p.action = @selector(popupChanged:);
        return p;
    }
    NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, tableColumn.width, 18)];
    f.bordered = NO;
    f.drawsBackground = NO;
    f.lineBreakMode = NSLineBreakByTruncatingTail;
    f.stringValue = value;
    f.toolTip = value.length > 40 ? value : nil;
    f.tag = c;
    BOOL editable = kind == GRColumnEdit && !_readOnly;
    f.editable = editable;
    f.selectable = YES;
    if (editable) f.delegate = self;
    NSColor* color = _colors[[NSString stringWithFormat:@"%ld:%ld", (long)row, (long)c]];
    f.textColor = color != nil ? color : [NSColor labelColor];
    return f;
}

- (void)controlTextDidEndEditing:(NSNotification*)note
{
    NSTextField* f = note.object;
    NSInteger row = [_table rowForView:f];
    if (row < 0 || row >= (NSInteger)_rows.count) return;
    NSInteger c = f.tag;
    _rows[(NSUInteger)row][(NSUInteger)c] = f.stringValue;
    if (self.edited) self.edited(row, c, f.stringValue);
}

- (void)popupChanged:(NSPopUpButton*)sender
{
    NSInteger row = [_table rowForView:sender];
    if (row < 0 || row >= (NSInteger)_rows.count) return;
    NSString* v = sender.titleOfSelectedItem != nil ? sender.titleOfSelectedItem : @"";
    _rows[(NSUInteger)row][(NSUInteger)sender.tag] = v;
    if (self.edited) self.edited(row, sender.tag, v);
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    if (self.selectionChanged) self.selectionChanged();
}

- (void)doubleClick:(id)sender
{
    (void)sender;
    NSInteger row = _table.clickedRow;
    if (row >= 0 && self.doubleClicked) self.doubleClicked(row);
}
@end
