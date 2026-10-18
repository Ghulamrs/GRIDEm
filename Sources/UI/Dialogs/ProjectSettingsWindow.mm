// =============================================================================
//  ProjectSettingsWindow.mm - see ProjectSettingsWindow.h
// =============================================================================
#import "ProjectSettingsWindow.h"
#import "CompilerOptionsView.h"
#import "DialogKit.h"
#include "../../Core/StringUtil.h"
#include "../../Services/RideToolchain.h"
#include <algorithm>
#include <stdexcept>

using namespace GRIDEm;

NSString* const GRProjectPageGeneral = @"General";
NSString* const GRProjectPageConfigurations = @"Configurations";
NSString* const GRProjectPageCompiler = @"Compiler";
NSString* const GRProjectPageCompilerOptions = @"Compiler Options";
NSString* const GRProjectPageDebugging = @"Debugging";

// ---- the compilers as the window names them, and the words behind them ----------
static NSString* ChoiceFor(Services::RideTool t)
{
    if (t == Services::RideTool::Auto) return @"auto (by language)";
    NSString* name = GRNS(Services::RideToolchain::Name(t));
    if (t == Services::RideTool::Cl || t == Services::RideTool::HostCxx)
        return [name stringByAppendingString:@" (the machine's own)"];
    return name;
}

static NSArray<NSString*>* CompilerChoices()
{
    NSMutableArray<NSString*>* r = [NSMutableArray arrayWithObject:ChoiceFor(Services::RideTool::Auto)];
    for (Services::RideTool t : Services::RideToolchain::Compilers()) [r addObject:ChoiceFor(t)];
    return r;
}

static NSString* ChoiceOf(const std::string& word)
{
    Services::RideTool t = Services::RideToolchain::FromWord(word);
    if (t == Services::RideTool::HostCxx) t = Services::RideTool::Cl;
    return ChoiceFor(t);
}

static std::string WordOf(NSString* choice)
{
    if (choice == nil) return "auto";
    for (Services::RideTool t : Services::RideToolchain::Compilers())
        if ([choice isEqualToString:ChoiceFor(t)]) return Services::RideToolchain::Word(t);
    return "auto";
}

static NSString* JoinSemi(const std::vector<std::string>& l) { return GRNS(Core::StringUtil::Join(";", l)); }

static std::vector<std::string> SplitSemi(NSString* v)
{
    std::vector<std::string> r;
    for (const std::string& s : Core::StringUtil::Split(GRStd(v), ';'))
    {
        std::string t = Core::StringUtil::Trim(s);
        if (!t.empty()) r.push_back(t);
    }
    return r;
}

@interface GRProjectSettingsWindow () <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation GRProjectSettingsWindow
{
    Model::GrideProjectPtr _project;
    Model::WorkspacePtr _workspace;
    std::vector<Model::BuildConfigurationPtr> _clones;
    Model::BuildConfigurationPtr _shown;
    NSArray<NSString*>* _pages;
    NSTableView* _sidebar;
    NSTabView* _pagesView;
    // General
    GRCheckList* _targets;
    GRCheckList* _deps;
    // Configurations
    NSPopUpButton* _configBox;
    NSPopUpButton* _targetBox;
    NSTextField* _key;
    NSTextField* _outDir;
    NSTextField* _objDir;
    NSTextField* _outputName;
    NSTextField* _defines;
    NSTextField* _includes;
    NSComboBox* _optimise;
    NSTextField* _compileFlags;
    NSTextField* _linkerCmd;
    NSTextField* _libs;
    NSTextField* _libDirs;
    NSTextField* _linkFlags;
    // Compiler
    NSPopUpButton* _compiler;
    GRTable* _folders;
    // Compiler Options
    GRCompilerOptionsView* _options;
    // Debugging
    NSTextView* _args;
    NSButton* _emulateOnly;
    NSString* _saveError;
}

+ (NSString*)readOnlyNote:(const Model::GrideProjectPtr&)p
{
    if (p->IsVs()) return @"(read-only Visual Studio project: the compiler, the compiler per folder and the arguments "
                          "can be changed - kept in the workspace)";
    if (p->IsCcs()) return @"(read-only CCS project: the compiler, the compiler per folder, the arguments and Emulate "
                           "only can be changed - kept in the workspace)";
    return @"(read-only project: the compiler, the compiler per folder and the arguments can be changed - kept in the workspace)";
}

