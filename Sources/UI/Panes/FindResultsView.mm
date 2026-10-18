// =============================================================================
//  FindResultsView.mm - see FindResultsView.h
// =============================================================================
#import "FindResultsView.h"
#import "../Common.h"
#import "PaneUtil.h"

@interface GRFindHit : NSObject
@property (nonatomic, copy) NSString* file;
@property (nonatomic, assign) NSInteger line;
@property (nonatomic, copy) NSString* text;
@end
@implementation GRFindHit
@end

@implementation GRFindResultsView
{
    NSTextField* _header;
    NSTableView* _table;
    NSMutableArray<GRFindHit*>* _hits;
    BOOL _batching;
}

- (instancetype)init
{
    self = [super initWithIdentifier:@"FindResults" title:@"Find Results" symbol:@"magnifyingglass"];
    if (self != nil) _hits = [NSMutableArray array];
    return self;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    _header = [GRPaneUtil label:@""];
    _header.textColor = [NSColor secondaryLabelColor];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ _header ]];
    NSScrollView* scroll = nil;
    _table = [GRPaneUtil tableWithColumns:@[ @"File", @"Line", @"Text" ] widths:@[ @260, @50, @500 ] scroll:&scroll];
    _table.dataSource = self;
    _table.delegate = self;
    _table.target = self;
    _table.doubleAction = @selector(doubleClicked:);
    [GRPaneUtil stack:@[ bar, scroll ] in:root grow:scroll];
    self.view = root;
}

- (void)begin:(NSString*)what
{
    if (!self.isViewLoaded) [self loadView];
    [_hits removeAllObjects];
    _header.stringValue = [NSString stringWithFormat:@"Find all \"%@\"", GROr(what, @"")];
    _batching = YES;
    [_table reloadData];
}

- (void)addFile:(NSString*)file line:(NSInteger)line text:(NSString*)text
{
    GRFindHit* h = [[GRFindHit alloc] init];
    h.file = file;
    h.line = line;
    h.text = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    [_hits addObject:h];
    if (!_batching) [_table reloadData];
}

- (void)end:(NSInteger)files
{
    _batching = NO;
    [_table reloadData];
    _header.stringValue = [_header.stringValue stringByAppendingFormat:@"   -   %lu match(es) in %ld file(s)",
                           (unsigned long)_hits.count, (long)files];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return (NSInteger)_hits.count;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    GRFindHit* h = _hits[(NSUInteger)row];
    NSString* column = tableColumn.identifier;
    NSTableCellView* cell = [GRPaneUtil cellIn:tableView identifier:column];
    cell.imageView.image = nil;
    if ([column isEqualToString:@"File"]) { cell.textField.stringValue = h.file; cell.textField.toolTip = h.file; }
    else if ([column isEqualToString:@"Line"]) cell.textField.stringValue = [NSString stringWithFormat:@"%ld", (long)h.line];
    else { cell.textField.stringValue = h.text; cell.textField.font = [GRPaneUtil monoFont]; }
    return cell;
}

- (void)doubleClicked:(id)sender
{
    (void)sender;
    NSInteger row = _table.clickedRow;
    if (row < 0 || row >= (NSInteger)_hits.count) return;
    GRFindHit* h = _hits[(NSUInteger)row];
    if (self.navigate) self.navigate(h.file, h.line);
}
@end
