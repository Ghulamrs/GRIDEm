// =============================================================================
//  ConfigurationManagerSheet.mm - see ConfigurationManagerSheet.h
// =============================================================================
#import "ConfigurationManagerSheet.h"
#import "DialogKit.h"
#include <algorithm>
#include <map>

#include <stdexcept>

using namespace GRIDEm;

@implementation GRConfigurationManagerSheet
{
    Model::WorkspacePtr _workspace;
    NSPopUpButton* _configBox;
    NSPopUpButton* _targetBox;
    GRCheckList* _projects;
    std::map<std::string, bool> _changes;     // "project|target" -> built
    NSString* _shownTarget;
}

- (instancetype)initWithWorkspace:(const Model::WorkspacePtr&)w
{
    if ((self = [super initWithTitle:@"Configuration Manager" size:NSMakeSize(560, 440)]))
    {
        _workspace = w;
        GRForm* form = [[GRForm alloc] init];
        _configBox = [form addPopup:@"Active configuration" items:@[@"Debug", @"Release"] selected:GRNS(w->ActiveConfig)];
        NSMutableArray* targets = [NSMutableArray array];
        for (Core::Target t : Core::TargetInfo::All()) [targets addObject:GRNS(Core::TargetInfo::Name(t))];
        _targetBox = [form addPopup:@"Active target" items:targets selected:GRNS(Core::TargetInfo::Name(w->ActiveTarget))];
        _targetBox.target = self;
        _targetBox.action = @selector(targetChanged:);
        [form addSection:@"Projects built for the selected target"];
        _projects = [[GRCheckList alloc] initWithHeight:220];
        [form addRow:@"" view:_projects.view];
        [self setContentView:form.view];
        [self fillProjects];
    }
    return self;
}

- (void)fillProjects
{
    _shownTarget = _targetBox.titleOfSelectedItem;
    Core::Target t = Core::TargetInfo::Parse(GRStd(_shownTarget));
    NSMutableArray* names = [NSMutableArray array];
    NSMutableArray* checks = [NSMutableArray array];
    for (const Model::GrideProjectPtr& p : _workspace->Projects)
    {
        bool on;
        std::map<std::string, bool>::const_iterator it = _changes.find(p->Name + "|" + GRStd(_shownTarget));
        if (it != _changes.end()) on = it->second;
        else on = std::find(p->Targets.begin(), p->Targets.end(), t) != p->Targets.end();
        [names addObject:GRNS(p->Name)];
        [checks addObject:@(on)];
    }
    [_projects setItems:names checked:checks];
}

- (void)storeChecks
{
    if (_shownTarget == nil) return;
    for (NSUInteger i = 0; i < _projects.titles.count; i++)
        _changes[GRStd(_projects.titles[i]) + "|" + GRStd(_shownTarget)] = [_projects isChecked:(NSInteger)i];
}

- (void)targetChanged:(id)sender
{
    (void)sender;
    [self storeChecks];
    [self fillProjects];
}

- (BOOL)accept
{
    [self storeChecks];
    _workspace->ActiveConfig = GRStd(_configBox.titleOfSelectedItem);
    _workspace->ActiveTarget = Core::TargetInfo::Parse(GRStd(_targetBox.titleOfSelectedItem));
    for (std::map<std::string, bool>::const_iterator it = _changes.begin(); it != _changes.end(); ++it)
    {
        size_t bar = it->first.find('|');
        Model::GrideProjectPtr p = _workspace->FindProject(it->first.substr(0, bar));
        if (!p || p->IsReadOnly) continue;
        Core::Target t = Core::TargetInfo::Parse(it->first.substr(bar + 1));
        std::vector<Core::Target>::iterator has = std::find(p->Targets.begin(), p->Targets.end(), t);
        bool there = has != p->Targets.end();
        if (it->second == there) continue;
        if (it->second) p->Targets.push_back(t);
        else p->Targets.erase(has);
        p->IsDirty = true;
    }
    try { _workspace->SaveProjects(); }
    catch (const std::exception& e) { [self showMessage:GRNS(e.what())]; }
    _workspace->IsDirty = true;
    return YES;
}
@end
