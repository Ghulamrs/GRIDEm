// =============================================================================
//  MainWindow.mm - the main window: construction, layout, toolbar, status line,
//  theme, settings and the window layout kept in gride.settings.json
//  (GRIDEd's UI/MainForm.cpp)
// =============================================================================
#import "MainWindow.h"
#import "Common.h"
#import "Commands.h"
#import "Theme.h"
#import "Icons.h"
#import "Editor/SourceDocument.h"
#import "Editor/CodeView.h"
#import "Panes/PaneHost.h"
#import "Panes/WorkspaceNavigator.h"
#import "Panes/ClassNavigator.h"
#import "Panes/FilesNavigator.h"
#import "Panes/BreakpointsNavigator.h"
#import "Panes/Inspector.h"
#import "Panes/OutputView.h"
#import "Panes/IssuesView.h"
#import "Panes/FindResultsView.h"
#import "Panes/TerminalView.h"
#import "Panes/VariablesView.h"
#import "Panes/StackView.h"
#import "Panes/RegistersView.h"
#import "Panes/MemoryView.h"
#import "Panes/DisassemblyView.h"
#import "Dialogs/DialogKit.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include "../Settings/ToolchainDefinitions.h"
#import "Editor/Gutter.h"
#import "Editor/Highlighter.h"

using namespace GRIDEm;

static NSString* const kTBScheme    = @"GR.Scheme";
static NSString* const kTBActivity  = @"GR.Activity";
static NSString* const kTBStop      = @"GR.Stop";
static NSString* const kTBInspector = @"GR.Inspector";
static NSArray<NSString*>* StepItems()
{
    return @[ @"Debug.BreakAll", @"Debug.StepOver", @"Debug.StepInto", @"Debug.StepOut" ];
}

// -----------------------------------------------------------------------------
//  the window's content: files dropped on it are opened (GRIDEd's DragDrop)
// -----------------------------------------------------------------------------
@interface GRDropView : NSView
@property (nonatomic, copy) void (^dropped)(NSArray<NSString*>* paths);
@end

@implementation GRDropView
- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    return self;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    NSDictionary* options = @{ NSPasteboardURLReadingFileURLsOnlyKey : @YES };
    BOOL files = [sender.draggingPasteboard canReadObjectForClasses:@[ [NSURL class] ] options:options];
    return files ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSDictionary* options = @{ NSPasteboardURLReadingFileURLsOnlyKey : @YES };
    NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[ [NSURL class] ] options:options];
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    for (NSURL* u in urls) if (u.path != nil) [paths addObject:u.path];
    if (paths.count == 0 || self.dropped == nil) return NO;
    self.dropped(paths);
    return YES;
}
@end

// -----------------------------------------------------------------------------
@implementation GRMainWindowController
{
    NSSplitViewController* _rootSplit;
    NSSplitViewController* _centerSplit;
    NSSplitViewController* _bottomSplit;
    NSSplitViewItem* _outputItem;
    NSView* _statusBar;
    NSTextField* _stMessage;
    NSTextField* _stIssues;
    NSPopUpButton* _stLanguage;
    NSTextField* _stConfig;
    NSTextField* _stTarget;
    NSTextField* _stEncoding;
    NSTextField* _stEol;
    NSTextField* _stLine;
    NSLayoutConstraint* _statusHeight;
    NSPopUpButton* _schemeButton;
    NSTextField* _activityText;
    NSProgressIndicator* _activitySpinner;
    NSString* _config;
    Core::Target _target;
    NSTimer* _idleTimer;
    NSMutableDictionary<NSString*, NSDate*>* _diskTimes;
    BOOL _checkingDisk;
    BOOL _stepsShown;
}

- (instancetype)initWithSession:(GRSession*)session
{
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1400, 860)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                        | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                        | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:w];
    if (self == nil) return nil;
    _session = session;
    _session.delegate = (id<GRSessionDelegate>)self;
    __weak GRSession* weakSession = session;
    [GRSourceDocument setWriteGuard:^BOOL(NSString* path) { return [weakSession mayWriteFile:path]; }];
    Model::GrideProject::BeforeWrite = [weakSession](const std::string& path) -> bool {
        GRSession* s = weakSession;
        return s == nil || [s mayWriteFile:[NSString stringWithUTF8String:path.c_str()]];
    };
    _config = @"Debug";
    _target = Core::Target::MacOS;
    _statusBarShown = YES;
    _diskTimes = [NSMutableDictionary dictionary];
    w.title = @"GRIDEm";
    w.minSize = NSMakeSize(800, 520);
    w.delegate = self;
    w.tabbingMode = NSWindowTabbingModeDisallowed;
    w.toolbarStyle = NSWindowToolbarStyleUnified;
    [GRSourceDocument setSettings:session.settings];

    [self createPanes];
    [self createLayout];
    [self createToolbar];
    [self registerCommandHandlers];
    [self wireDebugPanes];
    [self applyKeySet];
    [self applyTheme];
    [self restoreWindowFrame];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(documentStateChanged:)
                                                 name:GRSourceDocumentStateNotification object:nil];
    __weak GRMainWindowController* weakSelf = self;
    _idleTimer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer* timer) {
        (void)timer;
        GRMainWindowController* me = weakSelf;
        if (me == nil) return;
        [me updateTitle];
        [me updateActivity];
    }];
    [self updateScheme];
    [self updateStatus];
    [self updateTitle];
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_idleTimer invalidate];
}

