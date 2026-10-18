// =============================================================================
//  RegistersView.mm - see RegistersView.h
// =============================================================================
#import "RegistersView.h"
#import "../Common.h"
#import "PaneUtil.h"
#import "../Theme.h"
#include <map>
#include <string>

using namespace GRIDEm;

@implementation GRRegistersView
{
    NSTableView* _table;
    std::vector<Services::RegisterValue> _regs;
    std::map<std::string, std::string> _previous;     // the values at the stop before
    std::map<std::string, std::string> _current;
    NSString* _none;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"Registers" title:@"Registers" symbol:@"cpu"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
    NSScrollView* scroll = nil;
    _table = [GRPaneUtil tableWithColumns:@[ @"Register", @"Value", @"Register ", @"Value " ]
                                   widths:@[ @80, @130, @80, @130 ] scroll:&scroll];
    _table.dataSource = self;
    _table.delegate = self;
    [GRPaneUtil fill:root with:scroll];
    self.view = root;
}

- (void)setRegisters:(const std::vector<Services::RegisterValue>&)registers backend:(NSString*)backend
{
    if (!self.isViewLoaded) [self loadView];
    _previous = _current;
    _current.clear();
    for (const Services::RegisterValue& r : registers) _current[r.Name] = r.Value;
    _regs = registers;
    _none = registers.empty() ? [NSString stringWithFormat:@"(no registers from %@)", GROr(backend, @"the debugger")] : nil;
    [_table reloadData];
}

- (void)clearView
{
    _regs.clear();
    _previous.clear();
    _current.clear();
    _none = nil;
    if (self.isViewLoaded) [_table reloadData];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    if (_none != nil) return 1;
    return (NSInteger)((_regs.size() + 1) / 2);
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    NSString* column = tableColumn.identifier;
    NSTableCellView* cell = [GRPaneUtil cellIn:tableView identifier:column];
    cell.imageView.image = nil;
    cell.textField.font = [GRPaneUtil monoFont];
    cell.textField.textColor = [NSColor labelColor];
    if (_none != nil)
    {
        cell.textField.stringValue = [column isEqualToString:@"Register"] ? _none : @"";
        cell.textField.textColor = [NSColor secondaryLabelColor];
        return cell;
    }
    BOOL second = [column hasSuffix:@" "];
    size_t i = (size_t)row * 2 + (second ? 1 : 0);
    if (i >= _regs.size()) { cell.textField.stringValue = @""; return cell; }
    const Services::RegisterValue& r = _regs[i];
    if ([column hasPrefix:@"Register"]) cell.textField.stringValue = GRNS(r.Name);
    else
    {
        cell.textField.stringValue = GRNS(r.Value);
        std::map<std::string, std::string>::const_iterator it = _previous.find(r.Name);
        if (it != _previous.end() && it->second != r.Value)
            cell.textField.textColor = [[GRTheme current] resolvedFor:tableView.effectiveAppearance].errorText;
    }
    return cell;
}
@end
