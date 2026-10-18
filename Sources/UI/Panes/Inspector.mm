// =============================================================================
//  Inspector.mm - see Inspector.h
// =============================================================================
#import "Inspector.h"
#import "PaneUtil.h"
#include "../../Core/LanguageTarget.h"
#include "../../Core/StringUtil.h"
#include "../../Platform/Paths.h"
#include <ctime>
#include <stdexcept>

using namespace GRIDEm;

// =============================================================================
@implementation GRPropertyItem
+ (GRPropertyItem*)category:(NSString*)category name:(NSString*)name value:(NSString*)value
{
    GRPropertyItem* p = [[GRPropertyItem alloc] init];
    p.category = category;
    p.name = name;
    p.value = value != nil ? value : @"";
    p.readOnly = YES;
    return p;
}
@end

// =============================================================================
static NSString* JoinList(const std::vector<std::string>& l) { return GRNS(Core::StringUtil::Join(";", l)); }

// "a; b;; c" -> {a, b, c} (GRIDEd's ConfigurationView::Split)
static std::vector<std::string> SplitList(NSString* v)
{
    std::vector<std::string> r;
    for (const std::string& s : Core::StringUtil::Split(GRStd(v), ';'))
    {
        std::string t = Core::StringUtil::Trim(s);
        if (!t.empty()) r.push_back(t);
    }
    return r;
}

static GRPropertyItem* Editable(NSString* category, NSString* name, NSString* help, NSString* value,
                                void (^setter)(NSString*))
{
    GRPropertyItem* p = [GRPropertyItem category:category name:name value:value];
    p.help = help;
    p.readOnly = NO;
    p.setter = setter;
    return p;
}

@implementation GRPropertySheet

+ (NSArray<GRPropertyItem*>*)forFile:(NSString*)path project:(Model::GrideProjectPtr)project
                            settings:(Settings::GrideSettingsPtr)settings
{
    std::string p8 = GRStd(path);
    Core::Language pl = project ? project->Lang : Core::Language::C90;
    Core::Language lang = settings ? settings->DetectLanguage(p8, pl) : pl;
    NSMutableArray* r = [NSMutableArray array];
    GRPropertyItem* name = [GRPropertyItem category:@"File" name:@"Name" value:path.lastPathComponent];
    name.help = @"File name.";
    [r addObject:name];
    [r addObject:[GRPropertyItem category:@"File" name:@"Full Path" value:path]];
    GRPropertyItem* rel = [GRPropertyItem category:@"File" name:@"Relative Path"
                                             value:project ? GRNS(project->ToRelative(p8)) : path];
    rel.help = @"Path as stored in the .pro file.";
    [r addObject:rel];
    [r addObject:[GRPropertyItem category:@"File" name:@"Language" value:GRNS(Core::LanguageInfo::Name(lang))]];
    long long size = Platform::Paths::FileSize(p8);
    [r addObject:[GRPropertyItem category:@"File" name:@"Size (bytes)"
                                    value:[NSString stringWithFormat:@"%lld", size < 0 ? 0LL : size]]];
    std::time_t t = 0;
    NSString* modified = @"";
    if (Platform::Paths::ModifiedTime(p8, t))
        modified = [NSDateFormatter localizedStringFromDate:[NSDate dateWithTimeIntervalSince1970:(double)t]
                                                  dateStyle:NSDateFormatterShortStyle timeStyle:NSDateFormatterMediumStyle];
    [r addObject:[GRPropertyItem category:@"File" name:@"Modified" value:modified]];
    [r addObject:[GRPropertyItem category:@"Build" name:@"Project" value:project ? GRNS(project->Name) : @"(none)"]];
    BOOL excluded = project && project->IsExcluded(p8);
    GRPropertyItem* ex = [GRPropertyItem category:@"Build" name:@"Excluded From Build" value:excluded ? @"Yes" : @"No"];
    ex.help = @"Skip this file when building the project.";
    ex.isBool = YES;
    if (project && !project->IsReadOnly)
    {
        ex.readOnly = NO;
        Model::GrideProjectPtr proj = project;
        ex.setter = ^(NSString* v) {
            proj->SetExcluded(p8, [v isEqualToString:@"Yes"]);
            try { proj->Save(); } catch (const std::exception&) { }
        };
    }
    [r addObject:ex];
    return r;
}

