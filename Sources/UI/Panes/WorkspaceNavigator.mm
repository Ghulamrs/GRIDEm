// =============================================================================
//  WorkspaceNavigator.mm - see WorkspaceNavigator.h
// =============================================================================
#import "WorkspaceNavigator.h"
#import "../Common.h"
#import "PaneUtil.h"
#import "../Commands.h"
#import "../Icons.h"
#import "../Theme.h"
#include "../../Core/PathUtil.h"
#include "../../Platform/Paths.h"
#include "../../Services/RideToolchain.h"
#include <algorithm>

using namespace GRIDEm;

static NSPasteboardType const GRFilePathPasteboardType = @"org.gride.gridem.projectfile";

// =============================================================================
@implementation GRNavNode
{
    Model::GrideProjectPtr _project;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil) _children = [NSMutableArray array];
    return self;
}

- (Model::GrideProjectPtr)project { return _project; }
- (void)setProject:(const Model::GrideProjectPtr&)project { _project = project; }

- (NSString*)key
{
    NSString* pn = _project ? GRNS(_project->Name) : @"";
    return [NSString stringWithFormat:@"%ld|%@|%@|%@", (long)_kind, pn, GROr(_folder, @""), GROr(_filePath, @"")];
}
@end

// =============================================================================
@interface GRWorkspaceNavigator () <NSTextFieldDelegate, NSMenuDelegate>
@end

@implementation GRWorkspaceNavigator
{
    Model::WorkspacePtr _workspace;
    NSOutlineView* _tree;
    NSScrollView* _scroll;
    GRNavNode* _root;
    BOOL _hadRows;
    GRNavNode* _renaming;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"ProjectExplorer" title:@"Project Explorer" symbol:@"folder"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 400)];
    _tree = [GRPaneUtil outlineWithColumns:@[ @"Name" ] widths:@[ @240 ] scroll:&_scroll];
    _tree.headerView = nil;
    _tree.style = NSTableViewStyleSourceList;
    _tree.dataSource = self;
    _tree.delegate = self;
    _tree.doubleAction = @selector(doubleClicked:);
    _tree.target = self;
    _tree.menu = [[NSMenu alloc] initWithTitle:@"Project Explorer"];
    _tree.menu.delegate = self;
    [_tree registerForDraggedTypes:@[ GRFilePathPasteboardType ]];
    [_tree setDraggingSourceOperationMask:NSDragOperationMove forLocal:YES];
    [GRPaneUtil fill:root with:_scroll];
    self.view = root;
    [self refreshTree];
}

- (void)setWorkspace:(const Model::WorkspacePtr&)workspace
{
    _workspace = workspace;
    _hadRows = NO;
    [self refreshTree];
}

// ---- building the tree (GRIDEd's RefreshTree) -----------------------------------
static GRNavNode* MakeNode(NSString* title, NSImage* icon, GRNodeKind kind, const Model::GrideProjectPtr& p,
                           NSString* path, NSString* folder)
{
    GRNavNode* n = [[GRNavNode alloc] init];
    n.title = title;
    n.icon = icon;
    n.kind = kind;
    [n setProject:p];
    n.filePath = path;
    n.folder = folder;
    return n;
}

static NSImage* ProjectIcon(const Model::GrideProjectPtr& p)
{
    if (p->IsCcs()) return [GRIcons chipImage:16];
    if (p->Lang == Core::Language::Shalimar) return [GRIcons shalimarImage:16];
    NSImage* i = [GRIcons image:(p->IsVs() ? @"projectvs" : @"project") size:13];
    return i != nil ? i : [NSImage imageWithSystemSymbolName:@"shippingbox" accessibilityDescription:@"Project"];
}

static NSImage* FolderIcon()
{
    return [NSImage imageWithSystemSymbolName:@"folder" accessibilityDescription:@"Folder"];
}

