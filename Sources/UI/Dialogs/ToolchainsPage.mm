// =============================================================================
//  ToolchainsPage.mm - Settings > Toolchains (GRIDEd's OptionsDialog ToolchainPage,
//  one target at a time): the tools with a Found / Not installed status, the
//  search paths, the command templates, the flag spellings and the tool
//  environment. "Reset this target to defaults" takes everything from
//  toolchain.json again.
// =============================================================================
#import "SettingsWindow.h"
#import "DialogKit.h"
#include "../../Core/StringUtil.h"
#include "../../Platform/Environment.h"
#include "../../Platform/Paths.h"
#include "../../Settings/ToolchainDefinitions.h"
#include "../../Settings/ToolchainSettings.h"

using namespace GRIDEm;

@implementation GRToolchainsPage
{
    NSPopUpButton* _which;
    NSView* _host;
    Settings::ToolchainSettingsPtr _tc;
    NSMutableArray<GRPathField*>* _tools;
    GRPathField* _toolDir;
    GRPathField *_c90, *_cpp11, *_shl, *_cl, *_asmb, *_lnk, *_dasm, *_dlnk, *_rts, *_rtsDir, *_sim, *_emu, *_dbg;
    NSTextView *_inc, *_lib;
    NSTextField *_cmdCompile, *_cmdCompileCl, *_cmdCompileShl, *_cmdAsm, *_cmdLink, *_cmdDebugAsm, *_cmdDebugLink;
    NSTextField *_cmdRun, *_cmdEmulate, *_cmdDebug;
    NSTextField *_fInc, *_fDef, *_fLibDir, *_fLib, *_fAsmExt, *_fObjExt;
    GRPathField* _setup;
    NSTextView* _env;
    NSTextField* _codePage;
    NSTextField* _errPattern;
}

- (NSView*)makeContent
{
    _tools = [NSMutableArray array];
    NSMutableArray* names = [NSMutableArray array];
    for (Core::Target t : Core::TargetInfo::All()) [names addObject:GRNS(Core::TargetInfo::DisplayName(t))];
    _which = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [_which addItemsWithTitles:names];
    // the Mac's own target first in view
    [_which selectItemWithTitle:GRNS(Core::TargetInfo::DisplayName(Core::Target::MacOS))];
    _which.target = self;
    _which.action = @selector(targetChosen:);
    NSStackView* top = [NSStackView stackViewWithViews:@[[NSTextField labelWithString:@"Target:"], _which]];
    top.orientation = NSUserInterfaceLayoutOrientationHorizontal;

    _host = [[NSView alloc] initWithFrame:NSZeroRect];
    _host.translatesAutoresizingMaskIntoConstraints = NO;
    NSStackView* all = [NSStackView stackViewWithViews:@[top, _host]];
    all.orientation = NSUserInterfaceLayoutOrientationVertical;
    all.alignment = NSLayoutAttributeLeading;
    all.edgeInsets = NSEdgeInsetsMake(12, 0, 0, 0);
    [top.leadingAnchor constraintEqualToAnchor:all.leadingAnchor constant:16].active = YES;
    [_host.widthAnchor constraintEqualToAnchor:all.widthAnchor].active = YES;
    [_host setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];
    [self targetChosen:nil];
    return all;
}

- (Core::Target)chosenTarget
{
    NSInteger i = _which.indexOfSelectedItem;
    std::vector<Core::Target> all = Core::TargetInfo::All();
    if (i < 0 || i >= (NSInteger)all.size()) return Core::Target::MacOS;
    return all[(size_t)i];
}

- (void)targetChosen:(id)sender
{
    (void)sender;
    [self capture];
    _tc = self.work->Toolchain([self chosenTarget]);
    [self build];
}

// a path field for a tool, its status by toolchain.json's lookup with the tool folder typed
- (GRPathField*)tool:(GRForm*)form label:(NSString*)label value:(const std::string&)value
{
    GRPathField* f = [[GRPathField alloc] initFolder:NO status:YES];
    Settings::ToolchainSettingsPtr tc = _tc;
    __weak GRToolchainsPage* weakSelf = self;
    f.check = ^BOOL(NSString* v) {
        GRToolchainsPage* me = weakSelf;
        Settings::ToolchainSettings probe = *tc;
        if (me != nil && me->_toolDir != nil) probe.ToolDir = GRStd(me->_toolDir.value);
        std::string expanded = Platform::Environment::Expand(GRStd(v));
        std::string resolved = Platform::Paths::IsRooted(expanded) ? expanded : probe.Resolve(expanded);
        return Platform::Paths::IsFile(resolved);
    };
    f.value = GRNS(value);
    [form addRow:label view:f];
    [_tools addObject:f];
    return f;
}

