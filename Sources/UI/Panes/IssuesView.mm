// =============================================================================
//  IssuesView.mm - see IssuesView.h
// =============================================================================
#import "IssuesView.h"
#import "PaneUtil.h"
#include "../../Platform/Paths.h"
#include <vector>

using namespace GRIDEm;

@interface GRIssuesTable : NSTableView
@property (nonatomic, copy) void (^returnPressed)(void);
@end

@implementation GRIssuesTable
- (void)keyDown:(NSEvent*)event
{
    unichar c = event.charactersIgnoringModifiers.length > 0 ? [event.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if ((c == NSCarriageReturnCharacter || c == NSEnterCharacter) && self.returnPressed) { self.returnPressed(); return; }
    [super keyDown:event];
}
@end

@implementation GRIssuesView
{
    std::vector<Services::BuildMessagePtr> _all;
    std::vector<Services::BuildMessagePtr> _shown;
    GRIssuesTable* _table;
    NSButton* _errors;
    NSButton* _warnings;
    NSButton* _infos;
    NSInteger _errorCount, _warningCount, _infoCount;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"ErrorList" title:@"Error List" symbol:@"exclamationmark.triangle"];
}

- (NSButton*)toggle:(NSString*)symbol
{
    NSButton* b = [NSButton buttonWithTitle:@"" image:[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil]
                                     target:self action:@selector(filterChanged:)];
    [b setButtonType:NSButtonTypePushOnPushOff];
    b.controlSize = NSControlSizeSmall;
    b.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    b.imagePosition = NSImageLeading;
    b.state = NSControlStateValueOn;
    return b;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    _errors = [self toggle:@"xmark.octagon.fill"];
    _warnings = [self toggle:@"exclamationmark.triangle.fill"];
    _infos = [self toggle:@"info.circle.fill"];
    NSButton* clear = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"trash" accessibilityDescription:@"Clear"]
                                         target:self action:@selector(clearPressed:)];
    clear.bordered = NO;
    clear.toolTip = @"Clear";
    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ _errors, _warnings, _infos, spacer, clear ]];

    _table = [[GRIssuesTable alloc] initWithFrame:NSZeroRect];
    NSArray* titles = @[ @"", @"Code", @"Description", @"Project", @"File", @"Line" ];
    NSArray* widths = @[ @22, @70, @420, @110, @150, @50 ];
    for (NSUInteger i = 0; i < titles.count; i++)
    {
        NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:[NSString stringWithFormat:@"c%lu", (unsigned long)i]];
        c.title = titles[i];
        c.width = [widths[i] doubleValue];
        [_table addTableColumn:c];
    }
    _table.rowSizeStyle = NSTableViewRowSizeStyleSmall;
    _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    _table.dataSource = self;
    _table.delegate = self;
    _table.target = self;
    _table.doubleAction = @selector(doubleClicked:);
    __weak GRIssuesView* weakSelf = self;
    _table.returnPressed = ^{ [weakSelf goToRow:[weakSelf selectedRowOfTable]]; };
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.documentView = _table;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;
    [GRPaneUtil stack:@[ bar, scroll ] in:root grow:scroll];
    self.view = root;
    [self updateCounts];
}

- (NSInteger)selectedRowOfTable { return _table.selectedRow; }
- (NSInteger)errorCount { return _errorCount; }
- (NSInteger)warningCount { return _warningCount; }
- (NSInteger)messageCount { return _infoCount; }
- (std::vector<Services::BuildMessagePtr>)messages { return _all; }

- (void)clear
{
    _all.clear();
    _shown.clear();
    _errorCount = _warningCount = _infoCount = 0;
    if (self.isViewLoaded) [_table reloadData];
    [self updateCounts];
}

- (BOOL)isShown:(const Services::BuildMessagePtr&)m
{
    if (!self.isViewLoaded) return YES;
    if (m->Sev == Services::Severity::Error) return _errors.state == NSControlStateValueOn;
    if (m->Sev == Services::Severity::Warning) return _warnings.state == NSControlStateValueOn;
    return _infos.state == NSControlStateValueOn;
}