- (GRNavNode*)solutionFolder:(NSString*)path under:(GRNavNode*)root made:(NSMutableDictionary<NSString*, GRNavNode*>*)made
{
    NSString* lower = path.lowercaseString;
    GRNavNode* found = made[lower];
    if (found != nil) return found;
    NSRange slash = [path rangeOfString:@"/" options:NSBackwardsSearch];
    GRNavNode* parent = root;
    NSString* leaf = path;
    if (slash.location != NSNotFound && slash.location > 0)
    {
        parent = [self solutionFolder:[path substringToIndex:slash.location] under:root made:made];
        leaf = [path substringFromIndex:slash.location + 1];
    }
    GRNavNode* n = MakeNode([leaf stringByAppendingString:@"   [solution folder]"], FolderIcon(),
                            GRNodeSolutionFolder, nullptr, nil, path);
    [parent.children addObject:n];
    made[lower] = n;
    return n;
}

- (void)refreshTree
{
    if (!self.isViewLoaded) return;
    NSMutableSet<NSString*>* expanded = [NSMutableSet set];
    [self collectExpanded:_root into:expanded];
    NSString* selectedKey = self.selectedNode.key;

    _root = nil;
    if (_workspace)
    {
        Model::Workspace& w = *_workspace;
        size_t count = w.Projects.size();
        NSString* title = [NSString stringWithFormat:@"Workspace '%@' (%lu project%@)", GRNS(w.Name),
                           (unsigned long)count, count == 1 ? @"" : @"s"];
        _root = MakeNode(title, [NSImage imageWithSystemSymbolName:@"square.stack.3d.up" accessibilityDescription:@"Workspace"],
                         GRNodeWorkspace, nullptr, GRNS(w.FilePath), nil);
        NSMutableDictionary<NSString*, GRNavNode*>* made = [NSMutableDictionary dictionary];
        for (const std::string& path : w.SolutionFolders) [self solutionFolder:GRNS(path) under:_root made:made];
        for (const auto& kv : w.SolutionItems)
        {
            GRNavNode* fn = [self solutionFolder:GRNS(kv.first) under:_root made:made];
            for (const std::string& abs : kv.second)
            {
                NSString* text = GRNS(Platform::Paths::FileName(abs));
                BOOL missing = !Platform::Paths::IsFile(abs);
                if (missing) text = [text stringByAppendingString:@"  (missing)"];
                GRNavNode* item = MakeNode(text, [GRIcons imageForFile:GRNS(abs)], GRNodeSolutionItem, nullptr,
                                           GRNS(abs), GRNS(kv.first));
                item.dimmed = missing;
                [fn.children addObject:item];
            }
        }
        for (const Model::GrideProjectPtr& p : w.Projects)
        {
            std::string label = p->Name + "   [" + Core::LanguageInfo::Name(p->Lang) + "]";
            if (p->IsCcs())
                label = p->Name + "   [" + (Core::StringUtil::StartsWith(p->Template, "CCS 5.5") ? "CCS 5.5" : "CCS 7.4") + ", read-only]";
            else if (p->IsVs())
            {
                std::string version = "Visual Studio";
                if (p->Vs && !p->Vs->Version.empty()) version = Core::StringUtil::Split(p->Vs->Version, ',')[0];
                std::string toolset = (p->Vs && !p->Vs->Toolset.empty()) ? " " + p->Vs->Toolset : std::string();
                label = p->Name + "   [" + version + toolset + ", read-only]";
            }
            GRNavNode* pn = MakeNode(GRNS(label), ProjectIcon(p), GRNodeProject, p, GRNS(p->FilePath), nil);
            pn.bold = Core::StringUtil::EqualsIgnoreCase(p->Name, w.StartupName);
            pn.locked = p->IsReadOnly;
            GRNavNode* under = _root;
            if (p->IsVs() && p->Vs && !p->Vs->SolutionFolder.empty())
                under = [self solutionFolder:GRNS(p->Vs->SolutionFolder) under:_root made:made];
            [under.children addObject:pn];
            for (const Model::ProjectFolderPtr& f : p->Folders)
            {
                std::string folderText = f->Name;
                std::string own;
                if (p->FolderToolchains.Find(f->Name, own) && !Core::StringUtil::IsNullOrWhiteSpace(own))
                    folderText = f->Name + "  (" + Services::RideToolchain::Name(Services::RideToolchain::FromWord(own)) + ")";
                GRNavNode* fn = MakeNode(GRNS(folderText), FolderIcon(), GRNodeFolder, p, nil, GRNS(f->Name));
                [pn.children addObject:fn];
                std::vector<std::string> files = f->Files;
                std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
                    return Core::StringUtil::CompareIgnoreCase(a, b) < 0;
                });
                for (const std::string& rel : files)
                {
                    std::string abs = p->ToAbsolute(rel);
                    std::string text = Platform::Paths::FileName(Core::PathUtil::Normalize(rel));
                    BOOL excluded = p->IsExcluded(abs);
                    BOOL missing = !Platform::Paths::IsFile(abs);
                    if (excluded) text += "  (excluded)";
                    if (missing) text += "  (missing)";
                    GRNavNode* file = MakeNode(GRNS(text), [GRIcons imageForFile:GRNS(rel)], GRNodeFile, p, GRNS(abs),
                                               GRNS(f->Name));
                    file.dimmed = excluded || missing;
                    file.locked = p->IsReadOnly;
                    [fn.children addObject:file];
                }
            }
        }
    }
    [_tree reloadData];
    if (_root != nil)
    {
        [_tree expandItem:_root];
        if (!_hadRows)
        {
            // the first time: the projects and the solution folders open
            for (GRNavNode* n in _root.children)
                if (n.kind == GRNodeProject || n.kind == GRNodeSolutionFolder) [_tree expandItem:n];
        }
        else [self restoreExpanded:_root from:expanded];
        _hadRows = YES;
        if (selectedKey != nil) [self selectKey:selectedKey in:_root];
    }
}