// =============================================================================
//  panes and layout (GRIDEd's CreatePanes / CreateLayout)
// =============================================================================
- (void)createPanes
{
    __weak GRMainWindowController* weakSelf = self;
    _workspaceNavigator = [[GRWorkspaceNavigator alloc] init];
    _workspaceNavigator.fileActivated = ^(NSString* path, Model::GrideProjectPtr project) {
        (void)project;
        [weakSelf openDocument:path line:0];
    };
    _workspaceNavigator.selectionChanged = ^(GRNavNode* node) { [weakSelf explorerSelectionChanged:node]; };
    _workspaceNavigator.renameRequested = ^(Model::GrideProjectPtr project, NSString* oldPath, NSString* newName) {
        [weakSelf renameFile:project from:oldPath to:newName];
    };
    _workspaceNavigator.removeRequested = ^(GRNavNode* node) {
        (void)node;
        [weakSelf projectRemoveItem];
    };
    _workspaceNavigator.moveRequested = ^(Model::GrideProjectPtr project, NSString* path, NSString* toFolder) {
        [weakSelf moveFile:project path:path toFolder:toFolder];
    };
    _classNavigator = [[GRClassNavigator alloc] init];
    _classNavigator.navigate = ^(NSString* file, NSInteger line) { [weakSelf openDocument:file line:line]; };
    _filesNavigator = [[GRFilesNavigator alloc] init];
    _filesNavigator.fileActivated = ^(NSString* path) { [weakSelf openPath:path]; };
    _breakpointsNavigator = [[GRBreakpointsNavigator alloc] init];
    _breakpointsNavigator.navigate = ^(NSString* file, NSInteger line) { [weakSelf openDocument:file line:line]; };
    _breakpointsNavigator.removeRequested = ^(NSString* file, NSInteger line) {
        GRMainWindowController* me = weakSelf;
        [me.session removeBreakpoint:file line:line];
        [me breakpointsChanged];
    };

    _inspector = [[GRInspector alloc] init];
    _inspector.valueChanged = ^(GRPropertyItem* item) {
        (void)item;
        GRMainWindowController* me = weakSelf;
        Model::GrideProjectPtr p = [me contextProject];
        if (p && !p->IsReadOnly) p->IsDirty = true;
        [me saveWorkspace];
    };

    _outputView = [[GROutputView alloc] init];
    _outputView.navigate = ^(NSString* file, NSInteger line, NSInteger column) { [weakSelf navigateTo:file line:line column:column]; };
    _outputView.inputEntered = ^(NSString* line) {
        GRMainWindowController* me = weakSelf;
        if (![me.session writeToProgram:line]) [me setStatus:@"No program is running to read that"];
    };
    _issuesView = [[GRIssuesView alloc] init];
    _issuesView.navigate = ^(NSString* file, NSInteger line, NSInteger column) { [weakSelf navigateTo:file line:line column:column]; };
    _issuesView.countsChanged = ^(NSInteger errors, NSInteger warnings, NSInteger messages) {
        (void)messages;
        [weakSelf showIssueCounts:errors warnings:warnings];
    };
    _findResultsView = [[GRFindResultsView alloc] init];
    _findResultsView.navigate = ^(NSString* file, NSInteger line) { [weakSelf openDocument:file line:line]; };
    _terminalView = [[GRTerminalView alloc] init];

    _localsView = [[GRVariablesView alloc] initWithKind:GRVariablesLocals];
    _watchView = [[GRVariablesView alloc] initWithKind:GRVariablesWatch];
    _stackView = [[GRStackView alloc] init];
    _registersView = [[GRRegistersView alloc] init];
    _memoryView = [[GRMemoryView alloc] init];
    _disassemblyView = [[GRDisassemblyView alloc] init];

    _navigatorHost = [[GRPaneHost alloc] initWithName:@"Navigator" iconsOnly:YES];
    [_navigatorHost addPane:_workspaceNavigator hidden:NO];
    [_navigatorHost addPane:_classNavigator hidden:NO];
    [_navigatorHost addPane:_filesNavigator hidden:NO];
    [_navigatorHost addPane:_breakpointsNavigator hidden:NO];
    [_navigatorHost showPane:@"ProjectExplorer"];

    _inspectorHost = [[GRPaneHost alloc] initWithName:@"Inspector" iconsOnly:NO];
    [_inspectorHost addPane:_inspector hidden:NO];

    _debugHost = [[GRPaneHost alloc] initWithName:@"Debug" iconsOnly:NO];
    [_debugHost addPane:_localsView hidden:NO];
    [_debugHost addPane:_watchView hidden:NO];
    [_debugHost addPane:_stackView hidden:NO];
    [_debugHost addPane:_registersView hidden:NO];
    [_debugHost addPane:_memoryView hidden:NO];
    [_debugHost addPane:_disassemblyView hidden:YES];
    [_debugHost showPane:@"Locals"];

    _bottomHost = [[GRPaneHost alloc] initWithName:@"Bottom" iconsOnly:NO];
    [_bottomHost addPane:_outputView hidden:NO];
    [_bottomHost addPane:_issuesView hidden:NO];
    [_bottomHost addPane:_findResultsView hidden:NO];
    [_bottomHost addPane:_terminalView hidden:NO];
    [_bottomHost showPane:@"Output"];

    for (GRPaneHost* h in @[ _navigatorHost, _inspectorHost, _debugHost, _bottomHost ])
    {
        h.layoutChanged = ^(GRPaneHost* host) { [weakSelf hostLayoutChanged:host]; };
        [h restoreHidden:_session.settings->Layout->HiddenPanes];
    }
    _terminalView.workingDirectory = NSHomeDirectory();
}

- (NSTextField*)statusLabel:(NSString*)text
{
    NSTextField* f = [NSTextField labelWithString:text];
    f.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    f.textColor = [NSColor secondaryLabelColor];
    f.lineBreakMode = NSLineBreakByTruncatingTail;
    return f;
}