- (void)add:(const Services::BuildMessagePtr&)message
{
    if (!message) return;
    _all.push_back(message);
    if (message->Sev == Services::Severity::Error) _errorCount++;
    else if (message->Sev == Services::Severity::Warning) _warningCount++;
    else _infoCount++;
    if ([self isShown:message])
    {
        _shown.push_back(message);
        if (self.isViewLoaded)
            [_table insertRowsAtIndexes:[NSIndexSet indexSetWithIndex:_shown.size() - 1] withAnimation:NSTableViewAnimationEffectNone];
    }
    [self updateCounts];
}

- (void)updateCounts
{
    if (self.isViewLoaded)
    {
        _errors.title = [NSString stringWithFormat:@"%ld Errors", (long)_errorCount];
        _warnings.title = [NSString stringWithFormat:@"%ld Warnings", (long)_warningCount];
        _infos.title = [NSString stringWithFormat:@"%ld Messages", (long)_infoCount];
    }
    if (self.countsChanged) self.countsChanged(_errorCount, _warningCount, _infoCount);
}

- (void)filterChanged:(id)sender
{
    (void)sender;
    _shown.clear();
    for (const Services::BuildMessagePtr& m : _all)
        if ([self isShown:m]) _shown.push_back(m);
    [_table reloadData];
}

- (void)clearPressed:(id)sender { (void)sender; [self clear]; }

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return (NSInteger)_shown.size();
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    const Services::BuildMessagePtr& m = _shown[(size_t)row];
    NSString* column = tableColumn.identifier;
    NSTableCellView* cell = [GRPaneUtil cellIn:tableView identifier:column];
    cell.imageView.image = nil;
    cell.textField.toolTip = nil;
    if ([column isEqualToString:@"c0"])
    {
        NSString* symbol = m->Sev == Services::Severity::Error ? @"xmark.octagon.fill"
                         : m->Sev == Services::Severity::Warning ? @"exclamationmark.triangle.fill" : @"info.circle.fill";
        NSImage* img = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:GRNS(Services::BuildMessage::SeverityName(m->Sev))];
        NSColor* tint = m->Sev == Services::Severity::Error ? [NSColor systemRedColor]
                      : m->Sev == Services::Severity::Warning ? [NSColor systemYellowColor] : [NSColor systemBlueColor];
        NSImageSymbolConfiguration* cfg = [NSImageSymbolConfiguration configurationWithHierarchicalColor:tint];
        cell.imageView.image = [img imageWithSymbolConfiguration:cfg];
        cell.textField.stringValue = @"";
    }
    else if ([column isEqualToString:@"c1"]) cell.textField.stringValue = GRNS(m->Code);
    else if ([column isEqualToString:@"c2"]) { cell.textField.stringValue = GRNS(m->Text); cell.textField.toolTip = GRNS(m->Text); }
    else if ([column isEqualToString:@"c3"]) cell.textField.stringValue = GRNS(m->Project);
    else if ([column isEqualToString:@"c4"])
    {
        cell.textField.stringValue = m->File.empty() ? @"" : GRNS(Platform::Paths::FileName(m->File));
        cell.textField.toolTip = GRNS(m->File);
    }
    else cell.textField.stringValue = m->Line > 0 ? [NSString stringWithFormat:@"%d", m->Line] : @"";
    return cell;
}

- (void)goToRow:(NSInteger)row
{
    if (row < 0 || row >= (NSInteger)_shown.size()) return;
    const Services::BuildMessagePtr& m = _shown[(size_t)row];
    if (m->File.empty()) return;
    if (self.navigate) self.navigate(GRNS(m->File), m->Line, m->Column);
}

- (void)doubleClicked:(id)sender { (void)sender; [self goToRow:_table.clickedRow]; }
@end
