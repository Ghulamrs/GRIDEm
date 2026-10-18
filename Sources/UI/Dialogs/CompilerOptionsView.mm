// =============================================================================
//  CompilerOptionsView.mm - see CompilerOptionsView.h
// =============================================================================
#import "CompilerOptionsView.h"
#import "DialogKit.h"
#include "../../Core/StringUtil.h"
#include "../../Services/RideToolchain.h"
#include "../../Settings/ToolchainDefinitions.h"

using namespace GRIDEm;

@interface GRCompilerOptionsView () <NSTabViewDelegate>
@end

@implementation GRCompilerOptionsView
{
    Settings::CompilerOptionStorePtr _original;
    Settings::CompilerOptionStorePtr _draft;
    Core::Target _target;
    BOOL _readOnly;
    BOOL _pushing;
    NSMutableDictionary<NSString*, NSControl*>* _controls;
    NSMutableArray<GRForm*>* _forms;
    NSPopUpButton* _configPick;
    NSTextField* _targetLabel;
    NSTabView* _tabs;
    NSTextField* _preview;
    NSButton* _reset;
}

- (instancetype)initWithStore:(const Settings::CompilerOptionStorePtr&)store config:(NSString*)config
                       target:(Core::Target)target readOnly:(BOOL)readOnly
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, 640, 420)]))
    {
        _original = store;
        _draft = store ? store->Clone() : std::make_shared<Settings::CompilerOptionStore>();
        _target = target;
        _readOnly = readOnly;
        _controls = [NSMutableDictionary dictionary];
        _forms = [NSMutableArray array];

        // ---- top: the configuration edited, the target ----
        _configPick = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        [_configPick addItemsWithTitles:GRArray(Settings::CompilerOptionStore::Configurations())];
        [_configPick selectItemWithTitle:GRNS(Settings::CompilerOptionStore::Canon(GRStd(config)))];
        if (_configPick.indexOfSelectedItem < 0) [_configPick selectItemAtIndex:0];
        _configPick.target = self;
        _configPick.action = @selector(configChosen:);
        _targetLabel = [NSTextField labelWithString:@""];
        _targetLabel.textColor = [NSColor secondaryLabelColor];
        NSStackView* top = [NSStackView stackViewWithViews:@[[NSTextField labelWithString:@"Edit options for:"],
                                                             _configPick, _targetLabel]];
        top.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        top.spacing = 8;

        // ---- a tab per compilerOptions tab ----
        _tabs = [[NSTabView alloc] initWithFrame:NSZeroRect];
        _tabs.delegate = self;
        Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Current();
        for (const std::string& tab : defs->CompilerOptionTabs)
        {
            GRForm* form = [[GRForm alloc] init];
            form.labelWidth = 220;
            for (const Settings::CompilerOptionDefinitionPtr& o : defs->CompilerOptionsOf(tab))
            {
                NSControl* made = nil;
                if (o->IsCheck())
                {
                    NSButton* box = [form addCheck:GRNS(o->Label) on:NO];
                    box.target = self;
                    box.action = @selector(changed:);
                    made = box;
                }
                else if (o->IsChoice())
                {
                    NSPopUpButton* pick = [form addPopup:GRNS(o->Label) items:GRArray(o->Choices) selected:nil];
                    pick.target = self;
                    pick.action = @selector(changed:);
                    made = pick;
                }
                else
                {
                    NSTextField* field = [form addText:GRNS(o->Label) value:@""];
                    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(textChanged:)
                                                                 name:NSControlTextDidChangeNotification object:field];
                    made = field;
                }
                made.identifier = GRNS(o->Id);
                _controls[GRNS(o->Id)] = made;
            }
            [_forms addObject:form];
            NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:GRNS(tab)];
            item.label = GRNS(tab);
            item.view = form.view;
            [_tabs addTabViewItem:item];
        }

        // ---- bottom: the command line, Restore defaults ----
        _preview = [NSTextField wrappingLabelWithString:@""];
        _preview.selectable = YES;
        _preview.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
        _preview.preferredMaxLayoutWidth = 600;
        _reset = [NSButton buttonWithTitle:@"Restore defaults" target:self action:@selector(restore:)];
        _reset.enabled = !readOnly;
        NSStackView* bottom = [NSStackView stackViewWithViews:@[[NSTextField labelWithString:@"Command line:"],
                                                                _preview, _reset]];
        bottom.orientation = NSUserInterfaceLayoutOrientationVertical;
        bottom.alignment = NSLayoutAttributeLeading;
        bottom.spacing = 4;

        NSStackView* all = [NSStackView stackViewWithViews:@[top, _tabs, bottom]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.alignment = NSLayoutAttributeLeading;
        all.spacing = 8;
        all.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:all];
        [NSLayoutConstraint activateConstraints:@[
            [all.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:4],
            [all.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-4],
            [all.topAnchor constraintEqualToAnchor:self.topAnchor constant:4],
            [all.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-4],
            [_tabs.widthAnchor constraintEqualToAnchor:all.widthAnchor],
            [_tabs.heightAnchor constraintGreaterThanOrEqualToConstant:240],
            [bottom.widthAnchor constraintEqualToAnchor:all.widthAnchor],
        ]];
        [_tabs setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];
        [self setTarget:target];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (NSString*)config
{
    NSString* c = _configPick.titleOfSelectedItem;
    return c != nil ? c : @"Debug";
}

