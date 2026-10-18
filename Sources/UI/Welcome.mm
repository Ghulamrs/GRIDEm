// =============================================================================
//  Welcome.mm - see Welcome.h
// =============================================================================
#import "Welcome.h"
#import "Panes/PaneUtil.h"
#import "Theme.h"
#include "../Platform/Paths.h"

using namespace GRIDEm;

@implementation GRWelcomeWindow
{
    NSStackView* _start;
    NSStackView* _recent;
    NSStackView* _samples;
}

- (instancetype)init
{
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 720, 440)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                        | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.titlebarAppearsTransparent = YES;
    w.titleVisibility = NSWindowTitleHidden;
    w.title = @"Welcome to GRIDEm";
    w.releasedWhenClosed = NO;
    w.movableByWindowBackground = YES;
    self = [super initWithWindow:w];
    if (self != nil) [self build];
    return self;
}

- (NSTextField*)heading:(NSString*)text
{
    NSTextField* h = [NSTextField labelWithString:text];
    h.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    h.textColor = [NSColor secondaryLabelColor];
    return h;
}

- (NSButton*)link:(NSString*)title symbol:(NSString*)symbol action:(SEL)action represented:(NSString*)value
{
    NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
    b.bordered = NO;
    b.alignment = NSTextAlignmentLeft;
    b.font = [NSFont systemFontOfSize:13];
    b.contentTintColor = [NSColor controlAccentColor];
    if (symbol != nil)
    {
        b.image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil];
        b.imagePosition = NSImageLeading;
    }
    b.identifier = value;          // the command id or the path
    b.toolTip = value;
    return b;
}

static NSStackView* Column(NSArray<NSView*>* views)
{
    NSStackView* s = [NSStackView stackViewWithViews:views];
    s.orientation = NSUserInterfaceLayoutOrientationVertical;
    s.alignment = NSLayoutAttributeLeading;
    s.spacing = 6;
    return s;
}

- (void)build
{
    NSView* root = self.window.contentView;
    NSTextField* title = [NSTextField labelWithString:@"GRIDEm"];
    title.font = [NSFont systemFontOfSize:34 weight:NSFontWeightSemibold];
    title.textColor = [NSColor controlAccentColor];
    NSTextField* sub = [NSTextField labelWithString:@"C90  ·  C++11  ·  Shalimar      "
                                                    "Windows  ·  Linux  ·  macOS  ·  TMS320C6747"];
    sub.font = [NSFont systemFontOfSize:12];
    sub.textColor = [NSColor secondaryLabelColor];

    _start = Column(@[ [self heading:@"Start"],
        [self link:@"New Project..." symbol:@"plus.square" action:@selector(commandClicked:) represented:@"File.NewProject"],
        [self link:@"Open Workspace / Project..." symbol:@"folder" action:@selector(commandClicked:) represented:@"File.OpenWorkspace"],
        [self link:@"Open File..." symbol:@"doc" action:@selector(commandClicked:) represented:@"File.OpenFile"],
        [self link:@"Import CCS 7.4 / 5.5 Project..." symbol:@"cpu" action:@selector(commandClicked:) represented:@"File.OpenCcs"] ]);
    _recent = Column(@[ [self heading:@"Recent"] ]);
    _samples = Column(@[ [self heading:@"Samples"] ]);

    NSStackView* lists = [NSStackView stackViewWithViews:@[ _start, _recent, _samples ]];
    lists.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    lists.alignment = NSLayoutAttributeTop;
    lists.distribution = NSStackViewDistributionFillEqually;
    lists.spacing = 28;

    NSStackView* all = Column(@[ title, sub, lists ]);
    all.spacing = 10;
    [all setCustomSpacing:28 afterView:sub];
    all.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:all];
    [NSLayoutConstraint activateConstraints:@[
        [all.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:48],
        [all.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-32],
        [all.topAnchor constraintEqualToAnchor:root.topAnchor constant:52]
    ]];
}

+ (NSArray<NSString*>*)samples
{
    NSMutableArray* r = [NSMutableArray array];
    std::string dir = Platform::Paths::Combine(Platform::Paths::ResourcesDirectory(), "samples");
    if (!Platform::Paths::IsDirectory(dir)) return r;
    // a sample is a folder holding a .grws (samples/INS_Suite/INS_Suite.grws), or a .grws at the top
    for (const std::string& name : Platform::Paths::List(dir))
    {
        std::string p = Platform::Paths::Combine(dir, name);
        if (Platform::Paths::IsDirectory(p))
        {
            for (const std::string& f : Platform::Paths::ListFiles(p))
                if (Platform::Paths::Extension(f) == ".grws") { [r addObject:GRNS(Platform::Paths::Combine(p, f))]; break; }
        }
        else if (Platform::Paths::Extension(name) == ".grws") [r addObject:GRNS(p)];
    }
    return r;
}

- (void)clearList:(NSStackView*)list
{
    NSArray<NSView*>* views = list.arrangedSubviews;
    for (NSUInteger i = 1; i < views.count; i++) { [list removeArrangedSubview:views[i]]; [views[i] removeFromSuperview]; }
}

- (void)showWithRecent:(const Settings::RecentListPtr&)recent
{
    [self clearList:_recent];
    BOOL any = NO;
    if (recent)
        for (const std::string& p : recent->Workspaces)
        {
            NSString* path = GRNS(p);
            NSButton* b = [self link:path.lastPathComponent symbol:@"clock" action:@selector(pathClicked:) represented:path];
            [_recent addArrangedSubview:b];
            any = YES;
            if (_recent.arrangedSubviews.count > 9) break;
        }
    if (!any)
    {
        NSTextField* none = [NSTextField labelWithString:@"No recent workspaces yet."];
        none.textColor = [NSColor secondaryLabelColor];
        [_recent addArrangedSubview:none];
    }
    [self clearList:_samples];
    NSArray<NSString*>* samples = [GRWelcomeWindow samples];
    for (NSString* s in samples)
        [_samples addArrangedSubview:[self link:s.lastPathComponent.stringByDeletingPathExtension symbol:@"shippingbox"
                                         action:@selector(pathClicked:) represented:s]];
    if (samples.count == 0)
    {
        NSTextField* none = [NSTextField labelWithString:@"No samples in this copy of GRIDEm."];
        none.textColor = [NSColor secondaryLabelColor];
        [_samples addArrangedSubview:none];
    }
    [[GRTheme current] applyToWindow:self.window];
    [self.window center];
    [self showWindow:nil];
    [self.window makeKeyAndOrderFront:nil];
}

- (void)commandClicked:(NSButton*)sender
{
    if (self.commandRequested) self.commandRequested(sender.identifier);
}

- (void)pathClicked:(NSButton*)sender
{
    if (self.openRequested) self.openRequested(sender.identifier);
}
@end
