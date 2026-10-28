// =============================================================================
//  MainWindow.h - GRMainWindowController: the GRIDEm main window
//  (GRIDEd's UI/MainForm.h)
//
//  +--------------------------------------------------------------------------+
//  | [nav] Run Debug Stop | NavFilter > Debug > TMS320C6747 | activity | [insp] |  NSToolbar
//  +-----------+----------------------------------------------+---------------+
//  | Navigator |  GREditorArea (tabs, jump bar, editor)       |  Inspector    |
//  | Workspace |                                              |  Properties   |
//  | Class View+-----------------------+----------------------+               |
//  | Files     | Debug: Locals, Watch, | Output, Issues, Find |               |
//  | Breakpts  | Stack, Registers ...  | Results, Terminal    |               |
//  +-----------+-----------------------+----------------------+---------------+
//  | status line: message · issues · language · config · target · Ln, Col     |
//  +--------------------------------------------------------------------------+
//
//  As GRIDEd splits MainForm by menu, the controller is split by menu group:
//    MainWindow.mm            construction, layout, toolbar, status, theme, settings, layout save
//    MainWindow+Commands.mm   every GRIDEd command id bound to its handler, the enabled and
//                             checked tests, the menus filled as they open
//    MainWindow+File.mm       File and Project menus: workspaces, projects, files
//    MainWindow+Edit.mm       Edit, View, Window and Help menus; Find in Files
//    MainWindow+Build.mm      Build menu and Tools menu
//    MainWindow+Debug.mm      Debug menu: run, debug, breakpoints, the debug panes
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#import "Session.h"
#import "Editor/EditorArea.h"
#include "../Core/LanguageTarget.h"
#include "../Model/Workspace.h"
#include "../Services/CodeConverter.h"

@class GRPaneHost, GRSourceDocument;
@class GRWorkspaceNavigator, GRClassNavigator, GRFilesNavigator, GRBreakpointsNavigator, GRInspector;
@class GROutputView, GRIssuesView, GRFindResultsView, GRTerminalView;
@class GRVariablesView, GRStackView, GRRegistersView, GRMemoryView, GRDisassemblyView;

// what a build was started for (GRIDEd's AfterBuild)
typedef NS_ENUM(NSInteger, GRAfterBuild) { GRAfterNothing = 0, GRAfterRun, GRAfterDebug, GRAfterDebugAtEntry, GRAfterEmulate, GRAfterVerify };

// The GRSessionDelegate methods are spread over the categories (the build's in +Build, the run's
// and the debugger's in +Debug); the session is handed the controller as its delegate in -init.
@interface GRMainWindowController : NSWindowController
    <GREditorAreaDelegate, NSToolbarDelegate, NSMenuDelegate, NSWindowDelegate>

- (instancetype)initWithSession:(GRSession*)session;

@property (nonatomic, readonly) GRSession* session;

// ---- the areas -------------------------------------------------------------------
@property (nonatomic, readonly) GREditorArea* editorArea;
@property (nonatomic, readonly) GRPaneHost* navigatorHost;
@property (nonatomic, readonly) GRPaneHost* inspectorHost;
@property (nonatomic, readonly) GRPaneHost* debugHost;
@property (nonatomic, readonly) GRPaneHost* bottomHost;
@property (nonatomic, readonly) NSSplitViewItem* sidebarItem;
@property (nonatomic, readonly) NSSplitViewItem* inspectorItem;
@property (nonatomic, readonly) NSSplitViewItem* bottomItem;      // the debug + output area under the editor
@property (nonatomic, readonly) NSSplitViewItem* debugItem;       // its left half

// ---- the panes ---------------------------------------------------------------------
@property (nonatomic, readonly) GRWorkspaceNavigator* workspaceNavigator;
@property (nonatomic, readonly) GRClassNavigator* classNavigator;
@property (nonatomic, readonly) GRFilesNavigator* filesNavigator;
@property (nonatomic, readonly) GRBreakpointsNavigator* breakpointsNavigator;
@property (nonatomic, readonly) GRInspector* inspector;
@property (nonatomic, readonly) GROutputView* outputView;
@property (nonatomic, readonly) GRIssuesView* issuesView;
@property (nonatomic, readonly) GRFindResultsView* findResultsView;
@property (nonatomic, readonly) GRTerminalView* terminalView;
@property (nonatomic, readonly) GRVariablesView* localsView;
@property (nonatomic, readonly) GRVariablesView* watchView;
@property (nonatomic, readonly) GRStackView* stackView;
@property (nonatomic, readonly) GRRegistersView* registersView;
@property (nonatomic, readonly) GRMemoryView* memoryView;
@property (nonatomic, readonly) GRDisassemblyView* disassemblyView;

