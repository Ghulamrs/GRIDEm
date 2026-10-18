// =============================================================================
//  BreakpointsNavigator.mm - see BreakpointsNavigator.h
// =============================================================================
#import "BreakpointsNavigator.h"
#import "PaneUtil.h"
#import "../Icons.h"
#import "../Theme.h"

@interface GRBreakpointRow : NSObject
@property (nonatomic, copy) NSString* file;
@property (nonatomic, assign) NSInteger line;
@end
@implementation GRBreakpointRow
@end

@interface GRBreakpointsTable : NSTableView
@property (nonatomic, copy) void (^deletePressed)(void);
@end

@implementation GRBreakpointsTable
- (void)keyDown:(NSEvent*)event
{
    unichar c = event.charactersIgnoringModifiers.length > 0 ? [event.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if ((c == NSDeleteCharacter || c == NSDeleteFunctionKey) && self.deletePressed) { self.deletePressed(); return; }
    [super keyDown:event];
}
@end

@implementation GRBreakpointsNavigator
{
    GRBreakpointsTable* _table;
    NSMutableArray<GRBreakpointRow*>* _rows;
}

- (instancetype)init
{
    self = [super initWithIdentifier:@"Breakpoints" title:@"Breakpoints" symbol:@"smallcircle.filled.circle"];
    if (self != nil) _rows = [NSMutableArray array];
    return self;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 300)];
    _table = [[GRBreakpointsTable alloc] initWithFrame:NSZeroRect];
    NSArray* titles = @[ @"File", @"Line" ];
    NSArray* widths = @[ @190, @50 ];
    for (NSUInteger i = 0; i < titles.count; i++)
    {
        NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:titles[i]];
        c.title = titles[i];
        c.width = [widths[i] doubleValue];
        [_table addTableColumn:c];
    }
    _table.rowSizeStyle = NSTableViewRowSizeStyleSmall;
    _table.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;
    _table.dataSource = self;
    _table.delegate = self;
    _table.target = self;
    _table.doubleAction = @selector(doubleClicked:);
    __weak GRBreakpointsNavigator* weakSelf = self;
    _table.deletePressed = ^{ [weakSelf removeSelected]; };
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.documentView = _table;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;
    [GRPaneUtil fill:root with:scroll];
    self.view = root;
}

- (void)fill:(const GRIDEm::Model::BreakpointTable&)breakpoints
{
    if (!self.isViewLoaded) [self loadView];
    [_rows removeAllObjects];
    for (const auto& kv : breakpoints.Items())
        for (int line : kv.second)
        {
            GRBreakpointRow* r = [[GRBreakpointRow alloc] init];
            r.file = GRNS(kv.first);
            r.line = line;
            [_rows addObject:r];
        }
    [_table reloadData];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return (NSInteger)_rows.count;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    GRBreakpointRow* r = _rows[(NSUInteger)row];
    BOOL fileColumn = [tableColumn.identifier isEqualToString:@"File"];
    NSTableCellView* cell = [GRPaneUtil cellIn:tableView identifier:fileColumn ? @"BpFile" : @"BpLine"];
    if (fileColumn)
    {
        cell.imageView.image = [GRIcons dot:[[GRTheme current] resolvedFor:tableView.effectiveAppearance].breakpoint size:10];
        cell.textField.stringValue = r.file.lastPathComponent;
        cell.toolTip = r.file;
    }
    else
    {
        cell.imageView.image = nil;
        cell.textField.stringValue = [NSString stringWithFormat:@"%ld", (long)r.line];
    }
    return cell;
}

- (GRBreakpointRow*)current
{
    NSInteger row = _table.selectedRow;
    if (row < 0 || row >= (NSInteger)_rows.count) return nil;
    return _rows[(NSUInteger)row];
}

- (void)doubleClicked:(id)sender
{
    (void)sender;
    NSInteger row = _table.clickedRow;
    if (row < 0 || row >= (NSInteger)_rows.count) return;
    GRBreakpointRow* r = _rows[(NSUInteger)row];
    if (self.navigate) self.navigate(r.file, r.line);
}

- (void)removeSelected
{
    GRBreakpointRow* r = [self current];
    if (r != nil && self.removeRequested) self.removeRequested(r.file, r.line);
}
@end
