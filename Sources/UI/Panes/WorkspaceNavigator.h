// =============================================================================
//  WorkspaceNavigator.h - the workspace, its projects, folders and files
//  (GRIDEd's ProjectExplorerPane in UI/Panes/ExplorerPanes.h)
//
//  "Workspace 'X' (3 projects)" at the top; a Visual Studio solution's folders
//  nested as the .sln nests them; each project with its language, or "CCS 7.4,
//  read-only" / "Visual Studio 2022 v143, read-only" with a lock badge; the
//  startup project in bold; a folder that names its own compiler says so; a
//  file "(excluded)" or "(missing)". Expanded rows and the selection survive a
//  refresh. Files can be dragged to another folder of the same writable project.
//  The right-click menus carry GRIDEd's command ids.
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Model/Workspace.h"

typedef NS_ENUM(NSInteger, GRNodeKind) {
    GRNodeWorkspace = 0,
    GRNodeProject,
    GRNodeFolder,
    GRNodeFile,
    GRNodeSolutionFolder,
    GRNodeSolutionItem
};

// one row of the tree (GRIDEd's NodeInfo)
@interface GRNavNode : NSObject
@property (nonatomic, assign) GRNodeKind kind;
@property (nonatomic, copy) NSString* title;
@property (nonatomic, copy) NSString* filePath;      // the file, the .pro, the .grws; nil for a folder
@property (nonatomic, copy) NSString* folder;        // the project folder, or the solution folder path
@property (nonatomic, strong) NSImage* icon;
@property (nonatomic, assign) BOOL bold;
@property (nonatomic, assign) BOOL locked;           // read-only project
@property (nonatomic, assign) BOOL dimmed;           // excluded or missing
@property (nonatomic, strong) NSMutableArray<GRNavNode*>* children;
@property (nonatomic, readonly) NSString* key;       // kind|project|folder|path, to find it again after a refresh
- (GRIDEm::Model::GrideProjectPtr)project;
- (void)setProject:(const GRIDEm::Model::GrideProjectPtr&)project;
@end

@interface GRWorkspaceNavigator : GRPaneController <NSOutlineViewDataSource, NSOutlineViewDelegate>

- (instancetype)init;                                // "ProjectExplorer", "Project Explorer"

// double-click or Return on a file
@property (nonatomic, copy) void (^fileActivated)(NSString* path, GRIDEm::Model::GrideProjectPtr project);
@property (nonatomic, copy) void (^selectionChanged)(GRNavNode* node);
// a file renamed in place: the project, its full path, the new file name
@property (nonatomic, copy) void (^renameRequested)(GRIDEm::Model::GrideProjectPtr project, NSString* oldPath, NSString* newName);
// Delete on a file
@property (nonatomic, copy) void (^removeRequested)(GRNavNode* node);
// a file dragged to another folder of its project
@property (nonatomic, copy) void (^moveRequested)(GRIDEm::Model::GrideProjectPtr project, NSString* path, NSString* toFolder);

- (void)setWorkspace:(const GRIDEm::Model::WorkspacePtr&)workspace;
- (void)refreshTree;
@property (nonatomic, readonly) GRNavNode* selectedNode;
// selects a file's row ("Track Active Item")
- (void)reveal:(NSString*)path;
// starts editing the selected file's name (Project > Rename)
- (void)beginRename;
@end