- (Settings::CompilerOptionStorePtr)draft { return _draft; }

- (void)setTarget:(Core::Target)target
{
    _target = target;
    _targetLabel.stringValue = [NSString stringWithFormat:@"Target: %@ (%@)",
                                GRNS(Services::RideToolchain::Arch(target)), GRNS(Core::TargetInfo::Name(target))];
    [self push];
}

- (void)commit
{
    if (_original && !_readOnly) _original->CopyFrom(_draft);
}

// the draft's values for the configuration shown, into the controls; the tips and the greying
- (void)push
{
    _pushing = YES;
    std::string config = GRStd(self.config);
    Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Current();
    for (NSString* key in _controls)
    {
        NSControl* c = _controls[key];
        std::string id = GRStd(key);
        std::string value = _draft->Value(config, id);
        if ([c isKindOfClass:[NSPopUpButton class]])
        {
            NSPopUpButton* pick = (NSPopUpButton*)c;
            [pick selectItemWithTitle:GRNS(value)];
            if (pick.indexOfSelectedItem < 0 && pick.numberOfItems > 0) [pick selectItemAtIndex:0];
        }
        else if ([c isKindOfClass:[NSButton class]])
            ((NSButton*)c).state = value == "1" ? NSControlStateValueOn : NSControlStateValueOff;
        else if ([c isKindOfClass:[NSTextField class]])
            ((NSTextField*)c).stringValue = GRNS(value);
        std::string why;
        bool usable = Services::RideToolchain::OptionAvailable(id, _target, why);
        Settings::CompilerOptionDefinitionPtr o = defs->CompilerOption(id);
        NSString* tip;
        if (_readOnly) tip = @"Read from the CCS project - edit it in CCS";
        else if (usable) tip = o ? GRNS(o->Hint) : @"";
        else tip = [@"Unavailable: " stringByAppendingString:GRNS(why)];
        c.enabled = usable && !_readOnly;
        c.toolTip = tip;
    }
    _pushing = NO;
    [self showPreview];
}

- (void)pull:(NSControl*)c
{
    if (c.identifier == nil) return;
    std::string value;
    if ([c isKindOfClass:[NSPopUpButton class]])
    {
        NSString* t = ((NSPopUpButton*)c).titleOfSelectedItem;
        if (t == nil) return;
        value = GRStd(t);
    }
    else if ([c isKindOfClass:[NSButton class]])
        value = ((NSButton*)c).state == NSControlStateValueOn ? "1" : "0";
    else if ([c isKindOfClass:[NSTextField class]])
        value = GRStd(((NSTextField*)c).stringValue);
    else return;
    _draft->Set(GRStd(self.config), GRStd(c.identifier), value);
}

// the tab's compiler and the flags it would be given (RIDE's preview line)
- (void)showPreview
{
    NSTabViewItem* item = _tabs.selectedTabViewItem;
    if (item == nil) { _preview.stringValue = @""; return; }
    Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Current();
    Settings::ToolDefinitionPtr tool;
    for (const Settings::CompilerOptionDefinitionPtr& o : defs->CompilerOptionsOf(GRStd(item.label)))
    {
        tool = defs->ToolOfOption(o->Id);
        // general.*: cpp11 shows them, as in RIDE
        if (tool && tool->Role != "cCompiler" && o->ToolWord().empty()) continue;
        if (tool) break;
    }
    if (!tool) tool = defs->ToolByRole("cppCompiler");
    if (!tool) { _preview.stringValue = @""; return; }
    std::string archFlag = Core::StringUtil::Replace(tool->ArchFlag, "$(Arch)", Services::RideToolchain::Arch(_target));
    std::string flags = Services::RideToolchain::OptionFlags(_draft, tool, GRStd(self.config), _target);
    _preview.stringValue = GRNS(Core::StringUtil::Trim(tool->Name + " " + archFlag + " " + flags));
}

- (void)changed:(id)sender
{
    if (_pushing) return;
    [self pull:sender];
    [self showPreview];
}

- (void)textChanged:(NSNotification*)note
{
    [self changed:note.object];
}

- (void)configChosen:(id)sender { (void)sender; [self push]; }

- (void)tabView:(NSTabView*)tabView didSelectTabViewItem:(NSTabViewItem*)tabViewItem { (void)tabView; (void)tabViewItem; [self showPreview]; }

- (void)restore:(id)sender
{
    (void)sender;
    _draft->Reset(GRStd(self.config));
    [self push];
}
@end
