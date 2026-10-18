// =============================================================================
//  MainWindow+Build.mm - Build and Tools menus (GRIDEd's UI/MainForm.Build.cpp,
//  its Build and Tools halves); the build's events, which GRSession has moved
//  onto the main queue
// =============================================================================
#import "MainWindow.h"
#import "Common.h"
#import "Commands.h"
#import "Editor/SourceDocument.h"
#import "Editor/CodeView.h"
#import "Editor/Gutter.h"
#import "Panes/PaneHost.h"
#import "Panes/OutputView.h"
#import "Panes/IssuesView.h"
#import "Panes/TerminalView.h"
#import "Panes/WorkspaceNavigator.h"
#import "Dialogs/DialogKit.h"
#import "Dialogs/BatchBuildSheet.h"
#import "Dialogs/ConfigurationManagerSheet.h"
#import "Dialogs/ConvertSheet.h"
#import "Dialogs/ToolchainCheckSheet.h"
#import "Dialogs/ExternalToolsSheet.h"
#import "Dialogs/SnippetsSheet.h"
#import "Dialogs/SettingsWindow.h"
#include "../Core/StringUtil.h"
#include "../Platform/CommandLine.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include "../Services/MacroExpander.h"

using namespace GRIDEm;

@implementation GRMainWindowController (Build)

// =============================================================================
//  Build
// =============================================================================
- (BOOL)startBuild:(Services::BuildAction)action projects:(const std::vector<Model::GrideProjectPtr>&)projects
        singleFile:(NSString*)singleFile
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w || self.session.isBuilding) return NO;
    if (self.session.settings->Env->SaveBeforeBuild)
    {
        NSError* error = nil;
        [self.editorArea saveAll:&error];
        [self saveWorkspace];
    }
    Services::BuildRequestPtr r = std::make_shared<Services::BuildRequest>();
    r->Projects = projects;
    r->Config = GRStd([self currentConfig]);
    r->TargetKind = [self currentTarget];
    r->Action = action;
    r->SingleFile = GRStd(singleFile);
    r->WorkspaceDir = w->Directory();

    [self.outputView clear:@"Build"];
    [self.outputView showSource:@"Build"];
    [self.issuesView clear];
    [self clearGutterMarks];
    [self showPane:@"Output"];
    [self setStatus:@"Build started..."];
    if ([self.session startBuild:r]) return YES;
    [self setStatus:@"A build is already running"];
    return NO;
}

- (void)buildWorkspace:(Services::BuildAction)action
{
    if (![self ensureProgram]) return;
    [self startBuild:action projects:self.session.workspace->BuildOrder() singleFile:nil];
}

// Build Project builds what it depends on first; Rebuild, Clean, Assemble Only and Link Only act on it alone
- (void)buildProject:(Services::BuildAction)action
{
    if (![self ensureProgram]) return;
    Model::GrideProjectPtr p = [self contextProject];
    if (!p) return;
    if (action == Services::BuildAction::Build)
    {
        [self startBuild:action projects:self.session.workspace->BuildOrderFor(p) singleFile:nil];
        return;
    }
    std::vector<Model::GrideProjectPtr> one(1, p);
    [self startBuild:action projects:one singleFile:nil];
}

- (void)compileFile
{
    if (![self ensureProgram]) return;
    NSString* file = [self selectedFile];
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (file == nil && d != nil) file = d.path;
    if (file == nil) return;
    Model::GrideProjectPtr p = self.session.workspace->FindProjectForFile(GRStd(file));
    if (!p) { GRShowMessage(self.window, @"Compile", @"This file is not part of any project in the workspace."); return; }
    std::vector<Model::GrideProjectPtr> one(1, p);
    [self startBuild:Services::BuildAction::CompileFile projects:one singleFile:file];
}

// runs the checked project | config | target combinations one after another
- (void)batchBuild
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return;
    GRBatchBuildSheet* sheet = [[GRBatchBuildSheet alloc] initWithWorkspace:w];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        if (me.session.settings->Env->SaveBeforeBuild)
        {
            NSError* error = nil;
            [me.editorArea saveAll:&error];
            [me saveWorkspace];
        }
        Services::BuildAction action = sheet.action == GRBatchRebuild ? Services::BuildAction::Rebuild
                                     : sheet.action == GRBatchClean ? Services::BuildAction::Clean
                                     : Services::BuildAction::Build;
        std::vector<Services::BuildRequestPtr> requests;
        for (const GRBatchItem& item : [sheet selected])
        {
            Services::BuildRequestPtr r = std::make_shared<Services::BuildRequest>();
            r->Projects.push_back(item.Project);
            r->Config = item.Config;
            r->TargetKind = item.TargetKind;
            r->Action = action;
            r->WorkspaceDir = w->Directory();
            requests.push_back(r);
        }
        if (requests.empty()) return;
        [me.outputView clear:@"Build"];
        [me.outputView showSource:@"Build"];
        [me.issuesView clear];
        [me clearGutterMarks];
        [me showPane:@"Output"];
        [me setStatus:@"Batch build started..."];
        [me.session startBatch:requests];
    }];
}

