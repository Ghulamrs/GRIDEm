// =============================================================================
//  MainWindow+File.mm - File and Project menus: workspaces, projects, files
//  (GRIDEd's UI/MainForm.File.cpp)
// =============================================================================
#import "MainWindow.h"
#import "Common.h"
#import "Commands.h"
#import "Editor/SourceDocument.h"
#import "Editor/CodeView.h"
#import "Panes/PaneHost.h"
#import "Panes/WorkspaceNavigator.h"
#import "Panes/ClassNavigator.h"
#import "Panes/FilesNavigator.h"
#import "Panes/Inspector.h"
#import "Panes/OutputView.h"
#import "Welcome.h"
#import "Dialogs/DialogKit.h"
#import "Dialogs/Sheet.h"
#import "Dialogs/NewProjectSheet.h"
#import "Dialogs/NewItemSheet.h"
#import "Dialogs/ProjectSettingsWindow.h"
#import "Dialogs/DependenciesSheet.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "../Core/Json.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include "../Model/CcsProjectReader.h"
#include "../Model/VsProjectReader.h"
#include "../Model/VsSolution.h"
#include "../Services/ProjectTemplates.h"
#include "../Services/RideToolchain.h"
#import "Editor/Gutter.h"
#import "Editor/Highlighter.h"
#import "Panes/BreakpointsNavigator.h"
#import "Panes/DisassemblyView.h"
#import "Panes/FindResultsView.h"
#import "Panes/IssuesView.h"
#import "Panes/MemoryView.h"
#import "Panes/RegistersView.h"
#import "Panes/StackView.h"
#import "Panes/TerminalView.h"
#import "Panes/VariablesView.h"
#include <regex>

using namespace GRIDEm;

// C and C++ "int main(", Shalimar "fun <> = main(" (GRIDEd's FindEntryPoint)
static const char* const kMainPattern = "^\\s*((int|void)?\\s*|fun\\s*<[^>]*>\\s*=\\s*)main\\s*\\(";

@implementation GRMainWindowController (File)

// the project the Project and Build commands act on: the explorer's selection, else the active
// document's project, else the startup project
- (Model::GrideProjectPtr)contextProject
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return Model::GrideProjectPtr();
    Model::GrideProjectPtr p = [self selectedProject];
    if (p) return p;
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d != nil && d.path != nil)
    {
        p = w->FindProjectForFile(GRStd(d.path));
        if (p) return p;
    }
    return w->StartupProject();
}

// the program Start and Start Without Debugging run: the one whose file is in the editor, else the startup project
- (Model::GrideProjectPtr)runProject
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return Model::GrideProjectPtr();
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d != nil && d.path != nil)
    {
        Model::GrideProjectPtr p = w->FindProjectForFile(GRStd(d.path));
        if (p) return p;
    }
    return w->StartupProject();
}

// =============================================================================
//  opening workspaces / projects / files (GRIDEd's OpenPath)
// =============================================================================
- (void)openPath:(NSString*)path
{
    if (path.length == 0) return;
    std::string p = GRStd(path);
    std::string ext = Core::StringUtil::ToLower(Platform::Paths::Extension(p));
    Settings::GrideSettingsPtr s = self.session.settings;
    BOOL isDir = Platform::Paths::IsDirectory(p);

    BOOL ccs = Model::CcsProjectReader::IsCcsProject(p) && (isDir || ext == ".project" || ext == ".ccsproject" || ext == ".cproject");
    BOOL workspaceKind = ext == ".grws" || ext == ".pro" || ccs || Model::VsProjectReader::IsSolution(p)
                         || Model::VsProjectReader::IsVsProject(p);
    if (!workspaceKind)
    {
        if (isDir) { [self setStatus:[@"Not a project or workspace: " stringByAppendingString:path]]; return; }
        if (Platform::Paths::IsFile(p)) [self openDocument:path line:0];
        return;
    }

    __weak GRMainWindowController* weakSelf = self;
    [self closeWorkspaceThen:^(BOOL closed) {
        GRMainWindowController* me = weakSelf;
        if (!closed || me == nil) return;
        try
        {
            if (ext == ".grws")
            {
                [me setWorkspaceTo:Model::Workspace::Load(p, s->Env->CcsCompilerRoot())];
                [me addRecentWorkspace:path];
            }
            else if (ext == ".pro")
            {
                // GRIDE 2.0's .pro for a Visual Studio project: its solution, with the compiler and arguments the .pro gives
                std::string sln = Model::GrideProject::GreySolution(p);
                if (!sln.empty())
                {
                    Model::GrideProjectPtr g = Model::GrideProject::Load(p);
                    Model::WorkspacePtr w = Model::Workspace::ForSolution(Model::VsSolution::Read(sln));
                    for (const Model::GrideProjectPtr& q : w->Projects)
                        if (Core::StringUtil::EqualsIgnoreCase(q->Name, g->Name))
                        {
                            q->Toolchain = g->Toolchain;
                            q->ProgramArgs = g->ProgramArgs;
                            w->StartupName = q->Name;
                        }
                    w->ActiveConfig = Core::JsonParser::ParseFile(p)->GetString("activeConfiguration", w->ActiveConfig);
                    [me setWorkspaceTo:w];
                }
                else [me setWorkspaceTo:Model::Workspace::ForSingleProject(Model::GrideProject::Load(p))];
                [me addRecentWorkspace:path];
            }
            else if (ccs)
            {
                [me setWorkspaceTo:Model::Workspace::ForSingleProject(Model::CcsProjectReader::Read(p, s->Env->CcsCompilerRoot()))];
                [me addRecentWorkspace:GRNS(Model::CcsProjectReader::ResolveFolder(p))];
            }
            // a Visual Studio solution is a workspace of read-only projects; a project alone is one of them
            else if (Model::VsProjectReader::IsSolution(p))
            {
                [me setWorkspaceTo:Model::Workspace::ForSolution(Model::VsSolution::Read(p))];
                [me addRecentWorkspace:GRNS(Platform::Paths::FullPath(p))];
            }
            else
            {
                [me setWorkspaceTo:Model::Workspace::ForSingleProject(Model::VsProjectReader::Read(p, std::string()))];
                [me addRecentWorkspace:GRNS(Platform::Paths::FullPath(p))];
            }
            [me showStartupSource];
        }
        catch (const std::exception& ex)
        {
            GRShowMessage(me.window, @"GRIDEm", [NSString stringWithFormat:@"Could not open %@\n\n%s", path, ex.what()]);
        }
    }];
}