- (instancetype)initWithProject:(const Model::GrideProjectPtr&)p workspace:(const Model::WorkspacePtr&)w
                         config:(NSString*)config target:(Core::Target)target startPage:(NSString*)startPage
{
    NSString* title = [NSString stringWithFormat:@"%@ Property Pages", GRNS(p->Name)];
    if (p->IsReadOnly) title = [title stringByAppendingFormat:@"  %@", [GRProjectSettingsWindow readOnlyNote:p]];
    if ((self = [super initWithTitle:title size:NSMakeSize(920, 620)]))
    {
        _project = p;
        _workspace = w;
        for (const Model::BuildConfigurationPtr& c : p->Configurations) _clones.push_back(c->Clone());
        _pages = @[GRProjectPageGeneral, GRProjectPageConfigurations, GRProjectPageCompiler,
                   GRProjectPageCompilerOptions, GRProjectPageDebugging];

        _sidebar = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 190, 400)];
        NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"page"];
        [_sidebar addTableColumn:col];
        _sidebar.headerView = nil;
        _sidebar.style = NSTableViewStyleSourceList;
        _sidebar.rowHeight = 24;
        _sidebar.dataSource = self;
        _sidebar.delegate = self;
        NSScrollView* side = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 190, 400)];
        side.documentView = _sidebar;
        side.drawsBackground = NO;
        side.translatesAutoresizingMaskIntoConstraints = NO;
        [side.widthAnchor constraintEqualToConstant:190].active = YES;

        _pagesView = [[NSTabView alloc] initWithFrame:NSZeroRect];
        _pagesView.tabViewType = NSNoTabsNoBorder;
        _pagesView.translatesAutoresizingMaskIntoConstraints = NO;
        [self addPage:GRProjectPageGeneral view:[self makeGeneral]];
        [self addPage:GRProjectPageConfigurations view:[self makeConfigurations:config target:target]];
        [self addPage:GRProjectPageCompiler view:[self makeCompiler]];
        _options = [[GRCompilerOptionsView alloc] initWithStore:p->Options config:config target:target readOnly:p->IsReadOnly];
        [self addPage:GRProjectPageCompilerOptions view:_options];
        [self addPage:GRProjectPageDebugging view:[self makeDebugging]];

        NSStackView* split = [NSStackView stackViewWithViews:@[side, _pagesView]];
        split.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        split.alignment = NSLayoutAttributeTop;
        split.spacing = 0;
        [side.heightAnchor constraintEqualToAnchor:split.heightAnchor].active = YES;
        [_pagesView.heightAnchor constraintEqualToAnchor:split.heightAnchor].active = YES;
        [self setContentView:split];

        NSUInteger start = startPage != nil ? [_pages indexOfObject:startPage] : NSNotFound;
        if (start == NSNotFound) start = 0;
        [_sidebar reloadData];
        [_sidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:start] byExtendingSelection:NO];
        [_pagesView selectTabViewItemAtIndex:(NSInteger)start];
        [self showCurrent];
    }
    return self;
}

- (NSString*)saveError { return _saveError; }

- (void)addPage:(NSString*)title view:(NSView*)view
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:title];
    item.label = title;
    item.view = view;
    [_pagesView addTabViewItem:item];
}

// ---- pages ---------------------------------------------------------------------
- (NSView*)makeGeneral
{
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Targets of this project"];
    _targets = [[GRCheckList alloc] initWithHeight:100];
    NSMutableArray* titles = [NSMutableArray array];
    NSMutableArray* checks = [NSMutableArray array];
    for (Core::Target t : Core::TargetInfo::All())
    {
        [titles addObject:GRNS(Core::TargetInfo::Name(t))];
        bool on = std::find(_project->Targets.begin(), _project->Targets.end(), t) != _project->Targets.end();
        [checks addObject:@(on)];
    }
    [_targets setItems:titles checked:checks];
    _targets.enabled = !_project->IsReadOnly;
    [form addRow:@"" view:_targets.view];
    [form addSection:@"Depends on (built first)"];
    _deps = [[GRCheckList alloc] initWithHeight:140];
    NSMutableArray* names = [NSMutableArray array];
    NSMutableArray* on = [NSMutableArray array];
    if (_workspace)
        for (const Model::GrideProjectPtr& o : _workspace->Projects)
        {
            if (o == _project) continue;
            [names addObject:GRNS(o->Name)];
            bool d = Core::StringUtil::ContainsIgnoreCase(_project->DependsOn, o->Name);
            [on addObject:@(d)];
        }
    [_deps setItems:names checked:on];
    _deps.enabled = !_project->IsReadOnly;
    [form addRow:@"" view:_deps.view];
    return form.view;
}