- (void)configurationManager
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w) return;
    GRConfigurationManagerSheet* sheet = [[GRConfigurationManagerSheet alloc] initWithWorkspace:w];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        [me setCurrentConfig:GRNS(w->ActiveConfig)];
        [me setCurrentTarget:w->ActiveTarget];
        [me saveWorkspace];
        [me refreshProjectViews];
    }];
}

- (void)cancelBuild
{
    // RIDE's Build > Stop ends a running program the same way, and everything it started
    if (self.session.isRunning) { [self stopProgram]; return; }
    [self.session cancelBuild];
    self.afterBuild = GRAfterNothing;
    [self setStatus:@"Cancelling build..."];
}

// ---- the build's events (main queue) ----------------------------------------------------
- (void)sessionBuildOutput:(NSString*)line { [self.outputView append:line source:@"Build"]; }

- (void)sessionBuildMessage:(const Services::BuildMessagePtr&)message
{
    [self.issuesView add:message];
    [self markGutterFor:message];
}

- (BOOL)sessionAskYesNo:(NSString*)question
{
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"GRIDEm";
    a.informativeText = question;
    [a addButtonWithTitle:@"Yes"];
    [a addButtonWithTitle:@"No"];
    return [a runModal] == NSAlertFirstButtonReturn;
}

- (void)sessionBuildFinished:(const Services::BuildSummaryPtr&)s
{
    if (s->Cancelled) [self setStatus:@"Build cancelled"];
    else if (s->Ok()) [self setStatus:[NSString stringWithFormat:@"Build succeeded  (%d warning(s))", s->Warnings()]];
    else [self setStatus:[NSString stringWithFormat:@"Build failed  (%d error(s), %d warning(s))", s->Errors(), s->Warnings()]];
    if (s->Errors() > 0) [self showPane:@"ErrorList"];
    [self.editorArea refreshGutters];
    // RIDE puts the caret on the first error, opening the file when nothing has
    if (!s->Ok() && !s->Cancelled && self.session.settings->Env->JumpToFirstError)
    {
        Services::BuildMessagePtr first = s->FirstError();
        if (!first)
            for (const Services::BuildMessagePtr& m : [self.issuesView messages])
                if (m->Sev == Services::Severity::Error) { first = m; break; }
        if (first && Platform::Paths::IsFile(first->File)) [self navigateTo:GRNS(first->File) line:first->Line column:first->Column];
    }

    GRAfterBuild next = self.afterBuild;
    self.afterBuild = GRAfterNothing;
    if (!s->Ok()) return;
    if (!self.session.workspace || ![self runProject]) return;      // closed during the build
    if (next == GRAfterDebug)
    {
        Model::GrideProjectPtr p = [self runProject];
        [self launchDebugger:GRNS(Services::BuildEngine::ProgramPath(p, p->GetConfiguration(GRStd([self currentConfig]),
                                                                                             [self currentTarget])))];
    }
    else if (next == GRAfterRun || next == GRAfterEmulate || next == GRAfterVerify) [self runChosen:next];
}

// ---- the editor's marks where the build complained -----------------------------------------
- (void)clearGutterMarks
{
    for (GRSourceDocument* d in self.editorArea.documents)
        for (GRCodeView* v in [self.editorArea codeViewsFor:d]) [v.gutter clearMarks];
}

- (void)markGutterFor:(const Services::BuildMessagePtr&)m
{
    if (!m || m->File.empty() || m->Line <= 0) return;
    if (m->Sev != Services::Severity::Error && m->Sev != Services::Severity::Warning) return;
    GRSourceDocument* d = [self.editorArea documentForPath:GRNS(m->File)];
    if (d == nil) return;
    for (GRCodeView* v in [self.editorArea codeViewsFor:d])
    {
        std::map<int, GRMarkKind> marks = v.gutter.marks;
        GRMarkKind kind = m->Sev == Services::Severity::Error ? GRMarkError : GRMarkWarning;
        std::map<int, GRMarkKind>::iterator it = marks.find(m->Line);
        if (it == marks.end() || it->second < kind) marks[m->Line] = kind;
        v.gutter.marks = marks;
        NSMutableDictionary<NSNumber*, NSString*>* messages = [GROr(v.gutter.markMessages, @{}) mutableCopy];
        messages[@(m->Line)] = GRNS(m->Text);
        v.gutter.markMessages = messages;
        [v.gutter setNeedsDisplay:YES];
    }
}

