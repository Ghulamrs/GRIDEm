// =============================================================================
//  ClassNavigator.mm - see ClassNavigator.h
// =============================================================================
#import "ClassNavigator.h"
#import "PaneUtil.h"
#import "../Icons.h"
#include "../../Core/StringUtil.h"
#include "../../Core/TextFile.h"
#include "../../Platform/Paths.h"
#include <regex>
#include <string>
#include <vector>

using namespace GRIDEm;

@interface GRSymbolNode : NSObject
@property (nonatomic, copy) NSString* title;
@property (nonatomic, copy) NSString* symbol;     // SF Symbol
@property (nonatomic, copy) NSString* file;
@property (nonatomic, assign) NSInteger line;
@property (nonatomic, strong) NSMutableArray<GRSymbolNode*>* children;
@end

@implementation GRSymbolNode
- (instancetype)init
{
    self = [super init];
    if (self != nil) _children = [NSMutableArray array];
    return self;
}
@end

// one symbol found while scanning (plain C++, made off the main thread)
struct FoundSymbol
{
    int group;              // 0 functions, 1 types, 2 macros
    std::string text;
    std::string icon;       // "function", "class", "struct", "variable"
    std::string file;
    int line;
};

struct ScannedProject
{
    std::string name;
    bool shalimar;
    bool ccs;
    std::vector<FoundSymbol> symbols;
};

// GRIDEd's five patterns, the named groups numbered (std::regex has no names)
static void ScanFile(const std::string& path, std::vector<FoundSymbol>& out)
{
    static const std::regex fn("^(?:static\\s+|extern\\s+|inline\\s+|const\\s+|unsigned\\s+|signed\\s+)*"
                               "[A-Za-z_]\\w*[\\s\\*&]+([A-Za-z_]\\w*(::\\w+)?)\\s*\\([^;]*$");
    static const std::regex ty("^\\s*(typedef\\s+)?(struct|class|union|enum)\\s+([A-Za-z_]\\w*)");
    static const std::regex td("^\\s*\\}\\s*([A-Za-z_]\\w*)\\s*;");
    static const std::regex sf("^\\s*fun\\s*<[^>]*>\\s*=\\s*([A-Za-z_]\\w*)\\s*\\(");
    static const std::regex mc("^\\s*#\\s*define\\s+([A-Za-z_]\\w*)");

    std::string text, error;
    Core::FileEncoding enc;
    if (!Core::TextFile::Read(path, text, enc, error)) return;
    std::vector<std::string> lines = Core::StringUtil::SplitLines(text);
    for (size_t i = 0; i < lines.size(); i++)
    {
        const std::string& l = lines[i];
        int lineNo = (int)i + 1;
        std::smatch m;
        if (std::regex_search(l, m, mc)) { out.push_back(FoundSymbol{ 2, m[1].str(), "variable", path, lineNo }); continue; }
        if (std::regex_search(l, m, ty))
        {
            std::string kind = m[2].str();
            out.push_back(FoundSymbol{ 1, kind + " " + m[3].str(), kind == "class" ? "class" : "struct", path, lineNo });
            continue;
        }
        if (std::regex_search(l, m, td)) { out.push_back(FoundSymbol{ 1, "typedef " + m[1].str(), "struct", path, lineNo }); continue; }
        if (std::regex_search(l, m, sf)) { out.push_back(FoundSymbol{ 0, m[1].str() + "()", "function", path, lineNo }); continue; }
        if (!l.empty() && !Core::StringUtil::IsSpace(l[0]) && l[0] != '#' && l[0] != '/' && l[0] != '*')
        {
            if (std::regex_search(l, m, fn))
            {
                std::string name = m[1].str();
                if (!Core::StringUtil::EndsWith(Core::StringUtil::TrimEnd(l), ";") && name != "if" && name != "while")
                    out.push_back(FoundSymbol{ 0, name + "()", "function", path, lineNo });
            }
        }
    }
}

static NSString* SymbolFor(const std::string& icon)
{
    if (icon == "function") return @"function";
    if (icon == "class") return @"c.square";
    if (icon == "struct") return @"s.square";
    return @"number.square";
}

@implementation GRClassNavigator
{
    NSOutlineView* _tree;
    NSScrollView* _scroll;
    NSMutableArray<GRSymbolNode*>* _roots;
    BOOL _scanned;
    NSUInteger _generation;
}

- (instancetype)init
{
    self = [super initWithIdentifier:@"ClassView" title:@"Class View" symbol:@"curlybraces"];
    if (self != nil) _roots = [NSMutableArray array];
    return self;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, 400)];
    // a local for the out-parameter: ARC refuses the address of an instance variable
    // for an __autoreleasing write-back
    NSScrollView* scroll = nil;
    _tree = [GRPaneUtil outlineWithColumns:@[ @"Symbol" ] widths:@[ @240 ] scroll:&scroll];
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