- (NSView*)makeConfigurations:(NSString*)config target:(Core::Target)target
{
    GRForm* form = [[GRForm alloc] init];
    _configBox = [form addPopup:@"Configuration" items:@[@"Debug", @"Release"] selected:config];
    _configBox.target = self;
    _configBox.action = @selector(selectionChanged:);
    NSMutableArray* targets = [NSMutableArray array];
    for (Core::Target t : Core::TargetInfo::All()) [targets addObject:GRNS(Core::TargetInfo::Name(t))];
    _targetBox = [form addPopup:@"Target" items:targets selected:GRNS(Core::TargetInfo::Name(target))];
    _targetBox.target = self;
    _targetBox.action = @selector(selectionChanged:);
    [form addSection:@"General"];
    _key = [form addText:@"Configuration" value:@""];
    _key.editable = NO;
    _outDir = [form addText:@"Output Directory" value:@""];
    [form addNote:@"Relative to the project folder."];
    _objDir = [form addText:@"Intermediate Directory" value:@""];
    [form addNote:@"Where assembly and object files go."];
    _outputName = [form addText:@"Output Name" value:@""];
    [form addNote:@"Program name without extension. Empty = project name."];
    [form addSection:@"Compiler"];
    _defines = [form addMono:@"Preprocessor Definitions" value:@""];
    [form addNote:@"Separate with ';'."];
    _includes = [form addMono:@"Include Directories" value:@""];
    [form addNote:@"Separate with ';'. Relative to the project folder."];
    _optimise = [[NSComboBox alloc] initWithFrame:NSZeroRect];
    [_optimise addItemsWithObjectValues:@[@"O0", @"O1", @"O2", @"O3"]];
    [form addRow:@"Optimisation" view:_optimise];
    [form addNote:@"O0 (none) to O3 (full)."];
    _compileFlags = [form addMono:@"Additional Options" value:@""];
    [form addSection:@"Linker"];
    _linkerCmd = [form addMono:@"Linker Command File" value:@""];
    [form addNote:@"Memory map for the DSP target (e.g. c6747.cmd)."];
    _libs = [form addMono:@"Libraries" value:@""];
    [form addNote:@"Separate with ';' (e.g. rts6747.lib)."];
    _libDirs = [form addMono:@"Library Directories" value:@""];
    [form addNote:@"Separate with ';'."];
    _linkFlags = [form addMono:@"Additional Options" value:@""];
    if (_project->IsReadOnly)
        for (NSControl* c in @[_outDir, _objDir, _outputName, _defines, _includes, _optimise, _compileFlags,
                               _linkerCmd, _libs, _libDirs, _linkFlags])
            c.enabled = NO;
    return form.view;
}

- (NSView*)makeCompiler
{
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Compiler (auto: C to c90, C++ to cpp11)"];
    _compiler = [form addPopup:@"Compiler" items:CompilerChoices() selected:ChoiceOf(_project->Toolchain)];
    [form addSection:@"Compiler per folder"];
    _folders = [[GRTable alloc] initWithHeight:200];
    [_folders addColumn:@"Folder" width:220 kind:GRColumnText choices:nil];
    [_folders addColumn:@"Compiler" width:260 kind:GRColumnPopup choices:CompilerChoices()];
    for (const Model::ProjectFolderPtr& f : _project->Folders)
    {
        std::string own;
        if (!_project->FolderToolchains.Find(f->Name, own)) own = "auto";
        [_folders addRow:@[GRNS(f->Name), ChoiceOf(own)] tag:nil];
    }
    [_folders reload];
    [form addRow:@"" view:_folders.view];
    [form addNote:@"A folder naming its own compiler gives it every file in the folder (RIDE's compiler per group)."];
    return form.view;
}

- (NSView*)makeDebugging
{
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Program arguments (one per line)"];
    _args = [form addLines:@"Arguments" text:GRJoinLines(_project->ProgramArgs) height:110 mono:YES];
    [form addNote:@"One argument per line, so a path with a space in it stays one argument (RIDE's build.args)."];
    [form addSection:@"TMS320C6747"];
    _emulateOnly = [form addCheck:@"Emulate only (TMS320C6747): no .out is linked, Run is Emulate on vm6747"
                               on:_project->EmulateOnly];
    return form.view;
}