// =============================================================================
//  Tools
// =============================================================================
- (void)toolsConvert:(Services::ConvertDirection)direction
{
    NSString* input = [self selectedFile];
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (input == nil && d != nil) input = d.path;
    if (d != nil && d.isModified && d.path != nil) { NSError* error = nil; [d saveNow:&error]; }
    Settings::GrideSettingsPtr s = self.session.settings;
    GRConvertSheet* sheet = [[GRConvertSheet alloc] initWithSettings:s->Converter input:input direction:direction];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil || sheet.outputPath.length == 0) return;
        NSString* outFile = sheet.outputPath;
        [me.outputView append:[@"Converted to " stringByAppendingString:outFile] source:@"Tools"];
        Model::WorkspacePtr w = me.session.workspace;
        Model::GrideProjectPtr p = (w && input != nil) ? w->FindProjectForFile(GRStd(input)) : Model::GrideProjectPtr();
        void (^openResult)(void) = ^{ if (s->Converter->OpenResult) [me openDocument:outFile line:0]; };
        if (p && !p->IsReadOnly && !p->ContainsFile(GRStd(outFile)))
        {
            NSString* q = [NSString stringWithFormat:@"Add %@ to project %@?", outFile.lastPathComponent, GRNS(p->Name)];
            GRAskYesNo(me.window, @"Convert", q, ^(BOOL yes) {
                if (yes)
                {
                    std::string folder;
                    p->FolderOfFile(GRStd(input), folder);
                    p->AddFile(GRStd(outFile), folder);
                    try { p->Save(); }
                    catch (const std::exception& ex) { GRShowMessage(me.window, @"Convert", GRNS(ex.what())); }
                    [me refreshProjectViews];
                }
                openResult();
            });
            return;
        }
        openResult();
    }];
}

- (void)toolsToolchainCheck
{
    GRToolchainCheckSheet* sheet = [[GRToolchainCheckSheet alloc] initWithSettings:self.session.settings];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        (void)ok;
        if (sheet.openSettings) [weakSelf showSettings:@"Toolchains"];
    }];
}