// ---- state -------------------------------------------------------------------------
@property (nonatomic, assign) GRAfterBuild afterBuild;
@property (nonatomic, assign) BOOL programLineLast;       // the Debug output's last line is the program's, which its rest may go on
@property (nonatomic, assign) BOOL autoHidden;            // Window > Auto Hide All
@property (nonatomic, assign) BOOL statusBarShown;
@property (nonatomic, assign) BOOL classViewRescanQueued;

// ---- the window as a whole (MainWindow.mm) -------------------------------------------
- (void)setStatus:(NSString*)text;
- (void)updateTitle;
- (void)updateStatus;
- (void)updateScheme;                                     // the toolbar's project > config > target
- (void)updateToolbarForDebugging;                         // the step buttons come and go
// the toolbar's activity: what is happening, with the spinner while something runs
- (void)updateActivity;
// the language a new file takes (the status line's language pop-up)
- (GRIDEm::Core::Language)newFileLanguage;
- (NSString*)currentConfig;
- (GRIDEm::Core::Target)currentTarget;
- (void)setCurrentConfig:(NSString*)config;
- (void)setCurrentTarget:(GRIDEm::Core::Target)target;
// a pane by GRIDEd's identifier ("Output", "Locals" ...): shown, its area opened
- (void)showPane:(NSString*)identifier;
- (void)togglePane:(NSString*)identifier;
- (BOOL)isPaneShown:(NSString*)identifier;
- (GRPaneHost*)hostOfPane:(NSString*)identifier;
- (void)setArea:(NSSplitViewItem*)item shown:(BOOL)shown;
- (void)applyTheme;
- (void)applySettings;                                    // after the Settings window: theme, keys, editor, paths
- (void)applyKeySet;
- (NSString*)keysLine;                                    // the Locals pane's keys line, in the key set in force
- (void)storeLayout;
- (void)restoreLayout;
- (void)resetLayout;
- (void)setStatusBarVisible:(BOOL)visible;
- (void)saveEverything;                                   // settings and layout (quitting)
// before quitting: closes the documents (asking), stops what runs, saves the workspace
- (void)reviewBeforeQuit:(void (^)(BOOL mayQuit))done;
// the toolchain lines at start (GRIDEd's constructor)
- (void)reportToolchain;
@end

// -----------------------------------------------------------------------------
@interface GRMainWindowController (Commands)
- (void)registerCommandHandlers;
// the explorer's selection, else nil
- (NSString*)selectedFile;
- (GRIDEm::Model::GrideProjectPtr)selectedProject;
// tests
- (BOOL)hasWorkspace;
- (BOOL)hasDocument;
- (BOOL)hasProject;
- (BOOL)hasWritableProject;
- (BOOL)hasSelectedFile;
- (BOOL)canBuild;
- (BOOL)canEmulate;
- (BOOL)canBatch;
- (BOOL)isBuildingOrRunning;
- (BOOL)canStartDebug;
// the dynamic menus (recent lists, the Window menu's documents, external tools, the Build titles)
- (void)hookMenus:(NSMenu*)mainMenu;
@end