// a workspace just opened shows its program: the startup project's main file in the editor
- (void)showStartupSource
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w || !w->StartupProject()) return;
    for (GRSourceDocument* d in self.editorArea.documents)
        if (d.path != nil && w->FindProjectForFile(GRStd(d.path))) return;
    std::string path;
    if (w->StartupProject()->FileToOpen(path)) [self openDocument:GRNS(path) line:0];
}

- (void)setWorkspaceTo:(const Model::WorkspacePtr&)w
{
    [self.session setWorkspace:w];
    if (w)
    {
        [self.outputView clear:@"Build"];          // the last workspace's build and run are not this one's
        for (const std::string& err : w->LoadErrors) [self.outputView append:GRNS("Workspace: " + err) source:@"Tools"];
        if (!w->LoadErrors.empty()) [self showPane:@"Output"];
        [self setStatus:[NSString stringWithFormat:@"Opened workspace '%@' with %lu project(s)", GRNS(w->Name),
                                                   (unsigned long)w->Projects.size()]];
        [self setCurrentConfig:GRNS(w->ActiveConfig)];
        [self setCurrentTarget:w->ActiveTarget];
        [self.filesNavigator setRoot:GRNS(w->Directory())];
        self.terminalView.workingDirectory = GRNS(w->Directory());
        [self.inspector setItems:[GRPropertySheet forWorkspace:w] title:GRNS(w->Name)];
    }
    else
    {
        [self.filesNavigator setRoot:nil];
        [self.inspector setItems:nil title:nil];
    }
    // share the saved breakpoints with the open editors
    for (GRSourceDocument* d in self.editorArea.documents)
    {
        if (d.path == nil) continue;
        d.breakpoints = [self.session breakpointsFor:d.path];
        Model::GrideProjectPtr p = w ? w->FindProjectForFile(GRStd(d.path)) : Model::GrideProjectPtr();
        d.readOnly = p && w->IsDocumentReadOnly(GRStd(d.path));
        if (p) d.projectLanguage = p->Lang;
    }
    [self.editorArea refreshGutters];
    [self refreshProjectViews];
    [self reportImportedProjects];
    [self updateScheme];
    [self updateStatus];
    [self refreshBreakpointsPane];
}

- (void)closeWorkspaceThen:(void (^)(BOOL closed))done
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) { done(YES); return; }
    if (self.session.isDebugging) self.session.debugger->Stop();
    // the documents that belong to the workspace's projects close first (a modified one asks)
    NSMutableArray<GRSourceDocument*>* mine = [NSMutableArray array];
    for (GRSourceDocument* d in self.editorArea.documents)
        if (d.path != nil && w->FindProjectForFile(GRStd(d.path))) [mine addObject:d];
    [self closeDocuments:mine index:0 then:^(BOOL closed) {
        if (!closed) { done(NO); return; }
        [self saveWorkspace];
        [self setWorkspaceTo:Model::WorkspacePtr()];
        done(YES);
    }];
}

- (void)closeDocuments:(NSArray<GRSourceDocument*>*)docs index:(NSUInteger)i then:(void (^)(BOOL closed))done
{
    if (i >= docs.count) { done(YES); return; }
    __weak GRMainWindowController* weakSelf = self;
    [self.editorArea closeDocument:docs[i] done:^(BOOL closed) {
        GRMainWindowController* me = weakSelf;
        if (!closed || me == nil) { done(NO); return; }
        [me closeDocuments:docs index:i + 1 then:done];
    }];
}

- (void)saveWorkspace
{
    NSString* failed = [self.session saveWorkspace];
    if (failed != nil) [self.outputView append:[@"Could not save workspace: " stringByAppendingString:failed] source:@"Tools"];
}

- (GRSourceDocument*)openDocument:(NSString*)path line:(NSInteger)line
{
    if (path.length == 0) return nil;
    if (!Platform::Paths::IsFile(GRStd(path)))
    {
        GRShowMessage(self.window, @"GRIDEm", [@"File not found:\n" stringByAppendingString:path]);
        return nil;
    }
    BOOL fresh = [self.editorArea documentForPath:path] == nil;
    NSError* error = nil;
    GRSourceDocument* d = [self.editorArea openPath:path line:(line > 0 ? line : 0) column:1 error:&error];
    if (d == nil)
    {
        GRShowMessage(self.window, @"GRIDEm", [NSString stringWithFormat:@"Could not open %@\n\n%@", path,
                                                                         GROr(error.localizedDescription, @"")]);
        return nil;
    }
    if (fresh)
    {
        Model::WorkspacePtr w = self.session.workspace;
        Model::GrideProjectPtr p = w ? w->FindProjectForFile(GRStd(d.path)) : Model::GrideProjectPtr();
        d.breakpoints = [self.session breakpointsFor:d.path];
        d.readOnly = p && w->IsDocumentReadOnly(GRStd(d.path));
        if (p) d.projectLanguage = p->Lang;
        [self.editorArea refreshGutters];
        [self addRecentFile:d.path];
    }
    return d;
}

- (void)navigateTo:(NSString*)file line:(NSInteger)line column:(NSInteger)column
{
    if (file.length == 0) return;
    if (!Platform::Paths::IsFile(GRStd(file))) { [self setStatus:[@"Not found: " stringByAppendingString:file]]; return; }
    GRSourceDocument* d = [self openDocument:file line:0];
    if (d != nil && line > 0) [self.editorArea.activeCodeView goToLine:line column:MAX(1, column)];
}

- (void)addRecentFile:(NSString*)path
{
    Settings::RecentList::Push(self.session.settings->Recent->Files, GRStd(path));
    [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:[NSURL fileURLWithPath:path]];
}

- (void)addRecentWorkspace:(NSString*)path
{
    Settings::RecentList::Push(self.session.settings->Recent->Workspaces, GRStd(path));
}

- (void)refreshProjectViews
{
    Model::WorkspacePtr w = self.session.workspace;
    [self.workspaceNavigator setWorkspace:w];
    [self.classNavigator scan:w];
    [self updateTitle];
    [self updateScheme];
}