// the target's programs as toolchain.json names them, and where a bare name is looked for
- (NSString*)programsNote
{
    Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Current();
    Settings::TargetDefinitionPtr td = defs->TargetOf(_tc->TargetKind);
    std::vector<std::string> parts;
    if (td)
        for (const std::pair<const std::string, std::string>& role : td->Roles)
        {
            Settings::ToolDefinitionPtr tool = defs->Tool(role.second);
            if (tool && !tool->Exe.empty()) parts.push_back(role.first + ": " + tool->Exe);
        }
    std::string text = "toolchain.json names " + (parts.empty() ? std::string("no programs") : Core::StringUtil::Join(", ", parts))
        + " for this target. A name without a folder is looked for as each tool's lookup says: its environment variable, "
          "the tool folder, inside GRIDEm.app, RIDE's installation, PATH (also without .exe).";
    return GRNS(text);
}

- (void)build
{
    for (NSView* v in [_host.subviews copy]) [v removeFromSuperview];
    [_tools removeAllObjects];
    Settings::ToolchainSettingsPtr tc = _tc;
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Tools"];
    _toolDir = [[GRPathField alloc] initFolder:YES status:YES];
    _toolDir.value = GRNS(tc->ToolDir);
    __weak GRToolchainsPage* weakSelf = self;
    _toolDir.changed = ^{ [weakSelf toolDirChanged]; };
    [form addRow:@"Tool folder" view:_toolDir];
    [form addNote:[self programsNote]];
    _c90 = [self tool:form label:@"C90 compiler (c90)" value:tc->CompilerC90];
    _cpp11 = [self tool:form label:@"C++11 compiler (cpp11)" value:tc->CompilerCpp11];
    _shl = [self tool:form label:@"Shalimar compiler" value:tc->CompilerShalimar];
    _cl = [self tool:form label:@"Host compiler (cl)" value:tc->CompilerCl];
    _asmb = [self tool:form label:@"Assembler" value:tc->Assembler];
    _lnk = [self tool:form label:@"Linker" value:tc->Linker];
    _dasm = [self tool:form label:@"Debug assembler (clang)" value:tc->DebugAssembler];
    _dlnk = [self tool:form label:@"Debug linker (link.exe)" value:tc->DebugLinker];
    _rts = [self tool:form label:@"Runtime library" value:tc->Runtime];
    _rtsDir = [[GRPathField alloc] initFolder:YES status:YES];
    _rtsDir.value = GRNS(tc->RuntimeDir);
    [form addRow:@"Runtime directory (RTS6x)" view:_rtsDir];
    _sim = [self tool:form label:@"Simulator (sim6747)" value:tc->Simulator];
    _emu = [self tool:form label:@"Emulator (vm6747)" value:tc->Emulator];
    _dbg = [self tool:form label:@"Debugger" value:tc->Debugger];

    [form addSection:@"Search paths (one per line)"];
    _inc = [form addLines:@"Include directories" text:GRJoinLines(tc->IncludeDirs) height:60 mono:YES];
    _lib = [form addLines:@"Library directories" text:GRJoinLines(tc->LibDirs) height:60 mono:YES];

    [form addSection:@"Command templates"];
    _cmdCompile = [form addMono:@"Compile" value:GRNS(tc->CompileCmd)];
    _cmdCompileCl = [form addMono:@"Compile with cl" value:GRNS(tc->CompileCmdCl)];
    _cmdCompileShl = [form addMono:@"Compile Shalimar" value:GRNS(tc->CompileCmdShalimar)];
    _cmdAsm = [form addMono:@"Assemble" value:GRNS(tc->AssembleCmd)];
    _cmdLink = [form addMono:@"Link" value:GRNS(tc->LinkCmd)];
    _cmdDebugAsm = [form addMono:@"Debug assemble" value:GRNS(tc->DebugAssembleCmd)];
    _cmdDebugLink = [form addMono:@"Debug link" value:GRNS(tc->DebugLinkCmd)];
    _cmdRun = [form addMono:@"Run" value:GRNS(tc->RunCmd)];
    _cmdEmulate = [form addMono:@"Emulate" value:GRNS(tc->EmulateCmd)];
    _cmdDebug = [form addMono:@"Debug" value:GRNS(tc->DebugCmd)];
    [form addNote:@"Macros: $(Compiler) $(Assembler) $(Linker) $(DebugAssembler) $(DebugLinker) $(Runtime) $(RuntimeDir) "
                   "$(Simulator) $(Emulator) $(Debugger) $(ToolDir) $(Source) $(Sources) $(Name) $(AsmFile) $(AsmFiles) "
                   "$(ObjFile) $(Objects) $(Output) $(Program) $(ObjDir) $(OutDir) $(Arch) $(ArchFlag) $(ConfigFlags) "
                   "$(AsmSyntaxFlag) $(LangFlags) $(IncludeFlags) $(DefineFlags) $(LibDirFlags) $(LibFlags) $(CrtLibs) "
                   "$(Optimise) $(CompileFlags) $(LinkFlags) $(LinkerCmd) $(Config) $(Target) $(ProjectDir) $(ProjectName)"];

    [form addSection:@"Flag spelling"];
    _fInc = [form addMono:@"Include flag" value:GRNS(tc->IncludeFlag)];
    _fDef = [form addMono:@"Define flag" value:GRNS(tc->DefineFlag)];
    _fLibDir = [form addMono:@"Library dir flag" value:GRNS(tc->LibDirFlag)];
    _fLib = [form addMono:@"Library flag" value:GRNS(tc->LibFlag)];
    _fAsmExt = [form addMono:@"Assembly extension" value:GRNS(tc->AsmExt)];
    _fObjExt = [form addMono:@"Object extension" value:GRNS(tc->ObjExt)];

    [form addSection:@"Tool environment (optional)"];
    _setup = [[GRPathField alloc] initFolder:NO status:NO];
    _setup.value = GRNS(tc->SetupScript);
    [form addRow:@"Setup script" view:_setup];
    [form addNote:@"vcvars64.bat and the like are Windows only: kept so the file reads the same in GRIDEd, not run "
                   "on the Mac - the tools are started directly."];
    _env = [form addLines:@"Environment (NAME=value)" text:GRJoinLines(tc->EnvironmentVars) height:60 mono:YES];
    _codePage = [form addNumber:@"Output code page (0 = the system's)" value:tc->OutputCodePage min:0 max:65535 decimals:0];
    _errPattern = [form addMono:@"Error pattern (regex)" value:GRNS(tc->ErrorPattern)];
    [form addNote:@"Extra error format, tried before the built-in ones. Named groups: file, line, col, sev, code, msg."];
    NSButton* reset = [NSButton buttonWithTitle:@"Reset this target to defaults" target:self action:@selector(resetTarget:)];
    [form addRow:@"" view:reset];

    NSScrollView* v = form.view;
    v.translatesAutoresizingMaskIntoConstraints = NO;
    [_host addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [v.leadingAnchor constraintEqualToAnchor:_host.leadingAnchor],
        [v.trailingAnchor constraintEqualToAnchor:_host.trailingAnchor],
        [v.topAnchor constraintEqualToAnchor:_host.topAnchor],
        [v.bottomAnchor constraintEqualToAnchor:_host.bottomAnchor],
    ]];
}