- (void)createLayout
{
    _editorArea = [[GREditorArea alloc] initWithFrame:NSMakeRect(0, 0, 900, 560)];
    _editorArea.delegate = self;
    _editorArea.windowController = self;
    NSViewController* editorController = [[NSViewController alloc] init];
    editorController.view = _editorArea;

    _bottomSplit = [[NSSplitViewController alloc] init];
    _bottomSplit.splitView.vertical = YES;
    _bottomSplit.splitView.dividerStyle = NSSplitViewDividerStyleThin;
    _debugItem = [NSSplitViewItem splitViewItemWithViewController:_debugHost];
    _debugItem.canCollapse = YES;
    _debugItem.collapsed = YES;                   // opened by a debugging session
    _outputItem = [NSSplitViewItem splitViewItemWithViewController:_bottomHost];
    _outputItem.canCollapse = YES;
    [_bottomSplit addSplitViewItem:_debugItem];
    [_bottomSplit addSplitViewItem:_outputItem];

    _centerSplit = [[NSSplitViewController alloc] init];
    _centerSplit.splitView.vertical = NO;
    _centerSplit.splitView.dividerStyle = NSSplitViewDividerStyleThin;
    NSSplitViewItem* editorItem = [NSSplitViewItem splitViewItemWithViewController:editorController];
    editorItem.holdingPriority = NSLayoutPriorityDefaultLow - 1;
    _bottomItem = [NSSplitViewItem splitViewItemWithViewController:_bottomSplit];
    _bottomItem.canCollapse = YES;
    _bottomItem.minimumThickness = 90;
    [_centerSplit addSplitViewItem:editorItem];
    [_centerSplit addSplitViewItem:_bottomItem];

    NSViewController* centerHolder = _centerSplit;
    _rootSplit = [[NSSplitViewController alloc] init];
    _rootSplit.splitView.vertical = YES;
    _sidebarItem = [NSSplitViewItem sidebarWithViewController:_navigatorHost];
    _sidebarItem.minimumThickness = 180;
    _sidebarItem.canCollapse = YES;
    NSSplitViewItem* centerItem = [NSSplitViewItem splitViewItemWithViewController:centerHolder];
    _inspectorItem = [NSSplitViewItem inspectorWithViewController:_inspectorHost];
    _inspectorItem.minimumThickness = 220;
    _inspectorItem.canCollapse = YES;
    [_rootSplit addSplitViewItem:_sidebarItem];
    [_rootSplit addSplitViewItem:centerItem];
    [_rootSplit addSplitViewItem:_inspectorItem];

    // the status line under everything
    _stMessage = [self statusLabel:@"Ready"];
    [_stMessage setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [_stMessage setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    _stIssues = [self statusLabel:@""];
    _stLanguage = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _stLanguage.bordered = NO;
    _stLanguage.controlSize = NSControlSizeSmall;
    _stLanguage.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [_stLanguage addItemsWithTitles:@[ @"C90", @"C++11", @"Shalimar" ]];
    _stLanguage.target = self;
    _stLanguage.action = @selector(languageChosen:);
    _stConfig = [self statusLabel:@""];
    _stTarget = [self statusLabel:@""];
    _stEncoding = [self statusLabel:@""];
    _stEol = [self statusLabel:@""];
    _stLine = [self statusLabel:@""];
    NSStackView* bar = [NSStackView stackViewWithViews:@[ _stMessage, _stIssues, _stLanguage, _stConfig, _stTarget,
                                                          _stEncoding, _stEol, _stLine ]];
    bar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    bar.spacing = 14;
    bar.edgeInsets = NSEdgeInsetsMake(0, 10, 0, 10);
    bar.translatesAutoresizingMaskIntoConstraints = NO;
    _statusBar = bar;

    GRDropView* content = [[GRDropView alloc] initWithFrame:NSMakeRect(0, 0, 1400, 860)];
    __weak GRMainWindowController* weakSelf = self;
    content.dropped = ^(NSArray<NSString*>* paths) {
        for (NSString* p in paths) [weakSelf openPath:p];
    };
    NSView* split = _rootSplit.view;
    split.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:split];
    [content addSubview:bar];
    _statusHeight = [bar.heightAnchor constraintEqualToConstant:22];
    [NSLayoutConstraint activateConstraints:@[
        [split.topAnchor constraintEqualToAnchor:content.topAnchor],
        [split.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [split.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [split.bottomAnchor constraintEqualToAnchor:bar.topAnchor],
        [bar.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [bar.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [bar.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        _statusHeight
    ]];
    NSViewController* holder = [[NSViewController alloc] init];
    holder.view = content;
    [holder addChildViewController:_rootSplit];
    self.window.contentViewController = holder;
}

// a host's panes changed: an area with nothing shown in it closes
- (void)hostLayoutChanged:(GRPaneHost*)host
{
    if (_autoHidden) return;
    BOOL any = NO;
    for (id<GRPane> p in host.panes)
        if ([host isShown:p.paneIdentifier] && ![host isFloating:p.paneIdentifier]) { any = YES; break; }
    if (host == _navigatorHost) [self setArea:_sidebarItem shown:any];
    else if (host == _inspectorHost) [self setArea:_inspectorItem shown:any];
    else if (host == _debugHost) [self setArea:_debugItem shown:any && !_debugItem.collapsed];
    else if (host == _bottomHost) [self setArea:_outputItem shown:any];
}

- (void)setArea:(NSSplitViewItem*)item shown:(BOOL)shown
{
    if (item == nil) return;
    if (item.collapsed == !shown) return;
    item.animator.collapsed = !shown;
}

// =============================================================================
//  toolbar
// =============================================================================
- (void)createToolbar
{
    NSToolbar* tb = [[NSToolbar alloc] initWithIdentifier:@"GRIDEm.Main"];
    tb.delegate = self;
    tb.displayMode = NSToolbarDisplayModeIconOnly;
    tb.allowsUserCustomization = YES;
    tb.autosavesConfiguration = NO;

    // the views the toolbar items hold are made first: setting window.toolbar asks the delegate for
    // every item at once, and a nil view in an item crashed the launch
    _schemeButton = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 300, 24) pullsDown:YES];
    _schemeButton.menu.delegate = self;
    _activityText = [NSTextField labelWithString:@"Ready"];
    _activityText.alignment = NSTextAlignmentCenter;
    _activityText.lineBreakMode = NSLineBreakByTruncatingTail;
    _activityText.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    _activitySpinner = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 16, 16)];
    _activitySpinner.style = NSProgressIndicatorStyleSpinning;
    _activitySpinner.controlSize = NSControlSizeSmall;
    _activitySpinner.displayedWhenStopped = NO;

    self.window.toolbar = tb;
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar
{
    (void)toolbar;
    return @[ NSToolbarToggleSidebarItemIdentifier,
              @"Build.BuildWorkspace", @"Debug.StartNoDebug", @"Debug.Start", kTBStop, kTBScheme,
              NSToolbarFlexibleSpaceItemIdentifier, kTBActivity, NSToolbarFlexibleSpaceItemIdentifier, kTBInspector ];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar
{
    NSMutableArray* a = [[self toolbarDefaultItemIdentifiers:toolbar] mutableCopy];
    [a addObjectsFromArray:StepItems()];
    [a addObjectsFromArray:@[ @"Debug.Restart", @"Debug.ToggleBreakpoint", @"File.NewProject", @"File.OpenWorkspace",
                              @"File.Save", @"File.SaveAll", @"Tools.CToShalimar", @"Edit.Comment", @"Edit.Uncomment",
                              NSToolbarSpaceItemIdentifier ]];
    return a;
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
    willBeInsertedIntoToolbar:(BOOL)flag
{
    (void)toolbar; (void)flag;
    if ([identifier isEqualToString:kTBScheme])
    {
        NSToolbarItem* t = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
        t.label = @"Scheme";
        t.paletteLabel = @"Project, Configuration and Target";
        t.view = _schemeButton;
        t.toolTip = @"The startup project, the configuration and the target the next build uses";
        return t;
    }
    if ([identifier isEqualToString:kTBActivity])
    {
        NSToolbarItem* t = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
        t.label = @"Activity";
        t.paletteLabel = @"Activity";
        NSStackView* s = [NSStackView stackViewWithViews:@[ _activitySpinner, _activityText ]];
        s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        s.spacing = 6;
        [s.widthAnchor constraintGreaterThanOrEqualToConstant:260].active = YES;
        [s.widthAnchor constraintLessThanOrEqualToConstant:520].active = YES;
        t.view = s;
        return t;
    }
    if ([identifier isEqualToString:kTBStop])
    {
        NSToolbarItem* t = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
        t.label = @"Stop";
        t.paletteLabel = @"Stop";
        t.toolTip = @"Stop debugging, the running program or the build (⌘.)";
        t.image = [NSImage imageWithSystemSymbolName:@"stop.fill" accessibilityDescription:@"Stop"];
        t.target = self;
        t.action = @selector(stopAnything:);
        t.bordered = YES;
        return t;
    }
    if ([identifier isEqualToString:kTBInspector])
    {
        NSToolbarItem* t = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
        t.label = @"Inspector";
        t.paletteLabel = @"Inspector";
        t.toolTip = @"Show or hide the Inspector";
        t.image = [NSImage imageWithSystemSymbolName:@"sidebar.right" accessibilityDescription:@"Inspector"];
        t.target = self;
        t.action = @selector(toggleInspector:);
        t.bordered = YES;
        return t;
    }
    if ([GRCommandRegistry.shared command:identifier] != nil)
        return [GRCommandRegistry.shared toolbarItem:identifier showText:NO];
    return nil;
}

- (BOOL)validateToolbarItem:(NSToolbarItem*)item
{
    if ([item.itemIdentifier isEqualToString:kTBStop])
        return _session.isDebugging || _session.isRunning || _session.isBuilding;
    return YES;
}

- (void)stopAnything:(id)sender
{
    (void)sender;
    if (_session.isDebugging) [self debugStop];
    else if (_session.isRunning) [self stopProgram];
    else if (_session.isBuilding) [self cancelBuild];
}

- (void)toggleInspector:(id)sender
{
    (void)sender;
    [self setArea:_inspectorItem shown:_inspectorItem.collapsed];
}

// the step buttons are there only while a session runs (GRIDEd's Debug toolbar)
- (void)updateToolbarForDebugging
{
    NSToolbar* tb = self.window.toolbar;
    BOOL want = _session.isDebugging;
    if (want == _stepsShown) return;
    _stepsShown = want;
    if (want)
    {
        NSUInteger at = 0;
        for (NSUInteger i = 0; i < tb.items.count; i++)
            if ([tb.items[i].itemIdentifier isEqualToString:kTBStop]) { at = i + 1; break; }
        for (NSString* ident in StepItems()) [tb insertItemWithItemIdentifier:ident atIndex:at++];
    }
    else
    {
        for (NSInteger i = (NSInteger)tb.items.count - 1; i >= 0; i--)
            if ([StepItems() containsObject:tb.items[(NSUInteger)i].itemIdentifier]) [tb removeItemAtIndex:(NSUInteger)i];
    }
}

// ---- the scheme pop-up: project > configuration > target --------------------------------
- (void)updateScheme
{
    Model::WorkspacePtr w = _session.workspace;
    Model::GrideProjectPtr p = w ? w->StartupProject() : Model::GrideProjectPtr();
    NSString* project = p ? GRNS(p->Name) : (w ? GRNS(w->Name) : @"No Project");
    NSString* title = [NSString stringWithFormat:@"%@  ›  %@  ›  %@", project, _config, GRNS(Core::TargetInfo::Name(_target))];
    NSMenu* m = _schemeButton.menu;
    [m removeAllItems];
    [m addItemWithTitle:title action:nil keyEquivalent:@""];       // the pull-down's own title
    if (w && !w->Projects.empty())
    {
        NSMenuItem* head = [m addItemWithTitle:@"Startup Project" action:nil keyEquivalent:@""];
        head.enabled = NO;
        for (const Model::GrideProjectPtr& q : w->Projects)
        {
            NSMenuItem* it = [m addItemWithTitle:GRNS(q->Name) action:@selector(schemeProject:) keyEquivalent:@""];
            it.target = self;
            it.indentationLevel = 1;
            it.state = (p && q == p) ? NSControlStateValueOn : NSControlStateValueOff;
        }
        [m addItem:[NSMenuItem separatorItem]];
    }
    NSMenuItem* ch = [m addItemWithTitle:@"Configuration" action:nil keyEquivalent:@""];
    ch.enabled = NO;
    for (NSString* c in @[ @"Debug", @"Release" ])
    {
        NSMenuItem* it = [m addItemWithTitle:c action:@selector(schemeConfig:) keyEquivalent:@""];
        it.target = self;
        it.indentationLevel = 1;
        it.state = [c isEqualToString:_config] ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* th = [m addItemWithTitle:@"Target" action:nil keyEquivalent:@""];
    th.enabled = NO;
    for (Core::Target t : Core::TargetInfo::All())
    {
        NSMenuItem* it = [m addItemWithTitle:GRNS(Core::TargetInfo::DisplayName(t)) action:@selector(schemeTarget:) keyEquivalent:@""];
        it.target = self;
        it.tag = (NSInteger)t;
        it.indentationLevel = 1;
        it.state = t == _target ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [_schemeButton setTitle:title];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item
{
    if (item.action == @selector(schemeConfig:) || item.action == @selector(schemeTarget:))
        return !_session.isBuilding;            // GRIDEd: the combos wait while a build runs
    if (item.action == @selector(languageChosen:)) return YES;
    return YES;
}

- (void)schemeProject:(NSMenuItem*)sender
{
    Model::WorkspacePtr w = _session.workspace;
    if (!w) return;
    w->StartupName = GRStd(sender.title);
    w->IsDirty = true;
    [self saveWorkspace];
    [self refreshProjectViews];
    [self updateScheme];
}

- (void)schemeConfig:(NSMenuItem*)sender { [self setCurrentConfig:sender.title]; }
- (void)schemeTarget:(NSMenuItem*)sender { [self setCurrentTarget:(Core::Target)sender.tag]; }

- (NSString*)currentConfig { return _config; }
- (Core::Target)currentTarget { return _target; }

- (void)setCurrentConfig:(NSString*)config
{
    if (config.length == 0) config = @"Debug";
    _config = [config copy];
    Model::WorkspacePtr w = _session.workspace;
    if (w && w->ActiveConfig != GRStd(_config)) { w->ActiveConfig = GRStd(_config); w->IsDirty = true; }
    [self updateScheme];
    [self updateStatus];
}

- (void)setCurrentTarget:(Core::Target)target
{
    _target = target;
    Model::WorkspacePtr w = _session.workspace;
    if (w && w->ActiveTarget != target) { w->ActiveTarget = target; w->IsDirty = true; }
    [self updateScheme];
    [self updateStatus];
}

// ---- the language pop-up of the status line (GRIDEd's Language combo) ----------------------
- (Core::Language)newFileLanguage
{
    NSInteger i = _stLanguage.indexOfSelectedItem;
    if (i < 0 || i > 2) i = 0;
    return (Core::Language)i;
}

- (void)languageChosen:(id)sender
{
    (void)sender;
    GRSourceDocument* d = _editorArea.activeDocument;
    if (d == nil) { [self updateStatus]; return; }
    Core::Language l = [self newFileLanguage];
    if (d.language == l) return;
    d.language = l;
    for (GRCodeView* v in [_editorArea codeViewsFor:d]) [v applyLanguage];
    [self updateStatus];
}

// =============================================================================
//  status, title, activity
// =============================================================================
- (void)setStatus:(NSString*)text
{
    _stMessage.stringValue = GROr(text, @"");
    [self updateActivity];
}

- (void)showIssueCounts:(NSInteger)errors warnings:(NSInteger)warnings
{
    if (errors == 0 && warnings == 0) { _stIssues.stringValue = @""; return; }
    _stIssues.stringValue = [NSString stringWithFormat:@"⛔ %ld   ⚠ %ld", (long)errors, (long)warnings];
}

- (void)updateActivity
{
    BOOL busy = _session.isBuilding || _session.isRunning || _session.verifying
                || (_session.isDebugging && !_session.isInBreak);
    if (busy) [_activitySpinner startAnimation:nil];
    else [_activitySpinner stopAnimation:nil];
    NSString* text = _stMessage.stringValue.length > 0 ? _stMessage.stringValue : @"Ready";
    if (![_activityText.stringValue isEqualToString:text]) _activityText.stringValue = text;
}

- (void)updateTitle
{
    NSString* t = @"GRIDEm";
    Model::WorkspacePtr w = _session.workspace;
    if (w) t = [NSString stringWithFormat:@"%@ - %@", GRNS(w->Name), t];
    GRSourceDocument* d = _editorArea.activeDocument;
    if (d != nil) t = [NSString stringWithFormat:@"%@%@ - %@", d.displayName, d.isModified ? @" *" : @"", t];
    if (_session.isDebugging) t = [t stringByAppendingString:@"  (Debugging)"];
    if (![self.window.title isEqualToString:t]) self.window.title = t;
    NSURL* rep = (d != nil && d.path != nil) ? [NSURL fileURLWithPath:d.path] : nil;
    if (![self.window.representedURL isEqual:rep]) self.window.representedURL = rep;
    self.window.documentEdited = d != nil && d.isModified;
}

- (void)updateStatus
{
    GRSourceDocument* d = _editorArea.activeDocument;
    if (d != nil)
    {
        GRCodeView* v = _editorArea.activeCodeView;
        if (v != nil) _stLine.stringValue = [NSString stringWithFormat:@"Ln %ld, Col %ld", (long)[v currentLine], (long)[v currentColumn]];
        if (Core::LanguageInfo::IsCompiled(d.language)) [_stLanguage selectItemAtIndex:(NSInteger)d.language];
        _stEncoding.stringValue = GROr(d.encodingName, @"");
        _stEol.stringValue = GROr(d.lineEnding, @"");
    }
    else
    {
        _stLine.stringValue = @"";
        _stEncoding.stringValue = @"";
        _stEol.stringValue = @"";
    }
    _stTarget.stringValue = GRNS(Core::TargetInfo::Name(_target));
    _stConfig.stringValue = _config;
}

// =============================================================================
//  panes by identifier
// =============================================================================
- (GRPaneHost*)hostOfPane:(NSString*)identifier
{
    for (GRPaneHost* h in @[ _navigatorHost, _inspectorHost, _debugHost, _bottomHost ])
        if ([h hasPane:identifier]) return h;
    return nil;
}

- (NSSplitViewItem*)areaOfHost:(GRPaneHost*)host
{
    if (host == _navigatorHost) return _sidebarItem;
    if (host == _inspectorHost) return _inspectorItem;
    if (host == _debugHost) return _debugItem;
    return _outputItem;
}

- (void)showPane:(NSString*)identifier
{
    GRPaneHost* h = [self hostOfPane:identifier];
    if (h == nil) return;
    [h showPane:identifier];
    if ([h isFloating:identifier]) return;
    _autoHidden = NO;
    if (h == _debugHost || h == _bottomHost) [self setArea:_bottomItem shown:YES];
    [self setArea:[self areaOfHost:h] shown:YES];
}

- (void)togglePane:(NSString*)identifier
{
    GRPaneHost* h = [self hostOfPane:identifier];
    if (h == nil) return;
    // GRIDEd's PaneToggle: a pane shown and in front goes; otherwise it comes forward
    BOOL inFront = [h isShown:identifier] && [h.activePane.paneIdentifier isEqualToString:identifier]
                   && ![h isFloating:identifier] && ![self areaOfHost:h].collapsed;
    if (inFront) [h hidePane:identifier];
    else [self showPane:identifier];
}

- (BOOL)isPaneShown:(NSString*)identifier
{
    GRPaneHost* h = [self hostOfPane:identifier];
    if (h == nil || ![h isShown:identifier]) return NO;
    if ([h isFloating:identifier]) return YES;
    NSSplitViewItem* area = [self areaOfHost:h];
    if (area.collapsed) return NO;
    if ((h == _debugHost || h == _bottomHost) && _bottomItem.collapsed) return NO;
    return YES;
}

// =============================================================================
//  theme, settings, keys
// =============================================================================
- (void)applyTheme
{
    Settings::GrideSettingsPtr s = _session.settings;
    GRTheme* theme = [GRTheme themeNamed:GRNS(s->Env->Theme)];
    [theme applyOverrides:s->Env->SyntaxColors];
    [GRTheme setCurrent:theme];
    [theme applyToWindow:self.window];
    [_editorArea applySettings];
}

- (void)applySettings
{
    Settings::GrideSettingsPtr s = _session.settings;
    [GRSourceDocument setSettings:s];
    [self applyKeySet];
    [self applyTheme];
    [_editorArea applySettings];
    [self updateStatus];
}

- (void)applyKeySet
{
    Settings::GrideSettingsPtr s = _session.settings;
    NSMutableDictionary<NSString*, NSString*>* bindings = [NSMutableDictionary dictionary];
    for (const std::pair<std::string, std::string>& kv : s->KeyBindings.Items()) bindings[GRNS(kv.first)] = GRNS(kv.second);
    NSString* set = s->KeySet.empty() ? @"macos" : GRNS(s->KeySet);
    [GRCommandRegistry.shared applyKeySet:set bindings:bindings];
    if (NSApp.mainMenu != nil) [GRCommandRegistry.shared refreshMenu:NSApp.mainMenu];
    _localsView.keysLine = [self keysLine];
}

// RIDE's keys line under Locals, in the keys in force (GRIDEd's KeysLine)
- (NSString*)keysLine
{
    NSArray<NSString*>* ids = @[ @"Debug.Start", @"Debug.StepOver", @"Debug.StepInto", @"Debug.ToggleBreakpoint",
                                 @"Debug.UpStack", @"Debug.DownStack" ];
    NSMutableArray<NSString*>* keys = [NSMutableArray array];
    for (NSString* i in ids)
    {
        GRCommand* c = [GRCommandRegistry.shared command:i];
        [keys addObject:[GRCommandRegistry displayText:GROr(c.shortcutText, @"")]];
    }
    NSString* line = [NSString stringWithFormat:@"%@ carries on   %@ steps over   %@ steps into   %@ sets a breakpoint",
                      keys[0], keys[1], keys[2], keys[3]];
    if (keys[4].length > 0)
        line = [line stringByAppendingFormat:@"   %@ looks at what called this   %@ comes back down", keys[4], keys[5]];
    return line;
}

// =============================================================================
//  layout kept in the settings (GRIDEd's RestoreWindowBounds / StoreLayout / ResetLayout)
// =============================================================================
- (void)restoreWindowFrame
{
    Settings::LayoutSettingsPtr l = _session.settings->Layout;
    NSScreen* screen = [NSScreen mainScreen];
    NSRect visible = screen != nil ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
    CGFloat width = MAX(800, l->Width), height = MAX(560, l->Height);
    // Left / Top are measured from the top left of the screen, as GRIDEd measures them
    NSRect r = NSMakeRect(visible.origin.x + l->Left, NSMaxY(visible) - l->Top - height, width, height);
    if (!NSIntersectsRect(NSInsetRect(r, 60, 30), visible))
    {
        r.size.width = MIN(width, visible.size.width);
        r.size.height = MIN(height, visible.size.height);
        r.origin.x = NSMidX(visible) - r.size.width / 2;
        r.origin.y = NSMidY(visible) - r.size.height / 2;
    }
    [self.window setFrame:r display:NO];
    if (l->Maximized) [self.window zoom:nil];
}

- (void)restoreLayout
{
    Settings::LayoutSettingsPtr l = _session.settings->Layout;
    NSSplitView* root = _rootSplit.splitView;
    CGFloat total = root.bounds.size.width;
    if (!_sidebarItem.collapsed) [root setPosition:MAX(180, MIN(l->LeftWidth, total - 400)) ofDividerAtIndex:0];
    if (!_inspectorItem.collapsed) [root setPosition:MAX(400, total - l->RightWidth) ofDividerAtIndex:1];
    NSSplitView* center = _centerSplit.splitView;
    if (!_bottomItem.collapsed) [center setPosition:MAX(150, center.bounds.size.height - l->BottomHeight) ofDividerAtIndex:0];
    self.window.toolbar.visible = !Core::StringUtil::ContainsIgnoreCase(l->HiddenToolbars, "Standard");
    [self setStatusBarVisible:l->StatusBar];
}

- (void)setStatusBarVisible:(BOOL)visible
{
    _statusBarShown = visible;
    _statusBar.hidden = !visible;
    _statusHeight.constant = visible ? 22 : 0;
}

- (void)storeLayout
{
    Settings::LayoutSettingsPtr l = _session.settings->Layout;
    NSWindow* w = self.window;
    l->Maximized = w.isZoomed;
    NSScreen* screen = w.screen != nil ? w.screen : [NSScreen mainScreen];
    NSRect visible = screen != nil ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
    NSRect f = w.frame;
    l->Left = (int)(f.origin.x - visible.origin.x);
    l->Top = (int)(NSMaxY(visible) - NSMaxY(f));
    l->Width = (int)f.size.width;
    l->Height = (int)f.size.height;
    if (!_sidebarItem.collapsed) l->LeftWidth = (int)_navigatorHost.view.frame.size.width;
    if (!_inspectorItem.collapsed) l->RightWidth = (int)_inspectorHost.view.frame.size.width;
    if (!_bottomItem.collapsed) l->BottomHeight = (int)_bottomSplit.view.frame.size.height;
    l->HiddenPanes.clear();
    for (GRPaneHost* h in @[ _navigatorHost, _inspectorHost, _debugHost, _bottomHost ]) [h collectHidden:l->HiddenPanes];
    l->HiddenToolbars.clear();
    if (!w.toolbar.visible) l->HiddenToolbars.push_back("Standard");
    l->StatusBar = _statusBarShown;
}

- (void)resetLayout
{
    Settings::LayoutSettingsPtr l = _session.settings->Layout;
    Settings::LayoutSettings fresh;
    l->LeftWidth = fresh.LeftWidth;
    l->RightWidth = fresh.RightWidth;
    l->BottomHeight = fresh.BottomHeight;
    l->HiddenPanes.clear();
    l->HiddenToolbars = fresh.HiddenToolbars;
    l->StatusBar = true;
    for (GRPaneHost* h in @[ _navigatorHost, _inspectorHost, _debugHost, _bottomHost ])
    {
        [h dockAll];
        for (id<GRPane> p in h.panes)
        {
            if ([p.paneIdentifier isEqualToString:@"Disassembly"]) [h hidePane:p.paneIdentifier];
            else [h showPane:p.paneIdentifier];
        }
    }
    [_navigatorHost showPane:@"ProjectExplorer"];
    [_inspectorHost showPane:@"Properties"];
    [_bottomHost showPane:@"Output"];
    [_debugHost showPane:@"Locals"];
    _autoHidden = NO;
    [self setArea:_sidebarItem shown:YES];
    [self setArea:_inspectorItem shown:YES];
    [self setArea:_bottomItem shown:YES];
    [self setArea:_outputItem shown:YES];
    [self setArea:_debugItem shown:_session.isDebugging];
    [self restoreLayout];
}

- (void)saveEverything
{
    [self storeLayout];
    Settings::GrideSettingsPtr s = _session.settings;
    Model::WorkspacePtr w = _session.workspace;
    s->LastWorkspace = (w && !w->IsTemporary) ? w->FilePath : std::string();
    [_session saveSettings];
}

- (void)reportToolchain
{
    Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Current();
    if (defs)
    {
        std::string line = "toolchain: " + (defs->ShippedFound ? defs->ShippedFile
                                                                : std::string("no ") + Settings::ToolchainDefinitions::FileName + " found");
        if (defs->UserJson) line += " + " + defs->UserFile;
        [_outputView append:GRNS(line) source:@"Tools"];
        for (const std::string& p : defs->Problems) [_outputView append:GRNS("toolchain: " + p) source:@"Tools"];
    }
    if (_session.loadWarning != nil) [_outputView append:_session.loadWarning source:@"Tools"];
    [_outputView append:[@"Settings: " stringByAppendingString:GRNS(_session.settings->FilePath)] source:@"Tools"];
}

// =============================================================================
//  quitting (GRIDEd's OnFormClosing)
// =============================================================================
- (void)reviewBeforeQuit:(void (^)(BOOL mayQuit))done
{
    __weak GRMainWindowController* weakSelf = self;
    void (^closeDocuments)(void) = ^{
        GRMainWindowController* me = weakSelf;
        if (me == nil) { done(YES); return; }
        [me.editorArea closeAll:^(BOOL closed) {
            GRMainWindowController* again = weakSelf;
            if (!closed || again == nil) { done(closed); return; }
            if (again.session.isDebugging) again.session.debugger->Stop();
            if (again.session.isRunning) [again.session stopProgram];
            NSString* failed = [again.session saveWorkspace];    // the .grws only when something changed
            if (failed != nil) NSLog(@"GRIDEm: could not save the workspace: %@", failed);
            [again saveEverything];
            [again.terminalView stopShell];
            [again.session shutDown];
            done(YES);
        }];
    };
    if (_session.isBuilding)
    {
        GRAskYesNo(self.window, @"A build is running", @"Stop it and quit?", ^(BOOL yes) {
            GRMainWindowController* me = weakSelf;
            if (!yes || me == nil) { done(NO); return; }
            [me.session cancelBuild];
            closeDocuments();
        });
        return;
    }
    closeDocuments();
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    // the main window closing is the app quitting, which asks about every document
    [NSApp terminate:self];
    return NO;
}

// =============================================================================
//  files changed outside the IDE (GRIDEd's CheckFilesChangedOnDisk)
// =============================================================================
- (NSDate*)modificationDateOf:(NSString*)path
{
    NSDictionary* a = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    return a[NSFileModificationDate];
}

- (void)rememberDiskTimeOf:(GRSourceDocument*)d
{
    if (d.path == nil) return;
    NSDate* t = [self modificationDateOf:d.path];
    if (t != nil) _diskTimes[d.path] = t;
}

- (void)documentStateChanged:(NSNotification*)note
{
    GRSourceDocument* d = note.object;
    if (![d isKindOfClass:[GRSourceDocument class]]) return;
    if (!d.isModified) [self rememberDiskTimeOf:d];   // just saved (or reloaded)
    [self updateTitle];
    // a document was just saved: Class View reads the files again - once for a Save All
    if (!d.isModified && d.path != nil && !_classViewRescanQueued)
    {
        _classViewRescanQueued = YES;
        __weak GRMainWindowController* weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            GRMainWindowController* me = weakSelf;
            if (me == nil) return;
            me.classViewRescanQueued = NO;
            [me.classNavigator scan:me.session.workspace];
        });
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    (void)notification;
    if (_checkingDisk) return;
    _checkingDisk = YES;
    NSMutableArray<GRSourceDocument*>* changed = [NSMutableArray array];
    for (GRSourceDocument* d in _editorArea.documents)
    {
        if (d.path == nil) continue;
        NSDate* known = _diskTimes[d.path];
        NSDate* now = [self modificationDateOf:d.path];
        if (known == nil || now == nil) { if (now != nil) _diskTimes[d.path] = now; continue; }
        if ([now compare:known] == NSOrderedDescending) [changed addObject:d];
    }
    [self askReload:changed index:0];
}

- (void)askReload:(NSArray<GRSourceDocument*>*)docs index:(NSUInteger)i
{
    if (i >= docs.count) { _checkingDisk = NO; return; }
    GRSourceDocument* d = docs[i];
    NSString* q = [NSString stringWithFormat:@"%@\n\nThis file has been changed outside GRIDEm.", d.path];
    if (d.isModified) q = [q stringByAppendingString:@"\nYou also have unsaved changes - reloading will discard them."];
    q = [q stringByAppendingString:@"\n\nReload it?"];
    __weak GRMainWindowController* weakSelf = self;
    GRAskYesNo(self.window, @"Changed on disk", q, ^(BOOL yes) {
        GRMainWindowController* me = weakSelf;
        if (me == nil) return;
        if (yes)
        {
            NSError* error = nil;
            if (![d reload:&error] && error != nil) [me setStatus:error.localizedDescription];
        }
        [me rememberDiskTimeOf:d];
        [me askReload:docs index:i + 1];
    });
}

// =============================================================================
//  GREditorAreaDelegate
// =============================================================================
- (void)editorArea:(GREditorArea*)area didActivate:(GRSourceDocument*)document
{
    (void)area;
    if (document != nil && document.path != nil)
    {
        Model::WorkspacePtr w = _session.workspace;
        Model::GrideProjectPtr p = w ? w->FindProjectForFile(GRStd(document.path)) : Model::GrideProjectPtr();
        [_inspector setItems:[GRPropertySheet forFile:document.path project:p settings:_session.settings]
                       title:document.displayName];
        [_workspaceNavigator reveal:document.path];      // Track Active Item: the explorer follows the editor
        if (p)
        {
            _editorArea.projectName = GRNS(p->Name);
            _editorArea.projectRoot = GRNS(p->ProjectDir());
            _editorArea.projectLanguage = p->Lang;
        }
    }
    [self updateStatus];
    [self updateTitle];
}

- (void)editorAreaDocumentsChanged:(GREditorArea*)area
{
    (void)area;
    for (GRSourceDocument* d in _editorArea.documents)
        if (d.path != nil && _diskTimes[d.path] == nil) [self rememberDiskTimeOf:d];
    [self updateTitle];
}

- (void)editorArea:(GREditorArea*)area caretAtLine:(NSInteger)line column:(NSInteger)column
{
    (void)area;
    _stLine.stringValue = [NSString stringWithFormat:@"Ln %ld, Col %ld", (long)line, (long)column];
}

- (void)editorArea:(GREditorArea*)area breakpointToggled:(GRSourceDocument*)document line:(NSInteger)line
{
    (void)area; (void)line;
    if (document != nil && document.path == nil)
    {
        [self setStatus:@"Save the file to keep its breakpoints"];
        return;
    }
    [self breakpointsChanged];
}

- (void)editorArea:(GREditorArea*)area jumpToPath:(NSString*)path
{
    (void)area;
    [_workspaceNavigator reveal:path];
    [self showPane:@"ProjectExplorer"];
}

// =============================================================================
//  explorer events (GRIDEd's OnExplorerSelection / OnRenameRequested)
// =============================================================================
- (void)explorerSelectionChanged:(GRNavNode*)node
{
    if (node == nil) return;
    Model::WorkspacePtr w = _session.workspace;
    Settings::GrideSettingsPtr s = _session.settings;
    switch (node.kind)
    {
    case GRNodeWorkspace:
    case GRNodeSolutionFolder:
        if (w) [_inspector setItems:[GRPropertySheet forWorkspace:w] title:GRNS(w->Name)];
        break;
    case GRNodeProject:
        if ([node project]) [_inspector setItems:[GRPropertySheet forProject:[node project]] title:node.title];
        break;
    case GRNodeFile:
        [_inspector setItems:[GRPropertySheet forFile:node.filePath project:[node project] settings:s] title:node.title];
        break;
    case GRNodeSolutionItem:
        [_inspector setItems:[GRPropertySheet forFile:node.filePath project:Model::GrideProjectPtr() settings:s] title:node.title];
        break;
    default:
        if ([node project]) [_inspector setItems:[GRPropertySheet forProject:[node project]] title:node.title];
        break;
    }
}

- (void)renameFile:(Model::GrideProjectPtr)project from:(NSString*)oldPath to:(NSString*)newName
{
    if (!project) return;
    if (newName.length == 0 || [newName rangeOfString:@"/"].location != NSNotFound || [newName rangeOfString:@":"].location != NSNotFound)
    {
        GRShowMessage(self.window, @"Rename", @"The name contains characters that are not allowed.");
        [_workspaceNavigator refreshTree];
        return;
    }
    NSString* newPath = [[oldPath stringByDeletingLastPathComponent] stringByAppendingPathComponent:newName];
    NSError* error = nil;
    if ([[NSFileManager defaultManager] fileExistsAtPath:oldPath]
        && ![[NSFileManager defaultManager] moveItemAtPath:oldPath toPath:newPath error:&error])
    {
        GRShowMessage(self.window, @"Rename", error.localizedDescription);
        [self refreshProjectViews];
        return;
    }
    try
    {
        project->RenameFile(GRStd(oldPath), GRStd(newPath));
        project->Save();
    }
    catch (const std::exception& ex) { GRShowMessage(self.window, @"Rename", GRNS(ex.what())); }
    GRSourceDocument* d = [_editorArea documentForPath:oldPath];
    if (d != nil)
    {
        d.fileURL = [NSURL fileURLWithPath:newPath];
        [d detectLanguage];
        [_editorArea refreshTabs];
    }
    [_session moveBreakpointsFrom:oldPath to:newPath];
    [self refreshProjectViews];
}

- (void)moveFile:(Model::GrideProjectPtr)project path:(NSString*)path toFolder:(NSString*)folder
{
    if (!project || project->IsReadOnly) return;
    std::string abs = GRStd(path);
    project->RemoveFile(abs);
    project->AddFile(abs, GRStd(folder));
    try { project->Save(); }
    catch (const std::exception& ex) { GRShowMessage(self.window, @"Move", GRNS(ex.what())); }
    [self refreshProjectViews];
}
@end