// RIDE's Messages lines for every imported project of the workspace, and for every RIDE .pro
- (void)reportImportedProjects
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return;
    [self applyCcsStates];
    for (const Model::GrideProjectPtr& p : w->Projects)
        if (p->IsImported() || p->IsRide())
            for (const std::string& line : p->ImportReportLines()) [self.outputView append:GRNS(line) source:@"Tools"];
}

// a CCS project's own state lives in the settings, never in CCS's files (GRIDEd's ApplyCcsStates)
- (void)applyCcsStates
{
    Model::WorkspacePtr w = self.session.workspace;
    Settings::GrideSettingsPtr s = self.session.settings;
    if (!w) return;
    for (const Model::GrideProjectPtr& p : w->Projects)
    {
        if (!p->IsCcs()) continue;
        Core::JsonPtr state = s->CcsState(p->ProjectDir());
        if (!state) continue;
        if (state->Has("args") && p->ProgramArgs.empty()) p->ProgramArgs = state->GetStringList("args");
        if (!p->EmulateOnly) p->EmulateOnly = state->GetBool("emulateOnly", false);
        if (Services::RideToolchain::FromWord(p->Toolchain) == Services::RideTool::Auto)
            p->Toolchain = state->GetString("toolchain", p->Toolchain);
        Core::JsonPtr ft = state->GetObject("folderToolchains");
        if (ft)
            for (const std::string& folder : ft->KeyNames())
            {
                std::string word = ft->GetString(folder, std::string());
                std::string have;
                if (!p->FolderToolchains.Find(folder, have) && Services::RideToolchain::FromWord(word) != Services::RideTool::Auto)
                    p->FolderToolchains.Set(folder, word);
            }
        // the Property Pages' changes, unless the workspace's entry has its own
        if (!Model::Workspace::ImportedSettingsChanged(p)) p->ApplyImportedSettings(state->GetObject("settings"));
    }
}

- (void)rememberCcsState:(const Model::GrideProjectPtr&)p
{
    if (!p || !p->IsCcs()) return;
    Core::JsonPtr state = Core::JsonValue::MakeObject();
    if (!p->ProgramArgs.empty()) state->SetList("args", p->ProgramArgs);
    if (p->EmulateOnly) state->SetBool("emulateOnly", true);
    if (Services::RideToolchain::FromWord(p->Toolchain) != Services::RideTool::Auto)
        state->SetString("toolchain", Core::StringUtil::Trim(p->Toolchain));
    if (!p->FolderToolchains.Empty())
    {
        Core::JsonPtr ft = Core::JsonValue::MakeObject();
        for (const std::pair<std::string, std::string>& kv : p->FolderToolchains.Items()) ft->SetString(kv.first, kv.second);
        state->SetValue("folderToolchains", ft);
    }
    Core::JsonPtr settings = Model::Workspace::ImportedSettingsChanged(p);
    if (settings) state->SetValue("settings", settings);
    self.session.settings->RememberCcsState(p->ProjectDir(), state);
    [self.session saveSettings];
}

// an imported project's compiler words and arguments live in the workspace's entry: a workspace opened
// from a Visual Studio solution or project is saved beside it as <name>.grws the first time
// (GRIDEd's KeepImportedState)
- (void)keepImportedState:(const Model::GrideProjectPtr&)p
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w || !p || !p->IsImported()) return;
    if (!w->IsTemporary) { [self saveWorkspace]; return; }
    if (p->IsCcs()) return;
    if (Platform::Paths::Exists(w->FilePath))
    {
        [self.outputView append:GRNS("Workspace: " + p->Name + "'s compiler and arguments are kept for this session only - " +
                                     w->FilePath + " is already there; open it to keep them") source:@"Tools"];
        return;
    }
    try
    {
        w->Save();
        [self addRecentWorkspace:GRNS(w->FilePath)];
        [self.outputView append:GRNS("Workspace: saved as " + w->FilePath + " - " + p->Name +
                                     "'s compiler and arguments are kept there, its Visual Studio files are never written") source:@"Tools"];
        [self updateTitle];
    }
    catch (const std::exception& ex) { [self.outputView append:GRNS(std::string("Could not save workspace: ") + ex.what()) source:@"Tools"]; }
}

- (BOOL)ccsEnabled { return self.session.settings->Env->CcsEnabled ? YES : NO; }

- (BOOL)hasImportedProject
{
    Model::GrideProjectPtr p = [self contextProject];
    return p && ((p->IsCcs() && p->Ccs && p->Ccs->Reading) || (p->IsVs() && p->Vs && p->Vs->Reading));
}

// =============================================================================
//  the program of the file in the editor (GRIDEd's FindEntryPoint / ActiveFileHasMain / EnsureProgram)
// =============================================================================
- (BOOL)findEntryPoint:(const Model::GrideProjectPtr&)project file:(NSString**)file line:(NSInteger*)line
{
    std::regex re(kMainPattern);
    if (project)
        for (const std::string& f : project->AllFiles())
        {
            if (!Platform::Paths::IsFile(f) || Core::PathUtil::HasExtension(f, ".h")) continue;
            std::string text, error;
            if (!Platform::Paths::ReadFile(f, text, error)) continue;
            std::vector<std::string> lines = Core::StringUtil::SplitLines(text);
            for (size_t i = 0; i < lines.size(); i++)
                if (std::regex_search(lines[i], re)) { *file = GRNS(f); *line = (NSInteger)i + 1; return YES; }
        }
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d != nil && d.path != nil) { *file = d.path; *line = 1; return YES; }
    return NO;
}

- (BOOL)activeFileHasMain
{
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d == nil || d.path == nil) return NO;
    Core::Language l = self.session.settings->DetectLanguage(GRStd(d.path), Core::Language::Other);
    if (!Core::LanguageInfo::IsCompiled(l)) return NO;
    std::regex re(kMainPattern);
    for (const std::string& line : Core::StringUtil::SplitLines(GRStd(d.text)))
        if (std::regex_search(line, re)) return YES;
    return NO;
}