- (void)toolDirChanged
{
    for (GRPathField* f in _tools) [f updateStatus];
}

// one NAME=value per line; unlike the other lists, ';' is kept (PATH=a:b:$PATH)
static std::vector<std::string> EnvLines(NSTextView* t)
{
    return GRSplitLines(t.string);
}

- (void)capture
{
    if (!_tc || _toolDir == nil) return;
    Settings::ToolchainSettingsPtr tc = _tc;
    tc->ToolDir = GRStd(_toolDir.value);
    tc->CompilerC90 = GRStd(_c90.value); tc->CompilerCpp11 = GRStd(_cpp11.value);
    tc->CompilerShalimar = GRStd(_shl.value); tc->CompilerCl = GRStd(_cl.value);
    tc->Assembler = GRStd(_asmb.value); tc->Linker = GRStd(_lnk.value);
    tc->Runtime = GRStd(_rts.value); tc->Simulator = GRStd(_sim.value);
    tc->DebugAssembler = GRStd(_dasm.value); tc->DebugLinker = GRStd(_dlnk.value);
    tc->RuntimeDir = GRStd(_rtsDir.value);
    tc->Emulator = GRStd(_emu.value); tc->Debugger = GRStd(_dbg.value);
    tc->IncludeDirs = GRSplitLines(_inc.string);
    tc->LibDirs = GRSplitLines(_lib.string);
    tc->CompileCmd = GRStd(_cmdCompile.stringValue); tc->CompileCmdCl = GRStd(_cmdCompileCl.stringValue);
    tc->CompileCmdShalimar = GRStd(_cmdCompileShl.stringValue);
    tc->AssembleCmd = GRStd(_cmdAsm.stringValue); tc->LinkCmd = GRStd(_cmdLink.stringValue);
    tc->DebugAssembleCmd = GRStd(_cmdDebugAsm.stringValue); tc->DebugLinkCmd = GRStd(_cmdDebugLink.stringValue);
    tc->RunCmd = GRStd(_cmdRun.stringValue); tc->EmulateCmd = GRStd(_cmdEmulate.stringValue);
    tc->DebugCmd = GRStd(_cmdDebug.stringValue);
    tc->IncludeFlag = GRStd(_fInc.stringValue); tc->DefineFlag = GRStd(_fDef.stringValue);
    tc->LibDirFlag = GRStd(_fLibDir.stringValue); tc->LibFlag = GRStd(_fLib.stringValue);
    tc->AsmExt = GRStd(_fAsmExt.stringValue); tc->ObjExt = GRStd(_fObjExt.stringValue);
    tc->SetupScript = GRStd(_setup.value);
    tc->EnvironmentVars = EnvLines(_env);
    int cp = _codePage.intValue;
    tc->OutputCodePage = cp < 0 ? 0 : (cp > 65535 ? 65535 : cp);
    tc->ErrorPattern = Core::StringUtil::Trim(GRStd(_errPattern.stringValue));
}

- (void)resetTarget:(id)sender
{
    (void)sender;
    if (!_tc) return;
    *_tc = *_tc->Defaults();
    [self build];
}

- (void)commit { [self capture]; }
@end
