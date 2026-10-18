// =============================================================================
//  FilesNavigator.h - the folder of the workspace as it is on disk
//  (GRIDEd's FilesPane in UI/Panes/ExplorerPanes.h). Folders are read when
//  opened; hidden names (".git") are left out. FSEvents tells it when something
//  changes under the root, and the open folders are read again.
// =============================================================================
#pragma once
#import "PaneHost.h"

@interface GRFilesNavigator : GRPaneController <NSOutlineViewDataSource, NSOutlineViewDelegate>
- (instancetype)init;                                     // "Files", "Files"
@property (nonatomic, copy) void (^fileActivated)(NSString* path);
- (void)setRoot:(NSString*)directory;                     // nil or "" clears it
@property (nonatomic, readonly, copy) NSString* root;
@end