- (void)collectExpanded:(GRNavNode*)n into:(NSMutableSet<NSString*>*)keys
{
    if (n == nil) return;
    if ([_tree isItemExpanded:n]) [keys addObject:n.key];
    for (GRNavNode* c in n.children) [self collectExpanded:c into:keys];
}

- (void)restoreExpanded:(GRNavNode*)n from:(NSSet<NSString*>*)keys
{
    if ([keys containsObject:n.key]) [_tree expandItem:n];
    for (GRNavNode* c in n.children) [self restoreExpanded:c from:keys];
}

- (BOOL)selectKey:(NSString*)key in:(GRNavNode*)n
{
    if ([n.key isEqualToString:key])
    {
        NSInteger row = [_tree rowForItem:n];
        if (row >= 0) [_tree selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
        return row >= 0;
    }
    for (GRNavNode* c in n.children)
        if ([self selectKey:key in:c]) return YES;
    return NO;
}

- (GRNavNode*)selectedNode
{
    if (_tree == nil) return nil;
    NSInteger row = _tree.selectedRow;
    if (row < 0) return nil;
    return [_tree itemAtRow:row];
}

- (GRNavNode*)findFile:(NSString*)path in:(GRNavNode*)n
{
    if (n == nil) return nil;
    if ((n.kind == GRNodeFile || n.kind == GRNodeSolutionItem) && n.filePath != nil
        && Core::PathUtil::SamePath(GRStd(n.filePath), GRStd(path)))
        return n;
    for (GRNavNode* c in n.children)
    {
        GRNavNode* f = [self findFile:path in:c];
        if (f != nil) return f;
    }
    return nil;
}

- (void)reveal:(NSString*)path
{
    GRNavNode* n = [self findFile:path in:_root];
    if (n == nil) return;
    // open every row above it
    NSMutableArray* chain = [NSMutableArray array];
    [self chainTo:n from:_root into:chain];
    for (GRNavNode* c in chain) [_tree expandItem:c];
    NSInteger row = [_tree rowForItem:n];
    if (row < 0) return;
    [_tree selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    [_tree scrollRowToVisible:row];
}

- (BOOL)chainTo:(GRNavNode*)target from:(GRNavNode*)n into:(NSMutableArray*)chain
{
    if (n == target) return YES;
    for (GRNavNode* c in n.children)
        if ([self chainTo:target from:c into:chain]) { [chain insertObject:n atIndex:0]; return YES; }
    return NO;
}

// ---- data source ------------------------------------------------------------
- (NSInteger)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item
{
    (void)outlineView;
    if (item == nil) return _root != nil ? 1 : 0;
    return (NSInteger)((GRNavNode*)item).children.count;
}

- (id)outlineView:(NSOutlineView*)outlineView child:(NSInteger)index ofItem:(id)item
{
    (void)outlineView;
    if (item == nil) return _root;
    return ((GRNavNode*)item).children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item
{
    (void)outlineView;
    return ((GRNavNode*)item).children.count > 0;
}

- (NSView*)outlineView:(NSOutlineView*)outlineView viewForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    (void)tableColumn;
    GRNavNode* n = item;
    NSTableCellView* cell = [GRPaneUtil cellIn:outlineView identifier:@"NavCell"];
    cell.imageView.image = n.icon;
    NSString* text = n.title;
    if (n.locked && n.kind == GRNodeProject) text = [text stringByAppendingString:@"  \U0001F512"];
    cell.textField.stringValue = text;
    cell.textField.editable = (n.kind == GRNodeFile && !n.locked);
    cell.textField.delegate = self;
    NSFont* base = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    cell.textField.font = n.bold ? [NSFont boldSystemFontOfSize:base.pointSize] : base;
    cell.textField.textColor = n.dimmed ? [NSColor secondaryLabelColor] : [NSColor labelColor];
    cell.toolTip = n.filePath;
    return cell;
}

- (void)outlineViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    if (self.selectionChanged) self.selectionChanged(self.selectedNode);
}

- (void)doubleClicked:(id)sender
{
    (void)sender;
    GRNavNode* n = [_tree itemAtRow:_tree.clickedRow];
    if (n == nil) return;
    if ((n.kind == GRNodeFile || n.kind == GRNodeSolutionItem) && n.filePath != nil)
    {
        if (self.fileActivated) self.fileActivated(n.filePath, [n project]);
    }
    else if ([_tree isItemExpanded:n]) [_tree collapseItem:n];
    else [_tree expandItem:n];
}

// Return opens a file, Delete removes it (GRIDEd's OnKeyDown)
- (void)keyDown:(NSEvent*)event
{
    GRNavNode* n = self.selectedNode;
    unichar c = event.charactersIgnoringModifiers.length > 0 ? [event.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if (n != nil && (c == NSCarriageReturnCharacter || c == NSEnterCharacter)
        && (n.kind == GRNodeFile || n.kind == GRNodeSolutionItem))
    {
        if (self.fileActivated) self.fileActivated(n.filePath, [n project]);
        return;
    }
    if (n != nil && (c == NSDeleteCharacter || c == NSDeleteFunctionKey) && n.kind == GRNodeFile)
    {
        if (self.removeRequested) self.removeRequested(n);
        return;
    }
    [super keyDown:event];
}

// ---- rename in place -----------------------------------------------------------
- (void)beginRename
{
    GRNavNode* n = self.selectedNode;
    if (n == nil || n.kind != GRNodeFile || n.locked) return;
    NSInteger row = [_tree rowForItem:n];
    if (row < 0) return;
    _renaming = n;
    NSTableCellView* cell = [_tree viewAtColumn:0 row:row makeIfNecessary:YES];
    // edit the bare file name, without "(excluded)" / "(missing)"
    cell.textField.stringValue = n.filePath.lastPathComponent;
    [_tree editColumn:0 row:row withEvent:nil select:YES];
}

- (void)controlTextDidEndEditing:(NSNotification*)note
{
    NSTextField* field = note.object;
    NSInteger row = [_tree rowForView:field];
    GRNavNode* n = row >= 0 ? [_tree itemAtRow:row] : _renaming;
    _renaming = nil;
    if (n == nil || n.kind != GRNodeFile) return;
    NSString* name = [field.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (name.length == 0 || [name isEqualToString:n.filePath.lastPathComponent]
        || [name rangeOfString:@"/"].location != NSNotFound)
    {
        field.stringValue = n.title;
        return;
    }
    if (self.renameRequested) self.renameRequested([n project], n.filePath, name);
}

// ---- drag a file to another folder of its project --------------------------------
- (id<NSPasteboardWriting>)outlineView:(NSOutlineView*)outlineView pasteboardWriterForItem:(id)item
{
    (void)outlineView;
    GRNavNode* n = item;
    if (n.kind != GRNodeFile || n.locked) return nil;
    NSPasteboardItem* pb = [[NSPasteboardItem alloc] init];
    [pb setString:n.key forType:GRFilePathPasteboardType];
    return pb;
}

- (GRNavNode*)nodeForKey:(NSString*)key in:(GRNavNode*)n
{
    if (n == nil) return nil;
    if ([n.key isEqualToString:key]) return n;
    for (GRNavNode* c in n.children)
    {
        GRNavNode* f = [self nodeForKey:key in:c];
        if (f != nil) return f;
    }
    return nil;
}

- (NSDragOperation)outlineView:(NSOutlineView*)outlineView validateDrop:(id<NSDraggingInfo>)info
                  proposedItem:(id)item proposedChildIndex:(NSInteger)index
{
    (void)outlineView; (void)index;
    GRNavNode* target = item;
    NSString* key = [info.draggingPasteboard stringForType:GRFilePathPasteboardType];
    GRNavNode* source = [self nodeForKey:key in:_root];
    if (source == nil || target == nil) return NSDragOperationNone;
    if (target.kind == GRNodeFile) target = [self parentOf:target];
    if (target == nil || target.kind != GRNodeFolder) return NSDragOperationNone;
    if ([source project] != [target project] || [source.folder isEqualToString:target.folder]) return NSDragOperationNone;
    return NSDragOperationMove;
}

- (BOOL)outlineView:(NSOutlineView*)outlineView acceptDrop:(id<NSDraggingInfo>)info item:(id)item childIndex:(NSInteger)index
{
    (void)outlineView; (void)index;
    GRNavNode* target = item;
    if (target.kind == GRNodeFile) target = [self parentOf:target];
    NSString* key = [info.draggingPasteboard stringForType:GRFilePathPasteboardType];
    GRNavNode* source = [self nodeForKey:key in:_root];
    if (source == nil || target == nil || target.kind != GRNodeFolder) return NO;
    if (self.moveRequested) self.moveRequested([source project], source.filePath, target.folder);
    return YES;
}

- (GRNavNode*)parentOf:(GRNavNode*)n
{
    id parent = [_tree parentForItem:n];
    return parent;
}

// ---- the right-click menu (GRIDEd's BuildContextMenus) ----------------------------
- (void)menuNeedsUpdate:(NSMenu*)menu
{
    [menu removeAllItems];
    NSInteger row = _tree.clickedRow;
    if (row >= 0 && row != _tree.selectedRow)
        [_tree selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
    GRNavNode* n = row >= 0 ? [_tree itemAtRow:row] : nil;
    if (n == nil) return;
    NSArray<NSString*>* ids = nil;
    switch (n.kind)
    {
    case GRNodeWorkspace:
    case GRNodeSolutionFolder:
        ids = @[ @"Build.BuildWorkspace", @"Build.RebuildWorkspace", @"Build.CleanWorkspace", @"-", @"File.NewProject",
                 @"Project.AddProject", @"File.ImportCcs", @"-", @"Build.ConfigManager", @"Project.Dependencies",
                 @"Project.BuildOrder", @"-", @"Project.OpenFolder" ];
        break;
    case GRNodeProject:
        ids = @[ @"Build.BuildProject", @"Build.RebuildProject", @"Build.CleanProject", @"-", @"Project.SetStartup", @"-",
                 @"Project.AddNewItem", @"Project.AddExisting", @"Project.NewFolder", @"-", @"Project.RefreshCcs",
                 @"Project.CcsMapping", @"Project.Dependencies", @"Project.RemoveProject", @"-", @"Project.OpenFolder",
                 @"Project.CompilerOptions", @"Project.Properties" ];
        break;
    case GRNodeFolder:
        ids = @[ @"Project.AddNewItem", @"Project.AddExisting", @"-", @"Project.OpenFolder" ];
        break;
    case GRNodeFile:
    case GRNodeSolutionItem:
        ids = @[ @"File.OpenSelected", @"Build.Compile", @"-", @"Project.Exclude", @"Project.Rename", @"Project.Remove", @"-",
                 @"Tools.CToShalimar", @"Tools.ShalimarToC", @"-", @"Project.OpenFolder", @"View.Properties" ];
        break;
    }
    GRCommandRegistry* reg = [GRCommandRegistry shared];
    for (NSString* cid in ids)
    {
        if ([cid isEqualToString:@"-"]) { [menu addItem:[NSMenuItem separatorItem]]; continue; }
        NSMenuItem* item = [reg menuItem:cid];
        item.keyEquivalent = @"";
        [menu addItem:item];
    }
}

- (void)applyTheme
{
    [super applyTheme];
    [_tree reloadData];
}
@end