// with no workspace open, the file in the editor with main() becomes a program of its own: a project of
// that one file, in a temporary workspace, never written - built beside the file
- (BOOL)ensureProgram
{
    if (self.session.workspace) return YES;
    if (![self activeFileHasMain]) return NO;
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d.isModified) { NSError* error = nil; [d saveNow:&error]; }
    std::string file = Platform::Paths::FullPath(GRStd(d.path));
    std::string name = Platform::Paths::FileNameWithoutExtension(file);
    Model::GrideProjectPtr p = std::make_shared<Model::GrideProject>();
    p->FilePath = Platform::Paths::Combine(Platform::Paths::DirectoryName(file), name + ".pro");
    p->Name = name;
    p->Lang = self.session.settings->DetectLanguage(file, Core::Language::C90);
    p->Template = "Program";
    // the Mac's own programs and the DSP (GRIDEd's are Windows and the DSP)
    p->Targets.push_back(Core::Target::MacOS);
    p->Targets.push_back(Core::Target::C6747);
    for (Core::Target t : p->Targets)
    {
        p->Configurations.push_back(std::make_shared<Model::BuildConfiguration>("Debug", t));
        p->Configurations.push_back(std::make_shared<Model::BuildConfiguration>("Release", t));
    }
    p->AddFile(file, Model::GrideProject::DefaultFolderFor(file));
    p->IsDirty = false;
    p->IsReadOnly = true;              // the program's own .pro is never written
    Core::Target target = [self currentTarget];
    Model::WorkspacePtr w = Model::Workspace::ForSingleProject(p);
    w->ActiveConfig = GRStd([self currentConfig]);
    w->ActiveTarget = p->SupportsTarget(target) ? target : Core::Target::MacOS;
    [self setWorkspaceTo:w];
    [self.outputView append:GRNS("a program of its own: " + Platform::Paths::FileName(file) + " (no project open) is built as " + name)
                     source:@"Build"];
    return YES;
}

// =============================================================================
//  File menu
// =============================================================================
- (void)fileNewProject
{
    Settings::GrideSettingsPtr s = self.session.settings;
    GRNewProjectSheet* sheet = [[GRNewProjectSheet alloc] initWithLocation:GRNS(s->Env->DefaultProjectLocation)
                                                             haveWorkspace:self.session.workspace ? YES : NO];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        if (sheet.importCcs) { [me fileOpenCcs]; return; }
        Model::GrideProjectPtr p;
        try
        {
            p = Model::GrideProject::CreateNew(GRStd(sheet.projectLocation), GRStd(sheet.projectName), sheet.language,
                                               GRStd(sheet.templateName), [sheet targets]);
            Services::ProjectTemplates::Populate(p);
            // RIDE's settings: the compiler a project starts with and the installation's Compiler Options
            std::string word = Core::StringUtil::Trim(s->Env->DefaultCompiler);
            if (!word.empty() && !Core::StringUtil::EqualsIgnoreCase(word, "auto")
                && Services::RideToolchain::FromWord(word) != Services::RideTool::Auto)
                p->Toolchain = word;
            if (s->CompilerOptions && !s->CompilerOptions->Empty()) p->Options->CopyFrom(s->CompilerOptions);
            p->Save();
        }
        catch (const std::exception& ex)
        {
            GRShowMessage(me.window, @"New Project", GRNS(ex.what()));
            return;
        }
        s->Env->DefaultProjectLocation = GRStd(sheet.projectLocation);
        void (^openMain)(void) = ^{
            [me.outputView append:GRNS("Created project " + p->FilePath) source:@"Tools"];
            std::vector<std::string> files = p->AllFiles();
            for (const std::string& f : files)
                if (Core::StringUtil::StartsWith(Platform::Paths::FileNameWithoutExtension(f), "main") || files.size() == 1)
                {
                    [me openDocument:GRNS(f) line:0];
                    break;
                }
        };
        if (!me.session.workspace || sheet.newWorkspace)
        {
            [me closeWorkspaceThen:^(BOOL closed) {
                if (!closed) return;
                Model::WorkspacePtr w = Model::Workspace::ForSingleProject(p);
                if (!p->Targets.empty()) w->ActiveTarget = p->Targets[0];
                try { w->Save(); }                                    // <Name>/<Name>.grws
                catch (const std::exception& ex) { GRShowMessage(me.window, @"New Project", GRNS(ex.what())); }
                [me setWorkspaceTo:w];
                [me addRecentWorkspace:GRNS(w->FilePath)];
                openMain();
            }];
        }
        else
        {
            me.session.workspace->AddProject(p);
            [me saveWorkspace];
            [me refreshProjectViews];
            openMain();
        }
    }];
}

- (void)fileNewWorkspace
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"New Workspace";
    panel.nameFieldStringValue = @"Workspace.grws";
    panel.allowedContentTypes = @[ GROr([UTType typeWithFilenameExtension:@"grws"], UTTypeData) ];
    panel.directoryURL = [NSURL fileURLWithPath:GRNS(Platform::Paths::ExpandHome(self.session.settings->Env->DefaultProjectLocation))];
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        GRMainWindowController* me = weakSelf;
        if (r != NSModalResponseOK || me == nil) return;
        NSString* path = panel.URL.path;
        [me closeWorkspaceThen:^(BOOL closed) {
            if (!closed) return;
            Model::WorkspacePtr w = std::make_shared<Model::Workspace>();
            w->FilePath = GRStd(path);
            w->Name = Platform::Paths::FileNameWithoutExtension(w->FilePath);
            try { w->Save(); }
            catch (const std::exception& ex) { GRShowMessage(me.window, @"New Workspace", GRNS(ex.what())); return; }
            [me setWorkspaceTo:w];
            [me addRecentWorkspace:path];
        }];
    }];
}

- (void)fileNewFile
{
    GRSourceDocument* d = [self.editorArea newUntitled:[self newFileLanguage]];
    d.breakpoints = std::make_shared<std::set<int> >();
}

- (void)fileOpenWorkspace
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Open Project / Workspace";
    panel.message = @"A GRIDE workspace or project (.grws, .pro), a CCS 7.4 / 5.5 project folder, or a Visual Studio solution or project";
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = YES;
    panel.allowsMultipleSelection = NO;
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [weakSelf openPath:panel.URL.path];
    }];
}

- (void)fileOpenFile
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Open File";
    panel.allowsMultipleSelection = YES;
    panel.canChooseDirectories = NO;
    Model::WorkspacePtr w = self.session.workspace;
    if (w) panel.directoryURL = [NSURL fileURLWithPath:GRNS(w->Directory())];
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        for (NSURL* u in panel.URLs) [weakSelf openDocument:u.path line:0];
    }];
}