// ---- the configuration shown -----------------------------------------------------
- (void)capture
{
    if (!_shown || _project->IsReadOnly) return;
    _shown->OutDir = GRStd(_outDir.stringValue);
    _shown->ObjDir = GRStd(_objDir.stringValue);
    _shown->OutputName = GRStd(_outputName.stringValue);
    _shown->Defines = SplitSemi(_defines.stringValue);
    _shown->IncludeDirs = SplitSemi(_includes.stringValue);
    _shown->Optimise = GRStd(_optimise.stringValue);
    _shown->ExtraCompileFlags = GRStd(_compileFlags.stringValue);
    _shown->LinkerCmd = GRStd(_linkerCmd.stringValue);
    _shown->Libs = SplitSemi(_libs.stringValue);
    _shown->LibDirs = SplitSemi(_libDirs.stringValue);
    _shown->ExtraLinkFlags = GRStd(_linkFlags.stringValue);
}

- (void)showCurrent
{
    std::string cfg = GRStd(_configBox.titleOfSelectedItem);
    Core::Target t = Core::TargetInfo::Parse(GRStd(_targetBox.titleOfSelectedItem));
    std::string key = Core::ConfigName::Make(cfg, t);
    Model::BuildConfigurationPtr found;
    for (const Model::BuildConfigurationPtr& c : _clones)
        if (Core::StringUtil::EqualsIgnoreCase(c->Key(), key)) found = c;
    if (!found)
    {
        found = std::make_shared<Model::BuildConfiguration>(cfg, t);
        _clones.push_back(found);
    }
    _shown = found;
    _key.stringValue = GRNS(found->Key());
    _outDir.stringValue = GRNS(found->OutDir);
    _objDir.stringValue = GRNS(found->ObjDir);
    _outputName.stringValue = GRNS(found->OutputName);
    _defines.stringValue = JoinSemi(found->Defines);
    _includes.stringValue = JoinSemi(found->IncludeDirs);
    _optimise.stringValue = GRNS(found->Optimise);
    _compileFlags.stringValue = GRNS(found->ExtraCompileFlags);
    _linkerCmd.stringValue = GRNS(found->LinkerCmd);
    _libs.stringValue = JoinSemi(found->Libs);
    _libDirs.stringValue = JoinSemi(found->LibDirs);
    _linkFlags.stringValue = GRNS(found->ExtraLinkFlags);
    [_options setTarget:t];
}

- (void)selectionChanged:(id)sender
{
    (void)sender;
    [self capture];
    [self showCurrent];
}

// ---- OK (GRIDEd's OnAccept) --------------------------------------------------------
- (BOOL)accept
{
    [self capture];
    Model::GrideProjectPtr p = _project;
    if (!p->IsReadOnly)
    {
        p->Configurations = _clones;
        p->Targets.clear();
        std::vector<Core::Target> all = Core::TargetInfo::All();
        for (NSUInteger i = 0; i < all.size(); i++)
            if ([_targets isChecked:(NSInteger)i]) p->Targets.push_back(all[i]);
        p->DependsOn = GRVector(_deps.checkedTitles);
        [_options commit];
    }
    // a word already meaning the chosen compiler is kept as written ("cl" stays "cl")
    std::string chosen = WordOf(_compiler.titleOfSelectedItem);
    if (Services::RideToolchain::FromWord(p->Toolchain) != Services::RideToolchain::FromWord(chosen)) p->Toolchain = chosen;
    for (NSArray<NSString*>* row in _folders.rows)
    {
        std::string folder = GRStd(row[0]);
        std::string own;
        if (!p->FolderToolchains.Find(folder, own)) own = "auto";
        std::string word = WordOf(row[1]);
        if (Services::RideToolchain::FromWord(own) != Services::RideToolchain::FromWord(word))
            p->SetFolderToolchain(folder, word);
    }
    p->EmulateOnly = _emulateOnly.state == NSControlStateValueOn;
    p->ProgramArgs = GRSplitLines(_args.string);
    if (p->IsImported())
    {
        p->IsDirty = false;
        if (_workspace) _workspace->IsDirty = true;
    }
    else
    {
        p->IsDirty = true;
        try { p->Save(); }
        catch (const std::exception& e) { _saveError = GRNS(e.what()); }
    }
    return YES;
}

// ---- the sidebar -----------------------------------------------------------------
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView { (void)tableView; return (NSInteger)_pages.count; }

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row
{
    (void)column;
    NSTableCellView* cell = [tableView makeViewWithIdentifier:@"pageCell" owner:self];
    if (cell == nil)
    {
        cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 180, 24)];
        cell.identifier = @"pageCell";
        NSTextField* t = [NSTextField labelWithString:@""];
        t.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:t];
        cell.textField = t;
        [NSLayoutConstraint activateConstraints:@[
            [t.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:6],
            [t.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    }
    cell.textField.stringValue = _pages[(NSUInteger)row];
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    NSInteger row = _sidebar.selectedRow;
    if (row >= 0) [_pagesView selectTabViewItemAtIndex:row];
}
@end
