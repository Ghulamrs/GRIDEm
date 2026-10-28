// =============================================================================
//  MainWindow+Commands.mm - every GRIDEd command id bound to its handler, the
//  enabled and checked tests, and the menus that are filled as they open
//  (GRIDEd's UI/MainForm.Commands.cpp: RegisterCommands, the CanExecute tests,
//  OnFileMenuOpening, OnWindowMenuOpening, OnToolsMenuOpening, OnBuildMenuOpening)
//
//  The table itself (titles, symbols, the three key sets) is GRCommandRegistry's;
//  what is bound here is what each command does. The commands the Mac's
//  responder chain carries (Undo ... Select All, the Find bar, Page Setup, Quit,
//  Full Screen, the toolbar) need no handler.
// =============================================================================
#import "MainWindow.h"
#import "Common.h"
#import "Commands.h"
#import "Theme.h"
#import "Editor/SourceDocument.h"
#import "Editor/CodeView.h"
#import "Panes/PaneHost.h"
#import "Panes/WorkspaceNavigator.h"
#import "Panes/VariablesView.h"
#import "Dialogs/DialogKit.h"
#import "Dialogs/ShortcutsSheet.h"
#import "Dialogs/AboutPanel.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include "../Services/RideToolchain.h"
#import "Editor/Gutter.h"
#import "Editor/Highlighter.h"
#import "Panes/BreakpointsNavigator.h"
#import "Panes/ClassNavigator.h"
#import "Panes/DisassemblyView.h"
#import "Panes/FilesNavigator.h"
#import "Panes/FindResultsView.h"
#import "Panes/Inspector.h"
#import "Panes/IssuesView.h"
#import "Panes/MemoryView.h"
#import "Panes/OutputView.h"
#import "Panes/RegistersView.h"
#import "Panes/StackView.h"
#import "Panes/TerminalView.h"

using namespace GRIDEm;

static const NSInteger kGRDocListTag = 9200;

@implementation GRMainWindowController (Commands)

// ---- the explorer's selection -------------------------------------------------------
- (NSString*)selectedFile
{
    GRNavNode* n = self.workspaceNavigator.selectedNode;
    if (n == nil || (n.kind != GRNodeFile && n.kind != GRNodeSolutionItem)) return nil;
    return n.filePath;
}

- (Model::GrideProjectPtr)selectedProject
{
    GRNavNode* n = self.workspaceNavigator.selectedNode;
    if (n == nil) return Model::GrideProjectPtr();
    return [n project];
}

// ---- the tests (GRIDEd's CanExecute / IsChecked) -------------------------------------
- (BOOL)hasWorkspace { return self.session.workspace ? YES : NO; }
- (BOOL)hasDocument { return self.editorArea.activeDocument != nil; }
- (BOOL)hasProject { return [self contextProject] ? YES : NO; }
- (BOOL)hasWritableProject { Model::GrideProjectPtr p = [self contextProject]; return p && !p->IsReadOnly; }
- (BOOL)hasSelectedFile { return [self selectedFile] != nil; }

// something to build: a workspace with a project in it, or - none open - the file in the editor with main()
- (BOOL)canBuild
{
    if (self.session.isBuilding || self.session.isDebugging || self.session.isRunning || self.session.verifying) return NO;
    Model::WorkspacePtr w = self.session.workspace;
    if (w) return !w->Projects.empty();
    return [self activeFileHasMain];
}

// Emulate and Verify: vm6747 and sim6747 are for the TMS320C6747 target alone
- (BOOL)canEmulate { return [self canBuild] && Services::RideToolchain::IsEmulated([self currentTarget]); }
- (BOOL)isBuildingOrRunning { return self.session.isBuilding || self.session.isRunning; }
// a one-file program made from the editor's file has no batch
- (BOOL)canBatch
{
    Model::WorkspacePtr w = self.session.workspace;
    if (!w || ![self canBuild]) return NO;
    return !(w->Projects.size() == 1 && w->Projects[0]->IsReadOnly && !Platform::Paths::IsFile(w->Projects[0]->FilePath));
}
- (BOOL)canStartDebug { return self.session.isInBreak || [self canBuild]; }

