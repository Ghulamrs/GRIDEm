// =============================================================================
//  WindowsSheet.mm - see WindowsSheet.h
// =============================================================================
#import "WindowsSheet.h"
#import "DialogKit.h"

@implementation GRWindowsSheet
{
    GRTable* _list;
    GRWindowsAction _action;
    NSIndexSet* _selected;
}

- (instancetype)initWithTitles:(NSArray<NSString*>*)titles paths:(NSArray<NSString*>*)paths
{
    if ((self = [super initWithTitle:@"Windows" size:NSMakeSize(660, 440)]))
    {
        _list = [[GRTable alloc] initWithHeight:300];
        [_list addColumn:@"Name" width:180 kind:GRColumnText choices:nil];
        [_list addColumn:@"Path" width:440 kind:GRColumnText choices:nil];
        for (NSUInteger i = 0; i < titles.count; i++)
            [_list addRow:@[titles[i], i < paths.count ? paths[i] : @""] tag:nil];
        [_list reload];
        __weak GRWindowsSheet* weakSelf = self;
        _list.doubleClicked = ^(NSInteger row) { (void)row; [weakSelf run:GRWindowsActivate]; };
        NSStackView* all = [NSStackView stackViewWithViews:@[_list.view]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [_list.view.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [self setContentView:all];
        [self setOkTitle:@"Activate"];
        [self setCancelTitle:@"Close"];
        [self addTrailingButton:@"Save" action:^{ [weakSelf run:GRWindowsSave]; }];
        [self addTrailingButton:@"Close Window(s)" action:^{ [weakSelf run:GRWindowsClose]; }];
    }
    return self;
}

- (GRWindowsAction)action { return _action; }
- (NSIndexSet*)selected { return _selected; }

- (BOOL)collect
{
    _selected = [_list.selectedRows copy];
    return _selected.count > 0;
}

- (void)run:(GRWindowsAction)action
{
    _action = action;
    if ([self collect]) [self finish:YES];
}

- (BOOL)accept
{
    _action = GRWindowsActivate;
    return [self collect];
}
@end
