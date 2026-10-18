// =============================================================================
//  ExternalToolsSheet.mm - see ExternalToolsSheet.h
// =============================================================================
#import "ExternalToolsSheet.h"
#import "DialogKit.h"

using namespace GRIDEm;

@implementation GRExternalToolsSheet
{
    std::vector<Settings::ExternalToolPtr> _tools;
    Settings::ExternalToolPtr _current;
    GRTable* _list;
    NSTextField* _title;
    GRPathField* _command;
    NSTextField* _args;
    NSTextField* _dir;
}

- (instancetype)initWithTools:(const std::vector<Settings::ExternalToolPtr>&)tools
{
    if ((self = [super initWithTitle:@"External Tools" size:NSMakeSize(700, 420)]))
    {
        for (const Settings::ExternalToolPtr& t : tools) _tools.push_back(std::make_shared<Settings::ExternalTool>(*t));
        _list = [[GRTable alloc] initWithHeight:280];
        [_list addColumn:@"Tool" width:190 kind:GRColumnText choices:nil];
        _list.table.allowsMultipleSelection = NO;
        __weak GRExternalToolsSheet* weakSelf = self;
        _list.selectionChanged = ^{ [weakSelf toolSelected]; };
        [_list.view.widthAnchor constraintEqualToConstant:210].active = YES;

        GRForm* form = [[GRForm alloc] init];
        form.labelWidth = 100;
        _title = [form addText:@"Title" value:@""];
        _command = [[GRPathField alloc] initFolder:NO status:YES];
        [form addRow:@"Command" view:_command];
        _args = [form addText:@"Arguments" value:@""];
        _dir = [form addText:@"Initial folder" value:@""];
        [form addNote:@"Macros: $(FilePath) $(FileDir) $(FileName) $(ProjectDir) $(ProjectName) $(WorkspaceDir)"];

        NSStackView* row = [NSStackView stackViewWithViews:@[_list.view, form.view]];
        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        row.alignment = NSLayoutAttributeTop;
        row.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 0);
        [_list.view.heightAnchor constraintEqualToAnchor:row.heightAnchor constant:-14].active = YES;
        [form.view.heightAnchor constraintEqualToAnchor:row.heightAnchor constant:-14].active = YES;
        [self setContentView:row];
        [self addButton:@"Add" action:^{ [weakSelf addTool]; }];
        [self addButton:@"Remove" action:^{ [weakSelf removeTool]; }];
        [self fill:0];
    }
    return self;
}

- (std::vector<Settings::ExternalToolPtr>)tools { return _tools; }

- (void)fill:(NSInteger)select
{
    _current.reset();
    [_list clear];
    for (const Settings::ExternalToolPtr& t : _tools)
        [_list addRow:@[t->Title.empty() ? @"(untitled)" : GRNS(t->Title)] tag:nil];
    [_list reload];
    if (select >= 0 && select < (NSInteger)_tools.size())
        [_list.table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)select] byExtendingSelection:NO];
    [self toolSelected];
}

- (void)store
{
    if (!_current) return;
    _current->Title = GRStd(_title.stringValue);
    _current->Command = GRStd(_command.value);
    _current->Arguments = GRStd(_args.stringValue);
    _current->InitialDir = GRStd(_dir.stringValue);
}

- (void)toolSelected
{
    [self store];
    NSInteger i = _list.selectedRow;
    _current.reset();
    if (i >= 0 && i < (NSInteger)_tools.size()) _current = _tools[(size_t)i];
    BOOL on = _current != nullptr;
    for (NSControl* c in @[_title, _args, _dir]) c.enabled = on;
    _command.field.enabled = on;
    if (!on) return;
    _title.stringValue = GRNS(_current->Title);
    _command.value = GRNS(_current->Command);
    _args.stringValue = GRNS(_current->Arguments);
    _dir.stringValue = GRNS(_current->InitialDir);
}

- (void)addTool
{
    [self store];
    _tools.push_back(std::make_shared<Settings::ExternalTool>());
    [self fill:(NSInteger)_tools.size() - 1];
}

- (void)removeTool
{
    NSInteger i = _list.selectedRow;
    if (i < 0 || i >= (NSInteger)_tools.size()) return;
    _current.reset();
    _tools.erase(_tools.begin() + i);
    [self fill:i < (NSInteger)_tools.size() ? i : (NSInteger)_tools.size() - 1];
}

- (BOOL)accept
{
    [self store];
    return YES;
}
@end