// GRIDEd's Toolchain Command Prompt: on the Mac the Terminal pane, in the workspace folder, with the
// tool folders on its PATH
- (void)toolsOpenInTerminal
{
    Model::WorkspacePtr w = self.session.workspace;
    NSString* dir = w ? GRNS(w->Directory()) : NSHomeDirectory();
    BOOL fresh = !self.terminalView.running;
    self.terminalView.workingDirectory = dir;
    [self showPane:@"Terminal"];
    if (fresh) [self.terminalView startShell];
    NSString* extra = [self toolchainPath];
    NSMutableString* line = [NSMutableString stringWithFormat:@"cd '%@'", [dir stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"]];
    if (extra.length > 0)
        [line appendFormat:@" && export PATH='%@':\"$PATH\"", [extra stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"]];
    [line appendString:@" && echo 'GRIDEm toolchain shell - the tool folders are on PATH'\n"];
    [self.terminalView send:line];
}

// folders of every configured tool, for PATH (GRIDEd's ToolchainPath)
- (NSString*)toolchainPath
{
    std::vector<std::string> dirs;
    for (const Settings::ToolchainSettingsPtr& tc : self.session.settings->Toolchains)
    {
        if (!tc) continue;
        std::string d = tc->ToolDir.empty() ? std::string() : Platform::Paths::ExpandHome(Platform::Environment::Expand(tc->ToolDir));
        if (!d.empty() && Platform::Paths::IsDirectory(d)) Core::StringUtil::AddUniqueIgnoreCase(dirs, d);
        const std::string tools[] = { tc->Assembler, tc->Linker };
        for (const std::string& tool : tools)
        {
            std::string full = tc->Resolve(tool);
            if (full.empty()) continue;
            std::string fd = Platform::Paths::DirectoryName(full);
            if (!fd.empty() && Platform::Paths::IsDirectory(fd)) Core::StringUtil::AddUniqueIgnoreCase(dirs, fd);
        }
    }
    return GRNS(Core::StringUtil::Join(":", dirs));
}

- (void)toolsExternalTools
{
    Settings::GrideSettingsPtr s = self.session.settings;
    GRExternalToolsSheet* sheet = [[GRExternalToolsSheet alloc] initWithTools:s->ExternalTools];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        s->ExternalTools = [sheet tools];
        [me.session saveSettings];
    }];
}

// an external tool, its $(FilePath) ... filled in (GRIDEd's RunExternalTool). An application or a document
// is opened as Finder opens it; a program runs with its arguments in its folder
- (void)runExternalTool:(NSInteger)index
{
    Settings::GrideSettingsPtr s = self.session.settings;
    if (index < 0 || index >= (NSInteger)s->ExternalTools.size()) return;
    Settings::ExternalToolPtr t = s->ExternalTools[(size_t)index];
    Services::MacroExpander mx;
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d != nil && d.path != nil)
    {
        std::string f = GRStd(d.path);
        mx.Set("FilePath", f);
        mx.Set("FileDir", Platform::Paths::DirectoryName(f));
        mx.Set("FileName", Platform::Paths::FileName(f));
    }
    Model::GrideProjectPtr p = [self contextProject];
    if (p) { mx.Set("ProjectDir", p->ProjectDir()); mx.Set("ProjectName", p->Name); }
    Model::WorkspacePtr w = self.session.workspace;
    if (w) mx.Set("WorkspaceDir", w->Directory());
    std::string command = Platform::Paths::ExpandHome(mx.Expand(t->Command));
    std::string dir = Platform::Paths::ExpandHome(mx.Expand(t->InitialDir));
    std::vector<std::string> args = Platform::CommandLine::Split(mx.Expand(t->Arguments));
    NSString* cmd = GRNS(command);
    NSError* error = nil;
    BOOL started = NO;
    BOOL isDir = NO;
    BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:cmd isDirectory:&isDir];
    if (exists && (isDir || ![[NSFileManager defaultManager] isExecutableFileAtPath:cmd]))
    {
        // an .app, or a document: opened with its arguments as documents
        NSMutableArray<NSURL*>* urls = [NSMutableArray array];
        for (const std::string& a : args) [urls addObject:[NSURL fileURLWithPath:GRNS(a)]];
        NSWorkspaceOpenConfiguration* config = [NSWorkspaceOpenConfiguration configuration];
        if (urls.count > 0 && isDir)
            [[NSWorkspace sharedWorkspace] openURLs:urls withApplicationAtURL:[NSURL fileURLWithPath:cmd] configuration:config
                                  completionHandler:nil];
        else if (isDir)
            [[NSWorkspace sharedWorkspace] openApplicationAtURL:[NSURL fileURLWithPath:cmd] configuration:config completionHandler:nil];
        else [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:cmd]];
        started = YES;
    }
    else
    {
        NSTask* task = [[NSTask alloc] init];
        std::string program = command;
        if (program.find('/') == std::string::npos)
            for (const std::string& p2 : Platform::Paths::SearchPath())
            {
                std::string full = Platform::Paths::Combine(p2, program);
                if (Platform::Paths::IsExecutable(full)) { program = full; break; }
            }
        task.executableURL = [NSURL fileURLWithPath:GRNS(program)];
        NSMutableArray<NSString*>* a = [NSMutableArray array];
        for (const std::string& s2 : args) [a addObject:GRNS(s2)];
        task.arguments = a;
        if (Platform::Paths::IsDirectory(dir)) task.currentDirectoryURL = [NSURL fileURLWithPath:GRNS(dir) isDirectory:YES];
        started = [task launchAndReturnError:&error];
    }
    if (started) [self.outputView append:[@"Started " stringByAppendingString:GRNS(t->Title)] source:@"Tools"];
    else GRShowMessage(self.window, GRNS(t->Title), GROr(error.localizedDescription, @"It could not be started."));
}

- (void)toolsSnippets
{
    Settings::GrideSettingsPtr s = self.session.settings;
    GRSnippetsSheet* sheet = [[GRSnippetsSheet alloc] initWithSnippets:s->Snippets canInsert:self.editorArea.activeDocument != nil];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        s->Snippets = [sheet snippets];
        [me.session saveSettings];
        GRCodeView* v = me.editorArea.activeCodeView;
        if (sheet.insertText.length > 0 && v != nil) [v insertSnippet:sheet.insertText];
    }];
}

// Settings… (GRIDEd's Tools > Options): a copy is edited; OK saves it and the window applies it
- (void)showSettings:(NSString*)page
{
    __weak GRMainWindowController* weakSelf = self;
    [GRSettingsWindow showWithSettings:self.session.settings startPage:page applied:^(const Settings::GrideSettingsPtr& result) {
        GRMainWindowController* me = weakSelf;
        if (me == nil || !result) return;
        [me.session replaceSettings:result];
        [me applySettings];
        [me setStatus:[@"Settings saved to " stringByAppendingString:GRNS(result->FilePath)]];
    }];
}
@end