+ (NSArray<GRPropertyItem*>*)forProject:(Model::GrideProjectPtr)project
{
    NSMutableArray* r = [NSMutableArray array];
    if (!project) return r;
    const Model::GrideProject& p = *project;
    [r addObject:[GRPropertyItem category:@"Project" name:@"Name" value:GRNS(p.Name)]];
    [r addObject:[GRPropertyItem category:@"Project" name:@"Language" value:GRNS(Core::LanguageInfo::Name(p.Lang))]];
    [r addObject:[GRPropertyItem category:@"Project" name:@"Template" value:GRNS(p.Template)]];
    [r addObject:[GRPropertyItem category:@"Project" name:@"Project File" value:GRNS(p.FilePath)]];
    std::vector<std::string> targets;
    for (Core::Target t : p.Targets) targets.push_back(Core::TargetInfo::Name(t));
    [r addObject:[GRPropertyItem category:@"Project" name:@"Targets" value:GRNS(Core::StringUtil::Join(", ", targets))]];
    GRPropertyItem* ro = [GRPropertyItem category:@"Project" name:@"Read-only (CCS, Visual Studio, RIDE)"
                                            value:p.IsReadOnly ? @"Yes" : @"No"];
    ro.isBool = YES;
    [r addObject:ro];
    [r addObject:[GRPropertyItem category:@"Project" name:@"Files"
                                    value:[NSString stringWithFormat:@"%lu", (unsigned long)project->AllFiles().size()]]];
    if (p.Vs)
    {
        [r addObject:[GRPropertyItem category:@"Visual Studio" name:@"Version" value:GRNS(p.Vs->Version)]];
        [r addObject:[GRPropertyItem category:@"Visual Studio" name:@"Solution" value:GRNS(p.Vs->SolutionPath)]];
        [r addObject:[GRPropertyItem category:@"Visual Studio" name:@"Configurations"
                                        value:GRNS(Core::StringUtil::Join(", ", p.Vs->Configurations))]];
    }
    if (p.Ccs)
    {
        [r addObject:[GRPropertyItem category:@"Code Composer Studio" name:@"Device" value:GRNS(p.Ccs->DeviceVariant)]];
        [r addObject:[GRPropertyItem category:@"Code Composer Studio" name:@"Code Gen Tools" value:GRNS(p.Ccs->CodegenVersion)]];
        [r addObject:[GRPropertyItem category:@"Code Composer Studio" name:@"Output Format" value:GRNS(p.Ccs->OutputFormat)]];
    }
    return r;
}

+ (NSArray<GRPropertyItem*>*)forConfiguration:(Model::BuildConfigurationPtr)configuration
{
    NSMutableArray* r = [NSMutableArray array];
    if (!configuration) return r;
    Model::BuildConfigurationPtr c = configuration;
    [r addObject:[GRPropertyItem category:@"General" name:@"Configuration" value:GRNS(c->Key())]];
    [r addObject:Editable(@"General", @"Output Directory", @"Relative to the project folder.", GRNS(c->OutDir),
                          ^(NSString* v) { c->OutDir = GRStd(v); })];
    [r addObject:Editable(@"General", @"Intermediate Directory", @"Where .asm and object files go.", GRNS(c->ObjDir),
                          ^(NSString* v) { c->ObjDir = GRStd(v); })];
    [r addObject:Editable(@"General", @"Output Name", @"Program name without extension. Empty = project name.",
                          GRNS(c->OutputName), ^(NSString* v) { c->OutputName = GRStd(v); })];
    [r addObject:Editable(@"Compiler", @"Preprocessor Definitions", @"Separate with ';'.", JoinList(c->Defines),
                          ^(NSString* v) { c->Defines = SplitList(v); })];
    [r addObject:Editable(@"Compiler", @"Include Directories", @"Separate with ';'. Relative to the project folder.",
                          JoinList(c->IncludeDirs), ^(NSString* v) { c->IncludeDirs = SplitList(v); })];
    [r addObject:Editable(@"Compiler", @"Optimisation", @"O0 (none) to O3 (full).", GRNS(c->Optimise),
                          ^(NSString* v) { c->Optimise = GRStd(v); })];
    [r addObject:Editable(@"Compiler", @"Additional Options", nil, GRNS(c->ExtraCompileFlags),
                          ^(NSString* v) { c->ExtraCompileFlags = GRStd(v); })];
    [r addObject:Editable(@"Linker", @"Linker Command File", @"Memory map for the DSP target (e.g. c6747.cmd).",
                          GRNS(c->LinkerCmd), ^(NSString* v) { c->LinkerCmd = GRStd(v); })];
    [r addObject:Editable(@"Linker", @"Libraries", @"Separate with ';' (e.g. rts6747.lib).", JoinList(c->Libs),
                          ^(NSString* v) { c->Libs = SplitList(v); })];
    [r addObject:Editable(@"Linker", @"Library Directories", @"Separate with ';'.", JoinList(c->LibDirs),
                          ^(NSString* v) { c->LibDirs = SplitList(v); })];
    [r addObject:Editable(@"Linker", @"Additional Options", nil, GRNS(c->ExtraLinkFlags),
                          ^(NSString* v) { c->ExtraLinkFlags = GRStd(v); })];
    return r;
}

