// =============================================================================
//  StackView.mm - see StackView.h
// =============================================================================
#import "StackView.h"
#import "PaneUtil.h"
#include "../../Platform/Paths.h"

using namespace GRIDEm;

@interface GRStackTable : NSTableView
@property (nonatomic, copy) void (^returnPressed)(void);
@end
@implementation GRStackTable
- (void)keyDown:(NSEvent*)event
{
    unichar c = event.charactersIgnoringModifiers.length > 0 ? [event.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if ((c == NSCarriageReturnCharacter || c == NSEnterCharacter) && self.returnPressed) { self.returnPressed(); return; }
    [super keyDown:event];
}
@end

@interface GRStackRowView : NSTableRowView
@property (nonatomic, assign) BOOL lookedAt;
@end
@implementation GRStackRowView
- (void)drawBackgroundInRect:(NSRect)dirtyRect
{
    [super drawBackgroundInRect:dirtyRect];
    if (!self.lookedAt || self.selected) return;
    // GRIDEd's pale yellow for the frame looked at
    [[NSColor colorWithSRGBRed:1.0 green:243.0 / 255.0 blue:176.0 / 255.0 alpha:0.85] setFill];
    NSRectFill(self.bounds);
}
@end

@implementation GRStackView
{
    GRStackTable* _table;
    std::vector<Services::DebugFrame> _frames;
    NSInteger _current;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"CallStack" title:@"Call Stack" symbol:@"square.stack.3d.down.right"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 500, 200)];
    _table = [[GRStackTable alloc] initWithFrame:NSZeroRect];
    NSArray* titles = @[ @"Function", @"File", @"Line" ];
    NSArray* widths = @[ @260, @220, @60 ];
    for (NSUInteger i = 0; i < titles.count; i++)
    {
        NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:titles[i]];
        c.title = titles[i];
        c.width = [widths[i] doubleValue];
        [_table addTableColumn:c];
    }
    _table.rowSizeStyle = NSTableViewRowSizeStyleSmall;
    _table.dataSource = self;
    _table.delegate = self;
    _table.target = self;
    _table.doubleAction = @selector(doubleClicked:);
    __weak GRStackView* weakSelf = self;
    _table.returnPressed = ^{ [weakSelf choose:[weakSelf selected]]; };
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.documentView = _table;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;
    [GRPaneUtil fill:root with:scroll];
    self.view = root;
}

- (NSInteger)selected { return _table.selectedRow; }

- (void)setFrames:(const std::vector<Services::DebugFrame>&)frames current:(NSInteger)current
{
    if (!self.isViewLoaded) [self loadView];
    _frames = frames;
    _current = current;
    [_table reloadData];
}

- (void)clearView
{
    _frames.clear();
    if (self.isViewLoaded) [_table reloadData];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return (NSInteger)_frames.size();
}

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    (void)tableView;
    GRStackRowView* v = [[GRStackRowView alloc] initWithFrame:NSZeroRect];
    v.lookedAt = row == _current;
    return v;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    const Services::DebugFrame& f = _frames[(size_t)row];
    NSString* column = tableColumn.identifier;
    NSTableCellView* cell = [GRPaneUtil cellIn:tableView identifier:column];
    cell.imageView.image = nil;
    BOOL looked = row == _current;
    cell.textField.font = looked ? [NSFont boldSystemFontOfSize:[NSFont smallSystemFontSize]]
                                 : [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    cell.textField.textColor = looked ? [NSColor blackColor] : [NSColor labelColor];
    if ([column isEqualToString:@"Function"]) cell.textField.stringValue = GRNS(f.Function);
    else if ([column isEqualToString:@"File"])
    {
        cell.textField.stringValue = f.File.empty() ? @"" : GRNS(Platform::Paths::FileName(f.File));
        cell.textField.toolTip = GRNS(f.File);
    }
    else cell.textField.stringValue = f.Line > 0 ? [NSString stringWithFormat:@"%d", f.Line] : @"";
    return cell;
}

- (void)choose:(NSInteger)row
{
    if (row < 0 || row >= (NSInteger)_frames.size()) return;
    if (self.frameChosen) self.frameChosen(row);
}

- (void)doubleClicked:(id)sender { (void)sender; [self choose:_table.clickedRow]; }
@end
