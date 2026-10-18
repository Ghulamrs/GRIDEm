// =============================================================================
//  NewItemSheet.mm - see NewItemSheet.h
// =============================================================================
#import "NewItemSheet.h"
#import "DialogKit.h"
#include "../../Platform/Paths.h"
#include "../../Services/ProjectTemplates.h"
#include <vector>

using namespace GRIDEm;

@implementation GRNewItemSheet
{
    Model::GrideProjectPtr _project;
    std::vector<Services::ItemTemplate> _items;
    GRTable* _list;
    NSTextField* _name;
    NSComboBox* _folderBox;
    NSString* _filePath;
}

- (instancetype)initWithProject:(const Model::GrideProjectPtr&)project
{
    NSString* title = [NSString stringWithFormat:@"Add New Item - %@", GRNS(project->Name)];
    if ((self = [super initWithTitle:title size:NSMakeSize(620, 440)]))
    {
        _project = project;
        _items = Services::ProjectTemplates::Items();
        _list = [[GRTable alloc] initWithHeight:220];
        [_list addColumn:@"Template" width:300 kind:GRColumnText choices:nil];
        [_list addColumn:@"Extension" width:120 kind:GRColumnText choices:nil];
        [_list addColumn:@"Folder" width:120 kind:GRColumnText choices:nil];
        _list.table.allowsMultipleSelection = NO;
        for (const Services::ItemTemplate& t : _items)
            [_list addRow:@[GRNS(t.Name), GRNS(t.Extension), GRNS(t.Folder)] tag:nil];
        [_list reload];
        __weak GRNewItemSheet* weakSelf = self;
        _list.selectionChanged = ^{ [weakSelf templateChosen]; };

        _name = [NSTextField textFieldWithString:@""];
        _folderBox = [[NSComboBox alloc] initWithFrame:NSZeroRect];
        for (const Model::ProjectFolderPtr& f : project->Folders) [_folderBox addItemWithObjectValue:GRNS(f->Name)];
        NSGridView* grid = [NSGridView gridViewWithViews:@[
            @[[NSTextField labelWithString:@"Name"], _name],
            @[[NSTextField labelWithString:@"Folder"], _folderBox],
        ]];
        grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
        [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        [grid columnAtIndex:1].xPlacement = NSGridCellPlacementFill;

        NSStackView* all = [NSStackView stackViewWithViews:@[_list.view, grid]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.alignment = NSLayoutAttributeLeading;
        all.spacing = 12;
        all.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [_list.view.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [grid.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [self setContentView:all];
        [self setOkTitle:@"Add"];

        // the first template of the project's language, as GRIDEd chooses it
        NSUInteger first = 0;
        if (project->Lang == Core::Language::Cpp11) first = 2;
        if (project->Lang == Core::Language::Shalimar) first = 4;
        if (first >= _items.size()) first = 0;
        if (!_items.empty())
            [_list.table selectRowIndexes:[NSIndexSet indexSetWithIndex:first] byExtendingSelection:NO];
        [self templateChosen];
        self.window.initialFirstResponder = _name;
    }
    return self;
}

- (NSString*)filePath { return _filePath; }

- (NSString*)folder
{
    return [_folderBox.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

- (const Services::ItemTemplate*)current
{
    NSInteger i = _list.selectedRow;
    if (i < 0 || i >= (NSInteger)_items.size()) return nullptr;
    return &_items[(size_t)i];
}

- (void)templateChosen
{
    const Services::ItemTemplate* t = [self current];
    if (t == nullptr) return;
    NSString* typed = [_name.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSString* stem = typed.length > 0 ? typed.stringByDeletingPathExtension : @"newfile";
    _name.stringValue = [stem stringByAppendingString:GRNS(t->Extension)];
    _folderBox.stringValue = GRNS(t->Folder);
}

- (BOOL)accept
{
    const Services::ItemTemplate* t = [self current];
    if (t == nullptr) { [self showMessage:@"Please select an item template."]; return NO; }
    NSString* name = [_name.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSCharacterSet* bad = [NSCharacterSet characterSetWithCharactersInString:@"/\\:*?\"<>|"];
    if (name.length == 0 || [name rangeOfCharacterFromSet:bad].location != NSNotFound)
    {
        [self showMessage:@"Please enter a valid file name."];
        return NO;
    }
    if (name.pathExtension.length == 0) name = [name stringByAppendingString:GRNS(t->Extension)];
    NSString* folder = self.folder.length > 0 ? self.folder : GRNS(t->Folder);
    std::string dir = _project->ProjectDir();
    if (![folder isEqualToString:@"linker"] && ![folder isEqualToString:@"doc"])
        dir = Platform::Paths::Combine(dir, GRStd(folder));
    std::string path = Platform::Paths::Combine(dir, GRStd(name));
    if (Platform::Paths::Exists(path) && _project->ContainsFile(path))
    {
        [self showMessage:@"This file is already part of the project."];
        return NO;
    }
    _filePath = GRNS(path);
    return YES;
}
@end