- (void)fileOpenCcs
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Open CCS Project";
    panel.message = @"Select the Code Composer Studio project folder (the one holding .project / .ccsproject), or a CCS workspace folder";
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        GRMainWindowController* me = weakSelf;
        if (r != NSModalResponseOK || me == nil) return;
        dispatch_async(dispatch_get_main_queue(), ^{ [me importCcsFrom:panel.URL.path]; });
    }];
}

- (void)importCcsFrom:(NSString*)folder
{
    Settings::GrideSettingsPtr s = self.session.settings;
    std::string dir = GRStd(folder);
    __weak GRMainWindowController* weakSelf = self;
    // a workspace is not a project (RIDE): its members are named, and can all come in
    if (Model::CcsProjectReader::IsCcsWorkspace(dir))
    {
        std::string error;
        Model::CcsWorkspacePtr ws = Model::CcsWorkspace::Read(dir, error);
        std::vector<std::string> names;
        if (ws) for (const Model::CcsWorkspaceMember& m : ws->Projects) names.push_back(m.Name);
        std::string said = Platform::Paths::FileName(Core::StringUtil::TrimEnd(dir, '/')) +
                           " is a CCS workspace, not a project - open one of its projects: " +
                           (names.empty() ? std::string("(none registered)") : Core::StringUtil::Join(", ", names));
        [self.outputView append:GRNS(said) source:@"Tools"];
        if (ws) for (const std::string& n : ws->Notes) [self.outputView append:GRNS("CCS workspace: " + n) source:@"Tools"];
        if (names.empty()) return;
        GRAskYesNo(self.window, @"Import CCS Project", [GRNS(said) stringByAppendingString:@"\n\nAdd all of them to the current workspace?"],
                   ^(BOOL yes) {
            GRMainWindowController* me = weakSelf;
            if (!yes || me == nil) return;
            try
            {
                if (!me.session.workspace)
                    [me setWorkspaceTo:Model::Workspace::ForSingleProject(
                                           Model::CcsProjectReader::Read(ws->Projects[0].Location, s->Env->CcsCompilerRoot()))];
            }
            catch (const std::exception& ex) { [me.outputView append:GRNS(std::string("CCS project: ") + ex.what()) source:@"Tools"]; return; }
            Model::WorkspacePtr w = me.session.workspace;
            for (const Model::CcsWorkspaceMember& m : ws->Projects)
            {
                try
                {
                    Model::GrideProjectPtr p = Model::CcsProjectReader::Read(m.Location, s->Env->CcsCompilerRoot());
                    if (w->AddProject(p)) [me.outputView append:GRNS("Imported CCS project " + p->Name + " (read-only)") source:@"Tools"];
                }
                catch (const std::exception& ex) { [me.outputView append:GRNS("CCS project " + m.Name + ": " + ex.what()) source:@"Tools"]; }
            }
            if (!w->IsTemporary) [me saveWorkspace];
            [me refreshProjectViews];
            [me reportImportedProjects];
        });
        return;
    }
    if (!Model::CcsProjectReader::IsCcsProject(dir))
    {
        GRShowMessage(self.window, @"Import CCS Project", [@"No .project / .ccsproject found in\n" stringByAppendingString:folder]);
        return;
    }
    Model::WorkspacePtr w = self.session.workspace;
    if (w && !w->IsTemporary)
    {
        GRAskYesNo(self.window, @"Import CCS Project", @"Add the CCS project to the current workspace?\n(No = open it on its own)", ^(BOOL yes) {
            GRMainWindowController* me = weakSelf;
            if (me == nil) return;
            if (!yes) { [me openPath:folder]; return; }
            try
            {
                Model::GrideProjectPtr p = Model::CcsProjectReader::Read(dir, s->Env->CcsCompilerRoot());
                me.session.workspace->AddProject(p);
                [me saveWorkspace];
                [me refreshProjectViews];
                [me.outputView append:GRNS("Imported CCS project " + p->Name + " (read-only)") source:@"Tools"];
                if (p->Ccs) for (const std::string& line : p->Ccs->ReportLines) [me.outputView append:GRNS(line) source:@"Tools"];
            }
            catch (const std::exception& ex) { GRShowMessage(me.window, @"Import CCS Project", GRNS(ex.what())); }
        });
        return;
    }
    [self openPath:folder];
}

- (void)fileClose { [self.editorArea closeActive]; }

- (void)fileCloseWorkspace { [self closeWorkspaceThen:^(BOOL closed) { (void)closed; }]; }

- (void)fileSave
{
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d == nil) return;
    if (d.path == nil) { [self.editorArea saveDocument:d]; return; }
    NSError* error = nil;
    if ([d saveNow:&error])
    {
        [self addRecentFile:d.path];
        [self setStatus:[@"Saved " stringByAppendingString:d.path]];
    }
    else if (error != nil && error.code != NSUserCancelledError) GRShowMessage(self.window, @"Save", error.localizedDescription);
}

- (void)fileSaveAs
{
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d == nil) return;
    [d saveDocumentAs:nil];
}

- (void)fileSaveAll
{
    NSError* error = nil;
    if (![self.editorArea saveAll:&error] && error != nil) GRShowMessage(self.window, @"Save All", error.localizedDescription);
    [self saveWorkspace];
    [self.session saveSettings];
    [self setStatus:@"All files saved"];
}

// creates a .pro inside an existing folder of sources
- (void)fileImportFolder
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Import Existing Folder";
    panel.message = @"Select a folder with existing C90 / C++11 / Shalimar sources";
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [weakSelf importFolder:panel.URL.path];
    }];
}