// =============================================================================
- (void)registerCommandHandlers
{
    GRCommandRegistry* r = GRCommandRegistry.shared;
    __weak GRMainWindowController* weakSelf = self;
#define ME GRMainWindowController* me = weakSelf; if (me == nil) return
#define TEST(expr) ^BOOL { GRMainWindowController* me = weakSelf; return me != nil && (expr); }

    // ------------------------------------------------------------- File
    [r bind:@"File.NewProject" handler:^{ ME; [me fileNewProject]; }];
    [r bind:@"File.NewWorkspace" handler:^{ ME; [me fileNewWorkspace]; }];
    [r bind:@"File.NewFile" handler:^{ ME; [me fileNewFile]; }];
    [r bind:@"File.OpenWorkspace" handler:^{ ME; [me fileOpenWorkspace]; }];
    [r bind:@"File.OpenFile" handler:^{ ME; [me fileOpenFile]; }];
    [r bind:@"File.OpenCcs" handler:^{ ME; [me fileOpenCcs]; } when:TEST([me ccsEnabled])];
    [r bind:@"File.ImportCcs" handler:^{ ME; [me fileOpenCcs]; } when:TEST([me ccsEnabled])];
    [r bind:@"File.ImportFolder" handler:^{ ME; [me fileImportFolder]; }];
    [r bind:@"File.Close" handler:^{ ME; [me fileClose]; } when:TEST([me hasDocument])];
    [r bind:@"File.CloseWorkspace" handler:^{ ME; [me fileCloseWorkspace]; } when:TEST([me hasWorkspace])];
    [r bind:@"File.Save" handler:^{ ME; [me fileSave]; } when:TEST([me hasDocument])];
    [r bind:@"File.SaveAs" handler:^{ ME; [me fileSaveAs]; } when:TEST([me hasDocument])];
    [r bind:@"File.SaveAll" handler:^{ ME; [me fileSaveAll]; }];
    [r bind:@"File.ExportZip" handler:^{ ME; [me fileExportZip]; } when:TEST([me hasProject])];
    [r bind:@"File.Print" handler:^{ ME; [me filePrint]; } when:TEST([me hasDocument])];
    [r bind:@"File.OpenSelected" handler:^{ ME; [me fileOpenSelected]; } when:TEST([me hasSelectedFile])];

    // ------------------------------------------------------------- Edit (the editor's own)
    [r bind:@"Edit.FindInFiles" handler:^{ ME; [me editFindInFiles]; }];
    [r bind:@"Edit.GoToLine" handler:^{ ME; [me editGoToLine]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.JumpToDefinition" handler:^{ ME; [me editJumpToDefinition]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Comment" handler:^{ ME; [me.editorArea.activeCodeView commentSelection:YES]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Uncomment" handler:^{ ME; [me.editorArea.activeCodeView commentSelection:NO]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Format" handler:^{ ME; [me.editorArea.activeCodeView formatDocument]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Upper" handler:^{ ME; [me.editorArea.activeCodeView changeCase:YES]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Lower" handler:^{ ME; [me.editorArea.activeCodeView changeCase:NO]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Tabify" handler:^{ ME; [me editTabify]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Indent" handler:^{ ME; [me.editorArea.activeCodeView indentSelection:YES]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.Outdent" handler:^{ ME; [me.editorArea.activeCodeView indentSelection:NO]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.ToggleBookmark" handler:^{ ME; [me.editorArea.activeCodeView toggleBookmark]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.NextBookmark" handler:^{ ME; [me.editorArea.activeCodeView nextBookmark:YES]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.PrevBookmark" handler:^{ ME; [me.editorArea.activeCodeView nextBookmark:NO]; } when:TEST([me hasDocument])];
    [r bind:@"Edit.ClearBookmarks" handler:^{ ME; [me.editorArea.activeCodeView clearBookmarks]; } when:TEST([me hasDocument])];

    // ------------------------------------------------------------- View
    [r bind:@"View.Navigator" handler:^{ ME; [me setArea:me.sidebarItem shown:me.sidebarItem.collapsed]; }];
    [r setChecked:@"View.Navigator" test:TEST(!me.sidebarItem.collapsed)];
    [r bind:@"View.Inspector" handler:^{ ME; [me setArea:me.inspectorItem shown:me.inspectorItem.collapsed]; }];
    [r setChecked:@"View.Inspector" test:TEST(!me.inspectorItem.collapsed)];
    [r bind:@"View.BottomArea" handler:^{ ME; [me setArea:me.bottomItem shown:me.bottomItem.collapsed]; }];
    [r setChecked:@"View.BottomArea" test:TEST(!me.bottomItem.collapsed)];
    NSDictionary<NSString*, NSString*>* viewPanes = @{
        @"View.ProjectExplorer" : @"ProjectExplorer", @"View.ClassView" : @"ClassView", @"View.Files" : @"Files",
        @"View.Properties" : @"Properties", @"View.Output" : @"Output", @"View.ErrorList" : @"ErrorList",
        @"View.FindResults" : @"FindResults", @"View.Terminal" : @"Terminal"
    };
    for (NSString* commandId in viewPanes)
    {
        NSString* pane = viewPanes[commandId];
        [r bind:commandId handler:^{ ME; [me togglePane:pane]; }];
        [r setChecked:commandId test:TEST([me isPaneShown:pane])];
    }
    [r bind:@"View.StatusBar" handler:^{ ME; [me viewStatusBar]; }];
    [r setChecked:@"View.StatusBar" test:TEST(me.statusBarShown)];
    for (NSString* name in [GRTheme names])
    {
        NSString* commandId = [@"View.Theme" stringByAppendingString:name];
        [r bind:commandId handler:^{ ME; [me setThemeNamed:name]; }];
        [r setChecked:commandId test:TEST(Core::StringUtil::EqualsIgnoreCase(me.session.settings->Env->Theme, GRStd(name)))];
    }
    [r bind:@"View.ZoomIn" handler:^{ ME; [me viewZoom:1]; } when:TEST([me hasDocument])];
    [r bind:@"View.ZoomOut" handler:^{ ME; [me viewZoom:-1]; } when:TEST([me hasDocument])];
    [r bind:@"View.ZoomReset" handler:^{ ME; [me viewZoomReset]; } when:TEST([me hasDocument])];
    [r bind:@"View.LineNumbers" handler:^{ ME; [me viewLineNumbers]; }];
    [r setChecked:@"View.LineNumbers" test:TEST(me.session.settings->Env->LineNumbers)];
    [r bind:@"View.Whitespace" handler:^{ ME; [me viewWhitespace]; }];
    [r setChecked:@"View.Whitespace" test:TEST(me.session.settings->Env->ShowWhitespace)];

    // ------------------------------------------------------------- Project
    [r bind:@"Project.AddNewItem" handler:^{ ME; [me projectAddNewItem]; } when:TEST([me hasWritableProject])];
    [r bind:@"Project.AddExisting" handler:^{ ME; [me projectAddExisting]; } when:TEST([me hasWritableProject])];
    [r bind:@"Project.NewFolder" handler:^{ ME; [me projectNewFolder]; } when:TEST([me hasWritableProject])];
    [r bind:@"Project.Remove" handler:^{ ME; [me projectRemoveItem]; } when:TEST([me hasSelectedFile])];
    [r bind:@"Project.Rename" handler:^{ ME; [me projectRename]; } when:TEST([me hasSelectedFile])];
    [r bind:@"Project.Exclude" handler:^{ ME; [me projectExclude]; } when:TEST([me hasSelectedFile])];
    [r setChecked:@"Project.Exclude" test:TEST([me isSelectedExcluded])];
    [r bind:@"Project.AddProject" handler:^{ ME; [me projectAddProject]; }];
    [r bind:@"Project.RemoveProject" handler:^{ ME; [me projectRemoveProject]; } when:TEST([me hasProject])];
    [r bind:@"Project.SetStartup" handler:^{ ME; [me projectSetStartup]; } when:TEST([me hasProject])];
    [r bind:@"Project.Dependencies" handler:^{ ME; [me projectDependencies]; } when:TEST([me hasWorkspace])];
    [r bind:@"Project.BuildOrder" handler:^{ ME; [me projectBuildOrder]; } when:TEST([me hasWorkspace])];
    [r bind:@"Project.RefreshCcs" handler:^{ ME; [me projectRefreshImported]; } when:TEST([me hasProject])];
    [r bind:@"Project.CcsMapping" handler:^{ ME; [me projectMapping]; } when:TEST([me hasImportedProject])];
    [r bind:@"Project.CompilerOptions" handler:^{ ME; [me projectProperties:@"CompilerOptions"]; } when:TEST([me hasProject])];
    [r bind:@"Project.OpenFolder" handler:^{ ME; [me projectOpenFolder]; } when:TEST([me hasWorkspace])];
    [r bind:@"Project.Properties" handler:^{ ME; [me projectProperties:nil]; } when:TEST([me hasProject])];

    // ------------------------------------------------------------- Build
    [r bind:@"Build.BuildWorkspace" handler:^{ ME; [me buildWorkspace:Services::BuildAction::Build]; } when:TEST([me canBuild])];
    [r bind:@"Build.RebuildWorkspace" handler:^{ ME; [me buildWorkspace:Services::BuildAction::Rebuild]; } when:TEST([me canBuild])];
    [r bind:@"Build.CleanWorkspace" handler:^{ ME; [me buildWorkspace:Services::BuildAction::Clean]; } when:TEST([me canBuild])];
    [r bind:@"Build.BuildProject" handler:^{ ME; [me buildProject:Services::BuildAction::Build]; } when:TEST([me canBuild])];
    [r bind:@"Build.RebuildProject" handler:^{ ME; [me buildProject:Services::BuildAction::Rebuild]; } when:TEST([me canBuild])];
    [r bind:@"Build.CleanProject" handler:^{ ME; [me buildProject:Services::BuildAction::Clean]; } when:TEST([me canBuild])];
    [r bind:@"Build.Compile" handler:^{ ME; [me compileFile]; } when:TEST([me canBuild])];
    [r bind:@"Build.AssembleOnly" handler:^{ ME; [me buildProject:Services::BuildAction::AssembleOnly]; } when:TEST([me canBuild])];
    [r bind:@"Build.LinkOnly" handler:^{ ME; [me buildProject:Services::BuildAction::LinkOnly]; } when:TEST([me canBuild])];
    [r bind:@"Build.Batch" handler:^{ ME; [me batchBuild]; } when:TEST([me canBatch])];
    [r bind:@"Build.ConfigManager" handler:^{ ME; [me configurationManager]; } when:TEST([me hasWorkspace])];
    [r bind:@"Build.Cancel" handler:^{ ME; [me cancelBuild]; } when:TEST([me isBuildingOrRunning])];

    // ------------------------------------------------------------- Debug
    [r bind:@"Debug.Start" handler:^{ ME; [me debugStart]; } when:TEST([me canStartDebug])];
    [r bind:@"Debug.StartNoDebug" handler:^{ ME; [me debugStartNoDebug]; } when:TEST([me canBuild])];
    [r bind:@"Debug.Emulate" handler:^{ ME; [me debugEmulate]; } when:TEST([me canEmulate])];
    [r bind:@"Debug.Verify" handler:^{ ME; [me debugVerify]; } when:TEST([me canEmulate])];
    [r bind:@"Debug.StopProgram" handler:^{ ME; [me stopProgram]; } when:TEST(me.session.isRunning)];
    [r bind:@"Debug.Stop" handler:^{ ME; [me debugStop]; } when:TEST(me.session.isDebugging)];
    [r bind:@"Debug.Restart" handler:^{ ME; [me debugRestart]; } when:TEST(me.session.isDebugging)];
    [r bind:@"Debug.BreakAll" handler:^{ ME; me.session.debugger->BreakAll(); } when:TEST(me.session.isDebugging)];
    [r bind:@"Debug.StepInto" handler:^{ ME; [me debugStepInto]; } when:TEST([me canStartDebug])];
    [r bind:@"Debug.StepOver" handler:^{ ME; [me debugStepOver]; } when:TEST([me canStartDebug])];
    [r bind:@"Debug.StepOut" handler:^{ ME; me.session.debugger->StepOut(); } when:TEST(me.session.isInBreak)];
    [r bind:@"Debug.RunToCursor" handler:^{ ME; [me debugRunToCursor]; } when:TEST(me.session.isInBreak)];
    [r bind:@"Debug.UpStack" handler:^{ ME; [me lookAtFrame:me.session.debugger->CurrentFrame() + 1]; } when:TEST(me.session.isInBreak)];
    [r bind:@"Debug.DownStack" handler:^{ ME; [me lookAtFrame:me.session.debugger->CurrentFrame() - 1]; } when:TEST(me.session.isInBreak)];
    [r bind:@"Debug.AddWatch" handler:^{ ME; [me showPane:@"Watch"]; [me.watchView focusAdd]; } when:TEST(me.session.isDebugging)];
    [r bind:@"Debug.SetVariable" handler:^{ ME; [me showPane:@"Locals"]; [me.localsView focusSet]; } when:TEST(me.session.isInBreak)];
    [r bind:@"Debug.ToggleBreakpoint" handler:^{ ME; [me debugToggleBreakpoint]; } when:TEST([me hasDocument])];
    [r bind:@"Debug.DeleteAllBreakpoints" handler:^{ ME; [me debugDeleteAllBreakpoints]; }];
    NSDictionary<NSString*, NSString*>* debugPanes = @{
        @"Debug.WinWatch" : @"Watch", @"Debug.WinLocals" : @"Locals", @"Debug.WinCallStack" : @"CallStack",
        @"Debug.WinRegisters" : @"Registers", @"Debug.WinMemory" : @"Memory", @"Debug.WinDisassembly" : @"Disassembly",
        @"Debug.WinBreakpoints" : @"Breakpoints"
    };
    for (NSString* commandId in debugPanes)
    {
        NSString* pane = debugPanes[commandId];
        // GRIDEd's PaneToggle(showOnly): the debug windows are brought forward, never hidden by the command
        [r bind:commandId handler:^{
            ME;
            if ([me hostOfPane:pane] == me.debugHost) [me setArea:me.debugItem shown:YES];
            [me showPane:pane];
        }];
        [r setChecked:commandId test:TEST([me isPaneShown:pane])];
    }
    [r bind:@"Debug.Connect" handler:^{ ME; [me debugConnect]; }];
    [r bind:@"Debug.LoadProgram" handler:^{ ME; [me debugLoadProgram]; } when:TEST([me canStartDebug])];

    // ------------------------------------------------------------- Tools
    [r bind:@"Tools.CToShalimar" handler:^{ ME; [me toolsConvert:Services::ConvertDirection::CToShalimar]; }];
    [r bind:@"Tools.ShalimarToC" handler:^{ ME; [me toolsConvert:Services::ConvertDirection::ShalimarToC]; }];
    [r bind:@"Tools.ToolchainCheck" handler:^{ ME; [me toolsToolchainCheck]; }];
    [r bind:@"Tools.CommandPrompt" handler:^{ ME; [me toolsOpenInTerminal]; }];
    [r bind:@"Tools.ExternalTools" handler:^{ ME; [me toolsExternalTools]; }];
    [r bind:@"Tools.Snippets" handler:^{ ME; [me toolsSnippets]; }];
    [r bind:@"Tools.Keyboard" handler:^{ ME; [me showSettings:@"Keyboard"]; }];
    [r bind:@"Tools.Options" handler:^{ ME; [me showSettings:nil]; }];

    // ------------------------------------------------------------- Window
    [r bind:@"Window.Split" handler:^{ ME; [me.editorArea toggleSplit]; } when:TEST([me hasDocument])];
    [r setChecked:@"Window.Split" test:TEST(me.editorArea.isSplit)];
    [r bind:@"Window.Float" handler:^{ ME; [me windowFloat]; }];
    [r bind:@"Window.Dock" handler:^{ ME; [me windowDock]; }];
    [r bind:@"Window.AutoHide" handler:^{ ME; [me windowAutoHide]; }];
    [r setChecked:@"Window.AutoHide" test:TEST(me.autoHidden)];
    [r bind:@"Window.PinTab" handler:^{ ME; [me.editorArea togglePin:me.editorArea.activeDocument]; } when:TEST([me hasDocument])];
    [r bind:@"Window.CloseAll" handler:^{ ME; [me.editorArea closeAll:^(BOOL closed) { (void)closed; }]; } when:TEST([me hasDocument])];
    [r bind:@"Window.CloseAllButThis" handler:^{
        ME;
        [me.editorArea closeAllBut:me.editorArea.activeDocument done:^(BOOL closed) { (void)closed; }];
    } when:TEST([me hasDocument])];
    [r bind:@"Window.ResetLayout" handler:^{ ME; [me resetLayout]; }];
    [r bind:@"Window.SaveLayout" handler:^{
        ME;
        [me storeLayout];
        [me.session saveSettings];
        [me setStatus:@"Window layout saved"];
    }];
    [r bind:@"Window.Windows" handler:^{ ME; [me windowList]; } when:TEST([me hasDocument])];

    // ------------------------------------------------------------- Help
    [r bind:@"Help.ViewHelp" handler:^{ ME; [me openHelpFile:@"GRIDE_Help"]; }];
    NSDictionary<NSString*, NSString*>* references = @{
        @"Help.RefC90" : @"C90_Reference", @"Help.RefCpp11" : @"Cpp11_Reference", @"Help.RefShalimar" : @"Shalimar_Reference",
        @"Help.RefMasm" : @"MASM_LINK_Reference", @"Help.RefAsm6x" : @"ASM6x_LNK6x_Reference",
        @"Help.RefRts" : @"RTS6747_Reference", @"Help.RefVm" : @"VM6747_Reference"
    };
    for (NSString* commandId in references)
    {
        NSString* file = references[commandId];
        [r bind:commandId handler:^{ ME; [me openHelpFile:file]; }];
    }
    [r bind:@"Help.Shortcuts" handler:^{
        ME;
        GRShortcutsSheet* sheet = [[GRShortcutsSheet alloc] init];
        [sheet beginOn:me.window completion:^(BOOL ok) { (void)ok; }];
    }];
    [r bind:@"Help.ReleaseNotes" handler:^{ ME; [me helpReleaseNotes]; }];
    [r bind:@"Help.About" handler:^{ ME; GRShowAboutPanel(GRNS(me.session.settings->FilePath)); }];
#undef ME
#undef TEST
}

// =============================================================================
//  the menus filled as they open
// =============================================================================
static NSMenuItem* FindTagged(NSMenu* menu, NSInteger tag)
{
    for (NSMenuItem* it in menu.itemArray)
    {
        if (it.tag == tag && it.submenu != nil) return it;
        if (it.submenu != nil)
        {
            NSMenuItem* inner = FindTagged(it.submenu, tag);
            if (inner != nil) return inner;
        }
    }
    return nil;
}

- (void)hookMenus:(NSMenu*)mainMenu
{
    for (NSNumber* tag in @[ @(kGRRecentFilesTag), @(kGRRecentWorkspacesTag), @(kGRExternalToolsTag), @(kGRToolbarsMenuTag) ])
    {
        NSMenuItem* it = FindTagged(mainMenu, tag.integerValue);
        if (it != nil) it.submenu.delegate = self;
    }
    if (NSApp.windowsMenu != nil) NSApp.windowsMenu.delegate = self;
    for (NSMenuItem* top in mainMenu.itemArray)
        if ([top.title isEqualToString:@"Build"] || [top.title isEqualToString:@"Project"]) top.submenu.delegate = self;
}

- (NSInteger)tagOfSubmenu:(NSMenu*)menu
{
    NSMenu* parent = menu.supermenu;
    if (parent == nil) return 0;
    for (NSMenuItem* it in parent.itemArray)
        if (it.submenu == menu) return it.tag;
    return 0;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    NSInteger tag = [self tagOfSubmenu:menu];
    Settings::GrideSettingsPtr s = self.session.settings;
    if (tag == kGRRecentFilesTag || tag == kGRRecentWorkspacesTag)
    {
        [menu removeAllItems];
        const std::vector<std::string>& list = tag == kGRRecentFilesTag ? s->Recent->Files : s->Recent->Workspaces;
        for (const std::string& f : list)
        {
            NSMenuItem* it = [menu addItemWithTitle:GRNS(f) action:@selector(openRecent:) keyEquivalent:@""];
            it.target = self;
            it.representedObject = GRNS(f);
            it.tag = tag;
        }
        if (list.empty())
        {
            NSMenuItem* none = [menu addItemWithTitle:@"(none)" action:nil keyEquivalent:@""];
            none.enabled = NO;
        }
        return;
    }
    if (tag == kGRExternalToolsTag)
    {
        [menu removeAllItems];
        for (size_t i = 0; i < s->ExternalTools.size(); i++)
        {
            NSMenuItem* it = [menu addItemWithTitle:GRNS(s->ExternalTools[i]->Title) action:@selector(externalToolChosen:) keyEquivalent:@""];
            it.target = self;
            it.tag = (NSInteger)i;
        }
        if (s->ExternalTools.empty())
        {
            NSMenuItem* none = [menu addItemWithTitle:@"(none - Tools > External Tools...)" action:nil keyEquivalent:@""];
            none.enabled = NO;
        }
        return;
    }
    if (tag == kGRToolbarsMenuTag)
    {
        if (menu.numberOfItems == 0) [menu addItem:[GRCommandRegistry.shared menuItem:@"View.Toolbar"]];
        return;
    }
    if (menu == NSApp.windowsMenu) { [self fillDocumentList:menu]; return; }
    if ([menu.title isEqualToString:@"Build"] || [menu.title isEqualToString:@"Project"]) [self retitleBuildCommands:menu];
}

// "Build NavFilter", "Compile kalman.c" (GRIDEd's OnBuildMenuOpening)
- (void)retitleBuildCommands:(NSMenu*)menu
{
    GRCommandRegistry* r = GRCommandRegistry.shared;
    Model::GrideProjectPtr p = [self contextProject];
    NSString* name = p ? GRNS(p->Name) : @"Project";
    [r command:@"Build.BuildProject"].title = [@"Build " stringByAppendingString:name];
    [r command:@"Build.RebuildProject"].title = [@"Rebuild " stringByAppendingString:name];
    [r command:@"Build.CleanProject"].title = [@"Clean " stringByAppendingString:name];
    NSString* file = [self selectedFile];
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (file == nil && d != nil) file = d.path;
    [r command:@"Build.Compile"].title = file != nil ? [@"Compile " stringByAppendingString:file.lastPathComponent] : @"Compile";
    [r refreshMenu:menu];
}

// the open documents at the end of the Window menu (GRIDEd's OnWindowMenuOpening)
- (void)fillDocumentList:(NSMenu*)menu
{
    for (NSInteger i = menu.numberOfItems - 1; i >= 0; i--)
        if ([menu itemAtIndex:i].tag == kGRDocListTag) [menu removeItemAtIndex:i];
    NSArray<GRSourceDocument*>* docs = self.editorArea.documents;
    if (docs.count == 0) return;
    NSMenuItem* sep = [NSMenuItem separatorItem];
    sep.tag = kGRDocListTag;
    [menu addItem:sep];
    NSInteger n = 0;
    for (GRSourceDocument* d in docs)
    {
        if (++n > 9) break;
        NSMenuItem* it = [menu addItemWithTitle:[NSString stringWithFormat:@"%ld  %@", (long)n, d.displayName]
                                         action:@selector(documentChosen:) keyEquivalent:@""];
        it.target = self;
        it.tag = kGRDocListTag;
        it.representedObject = d;
        it.state = d == self.editorArea.activeDocument ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

- (void)documentChosen:(NSMenuItem*)sender
{
    GRSourceDocument* d = sender.representedObject;
    if ([self.editorArea.documents containsObject:d]) [self.editorArea activateDocument:d];
}

- (void)openRecent:(NSMenuItem*)sender
{
    NSString* f = sender.representedObject;
    Settings::GrideSettingsPtr s = self.session.settings;
    BOOL there = [[NSFileManager defaultManager] fileExistsAtPath:f];
    if (sender.tag == kGRRecentFilesTag)
    {
        if (there) { [self openDocument:f line:0]; return; }
        std::vector<std::string>& list = s->Recent->Files;
        for (size_t i = 0; i < list.size(); i++) if (list[i] == GRStd(f)) { list.erase(list.begin() + (long)i); break; }
    }
    else
    {
        if (there) { [self openPath:f]; return; }
        std::vector<std::string>& list = s->Recent->Workspaces;
        for (size_t i = 0; i < list.size(); i++) if (list[i] == GRStd(f)) { list.erase(list.begin() + (long)i); break; }
    }
    GRShowMessage(self.window, @"GRIDEm", [@"Not found:\n" stringByAppendingString:f]);
}

- (void)externalToolChosen:(NSMenuItem*)sender { [self runExternalTool:sender.tag]; }
@end
