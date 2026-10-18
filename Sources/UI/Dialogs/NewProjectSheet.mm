// =============================================================================
//  NewProjectSheet.mm - see NewProjectSheet.h
// =============================================================================
#import "NewProjectSheet.h"
#import "DialogKit.h"
#include "../../Platform/Paths.h"
#include "../../Services/ProjectTemplates.h"

using namespace GRIDEm;

// ---- one template in the grid ------------------------------------------------
@interface GRTemplateItem : NSCollectionViewItem
@end

@implementation GRTemplateItem
- (void)loadView
{
    NSView* v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 150, 92)];
    v.wantsLayer = YES;
    v.layer.cornerRadius = 6;
    NSImageView* image = [[NSImageView alloc] initWithFrame:NSZeroRect];
    image.translatesAutoresizingMaskIntoConstraints = NO;
    image.symbolConfiguration = [NSImageSymbolConfiguration configurationWithPointSize:28 weight:NSFontWeightRegular];
    NSTextField* label = [NSTextField wrappingLabelWithString:@""];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.alignment = NSTextAlignmentCenter;
    label.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [v addSubview:image];
    [v addSubview:label];
    [NSLayoutConstraint activateConstraints:@[
        [image.topAnchor constraintEqualToAnchor:v.topAnchor constant:8],
        [image.centerXAnchor constraintEqualToAnchor:v.centerXAnchor],
        [image.heightAnchor constraintEqualToConstant:36],
        [label.topAnchor constraintEqualToAnchor:image.bottomAnchor constant:4],
        [label.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:4],
        [label.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-4],
    ]];
    self.view = v;
    self.imageView = image;
    self.textField = label;
}

- (void)setSelected:(BOOL)selected
{
    [super setSelected:selected];
    self.view.layer.backgroundColor = selected ? [NSColor selectedContentBackgroundColor].CGColor
                                               : [NSColor clearColor].CGColor;
    self.textField.textColor = selected ? [NSColor whiteColor] : [NSColor labelColor];
}
@end

// -----------------------------------------------------------------------------
@interface GRNewProjectSheet () <NSCollectionViewDataSource, NSCollectionViewDelegate>
@end

@implementation GRNewProjectSheet
{
    GRTable* _languages;
    NSCollectionView* _templates;
    NSMutableArray<NSString*>* _templateNames;
    NSTextField* _description;
    NSTextField* _layout;
    NSTextField* _name;
    GRPathField* _location;
    NSPopUpButton* _workspace;
    NSMutableArray<NSButton*>* _targetBoxes;
    NSString* _chosenTemplate;
    Core::Language _language;
    BOOL _importCcs;
}