+ (NSArray<GRPropertyItem*>*)forWorkspace:(Model::WorkspacePtr)workspace
{
    NSMutableArray* r = [NSMutableArray array];
    if (!workspace) return r;
    const Model::Workspace& w = *workspace;
    [r addObject:[GRPropertyItem category:@"Workspace" name:@"Name" value:GRNS(w.Name)]];
    [r addObject:[GRPropertyItem category:@"Workspace" name:@"Workspace File" value:GRNS(w.FilePath)]];
    [r addObject:[GRPropertyItem category:@"Workspace" name:@"Projects"
                                    value:[NSString stringWithFormat:@"%lu", (unsigned long)w.Projects.size()]]];
    [r addObject:[GRPropertyItem category:@"Workspace" name:@"Startup Project" value:GRNS(w.StartupName)]];
    [r addObject:[GRPropertyItem category:@"Build" name:@"Active Configuration" value:GRNS(w.ActiveConfig)]];
    [r addObject:[GRPropertyItem category:@"Build" name:@"Active Target" value:GRNS(Core::TargetInfo::Name(w.ActiveTarget))]];
    return r;
}
@end

// =============================================================================
@interface GRPropertyForm () <NSTextFieldDelegate>
@end

@implementation GRPropertyForm
{
    NSArray<GRPropertyItem*>* _items;
    NSStackView* _stack;
    NSMapTable<NSView*, GRPropertyItem*>* _fieldItems;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        _fieldItems = [NSMapTable weakToStrongObjectsMapTable];
        _stack = [[NSStackView alloc] initWithFrame:NSZeroRect];
        _stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        _stack.alignment = NSLayoutAttributeLeading;
        _stack.spacing = 10;
        _stack.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
        [GRPaneUtil fill:self with:_stack];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)setItems:(NSArray<GRPropertyItem*>*)items
{
    _items = [items copy];
    for (NSView* v in [_stack.arrangedSubviews copy]) { [_stack removeArrangedSubview:v]; [v removeFromSuperview]; }
    [_fieldItems removeAllObjects];
    // the rows grouped by category, in the order the categories first appear (GRIDEd's Categorized)
    NSMutableArray<NSString*>* categories = [NSMutableArray array];
    for (GRPropertyItem* p in _items)
        if (![categories containsObject:p.category]) [categories addObject:p.category];
    for (NSString* category in categories)
    {
        NSTextField* heading = [NSTextField labelWithString:category];
        heading.font = [NSFont boldSystemFontOfSize:[NSFont smallSystemFontSize]];
        heading.textColor = [NSColor secondaryLabelColor];
        [_stack addArrangedSubview:heading];
        NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray array];
        for (GRPropertyItem* p in _items)
        {
            if (![p.category isEqualToString:category]) continue;
            NSTextField* label = [NSTextField labelWithString:p.name];
            label.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
            label.alignment = NSTextAlignmentRight;
            label.toolTip = p.help;
            NSView* value;
            if (p.isBool)
            {
                NSButton* box = [NSButton checkboxWithTitle:@"" target:self action:@selector(boxChanged:)];
                box.state = [p.value isEqualToString:@"Yes"] ? NSControlStateValueOn : NSControlStateValueOff;
                box.enabled = !p.readOnly;
                value = box;
            }
            else if (p.readOnly)
            {
                NSTextField* f = [NSTextField wrappingLabelWithString:p.value];
                f.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
                f.selectable = YES;
                f.preferredMaxLayoutWidth = 220;
                value = f;
            }
            else
            {
                NSTextField* f = [NSTextField textFieldWithString:p.value];
                f.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
                f.delegate = self;
                f.placeholderString = p.help;
                [f.widthAnchor constraintGreaterThanOrEqualToConstant:160].active = YES;
                value = f;
            }
            value.toolTip = p.help;
            [_fieldItems setObject:p forKey:value];
            [rows addObject:@[ label, value ]];
        }
        NSGridView* grid = [NSGridView gridViewWithViews:rows];
        grid.rowSpacing = 4;
        grid.columnSpacing = 8;
        grid.xPlacement = NSGridCellPlacementLeading;
        grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
        if (grid.numberOfColumns > 0) [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        [_stack addArrangedSubview:grid];
    }
}

