// =============================================================================
//  BatchBuildSheet.mm - see BatchBuildSheet.h
// =============================================================================
#import "BatchBuildSheet.h"
#import "DialogKit.h"

using namespace GRIDEm;

@implementation GRBatchBuildSheet
{
    std::vector<GRBatchItem> _items;
    std::vector<GRBatchItem> _selected;
    GRCheckList* _list;
    GRBatchAction _action;
}

- (instancetype)initWithWorkspace:(const Model::WorkspacePtr&)w
{
    if ((self = [super initWithTitle:@"Batch Build" size:NSMakeSize(600, 480)]))
    {
        _action = GRBatchBuild;
        NSMutableArray* titles = [NSMutableArray array];
        for (const Model::GrideProjectPtr& p : w->Projects)
            for (Core::Target t : Core::TargetInfo::All())
            {
                if (!p->SupportsTarget(t)) continue;
                for (const char* config : { "Debug", "Release" })
                {
                    GRBatchItem item;
                    item.Project = p;
                    item.Config = config;
                    item.TargetKind = t;
                    _items.push_back(item);
                    [titles addObject:[NSString stringWithFormat:@"%@  |  %s  |  %@", GRNS(p->Name), config,
                                       GRNS(Core::TargetInfo::Name(t))]];
                }
            }
        _list = [[GRCheckList alloc] initWithHeight:300];
        [_list setItems:titles checked:@[]];
        NSTextField* heading = [NSTextField labelWithString:@"Check the project configurations to build:"];
        NSStackView* all = [NSStackView stackViewWithViews:@[heading, _list.view]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.alignment = NSLayoutAttributeLeading;
        all.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [_list.view.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [self setContentView:all];

        [self setOkTitle:@"Build"];
        [self setCancelTitle:@"Close"];
        __weak GRBatchBuildSheet* weakSelf = self;
        [self addTrailingButton:@"Clean" action:^{ [weakSelf run:GRBatchClean]; }];
        [self addTrailingButton:@"Rebuild" action:^{ [weakSelf run:GRBatchRebuild]; }];
        [self addButton:@"Select All" action:^{ [weakSelf selectAll:YES]; }];
        [self addButton:@"Deselect All" action:^{ [weakSelf selectAll:NO]; }];
    }
    return self;
}

- (GRBatchAction)action { return _action; }
- (std::vector<GRBatchItem>)selected { return _selected; }

- (void)selectAll:(BOOL)on { [_list setAll:on]; }

- (BOOL)collect
{
    _selected.clear();
    for (size_t i = 0; i < _items.size(); i++)
        if ([_list isChecked:(NSInteger)i]) _selected.push_back(_items[i]);
    if (_selected.empty()) { [self showMessage:@"Nothing is checked."]; return NO; }
    return YES;
}

- (void)run:(GRBatchAction)action
{
    _action = action;
    if ([self collect]) [self finish:YES];
}

- (BOOL)accept
{
    _action = GRBatchBuild;
    return [self collect];
}
@end