- (void)importFolder:(NSString*)folder
{
    std::string dir = GRStd(folder);
    std::string name = Core::StringUtil::Replace(Platform::Paths::FileName(dir), " ", "_");
    std::string pro = Platform::Paths::Combine(dir, name + ".pro");
    if (Platform::Paths::IsFile(pro)) { [self openPath:GRNS(pro)]; return; }
    Model::GrideProjectPtr p = std::make_shared<Model::GrideProject>();
    p->FilePath = pro;
    p->Name = name;
    p->Template = "Imported";
    p->Targets.push_back(Core::Target::MacOS);
    p->Targets.push_back(Core::Target::C6747);
    int c = 0, cpp = 0, shl = 0;
    NSDirectoryEnumerator* e = [[NSFileManager defaultManager] enumeratorAtPath:folder];
    for (NSString* rel in e)
    {
        std::string r = GRStd(rel);
        std::string ext = Core::StringUtil::ToLower(Platform::Paths::Extension(r));
        if (ext != ".c" && ext != ".h" && ext != ".cpp" && ext != ".hpp" && ext != ".shl" && ext != ".shm" && ext != ".asm"
            && ext != ".cmd")
            continue;
        if (Core::StringUtil::StartsWith(r, "out/") || Core::StringUtil::StartsWith(r, "obj/") || Core::StringUtil::StartsWith(r, "."))
            continue;
        std::string f = Platform::Paths::Combine(dir, r);
        std::string sub = Platform::Paths::DirectoryName(r);
        if (sub.empty()) sub = Model::GrideProject::DefaultFolderFor(f);
        p->AddFile(f, sub);
        if (ext == ".c") c++;
        else if (ext == ".cpp") cpp++;
        else if (ext == ".shl" || ext == ".shm") shl++;
    }
    p->Lang = (cpp > c && cpp >= shl) ? Core::Language::Cpp11 : (shl > c ? Core::Language::Shalimar : Core::Language::C90);
    for (Core::Target t : p->Targets)
    {
        p->Configurations.push_back(std::make_shared<Model::BuildConfiguration>("Debug", t));
        p->Configurations.push_back(std::make_shared<Model::BuildConfiguration>("Release", t));
    }
    try { p->Save(); }
    catch (const std::exception& ex) { GRShowMessage(self.window, @"Import Folder", GRNS(ex.what())); return; }
    [self.outputView append:[NSString stringWithFormat:@"Created %@ with %lu file(s)", GRNS(pro), (unsigned long)p->AllFiles().size()]
                     source:@"Tools"];
    Model::WorkspacePtr w = self.session.workspace;
    if (w && !w->IsTemporary)
    {
        w->AddProject(p);
        [self saveWorkspace];
        [self refreshProjectViews];
    }
    else [self openPath:GRNS(pro)];
}

// the project's files in a .zip, under <Name>/ (GRIDEd's ExportZip), written with ditto
- (void)fileExportZip
{
    Model::GrideProjectPtr p = [self contextProject];
    if (!p) return;
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[ UTTypeZIP ];
    panel.nameFieldStringValue = [GRNS(p->Name) stringByAppendingString:@".zip"];
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        GRMainWindowController* me = weakSelf;
        if (r != NSModalResponseOK || me == nil) return;
        [me exportProject:p to:panel.URL.path];
    }];
}

- (void)exportProject:(const Model::GrideProjectPtr&)p to:(NSString*)zipPath
{
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* stage = [NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
    NSString* root = [stage stringByAppendingPathComponent:GRNS(p->Name)];
    [fm createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];
    int n = 0;
    std::vector<std::pair<std::string, std::string> > entries;      // file -> path in the archive
    if (!p->IsCcs() && Platform::Paths::IsFile(p->FilePath)) entries.push_back(std::make_pair(p->FilePath, Platform::Paths::FileName(p->FilePath)));
    for (const std::string& f : p->AllFiles())
    {
        if (!Platform::Paths::IsFile(f)) continue;
        std::string rel = p->ToRelative(f);
        if (Platform::Paths::IsRooted(rel) || Core::StringUtil::StartsWith(rel, "../")) rel = "external/" + Platform::Paths::FileName(f);
        entries.push_back(std::make_pair(f, rel));
    }
    for (const std::pair<std::string, std::string>& e : entries)
    {
        NSString* to = [root stringByAppendingPathComponent:GRNS(e.second)];
        [fm createDirectoryAtPath:[to stringByDeletingLastPathComponent] withIntermediateDirectories:YES attributes:nil error:nil];
        if ([fm copyItemAtPath:GRNS(e.first) toPath:to error:nil]) n++;
    }
    [fm removeItemAtPath:zipPath error:nil];
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/ditto"];
    task.arguments = @[ @"-c", @"-k", @"--keepParent", root, zipPath ];
    NSError* error = nil;
    BOOL ok = [task launchAndReturnError:&error];
    if (ok) { [task waitUntilExit]; ok = task.terminationStatus == 0; }
    [fm removeItemAtPath:stage error:nil];
    if (ok) [self setStatus:[NSString stringWithFormat:@"Exported %d file(s) to %@", n, zipPath]];
    else GRShowMessage(self.window, @"Export", GROr(error.localizedDescription, @"ditto could not write the archive."));
}

// the active document, numbered, in the editor's font (GRIDEd's OnPrintPage)
- (void)filePrint
{
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d == nil) return;
    NSArray<NSString*>* lines = [d.text componentsSeparatedByString:@"\n"];
    NSMutableString* numbered = [NSMutableString string];
    for (NSUInteger i = 0; i < lines.count; i++)
        [numbered appendFormat:@"%5lu  %@\n", (unsigned long)i + 1, [lines[i] stringByReplacingOccurrencesOfString:@"\t" withString:@"    "]];
    NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
    info.horizontalPagination = NSPrintingPaginationModeFit;
    info.verticallyCentered = NO;
    CGFloat width = info.paperSize.width - info.leftMargin - info.rightMargin;
    NSTextView* view = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, width, 100)];
    NSFont* font = [NSFont userFixedPitchFontOfSize:9];
    view.font = font;
    view.string = numbered;
    [view sizeToFit];
    NSPrintOperation* op = [NSPrintOperation printOperationWithView:view printInfo:info];
    op.jobTitle = d.displayName;
    [op runOperationModalForWindow:self.window delegate:nil didRunSelector:nil contextInfo:nullptr];
}

- (void)fileOpenSelected
{
    NSString* f = [self selectedFile];
    if (f != nil) [self openDocument:f line:0];
}