- (instancetype)initWithLocation:(NSString*)defaultLocation haveWorkspace:(BOOL)haveWorkspace
{
    if ((self = [super initWithTitle:@"New Project" size:NSMakeSize(900, 620)]))
    {
        _templateNames = [NSMutableArray array];
        _targetBoxes = [NSMutableArray array];
        _language = Core::Language::C90;

        // ---- three columns: language | templates | folder layout ----
        _languages = [[GRTable alloc] initWithHeight:280];
        [_languages addColumn:@"Language" width:170 kind:GRColumnText choices:nil];
        _languages.table.allowsMultipleSelection = NO;
        for (NSString* l in @[@"C90", @"C++11", @"Shalimar", @"Import CCS 7.4 / 5.5…"]) [_languages addRow:@[l] tag:nil];
        [_languages reload];
        __weak GRNewProjectSheet* weakSelf = self;
        _languages.selectionChanged = ^{ [weakSelf languageChanged]; };
        [_languages.view.widthAnchor constraintEqualToConstant:180].active = YES;

        NSCollectionViewFlowLayout* flow = [[NSCollectionViewFlowLayout alloc] init];
        flow.itemSize = NSMakeSize(150, 92);
        flow.minimumInteritemSpacing = 8;
        flow.minimumLineSpacing = 8;
        flow.sectionInset = NSEdgeInsetsMake(8, 8, 8, 8);
        _templates = [[NSCollectionView alloc] initWithFrame:NSMakeRect(0, 0, 400, 240)];
        _templates.collectionViewLayout = flow;
        _templates.selectable = YES;
        _templates.allowsMultipleSelection = NO;
        _templates.dataSource = self;
        _templates.delegate = self;
        [_templates registerClass:[GRTemplateItem class] forItemWithIdentifier:@"template"];
        NSScrollView* tscroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 240)];
        tscroll.documentView = _templates;
        tscroll.hasVerticalScroller = YES;
        tscroll.borderType = NSBezelBorder;
        tscroll.translatesAutoresizingMaskIntoConstraints = NO;
        [tscroll.heightAnchor constraintGreaterThanOrEqualToConstant:220].active = YES;
        _description = [NSTextField wrappingLabelWithString:@""];
        _description.textColor = [NSColor secondaryLabelColor];
        _description.preferredMaxLayoutWidth = 380;
        NSStackView* middle = [NSStackView stackViewWithViews:@[tscroll, _description]];
        middle.orientation = NSUserInterfaceLayoutOrientationVertical;
        middle.alignment = NSLayoutAttributeLeading;
        [tscroll.widthAnchor constraintEqualToAnchor:middle.widthAnchor].active = YES;

        _layout = [NSTextField wrappingLabelWithString:@""];
        _layout.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        _layout.preferredMaxLayoutWidth = 240;
        NSBox* layoutBox = [[NSBox alloc] initWithFrame:NSZeroRect];
        layoutBox.title = @"Folder layout created";
        layoutBox.contentView = _layout;
        [layoutBox.widthAnchor constraintEqualToConstant:260].active = YES;

        NSStackView* columns = [NSStackView stackViewWithViews:@[_languages.view, middle, layoutBox]];
        columns.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        columns.alignment = NSLayoutAttributeTop;
        columns.spacing = 12;
        columns.distribution = NSStackViewDistributionFill;
        [middle setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

        // ---- fields ----
        NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
        grid.rowSpacing = 8;
        grid.columnSpacing = 10;
        grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
        [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        [grid columnAtIndex:1].xPlacement = NSGridCellPlacementFill;
        _name = [NSTextField textFieldWithString:@"Project1"];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(nameChanged:)
                                                     name:NSControlTextDidChangeNotification object:_name];
        [grid addRowWithViews:@[[NSTextField labelWithString:@"Project name"], _name]];
        _location = [[GRPathField alloc] initFolder:YES status:NO];
        _location.value = defaultLocation != nil ? defaultLocation : @"";
        [grid addRowWithViews:@[[NSTextField labelWithString:@"Location"], _location]];
        _workspace = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        if (haveWorkspace) [_workspace addItemWithTitle:@"Add to current workspace"];
        [_workspace addItemWithTitle:@"Create new workspace"];
        [grid addRowWithViews:@[[NSTextField labelWithString:@"Workspace"], _workspace]];
        NSStackView* targets = [NSStackView stackViewWithViews:@[]];
        targets.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        targets.spacing = 18;
        for (Core::Target t : Core::TargetInfo::All())
        {
            NSButton* box = [NSButton checkboxWithTitle:GRNS(Core::TargetInfo::Name(t)) target:nil action:nil];
            box.tag = (NSInteger)t;
            box.state = (t == Core::Target::MacOS || t == Core::Target::C6747) ? NSControlStateValueOn : NSControlStateValueOff;
            [_targetBoxes addObject:box];
            [targets addArrangedSubview:box];
        }
        [grid addRowWithViews:@[[NSTextField labelWithString:@"Targets"], targets]];

        NSStackView* all = [NSStackView stackViewWithViews:@[columns, grid]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.alignment = NSLayoutAttributeLeading;
        all.spacing = 14;
        all.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [columns.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [grid.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [self setContentView:all];
        [self setOkTitle:@"Create"];
        [_languages.table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
        [self languageChanged];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

// ---- results ----
- (NSString*)projectName
{
    return [_name.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}
- (NSString*)projectLocation { return _location.value; }
- (Core::Language)language { return _language; }
- (NSString*)templateName { return _chosenTemplate; }
- (BOOL)newWorkspace { return [_workspace.titleOfSelectedItem hasPrefix:@"Create"]; }
- (BOOL)importCcs { return _importCcs; }

- (std::vector<Core::Target>)targets
{
    std::vector<Core::Target> r;
    for (NSButton* b in _targetBoxes)
        if (b.state == NSControlStateValueOn) r.push_back((Core::Target)b.tag);
    return r;
}

// ---- behaviour (GRIDEd's OnLanguageChanged / OnTemplateChanged) ----
- (void)languageChanged
{
    NSInteger i = _languages.selectedRow;
    if (i < 0) i = 0;
    _importCcs = i == 3;
    [_templateNames removeAllObjects];
    if (_importCcs)
    {
        [_templateNames addObject:@"Open CCS 7.4 / 5.5 project (read-only)"];
        [self setOkTitle:@"Choose…"];
    }
    else
    {
        _language = i == 1 ? Core::Language::Cpp11 : (i == 2 ? Core::Language::Shalimar : Core::Language::C90);
        for (const std::string& t : Services::ProjectTemplates::For(_language)) [_templateNames addObject:GRNS(t)];
        [self setOkTitle:@"Create"];
    }
    [_templates reloadData];
    if (_templateNames.count > 0)
    {
        NSSet<NSIndexPath*>* first = [NSSet setWithObject:[NSIndexPath indexPathForItem:0 inSection:0]];
        [_templates selectItemsAtIndexPaths:first scrollPosition:NSCollectionViewScrollPositionTop];
        _chosenTemplate = _templateNames[0];
    }
    else _chosenTemplate = nil;
    [self templateChanged];
}

- (void)nameChanged:(NSNotification*)note { (void)note; [self templateChanged]; }

- (void)templateChanged
{
    NSString* t = _chosenTemplate;
    if (t == nil) return;
    if (_importCcs)
    {
        _description.stringValue = @"Reads .project / .cproject / .ccsproject. Files are shown and can be edited; "
                                    "the CCS project files are never changed.";
        _layout.stringValue = @"<ccs project>/\n+-- .project\n+-- .cproject\n+-- .ccsproject\n`-- *.c *.h *.cmd";
        return;
    }
    _description.stringValue = GRNS(Services::ProjectTemplates::Describe(GRStd(t)));
    NSString* n = self.projectName;
    if (n.length == 0) n = @"Project";
    NSString* ext = @".c";
    if (_language == Core::Language::Cpp11) ext = @".cpp";
    if (_language == Core::Language::Shalimar) ext = @".shl";
    NSMutableString* sb = [NSMutableString string];
    [sb appendFormat:@"%@/\n", n];
    [sb appendFormat:@"+-- %@.pro\n", n];
    [sb appendString:@"+-- src/\n"];
    if (![t isEqualToString:@"Empty Project"] && ![t isEqualToString:@"From C Source"])
        [sb appendFormat:@"|   `-- main%@\n", ext];
    [sb appendString:@"+-- inc/\n+-- lib/\n"];
    if ([t containsString:@"DSP"]) [sb appendString:@"+-- c6747.cmd\n"];
    [sb appendString:@"`-- out/<cfg>/<target>/\n\n"];
    [sb appendString:@"Paths inside the .pro are\nrelative to the .pro folder."];
    _layout.stringValue = sb;
}

- (BOOL)accept
{
    if (_importCcs) return YES;
    NSString* name = self.projectName;
    NSCharacterSet* bad = [NSCharacterSet characterSetWithCharactersInString:@"/\\:*?\"<>| "];
    if (name.length == 0 || [name rangeOfCharacterFromSet:bad].location != NSNotFound)
    {
        [self showMessage:@"Please enter a project name without spaces or special characters."];
        return NO;
    }
    NSString* location = self.projectLocation;
    if (location.length == 0) { [self showMessage:@"Please choose a location."]; return NO; }
    std::string pro = Platform::Paths::Combine(Platform::Paths::Combine(GRStd(location.stringByExpandingTildeInPath),
                                                                         GRStd(name)), GRStd(name) + ".pro");
    if (Platform::Paths::Exists(pro))
    {
        [self showMessage:@"A project with this name already exists in that location."];
        return NO;
    }
    if (self.targets.empty()) { [self showMessage:@"Select at least one target."]; return NO; }
    if (_chosenTemplate == nil) { [self showMessage:@"Please choose a template."]; return NO; }
    return YES;
}

// ---- the template grid ----
- (NSInteger)collectionView:(NSCollectionView*)collectionView numberOfItemsInSection:(NSInteger)section
{
    (void)collectionView; (void)section;
    return (NSInteger)_templateNames.count;
}

- (NSCollectionViewItem*)collectionView:(NSCollectionView*)collectionView
     itemForRepresentedObjectAtIndexPath:(NSIndexPath*)indexPath
{
    GRTemplateItem* item = [collectionView makeItemWithIdentifier:@"template" forIndexPath:indexPath];
    NSString* name = _templateNames[(NSUInteger)indexPath.item];
    item.textField.stringValue = name;
    NSString* symbol = @"doc.text";
    if (_importCcs) symbol = @"square.and.arrow.down";
    else if ([name containsString:@"DSP"]) symbol = @"cpu";
    else if ([name containsString:@"Library"]) symbol = @"books.vertical";
    else if ([name containsString:@"Empty"]) symbol = @"folder";
    else if ([name containsString:@"Module"]) symbol = @"shippingbox";
    item.imageView.image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:name];
    return item;
}

- (void)collectionView:(NSCollectionView*)collectionView didSelectItemsAtIndexPaths:(NSSet<NSIndexPath*>*)indexPaths
{
    (void)collectionView;
    NSIndexPath* p = indexPaths.anyObject;
    if (p == nil || p.item < 0 || p.item >= (NSInteger)_templateNames.count) return;
    _chosenTemplate = _templateNames[(NSUInteger)p.item];
    [self templateChanged];
}
@end
