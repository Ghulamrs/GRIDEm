// =============================================================================
//  FilesNavigator.mm - see FilesNavigator.h
// =============================================================================
#import "FilesNavigator.h"
#import "PaneUtil.h"
#import "../Icons.h"
#include "../../Platform/FileWatch.h"
#include "../../Platform/Paths.h"
#include <memory>

using namespace GRIDEm;

@interface GRFileNode : NSObject
@property (nonatomic, copy) NSString* path;
@property (nonatomic, assign) BOOL isFolder;
@property (nonatomic, strong) NSMutableArray<GRFileNode*>* children;   // nil until read
@end

@implementation GRFileNode
@end

@implementation GRFilesNavigator
{
    NSOutlineView* _tree;
    NSScrollView* _scroll;
    GRFileNode* _rootNode;
    NSString* _root;
    std::unique_ptr<Platform::FileWatch> _watch;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"Files" title:@"Files" symbol:@"doc.on.doc"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 400)];
    // a local for the out-parameter: ARC refuses the address of an instance variable
    // for an __autoreleasing write-back
    NSScrollView* scroll = nil;
    _tree = [GRPaneUtil outlineWithColumns:@[ @"Name" ] widths:@[ @240 ] scroll:&scroll];
    _scroll = scroll;
    _tree.headerView = nil;
    _tree.style = NSTableViewStyleSourceList;
    _tree.dataSource = self;
    _tree.delegate = self;
    _tree.target = self;
    _tree.doubleAction = @selector(doubleClicked:);
    [GRPaneUtil fill:root with:_scroll];
    self.view = root;
}

- (NSString*)root { return _root; }

- (void)setRoot:(NSString*)directory
{
    if (!self.isViewLoaded) [self loadView];
    _watch.reset();
    _root = [directory copy];
    _rootNode = nil;
    if (directory.length > 0 && Platform::Paths::IsDirectory(GRStd(directory)))
    {
        _rootNode = [[GRFileNode alloc] init];
        _rootNode.path = directory;
        _rootNode.isFolder = YES;
        _watch.reset(new Platform::FileWatch());
        __weak GRFilesNavigator* weakSelf = self;
        std::vector<std::string> folders;
        folders.push_back(GRStd(directory));
        _watch->Start(folders, [weakSelf](const std::vector<std::string>& paths) {
            (void)paths;
            [weakSelf diskChanged];
        });
    }
    [_tree reloadData];
    if (_rootNode != nil) [_tree expandItem:_rootNode];
}

- (void)readChildren:(GRFileNode*)n
{
    n.children = [NSMutableArray array];
    std::string dir = GRStd(n.path);
    for (const std::string& name : Platform::Paths::ListDirectories(dir))
    {
        if (!name.empty() && name[0] == '.') continue;
        GRFileNode* c = [[GRFileNode alloc] init];
        c.path = GRNS(Platform::Paths::Combine(dir, name));
        c.isFolder = YES;
        [n.children addObject:c];
    }
    for (const std::string& name : Platform::Paths::ListFiles(dir))
    {
        if (!name.empty() && name[0] == '.') continue;
        GRFileNode* c = [[GRFileNode alloc] init];
        c.path = GRNS(Platform::Paths::Combine(dir, name));
        c.isFolder = NO;
        [n.children addObject:c];
    }
}

// something changed on disk: the folders already read are read again, the open ones stay open
- (void)diskChanged
{
    if (_rootNode == nil) return;
    NSMutableSet<NSString*>* open = [NSMutableSet set];
    [self collectOpen:_rootNode into:open];
    [self forget:_rootNode];
    [_tree reloadData];
    [self reopen:_rootNode from:open];
}

- (void)collectOpen:(GRFileNode*)n into:(NSMutableSet<NSString*>*)open
{
    if ([_tree isItemExpanded:n]) [open addObject:n.path];
    for (GRFileNode* c in n.children) [self collectOpen:c into:open];
}

- (void)forget:(GRFileNode*)n
{
    n.children = nil;
}

- (void)reopen:(GRFileNode*)n from:(NSSet<NSString*>*)open
{
    if (![open containsObject:n.path]) return;
    [_tree expandItem:n];
    for (GRFileNode* c in n.children)
        if (c.isFolder) [self reopen:c from:open];
}

- (NSInteger)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item
{
    (void)outlineView;
    if (item == nil) return _rootNode != nil ? 1 : 0;
    GRFileNode* n = item;
    if (n.children == nil) [self readChildren:n];
    return (NSInteger)n.children.count;
}

- (id)outlineView:(NSOutlineView*)outlineView child:(NSInteger)index ofItem:(id)item
{
    (void)outlineView;
    if (item == nil) return _rootNode;
    GRFileNode* n = item;
    if (n.children == nil) [self readChildren:n];
    return n.children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item
{
    (void)outlineView;
    return ((GRFileNode*)item).isFolder;
}

- (NSView*)outlineView:(NSOutlineView*)outlineView viewForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    (void)tableColumn;
    GRFileNode* n = item;
    NSTableCellView* cell = [GRPaneUtil cellIn:outlineView identifier:@"FileCell"];
    cell.imageView.image = n.isFolder ? [NSImage imageWithSystemSymbolName:@"folder" accessibilityDescription:@"Folder"]
                                      : [GRIcons imageForFile:n.path];
    cell.textField.stringValue = n == _rootNode ? n.path : n.path.lastPathComponent;
    cell.toolTip = n.path;
    return cell;
}

- (void)doubleClicked:(id)sender
{
    (void)sender;
    GRFileNode* n = [_tree itemAtRow:_tree.clickedRow];
    if (n == nil) return;
    if (!n.isFolder) { if (self.fileActivated) self.fileActivated(n.path); return; }
    if ([_tree isItemExpanded:n]) [_tree collapseItem:n];
    else [_tree expandItem:n];
}
@end
