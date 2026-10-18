// =============================================================================
//  ToolchainCheckSheet.mm - see ToolchainCheckSheet.h
// =============================================================================
#import "ToolchainCheckSheet.h"
#import "DialogKit.h"
#include "../../Platform/Paths.h"
#include "../../Services/CodeConverter.h"
#include "../../Settings/ToolchainDefinitions.h"

using namespace GRIDEm;

@implementation GRToolchainCheckSheet
{
    GRTable* _list;
    int _found;
    int _missing;
    BOOL _openSettings;
}

- (instancetype)initWithSettings:(const Settings::GrideSettingsPtr&)s
{
    if ((self = [super initWithTitle:@"Toolchain Check" size:NSMakeSize(900, 560)]))
    {
        _list = [[GRTable alloc] initWithHeight:420];
        [_list addColumn:@"Tool" width:190 kind:GRColumnText choices:nil];
        [_list addColumn:@"Resolved path" width:320 kind:GRColumnText choices:nil];
        [_list addColumn:@"Status" width:110 kind:GRColumnText choices:nil];
        [_list addColumn:@"Looked in" width:260 kind:GRColumnText choices:nil];

        Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Current();
        [self group:@"toolchain.json"];
        [self line:@"Shipped file" shown:GRNS(defs->ShippedFound ? defs->ShippedFile : "not found: " + defs->ShippedFile)
             state:defs->ShippedFound ? @"Found" : @"Missing" ok:defs->ShippedFound looked:@""];
        [self line:@"Your overrides" shown:GRNS(defs->UserJson ? defs->UserFile : "(none) " + defs->UserFile)
             state:defs->UserJson ? @"In use" : @"None" ok:YES looked:@""];
        for (const std::string& p : defs->Problems) [self line:@"Problem" shown:GRNS(p) state:@"Check" ok:NO looked:@""];

        for (const Settings::ToolchainSettingsPtr& tc : s->Toolchains)
        {
            [self group:GRNS(Core::TargetInfo::DisplayName(tc->TargetKind))];
            [self tool:tc name:@"C90 compiler (c90)" value:tc->CompilerC90];
            [self tool:tc name:@"C++11 compiler (cpp11)" value:tc->CompilerCpp11];
            [self tool:tc name:@"Shalimar compiler" value:tc->CompilerShalimar];
            if (!tc->Assembler.empty()) [self tool:tc name:@"Assembler" value:tc->Assembler];
            if (!tc->Linker.empty()) [self tool:tc name:@"Linker" value:tc->Linker];
            if (!tc->DebugAssembler.empty()) [self tool:tc name:@"Debug assembler (clang)" value:tc->DebugAssembler];
            if (!tc->Runtime.empty()) [self tool:tc name:@"Runtime" value:tc->Runtime];
            if (!tc->RuntimeDir.empty())
            {
                std::string dir = tc->ResolveRuntimeDir();
                std::string path = dir.empty() ? tc->RuntimeDir : Platform::Paths::Combine(dir, "rts6x.lib");
                [self plain:@"Runtime directory (RTS6x)" path:path];
            }
            if (!tc->Simulator.empty()) [self tool:tc name:@"Simulator (sim6747)" value:tc->Simulator];
            if (!tc->Emulator.empty()) [self tool:tc name:@"Emulator (vm6747)" value:tc->Emulator];
            if (!tc->Debugger.empty()) [self tool:tc name:@"Debugger" value:tc->Debugger];
        }
        [self group:@"Converters"];
        Services::CodeConverter conv(s->Converter);
        [self converter:conv name:@"C -> Shalimar" direction:Services::ConvertDirection::CToShalimar];
        [self converter:conv name:@"Shalimar -> C" direction:Services::ConvertDirection::ShalimarToC];
        [_list reload];

        NSTextField* sum = [NSTextField labelWithString:
                            [NSString stringWithFormat:@"%d tool(s) found, %d not installed yet", _found, _missing]];
        sum.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
        NSStackView* all = [NSStackView stackViewWithViews:@[sum, _list.view]];
        all.orientation = NSUserInterfaceLayoutOrientationVertical;
        all.alignment = NSLayoutAttributeLeading;
        all.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
        [_list.view.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-28].active = YES;
        [self setContentView:all];
        [self removeOkButton];
        __weak GRToolchainCheckSheet* weakSelf = self;
        [self addTrailingButton:@"Open Settings…" action:^{
            GRToolchainCheckSheet* me = weakSelf;
            if (me == nil) return;
            me->_openSettings = YES;
            [me finish:YES];
        }];
    }
    return self;
}

- (BOOL)openSettings { return _openSettings; }

- (void)group:(NSString*)title
{
    [_list addRow:@[title, @"", @"", @""] tag:nil];
    [_list setColor:[NSColor secondaryLabelColor] row:(NSInteger)_list.rows.count - 1 column:0];
}

- (void)line:(NSString*)name shown:(NSString*)shown state:(NSString*)state ok:(BOOL)ok looked:(NSString*)looked
{
    [_list addRow:@[[@"   " stringByAppendingString:name], shown, state, looked] tag:nil];
    [_list setColor:ok ? [NSColor systemGreenColor] : [NSColor systemOrangeColor]
                row:(NSInteger)_list.rows.count - 1 column:2];
}

// a tool that is not there says where it was looked for
- (void)tool:(const Settings::ToolchainSettingsPtr&)tc name:(NSString*)name value:(const std::string&)tool
{
    std::string path = tc->Resolve(tool);
    bool ok = !path.empty() && Platform::Paths::IsFile(path);
    [self line:name shown:path.empty() ? @"(not set)" : GRNS(path) state:ok ? @"Found" : @"Not installed" ok:ok
        looked:ok ? @"" : GRNS(tc->LookedIn(tool))];
    if (ok) _found++; else _missing++;
}

- (void)converter:(const Services::CodeConverter&)conv name:(NSString*)name direction:(Services::ConvertDirection)d
{
    std::string path = conv.ToolPath(d);
    bool ok = !path.empty() && Platform::Paths::IsFile(path);
    [self line:name shown:path.empty() ? @"(not set)" : GRNS(path) state:ok ? @"Found" : @"Not installed" ok:ok
        looked:ok ? @"" : GRNS(conv.LookedIn(d))];
    if (ok) _found++; else _missing++;
}

- (void)plain:(NSString*)name path:(const std::string&)path
{
    bool ok = !path.empty() && Platform::Paths::IsFile(path);
    [self line:name shown:path.empty() ? @"(not set)" : GRNS(path) state:ok ? @"Found" : @"Not installed" ok:ok looked:@""];
    if (ok) _found++; else _missing++;
}
@end