// -----------------------------------------------------------------------------
@interface GRMainWindowController (File)
- (GRIDEm::Model::GrideProjectPtr)contextProject;         // explorer selection, the document's, the startup project
- (GRIDEm::Model::GrideProjectPtr)runProject;             // the document's project, else the startup project
- (void)openPath:(NSString*)path;
- (GRSourceDocument*)openDocument:(NSString*)path line:(NSInteger)line;
- (void)navigateTo:(NSString*)file line:(NSInteger)line column:(NSInteger)column;
- (void)setWorkspaceTo:(const GRIDEm::Model::WorkspacePtr&)workspace;
- (void)closeWorkspaceThen:(void (^)(BOOL closed))done;
- (void)saveWorkspace;
- (void)refreshProjectViews;
- (void)reportImportedProjects;
- (void)addRecentFile:(NSString*)path;
- (void)addRecentWorkspace:(NSString*)path;
- (void)showStartupSource;
- (BOOL)findEntryPoint:(const GRIDEm::Model::GrideProjectPtr&)project file:(NSString**)file line:(NSInteger*)line;
- (BOOL)activeFileHasMain;
- (BOOL)ensureProgram;
// File menu
- (void)fileNewProject;
- (void)fileNewWorkspace;
- (void)fileNewFile;
- (void)fileOpenWorkspace;
- (void)fileOpenFile;
- (void)fileOpenCcs;
- (void)fileClose;
- (void)fileCloseWorkspace;
- (void)fileSave;
- (void)fileSaveAs;
- (void)fileSaveAll;
- (void)fileImportFolder;
- (void)fileExportZip;
- (void)filePrint;
- (void)fileOpenSelected;
// Project menu
- (void)projectAddNewItem;
- (void)projectAddExisting;
- (void)projectNewFolder;
- (void)projectRemoveItem;
- (void)projectRename;
- (void)projectExclude;
- (BOOL)isSelectedExcluded;
- (void)projectAddProject;
- (void)projectRemoveProject;
- (void)projectSetStartup;
- (void)projectDependencies;
- (void)projectBuildOrder;
- (void)projectRefreshImported;
- (void)projectMapping;
- (BOOL)hasImportedProject;
- (BOOL)ccsEnabled;
- (void)projectOpenFolder;
- (void)projectProperties:(NSString*)startPage;
@end

// -----------------------------------------------------------------------------
@interface GRMainWindowController (Edit)
- (void)editFindInFiles;
- (void)editGoToLine;
- (void)editJumpToDefinition;
- (void)editTabify;
- (void)viewZoom:(CGFloat)delta;
- (void)viewZoomReset;
- (void)viewLineNumbers;
- (void)viewWhitespace;
- (void)viewStatusBar;
- (void)setThemeNamed:(NSString*)name;
- (void)windowFloat;
- (void)windowDock;
- (void)windowAutoHide;
- (void)windowList;
- (void)openHelpFile:(NSString*)baseName;
- (void)helpReleaseNotes;
@end

// -----------------------------------------------------------------------------
@interface GRMainWindowController (Build)
- (BOOL)startBuild:(GRIDEm::Services::BuildAction)action
          projects:(const std::vector<GRIDEm::Model::GrideProjectPtr>&)projects
        singleFile:(NSString*)singleFile;
- (void)buildWorkspace:(GRIDEm::Services::BuildAction)action;
- (void)buildProject:(GRIDEm::Services::BuildAction)action;
- (void)compileFile;
- (void)batchBuild;
- (void)configurationManager;
- (void)cancelBuild;
// Tools
- (void)toolsConvert:(GRIDEm::Services::ConvertDirection)direction;
- (void)toolsToolchainCheck;
- (void)toolsOpenInTerminal;
- (void)toolsExternalTools;
- (void)runExternalTool:(NSInteger)index;
- (void)toolsSnippets;
- (void)showSettings:(NSString*)page;
- (NSString*)toolchainPath;
@end

// -----------------------------------------------------------------------------
@interface GRMainWindowController (Debug)
- (void)debugStart;
- (void)debugStartNoDebug;
- (void)debugEmulate;
- (void)debugVerify;
- (void)stopProgram;
- (void)debugStop;
- (void)debugRestart;
- (void)debugRunToCursor;
- (void)debugToggleBreakpoint;
- (void)debugDeleteAllBreakpoints;
- (void)debugConnect;
- (void)debugLoadProgram;
- (void)lookAtFrame:(NSInteger)index;
- (void)variableSet:(NSString*)name value:(NSString*)value;
- (void)launchDebugger:(NSString*)program;
- (void)launchDebugger:(NSString*)program atEntry:(BOOL)atEntry;
- (void)debugStepInto;
- (void)debugStepOver;
- (void)debugStartAtEntry;
- (void)runChosen:(GRAfterBuild)how;
- (void)refreshDebugPanes;
- (void)clearExecutionMarkers;
- (void)breakpointsChanged;                               // toggled in a gutter, removed, deleted
- (void)refreshBreakpointsPane;
// the window's ends of the debug panes' blocks
- (void)wireDebugPanes;
@end