// =============================================================================
//  Project menu
// =============================================================================
- (void)projectAddNewItem
{
    Model::GrideProjectPtr p = [self contextProject];
    if (!p || p->IsReadOnly) return;
    GRNewItemSheet* sheet = [[GRNewItemSheet alloc] initWithProject:p];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        std::string path = GRStd(sheet.filePath);
        try
        {
            Core::PathUtil::EnsureDirectory(Platform::Paths::DirectoryName(path));
            if (!Platform::Paths::Exists(path))
            {
                std::string error;
                if (!Platform::Paths::WriteFile(path, Services::ProjectTemplates::ItemContent(Platform::Paths::FileName(path), p->Lang), error))
                    throw std::runtime_error(error);
            }
            p->AddFile(path, GRStd(sheet.folder));
            p->Save();
        }
        catch (const std::exception& ex) { GRShowMessage(me.window, @"Add New Item", GRNS(ex.what())); return; }
        [me refreshProjectViews];
        [me.workspaceNavigator reveal:sheet.filePath];
        [me openDocument:sheet.filePath line:0];
    }];
}

- (void)projectAddExisting
{
    Model::GrideProjectPtr p = [self contextProject];
    if (!p || p->IsReadOnly) return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = [@"Add Existing Item - " stringByAppendingString:GRNS(p->Name)];
    panel.allowsMultipleSelection = YES;
    panel.directoryURL = [NSURL fileURLWithPath:GRNS(p->ProjectDir())];
    GRNavNode* n = self.workspaceNavigator.selectedNode;
    NSString* folder = (n != nil && n.kind == GRNodeFolder) ? n.folder : nil;
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        GRMainWindowController* me = weakSelf;
        if (r != NSModalResponseOK || me == nil) return;
        int added = 0;
        for (NSURL* u in panel.URLs)
            if (p->AddFile(GRStd(u.path), GRStd(folder))) added++;
        try { p->Save(); }
        catch (const std::exception& ex) { GRShowMessage(me.window, @"Add Existing Item", GRNS(ex.what())); }
        [me refreshProjectViews];
        [me setStatus:[NSString stringWithFormat:@"Added %d file(s) to %@", added, GRNS(p->Name)]];
    }];
}

- (void)projectNewFolder
{
    Model::GrideProjectPtr p = [self contextProject];
    if (!p || p->IsReadOnly) return;
    GRInputSheet* sheet = [[GRInputSheet alloc] initWithTitle:@"New Folder" prompt:@"Folder name" value:@"NewFolder"];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        std::string name = Core::PathUtil::Normalize(GRStd(sheet.value));
        p->GetOrAddFolder(name);
        Core::PathUtil::EnsureDirectory(p->ToAbsolute(name));
        try { p->Save(); }
        catch (const std::exception& ex) { GRShowMessage(me.window, @"New Folder", GRNS(ex.what())); }
        [me refreshProjectViews];
    }];
}

- (void)projectRemoveItem
{
    NSString* f = [self selectedFile];
    Model::GrideProjectPtr p = [self selectedProject];
    if (f == nil || !p) return;
    if (p->IsReadOnly)
    {
        GRShowMessage(self.window, @"Remove", p->IsVs() ? @"Visual Studio projects are read-only in GRIDEm." : @"CCS projects are read-only in GRIDEm.");
        return;
    }
    NSString* q = [NSString stringWithFormat:@"Remove %@ from %@?\n(The file stays on disk.)", f.lastPathComponent, GRNS(p->Name)];
    __weak GRMainWindowController* weakSelf = self;
    GRAskYesNo(self.window, @"Remove", q, ^(BOOL yes) {
        GRMainWindowController* me = weakSelf;
        if (!yes || me == nil) return;
        p->RemoveFile(GRStd(f));
        try { p->Save(); }
        catch (const std::exception& ex) { GRShowMessage(me.window, @"Remove", GRNS(ex.what())); }
        [me refreshProjectViews];
    });
}

- (void)projectRename { [self.workspaceNavigator beginRename]; }

- (BOOL)isSelectedExcluded
{
    NSString* f = [self selectedFile];
    Model::GrideProjectPtr p = [self selectedProject];
    return f != nil && p && p->IsExcluded(GRStd(f));
}

- (void)projectExclude
{
    NSString* f = [self selectedFile];
    Model::GrideProjectPtr p = [self selectedProject];
    if (f == nil || !p || p->IsReadOnly) return;
    p->SetExcluded(GRStd(f), !p->IsExcluded(GRStd(f)));
    try { p->Save(); }
    catch (const std::exception& ex) { GRShowMessage(self.window, @"Exclude", GRNS(ex.what())); }
    [self refreshProjectViews];
}

- (void)projectAddProject
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Add Project to Workspace";
    panel.message = @"A GRIDE project (.pro), a CCS 7.4 / 5.5 project (.project, .ccsproject) or a Visual Studio project (.vcxproj, .vcproj)";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        GRMainWindowController* me = weakSelf;
        if (r != NSModalResponseOK || me == nil) return;
        std::string file = GRStd(panel.URL.path);
        Settings::GrideSettingsPtr s = me.session.settings;
        try
        {
            Model::WorkspacePtr w = me.session.workspace;
            Model::GrideProjectPtr p;
            if (Core::PathUtil::HasExtension(file, ".pro")) p = Model::GrideProject::Load(file);
            else if (Model::VsProjectReader::IsVsProject(file)) p = Model::VsProjectReader::Read(file, w ? w->SolutionPath : std::string());
            else p = Model::CcsProjectReader::Read(file, s->Env->CcsCompilerRoot());
            if (!w) { [me setWorkspaceTo:Model::Workspace::ForSingleProject(p)]; return; }
            if (!w->AddProject(p)) { GRShowMessage(me.window, @"GRIDEm", @"That project is already in the workspace."); return; }
            [me saveWorkspace];
            [me refreshProjectViews];
            if (p->IsImported() || p->IsRide()) [me reportImportedProjects];
        }
        catch (const std::exception& ex) { GRShowMessage(me.window, @"Add Project", GRNS(ex.what())); }
    }];
}

- (void)projectRemoveProject
{
    Model::GrideProjectPtr p = [self contextProject];
    Model::WorkspacePtr w = self.session.workspace;
    if (!p || !w) return;
    NSString* q = [NSString stringWithFormat:@"Remove project '%@' from the workspace?\n(Its files stay on disk.)", GRNS(p->Name)];
    __weak GRMainWindowController* weakSelf = self;
    GRAskYesNo(self.window, @"GRIDEm", q, ^(BOOL yes) {
        GRMainWindowController* me = weakSelf;
        if (!yes || me == nil) return;
        NSMutableArray<GRSourceDocument*>* mine = [NSMutableArray array];
        for (GRSourceDocument* d in me.editorArea.documents)
            if (d.path != nil && p->ContainsFile(GRStd(d.path))) [mine addObject:d];
        [me closeDocuments:mine index:0 then:^(BOOL closed) {
            if (!closed) return;
            w->RemoveProject(p);
            [me saveWorkspace];
            [me refreshProjectViews];
        }];
    });
}