- (void)commit:(GRPropertyItem*)p value:(NSString*)value
{
    if (p == nil || p.readOnly || p.setter == nil || [p.value isEqualToString:value]) return;
    p.value = value;
    p.setter(value);
    if (self.valueChanged) self.valueChanged(p);
}

- (void)boxChanged:(NSButton*)sender
{
    [self commit:[_fieldItems objectForKey:sender] value:sender.state == NSControlStateValueOn ? @"Yes" : @"No"];
}

- (void)controlTextDidEndEditing:(NSNotification*)note
{
    NSTextField* f = note.object;
    [self commit:[_fieldItems objectForKey:f] value:f.stringValue];
}
@end

// =============================================================================
@implementation GRInspector
{
    NSTextField* _title;
    NSScrollView* _scroll;
    GRPropertyForm* _form;
    NSTextField* _empty;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"Properties" title:@"Properties" symbol:@"slider.horizontal.3"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 280, 400)];
    _title = [GRPaneUtil label:@""];
    _title.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ _title ]];
    _form = [[GRPropertyForm alloc] initWithFrame:NSMakeRect(0, 0, 280, 400)];
    __weak GRInspector* weakSelf = self;
    _form.valueChanged = ^(GRPropertyItem* item) {
        GRInspector* s = weakSelf;
        if (s != nil && s.valueChanged) s.valueChanged(item);
    };
    _scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _scroll.hasVerticalScroller = YES;
    _scroll.autohidesScrollers = YES;
    _scroll.borderType = NSNoBorder;
    _scroll.drawsBackground = NO;
    NSClipView* clip = [[NSClipView alloc] initWithFrame:NSZeroRect];
    clip.drawsBackground = NO;
    _scroll.contentView = clip;
    _form.translatesAutoresizingMaskIntoConstraints = NO;
    _scroll.documentView = _form;
    [NSLayoutConstraint activateConstraints:@[
        [_form.leadingAnchor constraintEqualToAnchor:clip.leadingAnchor],
        [_form.trailingAnchor constraintEqualToAnchor:clip.trailingAnchor],
        [_form.topAnchor constraintEqualToAnchor:clip.topAnchor]
    ]];
    _empty = [GRPaneUtil label:@"Nothing selected"];
    _empty.textColor = [NSColor secondaryLabelColor];
    [GRPaneUtil stack:@[ bar, _scroll ] in:root grow:_scroll];
    _empty.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_empty];
    [NSLayoutConstraint activateConstraints:@[
        [_empty.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
        [_empty.centerYAnchor constraintEqualToAnchor:root.centerYAnchor]
    ]];
    self.view = root;
}

- (void)setItems:(NSArray<GRPropertyItem*>*)items title:(NSString*)title
{
    if (!self.isViewLoaded) [self loadView];
    _title.stringValue = title != nil ? title : @"";
    [_form setItems:items];
    _empty.hidden = items.count > 0;
    _scroll.hidden = items.count == 0;
}

- (void)refreshValues:(NSArray<GRPropertyItem*>*)items
{
    [self setItems:items title:_title.stringValue];
}
@end
