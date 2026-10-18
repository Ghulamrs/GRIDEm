// =============================================================================
//  DependenciesSheet.mm - see DependenciesSheet.h
// =============================================================================
#import "DependenciesSheet.h"
#import "DialogKit.h"
#include "../../Core/StringUtil.h"
#include <map>

#include <stdexcept>

using namespace GRIDEm;

@implementation GRDependenciesSheet
{
    Model::WorkspacePtr _workspace;
    std::map<std::string, std::vector<std::string> > _deps;
    NSPopUpButton* _projectBox;
    GRCheckList* _list;
}

- (instancetype)initWithWorkspace:(const Model::WorkspacePtr&)w selected:(const Model::GrideProjectPtr&)selected
{
    if ((self = [super initWithTitle:@"Project Dependencies" size:NSMakeSize(480, 440)]))
    {
        _workspace = w;
        NSMutableArray* names = [NSMutableArray array];
        for (const Model::GrideProjectPtr& p : w->Projects)
        {
            _deps[p->Name] = p->DependsOn;
            [names addObject:GRNS(p->Name)];
        }
        GRForm* form = [[GRForm alloc] init];
        form.labelWidth = 80;
        _projectBox = [form addPopup:@"Projects" items:names selected:selected ? GRNS(selected->Name) : nil];
        _projectBox.target = self;
        _projectBox.action = @selector(projectChanged:);
        [form addSection:@"Depends on:"];
        _list = [[GRCheckList alloc] initWithHeight:240];
        __weak GRDependenciesSheet* weakSelf = self;
        _list.toggled = ^(NSInteger row, BOOL on) { [weakSelf toggled:row on:on]; };
        [form addRow:@"" view:_list.view];
        [self setContentView:form.view];
        [self projectChanged:nil];
    }
    return self;
}

- (std::string)current { return GRStd(_projectBox.titleOfSelectedItem); }

- (void)projectChanged:(id)sender
{
    (void)sender;
    std::string cur = [self current];
    NSMutableArray* names = [NSMutableArray array];
    NSMutableArray* checks = [NSMutableArray array];
    for (const Model::GrideProjectPtr& p : _workspace->Projects)
    {
        if (p->Name == cur) continue;
        [names addObject:GRNS(p->Name)];
        [checks addObject:@(Core::StringUtil::ContainsIgnoreCase(_deps[cur], p->Name))];
    }
    [_list setItems:names checked:checks];
}

- (void)toggled:(NSInteger)row on:(BOOL)on
{
    std::string cur = [self current];
    std::string other = GRStd(_list.titles[(NSUInteger)row]);
    std::vector<std::string>& d = _deps[cur];
    for (size_t i = 0; i < d.size(); i++)
        if (d[i] == other) { d.erase(d.begin() + (long)i); break; }
    if (on) d.push_back(other);
}

- (BOOL)accept
{
    for (const Model::GrideProjectPtr& p : _workspace->Projects)
    {
        if (p->IsReadOnly) continue;
        p->DependsOn = _deps[p->Name];
        p->IsDirty = true;
    }
    try { _workspace->SaveProjects(); }
    catch (const std::exception& e) { [self showMessage:GRNS(e.what())]; }
    return YES;
}
@end