- (void)projectSetStartup
{
    Model::GrideProjectPtr p = [self contextProject];
    Model::WorkspacePtr w = self.session.workspace;
    if (!p || !w) return;
    w->StartupName = p->Name;
    w->IsDirty = true;
    [self saveWorkspace];
    [self refreshProjectViews];
}

- (void)projectDependencies
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return;
    GRDependenciesSheet* sheet = [[GRDependenciesSheet alloc] initWithWorkspace:w selected:[self contextProject]];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        if (ok) { [weakSelf saveWorkspace]; [weakSelf refreshProjectViews]; }
    }];
}

- (void)projectBuildOrder
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return;
    NSMutableString* sb = [NSMutableString stringWithString:@"Projects are built in this order (dependencies first):\n\n"];
    int i = 1;
    for (const Model::GrideProjectPtr& p : w->BuildOrder())
    {
        [sb appendFormat:@"%3d. %@", i++, GRNS(p->Name)];
        if (!p->DependsOn.empty()) [sb appendFormat:@"    (depends on %@)", GRNS(Core::StringUtil::Join(", ", p->DependsOn))];
        [sb appendString:@"\n"];
    }
    GRTextSheet* sheet = [[GRTextSheet alloc] initWithTitle:@"Build Order" text:sb mono:YES];
    [sheet beginOn:self.window completion:^(BOOL ok) { (void)ok; }];
}

// read again what CCS or Visual Studio wrote, keeping what GRIDEm keeps for the project
- (void)projectRefreshImported
{
    Model::WorkspacePtr w = self.session.workspace;
    Settings::GrideSettingsPtr s = self.session.settings;
    if (!w) return;
    int refreshed = 0;
    for (size_t i = 0; i < w->Projects.size(); i++)
    {
        Model::GrideProjectPtr p = w->Projects[i];
        if (!p->IsCcs() && !p->IsVs()) continue;      // a RIDE .pro is not refreshed from CCS or Visual Studio
        try
        {
            Model::GrideProjectPtr again;
            if (p->IsVs())
            {
                again = Model::VsProjectReader::Read(p->FilePath, p->Vs ? p->Vs->SolutionPath : std::string());
                if (again->Vs) again->Vs->SolutionFolder = p->Vs ? p->Vs->SolutionFolder : std::string();
                again->DependsOn.insert(again->DependsOn.end(), p->DependsOn.begin(), p->DependsOn.end());
            }
            else again = Model::CcsProjectReader::Read(p->ProjectDir(), s->Env->CcsCompilerRoot());
            again->Toolchain = p->Toolchain;
            for (const std::pair<std::string, std::string>& kv : p->FolderToolchains.Items()) again->FolderToolchains.Set(kv.first, kv.second);
            again->ProgramArgs = p->ProgramArgs;
            again->EmulateOnly = p->EmulateOnly;
            again->IsDirty = false;
            w->Projects[i] = again;
            refreshed++;
        }
        catch (const std::exception& ex) { [self.outputView append:GRNS("Refresh " + p->Name + ": " + ex.what()) source:@"Tools"]; }
    }
    for (GRSourceDocument* d in self.editorArea.documents)
    {
        if (d.path == nil) continue;
        Model::GrideProjectPtr p = w->FindProjectForFile(GRStd(d.path));
        d.readOnly = p && w->IsDocumentReadOnly(GRStd(d.path));
    }
    [self refreshProjectViews];
    [self reportImportedProjects];
    [self setStatus:[NSString stringWithFormat:@"Refreshed %d imported project(s)", refreshed]];
}

// Project > CCS / VS Mapping: what the files say, and what GRIDEm builds with
- (void)projectMapping
{
    Model::GrideProjectPtr p = [self contextProject];
    if (!p) return;
    NSString* title = nil;
    std::string text;
    if (p->IsCcs() && p->Ccs && p->Ccs->Reading)
    {
        title = [@"CCS mapping - " stringByAppendingString:GRNS(p->Name)];
        text = p->Ccs->Reading->MappingText([[self currentConfig] caseInsensitiveCompare:@"Debug"] == NSOrderedSame);
    }
    else if (p->IsVs() && p->Vs && p->Vs->Reading)
    {
        title = [@"Visual Studio mapping - " stringByAppendingString:GRNS(p->Name)];
        text = Core::StringUtil::Join("\n", p->Vs->ReportLines) + "\n\n" + p->Vs->Reading->MappingText();
    }
    if (title == nil) return;
    GRTextSheet* sheet = [[GRTextSheet alloc] initWithTitle:title text:GRNS(text) mono:YES];
    [sheet beginOn:self.window completion:^(BOOL ok) { (void)ok; }];
}

- (void)projectOpenFolder
{
    NSString* target = [self selectedFile];
    if (target == nil)
    {
        Model::GrideProjectPtr p = [self contextProject];
        Model::WorkspacePtr w = self.session.workspace;
        if (p) target = GRNS(p->ProjectDir());
        else if (w) target = GRNS(w->Directory());
    }
    if (target == nil) return;
    BOOL dir = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:target isDirectory:&dir]) return;
    if (dir) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:target isDirectory:YES]];
    else [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:target] ]];
}

// Project > Properties, and RIDE's Compiler Options (its own page)
- (void)projectProperties:(NSString*)startPage
{
    Model::GrideProjectPtr p = [self contextProject];
    if (!p) return;
    GRProjectSettingsWindow* sheet = [[GRProjectSettingsWindow alloc] initWithProject:p workspace:self.session.workspace
                                                                               config:[self currentConfig]
                                                                               target:[self currentTarget]
                                                                            startPage:startPage];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        if (sheet.saveError.length > 0) [me.outputView append:[@"Project: " stringByAppendingString:sheet.saveError] source:@"Tools"];
        if (p->IsCcs()) [me rememberCcsState:p];
        [me keepImportedState:p];
        [me refreshProjectViews];
    }];
}
@end