- (void)scan:(const Model::WorkspacePtr&)workspace
{
    // what to read is gathered here, on the main thread; the reading is done on a queue
    std::vector<ScannedProject> projects;
    std::vector<std::vector<std::string> > files;
    if (workspace)
        for (const Model::GrideProjectPtr& p : workspace->Projects)
        {
            ScannedProject sp;
            sp.name = p->Name;
            sp.shalimar = p->Lang == Core::Language::Shalimar;
            sp.ccs = p->IsCcs();
            projects.push_back(sp);
            files.push_back(p->AllFiles());
        }
    NSUInteger generation = ++_generation;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        std::vector<ScannedProject> result = projects;
        for (size_t i = 0; i < result.size(); i++)
            for (const std::string& f : files[i])
            {
                if (!Platform::Paths::IsFile(f)) continue;
                std::string ext = Core::StringUtil::ToLower(Platform::Paths::Extension(f));
                if (ext != ".c" && ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".shl" && ext != ".shm" && ext != ".cc")
                    continue;
                ScanFile(f, result[i].symbols);
            }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self->_generation) return;   // a newer scan is under way
            [self show:result];
        });
    });
}

- (void)show:(const std::vector<ScannedProject>&)projects
{
    if (!self.isViewLoaded) [self loadView];
    NSMutableSet<NSString*>* open = [NSMutableSet set];
    for (GRSymbolNode* p in _roots)
    {
        if ([_tree isItemExpanded:p]) [open addObject:p.title];
        for (GRSymbolNode* g in p.children)
            if ([_tree isItemExpanded:g]) [open addObject:[p.title stringByAppendingFormat:@"/%@", g.title]];
    }
    BOOL first = !_scanned;
    [_roots removeAllObjects];
    NSArray<NSString*>* groupNames = @[ @"Functions", @"Types", @"Macros" ];
    NSArray<NSString*>* groupSymbols = @[ @"function", @"square.stack", @"number" ];
    for (const ScannedProject& sp : projects)
    {
        GRSymbolNode* pn = [[GRSymbolNode alloc] init];
        pn.title = GRNS(sp.name);
        pn.symbol = sp.ccs ? @"cpu" : @"shippingbox";
        NSMutableArray<GRSymbolNode*>* groups = [NSMutableArray array];
        for (NSUInteger g = 0; g < 3; g++)
        {
            GRSymbolNode* gn = [[GRSymbolNode alloc] init];
            gn.title = groupNames[g];
            gn.symbol = groupSymbols[g];
            [groups addObject:gn];
        }
        for (const FoundSymbol& s : sp.symbols)
        {
            GRSymbolNode* leaf = [[GRSymbolNode alloc] init];
            leaf.title = GRNS(s.text);
            leaf.symbol = SymbolFor(s.icon);
            leaf.file = GRNS(s.file);
            leaf.line = s.line;
            [groups[(NSUInteger)s.group].children addObject:leaf];
        }
        for (GRSymbolNode* g in groups)
            if (g.children.count > 0) [pn.children addObject:g];
        if (pn.children.count == 0)
        {
            GRSymbolNode* none = [[GRSymbolNode alloc] init];
            none.title = @"(no symbols found)";
            [pn.children addObject:none];
        }
        [_roots addObject:pn];
    }
    [_tree reloadData];
    for (GRSymbolNode* p in _roots)
    {
        if (first || [open containsObject:p.title]) [_tree expandItem:p];
        if (!first)
            for (GRSymbolNode* g in p.children)
                if ([open containsObject:[p.title stringByAppendingFormat:@"/%@", g.title]]) [_tree expandItem:g];
    }
    _scanned = YES;
}

- (NSInteger)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item
{
    (void)outlineView;
    if (item == nil) return (NSInteger)_roots.count;
    return (NSInteger)((GRSymbolNode*)item).children.count;
}

- (id)outlineView:(NSOutlineView*)outlineView child:(NSInteger)index ofItem:(id)item
{
    (void)outlineView;
    if (item == nil) return _roots[(NSUInteger)index];
    return ((GRSymbolNode*)item).children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item
{
    (void)outlineView;
    return ((GRSymbolNode*)item).children.count > 0;
}

- (NSView*)outlineView:(NSOutlineView*)outlineView viewForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    (void)tableColumn;
    GRSymbolNode* n = item;
    NSTableCellView* cell = [GRPaneUtil cellIn:outlineView identifier:@"SymbolCell"];
    cell.imageView.image = n.symbol != nil ? [NSImage imageWithSystemSymbolName:n.symbol accessibilityDescription:nil] : nil;
    cell.textField.stringValue = n.title;
    cell.textField.textColor = n.file == nil && n.children.count == 0 ? [NSColor secondaryLabelColor] : [NSColor labelColor];
    cell.toolTip = n.file != nil ? [NSString stringWithFormat:@"%@:%ld", n.file, (long)n.line] : nil;
    return cell;
}

- (void)doubleClicked:(id)sender
{
    (void)sender;
    GRSymbolNode* n = [_tree itemAtRow:_tree.clickedRow];
    if (n == nil) return;
    if (n.file != nil) { if (self.navigate) self.navigate(n.file, n.line); return; }
    if ([_tree isItemExpanded:n]) [_tree collapseItem:n];
    else [_tree expandItem:n];
}
@end
