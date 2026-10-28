// =============================================================================
//  MainWindow+Debug.mm - Debug menu: run, debug, breakpoints, the debug panes
//  (GRIDEd's UI/MainForm.Build.cpp, its Debug half, and the panes' UpdateFrom)
// =============================================================================
#import "MainWindow.h"
#import "Common.h"
#import "Commands.h"
#import "Editor/SourceDocument.h"
#import "Editor/CodeView.h"
#import "Panes/PaneHost.h"
#import "Panes/OutputView.h"
#import "Panes/BreakpointsNavigator.h"
#import "Panes/VariablesView.h"
#import "Panes/StackView.h"
#import "Panes/RegistersView.h"
#import "Panes/MemoryView.h"
#import "Panes/DisassemblyView.h"
#import "Dialogs/DialogKit.h"
#import "Dialogs/Sheet.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include "../Services/GdbDebugger.h"
#include "../Services/ShalimarSession.h"
#include "../Services/RideToolchain.h"
#import "Editor/Gutter.h"
#import "Editor/Highlighter.h"
#import "Panes/ClassNavigator.h"
#import "Panes/FilesNavigator.h"
#import "Panes/FindResultsView.h"
#import "Panes/Inspector.h"
#import "Panes/IssuesView.h"
#import "Panes/TerminalView.h"
#import "Panes/WorkspaceNavigator.h"

using namespace GRIDEm;

@implementation GRMainWindowController (Debug)

// =============================================================================
//  starting
// =============================================================================
- (void)debugStart
{
    if (![self ensureProgram]) return;
    if (self.session.isInBreak) { self.session.debugger->Continue(); return; }
    Model::GrideProjectPtr p = [self runProject];
    if (!self.session.workspace || !p) return;
    self.afterBuild = GRAfterDebug;
    if (![self startBuild:Services::BuildAction::Build projects:self.session.workspace->BuildOrderFor(p) singleFile:nil])
        self.afterBuild = GRAfterNothing;
}

- (void)debugStartNoDebug
{
    if (![self ensureProgram]) return;
    Model::GrideProjectPtr p = [self runProject];
    if (!self.session.workspace || !p) return;
    // a Run of a cross target is turned away before anything is built (RIDE's buildAndRun)
    Core::Target t = [self currentTarget];
    if (!Services::RideToolchain::RunsHere(Services::RideTool::C90, t))
    {
        NSString* why = GRNS(Services::RideToolchain::WhyNotRun(Services::RideTool::C90, t));
        [self.outputView clear:@"Build"];
        [self.outputView showSource:@"Build"];
        [self showPane:@"Output"];
        [self.outputView append:why source:@"Build"];
        [self setStatus:why];
        return;
    }
    self.afterBuild = GRAfterRun;
    if (![self startBuild:Services::BuildAction::Build projects:self.session.workspace->BuildOrderFor(p) singleFile:nil])
        self.afterBuild = GRAfterNothing;
}

// RIDE's Build > Emulate on vm6747 and Build > Verify: the build a Run makes, with what runs the program changed
- (void)debugEmulate
{
    if (![self ensureProgram]) return;
    Model::GrideProjectPtr p = [self runProject];
    if (!self.session.workspace || !p) return;
    if (!Services::RideToolchain::IsEmulated([self currentTarget]))
    {
        [self setStatus:@"Emulate on vm6747 is for tms6747 - choose it under Target"];
        return;
    }
    self.afterBuild = GRAfterEmulate;
    if (![self startBuild:Services::BuildAction::Build projects:self.session.workspace->BuildOrderFor(p) singleFile:nil])
        self.afterBuild = GRAfterNothing;
}

- (void)debugVerify
{
    if (![self ensureProgram]) return;
    Model::GrideProjectPtr p = [self runProject];
    if (!self.session.workspace || !p) return;
    if (!Services::RideToolchain::IsEmulated([self currentTarget]))
    {
        [self setStatus:@"Verify is for tms6747 - choose it under Target"];
        return;
    }
    self.afterBuild = GRAfterVerify;
    if (![self startBuild:Services::BuildAction::Build projects:self.session.workspace->BuildOrderFor(p) singleFile:nil])
        self.afterBuild = GRAfterNothing;
}

// =============================================================================
//  a program run inside the Output pane (GRIDEd's RunChosen)
//  on tms6747 the simulator on the .out (the emulator on the assembly when the project says
//  emulateOnly), Emulate always the emulator, Verify both; a program for this Mac on a terminal
// =============================================================================
- (void)runChosen:(GRAfterBuild)how
{
    Model::GrideProjectPtr p = [self runProject];
    if (!self.session.workspace || !p || self.session.isRunning || self.session.verifying) return;
    Settings::GrideSettingsPtr s = self.session.settings;
    Core::Target t = [self currentTarget];
    Model::BuildConfigurationPtr cfg = p->GetConfiguration(GRStd([self currentConfig]), t);
    std::string program = Services::BuildEngine::ProgramPath(p, cfg);
    Settings::ToolchainSettingsPtr tc = s->Toolchain(t);
    bool emulated = Services::RideToolchain::IsEmulated(t);

    [self.outputView showSource:@"Build"];
    [self showPane:@"Output"];
    const std::vector<std::string>& args = p->ProgramArgs;
    bool shalimar = false;
    for (const std::string& f : p->AllFiles())
        if (s->DetectLanguage(f, p->Lang) == Core::Language::Shalimar) shalimar = true;
    std::string cmd, shown;
    if (emulated)
    {
        if (how == GRAfterRun && p->EmulateOnly) how = GRAfterEmulate;
        std::string sim = tc->ResolveFor("simulator", tc->Simulator);
        std::string vm = tc->ResolveFor("emulator", tc->Emulator);
        std::string out = Services::RideToolchain::TiProgramOf(program);
        std::string asmDir = p->ToAbsolute(cfg->ObjDir);       // the assembly the build wrote, which vm6747 runs
        std::string shmrt = shalimar ? Services::RideToolchain::FindShalimarRuntimeDir(tc->ToolDir, Settings::ToolchainSettings::RideRoot,
                                                                                     cfg->IsDebug())
                                     : std::string();
        std::string missing = Services::RideToolchain::SimulationMissing(sim, out);
        if (how == GRAfterVerify)
        {
            [self setStatus:[NSString stringWithFormat:@"Verifying %@ on vm6747 and sim6747...", GRNS(Platform::Paths::FileName(out))]];
            [self.session startVerify:Services::RideToolchain::EmulateCommand(vm, asmDir, shalimar, std::vector<std::string>(), shmrt)
                             simulate:Services::RideToolchain::SimulateCommand(sim, out)
                              workDir:p->ProjectDir()
                              outName:Platform::Paths::FileName(out)
                              missing:missing
                         hasArguments:!args.empty()];
            return;
        }
        if (how == GRAfterEmulate)
        {
            if (!Platform::Paths::IsFile(vm))
            {
                [self.outputView append:GRNS("no " + tc->Emulator + " - the emulator runs the assembly (looked in " +
                                             tc->LookedIn(tc->Emulator) + ")") source:@"Build"];
                [self setStatus:@"no emulator"];
                return;
            }
            cmd = Services::RideToolchain::EmulateCommand(vm, asmDir, shalimar, args, shmrt);
            shown = "$ " + Platform::Paths::FileNameWithoutExtension(vm) + " " + Core::PathUtil::MakeRelative(p->ProjectDir(), asmDir);
        }
        else
        {
            if (!missing.empty()) { [self.outputView append:GRNS(missing) source:@"Build"]; [self setStatus:GRNS(missing)]; return; }
            if (!args.empty())
                [self.outputView append:@"[run] the simulator takes no command line - Emulate on vm6747 hands them over" source:@"Build"];
            cmd = Services::RideToolchain::SimulateCommand(sim, out);
            shown = Services::RideToolchain::RunnerLine(true, program);
        }
    }
    else
    {
        if (tc->RunCmd.empty() || !Services::RideToolchain::RunsHere(Services::RideTool::C90, t))
        {
            std::string why = Services::RideToolchain::WhyNotRun(Services::RideTool::C90, t);
            if (why.empty()) why = Core::TargetInfo::Name(t) + " programs cannot run on this Mac - copy " + program + " to the target";
            [self.outputView append:GRNS(why) source:@"Build"];
            [self setStatus:GRNS(why)];
            return;
        }
        if (!Platform::Paths::IsFile(program))
        {
            [self.outputView append:GRNS("no " + program + " - build it first") source:@"Build"];
            [self setStatus:@"nothing to run"];
            return;
        }
        cmd = Services::RideToolchain::LaunchCommand(program, false, args, std::string(), std::string());
        shown = "$ " + Core::PathUtil::MakeRelative(p->ProjectDir(), program) + (args.empty() ? std::string() : " " + Core::StringUtil::Join(" ", args));
    }
    [self.outputView append:GRNS(shown) source:@"Build"];
    // a program for this Mac runs on a terminal, so its output comes as it is written; the C6747 runners on pipes
    if (![self.session startProgram:cmd workDir:p->ProjectDir() environment:tc->EnvironmentVars onTerminal:!emulated])
    {
        [self setStatus:@"The program could not be started"];
        return;
    }
    self.outputView.echoInput = YES;
    self.outputView.inputEnabled = YES;
    [self setStatus:[NSString stringWithFormat:@"Running %@ - type its input below the output, Stop ends it",
                                               GRNS(Platform::Paths::FileName(program))]];
}

- (void)stopProgram
{
    if (!self.session.isRunning) return;
    [self setStatus:@"Stopping the program..."];
    [self.session stopProgram];
}

- (void)sessionRunOutput:(NSString*)line { [self.outputView append:line source:@"Build"]; }

- (void)sessionRunExited:(int)status stopped:(BOOL)stopped
{
    self.outputView.inputEnabled = NO;
    [self.outputView append:@"" source:@"Build"];
    [self.outputView append:stopped ? @"[stopped]" : [NSString stringWithFormat:@"[program returned %d]", status] source:@"Build"];
    [self setStatus:stopped ? @"The program was stopped" : [NSString stringWithFormat:@"The program returned %d", status]];
}

// =============================================================================
//  the debugger (GRIDEd's LaunchDebugger: RIDE's engine for the build, the placeholder with the reason)
// =============================================================================
// what the emulator debugs: the one assembly file of a one-file program, else the object folder of them
- (std::string)assemblyProgramFor:(const Model::GrideProjectPtr&)p config:(const Model::BuildConfigurationPtr&)cfg
                        toolchain:(const Settings::ToolchainSettingsPtr&)tc
{
    std::string objDir = p->ToAbsolute(cfg->ObjDir);
    if (Platform::Paths::IsDirectory(objDir))
    {
        std::vector<std::string> asms;
        for (const std::string& f : Platform::Paths::ListFiles(objDir))
            if (Core::StringUtil::EndsWithIgnoreCase(f, tc->AsmExt)) asms.push_back(Platform::Paths::Combine(objDir, f));
        if (asms.size() == 1) return asms[0];
    }
    return objDir;
}

- (void)debugStepInto
{
    if (self.session.isInBreak) self.session.debugger->StepInto();
    else [self debugStartAtEntry];
}

- (void)debugStepOver
{
    if (self.session.isInBreak) self.session.debugger->StepOver();
    else [self debugStartAtEntry];
}

// F11 or F10 with nothing debugged: the build, then the program started and stopped at main's first line
- (void)debugStartAtEntry
{
    if (self.session.isDebugging) return;
    if (![self ensureProgram]) return;
    Model::GrideProjectPtr p = [self runProject];
    if (!self.session.workspace || !p) return;
    self.afterBuild = GRAfterDebugAtEntry;
    if (![self startBuild:Services::BuildAction::Build projects:self.session.workspace->BuildOrderFor(p) singleFile:nil])
        self.afterBuild = GRAfterNothing;
}

- (void)launchDebugger:(NSString*)program { [self launchDebugger:program atEntry:NO]; }

- (void)launchDebugger:(NSString*)program atEntry:(BOOL)atEntry
{
    NSString* file = nil;
    NSInteger line = 1;
    Model::GrideProjectPtr p = self.session.workspace ? [self runProject] : Model::GrideProjectPtr();
    if (![self findEntryPoint:p file:&file line:&line])
    {
        GRShowMessage(self.window, @"Debug", @"No main() found and no file is open.");
        return;
    }
    [self.outputView clear:@"Debug"];
    [self.outputView showSource:@"Debug"];
    [self showPane:@"Output"];
    self.programLineLast = NO;
    Settings::GrideSettingsPtr s = self.session.settings;
    std::shared_ptr<Services::DebugEngine> engine = std::make_shared<Services::PlaceholderDebugger>();
    if (p)
    {
        Core::Target t = [self currentTarget];
        Model::BuildConfigurationPtr cfg = p->GetConfiguration(GRStd([self currentConfig]), t);
        Settings::ToolchainSettingsPtr tc = s->Toolchain(t);
        bool shalimar = false;
        for (const std::string& f : p->AllFiles())
            if (s->DetectLanguage(f, p->Lang) == Core::Language::Shalimar) shalimar = true;
        std::string shmrt = shalimar ? Services::RideToolchain::FindShalimarRuntimeDir(tc->ToolDir, Settings::ToolchainSettings::RideRoot,
                                                                                     cfg->IsDebug())
                                     : std::string();
        Services::DebugLaunchPtr launch = Services::DebuggerChoice::For(p, cfg, tc, s, GRStd(program),
                                                                        [self assemblyProgramFor:p config:cfg toolchain:tc], shalimar, shmrt);
        if (!launch || !launch->Possible())
            [self.outputView append:GRNS((launch ? launch->WhyNot : std::string("nothing can debug this build")) +
                                         " - the placeholder stands in") source:@"Debug"];
        else if (launch->Kind == Services::DebuggerKind::Shalimar) engine = std::make_shared<Services::ShalimarSession>(launch);
        else engine = std::make_shared<Services::ProcessDebugger>(launch);
    }
    [self.session syncBreakpoints];
    [self.session attachDebugger:engine];
    if (p) engine->SetSourceFiles(p->AllFiles());
    // the watches kept in the pane go into the new engine (GRIDEd's WatchPane::UpdateFrom)
    for (NSString* x in self.watchView.expressions) engine->AddWatch(GRStd(x));
    [self.localsView clearView];
    [self.stackView clearView];
    [self setArea:self.debugItem shown:YES];
    [self showPane:@"Watch"];
    [self showPane:@"CallStack"];
    [self showPane:@"Locals"];
    // the program's input line below the output when its debugger hands input over; what is typed comes back
    // as the program's console echoes it
    if (engine->TakesInput() && !self.session.isRunning)
    {
        self.outputView.echoInput = NO;
        [self.outputView showInput:YES focus:NO];
    }
    engine->StopAtEntry = atEntry;
    engine->Start(GRStd(program), [self currentTarget], GRStd(file), (int)line);
    [self updateToolbarForDebugging];
}

- (void)debugStop
{
    self.session.debugger->Stop();
    [self updateToolbarForDebugging];
}

- (void)debugRestart
{
    self.session.debugger->Stop();
    Model::GrideProjectPtr p = self.session.workspace ? [self runProject] : Model::GrideProjectPtr();
    if (p)
        [self launchDebugger:GRNS(Services::BuildEngine::ProgramPath(p, p->GetConfiguration(GRStd([self currentConfig]), [self currentTarget])))];
}

- (void)debugRunToCursor
{
    GRSourceDocument* d = self.editorArea.activeDocument;
    GRCodeView* v = self.editorArea.activeCodeView;
    if (d != nil && d.path != nil && v != nil) self.session.debugger->RunToCursor(GRStd(d.path), (int)[v currentLine]);
}

- (void)debugToggleBreakpoint
{
    GRCodeView* v = self.editorArea.activeCodeView;
    if (v != nil) [v toggleBreakpoint:0];
}

- (void)debugDeleteAllBreakpoints
{
    [self.session deleteAllBreakpoints];
    [self breakpointsChanged];
}

- (void)debugConnect
{
    Settings::ToolchainSettingsPtr tc = self.session.settings->Toolchain(Core::Target::C6747);
    std::string vm = tc->Resolve(tc->Emulator);
    std::string sim = tc->Resolve(tc->Simulator);
    NSMutableString* sb = [NSMutableString string];
    [sb appendString:@"VM6747 connection\n=================\n\n"];
    [sb appendFormat:@"Emulator  : %@%@\n", GRNS(vm), Platform::Paths::IsFile(vm) ? @"  (found)" : @"  (not installed yet)"];
    [sb appendFormat:@"Simulator : %@%@\n", GRNS(sim), Platform::Paths::IsFile(sim) ? @"  (found)" : @"  (not installed yet)"];
    [sb appendFormat:@"Run cmd   : %@\n", GRNS(tc->RunCmd)];
    [sb appendFormat:@"Emulate   : %@\n", GRNS(tc->EmulateCmd)];
    [sb appendFormat:@"Debug cmd : %@\n\n", GRNS(tc->DebugCmd)];
    [sb appendFormat:@"Active debugger: %@\n\n", GRNS(self.session.debugger->Name())];
    [sb appendString:@"GRIDEm talks to debuggers through the DebugEngine class (Services/Debugger.h): sim6747 and vm6747 "
                      "through ProcessDebugger and toolchain.json's session definitions (Services/GdbDebugger.h), as GRIDEd does.\n"];
    GRTextSheet* sheet = [[GRTextSheet alloc] initWithTitle:@"Connect to VM6747" text:sb mono:YES];
    [sheet beginOn:self.window completion:^(BOOL ok) { (void)ok; }];
}

- (void)debugLoadProgram
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Load Program";
    panel.message = @"A program to debug: a .out for the C6747, or a program for this Mac";
    __weak GRMainWindowController* weakSelf = self;
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK) [weakSelf launchDebugger:panel.URL.path];
    }];
}

// =============================================================================
//  the debugger's events (main queue)
// =============================================================================
- (void)sessionDebugState:(Services::DebugState)state
{
    if (state == Services::DebugState::Running)
    {
        [self clearExecutionMarkers];
        [self setStatus:@"Running..."];
    }
    if (state == Services::DebugState::Idle || state == Services::DebugState::Terminated)
    {
        if (!self.outputView.echoInput && !self.session.isRunning)
        {
            [self.outputView showInput:NO focus:NO];
            self.outputView.echoInput = YES;
        }
        [self clearExecutionMarkers];
        [self.localsView clearView];
        [self.watchView clearView];
        [self.stackView clearView];
        [self.registersView clearView];
        [self.memoryView clearView];
        [self.disassemblyView clearView];
        std::shared_ptr<Services::DebugEngine> d = self.session.debugger;
        if (d && d->Exited()) [self setStatus:[NSString stringWithFormat:@"Debugging ended - the program returned %d", d->ExitStatus()]];
        else [self setStatus:@"Debugging ended"];
    }
    [self updateToolbarForDebugging];
    [self updateTitle];
}

- (void)sessionDebugStopped:(const Services::StopInfoPtr&)info
{
    [self clearExecutionMarkers];
    // a stop in a file that is not on this machine is said in the status line, not with a message box
    if (info && Platform::Paths::IsFile(info->File))
    {
        GRSourceDocument* d = [self openDocument:GRNS(info->File) line:0];
        if (d != nil)
        {
            [self.editorArea showExecutionLine:info->Line inPath:d.path];
            [self.editorArea.activeCodeView goToLine:info->Line];
        }
    }
    // a new stop: what the panes saw at the last one is what a value is compared with (RIDE's NewStop)
    [self.watchView newStop];
    [self.localsView newStop];
    [self refreshDebugPanes];
    if (info)
        [self setStatus:[NSString stringWithFormat:@"%@:%d%@", GRNS(Platform::Paths::FileName(info->File)), info->Line,
                                                   info->Function.empty() ? @"" : [@" in " stringByAppendingString:GRNS(info->Function)]]];
    // the stop brings the window forward (GRIDEd's Activate); another app in front is asked for attention
    [self.window makeKeyAndOrderFront:nil];
    if (!NSApp.isActive) [NSApp requestUserAttention:NSInformationalRequest];
}

- (void)sessionDebugOutput:(NSString*)line
{
    [self.outputView append:line source:@"Debug"];
    self.programLineLast = NO;
}

// the rest of a line the program had not finished goes on that line while nothing else was written after it
- (void)sessionDebugProgramOutput:(NSString*)line
{
    std::string rest;
    if (Services::DebugEngine::Continues(GRStd(line), rest) && self.programLineLast)
        [self.outputView continueLine:GRNS(rest) source:@"Debug"];
    else [self.outputView append:GRNS(rest) source:@"Debug"];
    self.programLineLast = YES;
}

// =============================================================================
//  the debug panes (GRIDEd's DebugPaneBase::UpdateFrom for each)
// =============================================================================
- (void)refreshDebugPanes
{
    std::shared_ptr<Services::DebugEngine> d = self.session.debugger;
    if (!d) return;
    BOOL paused = d->State() == Services::DebugState::Break;
    BOOL atStop = d->CurrentFrame() == 0;
    NSString* backend = GRNS(d->Backend());

    // Locals: the stop line, and what frame is looked at
    std::vector<Services::DebugVariable> vars;
    if (paused)
    {
        Services::StopInfoPtr loc = d->Location();
        std::string head = loc ? Services::DebugTranscript::StopLine(loc->File, loc->Line, loc->Function) : std::string();
        std::string frameName = loc ? loc->Function : std::string();
        std::vector<Services::DebugFrame> frames;
        if (d->CurrentFrame() > 0)
        {
            frames = d->CallStack();
            if (d->CurrentFrame() < (int)frames.size())
            {
                head += "    -    " + Services::DebugTranscript::LookingAt(frames[(size_t)d->CurrentFrame()]);
                frameName = frames[(size_t)d->CurrentFrame()].Function;
            }
        }
        [self.localsView setStopLine:GRNS(head)];
        vars = d->Locals();
        std::string why = vars.empty() ? d->WhyNoLocals() : std::string();
        [self.localsView setVariables:vars frameName:GRNS(frameName) atStop:atStop whyNone:GRNS(why)];
        // Call Stack
        if (frames.empty()) frames = d->CallStack();
        [self.stackView setFrames:frames current:d->CurrentFrame()];
        [self.registersView setRegisters:d->Registers() backend:backend];
    }
    else
    {
        [self.localsView setStopLine:@""];
        [self.localsView setVariables:std::vector<Services::DebugVariable>() frameName:@"" atStop:NO whyNone:@""];
        [self.stackView setFrames:std::vector<Services::DebugFrame>() current:0];
    }
    [self.watchView setWatches:d->Watches() paused:paused atStop:atStop];
    [self.memoryView refreshPaused:paused locals:vars backend:backend];

    // Disassembly shows the compiler's assembly of the current source
    Services::StopInfoPtr loc = d->Location();
    Model::WorkspacePtr w = self.session.workspace;
    if (paused && loc && w)
    {
        Model::GrideProjectPtr p = w->FindProjectForFile(loc->File);
        if (p)
        {
            Model::BuildConfigurationPtr cfg = p->GetConfiguration(GRStd([self currentConfig]), [self currentTarget]);
            [self.disassemblyView showFile:GRNS(loc->File) assembly:GRNS(self.session.engine->AsmFileFor(p, cfg, loc->File))];
        }
    }
}

- (void)clearExecutionMarkers { [self.editorArea showExecutionLine:0 inPath:nil]; }

// RIDE's Ctrl-Up / Ctrl-Down: the frame looked at - its variables in Locals, the editor on its line
- (void)lookAtFrame:(NSInteger)index
{
    std::shared_ptr<Services::DebugEngine> d = self.session.debugger;
    if (!self.session.isInBreak) return;
    if (index < 0) { [self setStatus:@"at the stop"]; return; }
    std::vector<Services::DebugFrame> frames = d->CallStack();
    if (index >= (NSInteger)frames.size())
    {
        [self setStatus:frames.size() <= 1 ? @"nothing called this" : @"the stack ends here"];
        return;
    }
    if (index == d->CurrentFrame() && index == 0) { [self setStatus:@"at the stop"]; return; }
    if (!d->SelectFrame((int)index))
    {
        std::string why = d->WhyNoStackWalk();
        [self setStatus:GRNS(why.empty() ? d->Name() + " cannot look at another frame" : why)];
        return;
    }
    const Services::DebugFrame& f = frames[(size_t)index];
    if (!f.File.empty() && f.Line > 0 && Platform::Paths::IsFile(f.File))
    {
        [self openDocument:GRNS(f.File) line:0];
        [self.editorArea.activeCodeView goToLine:f.Line];
    }
    [self refreshDebugPanes];
    [self setStatus:index == 0 ? @"back at the stop" : GRNS(Services::DebugTranscript::LookingAt(f))];
}

- (void)variableSet:(NSString*)name value:(NSString*)value
{
    if (!self.session.isInBreak) return;
    std::string said;
    if (!self.session.debugger->SetVariable(GRStd(name), GRStd(value), said))
    {
        self.programLineLast = NO;
        [self.outputView append:[NSString stringWithFormat:@"cannot set %@: %@", name, GRNS(said)] source:@"Debug"];
        [self setStatus:[NSString stringWithFormat:@"cannot set %@ - %@", name, GRNS(said)]];
    }
    else [self setStatus:[NSString stringWithFormat:@"%@ = %@", name, value]];
    [self refreshDebugPanes];
}

- (void)wireDebugPanes
{
    __weak GRMainWindowController* weakSelf = self;
    self.stackView.frameChosen = ^(NSInteger index) { [weakSelf lookAtFrame:index]; };
    self.localsView.setRequested = ^(NSString* name, NSString* value) { [weakSelf variableSet:name value:value]; };
    self.watchView.watchAdded = ^(NSString* expression) {
        GRMainWindowController* me = weakSelf;
        if (me == nil) return;
        std::shared_ptr<Services::DebugEngine> d = me.session.debugger;
        if (d) d->AddWatch(GRStd(expression));
        [me.watchView setWatches:d->Watches() paused:me.session.isInBreak atStop:d->CurrentFrame() == 0];
    };
    self.watchView.watchChanged = ^(NSInteger index, NSString* expression) {
        GRMainWindowController* me = weakSelf;
        if (me == nil) return;
        std::shared_ptr<Services::DebugEngine> d = me.session.debugger;
        if (d) d->SetWatch((int)index, GRStd(expression));
        [me.watchView setWatches:d->Watches() paused:me.session.isInBreak atStop:d->CurrentFrame() == 0];
    };
    self.watchView.watchRemoved = ^(NSInteger index) {
        GRMainWindowController* me = weakSelf;
        if (me == nil) return;
        std::shared_ptr<Services::DebugEngine> d = me.session.debugger;
        if (d) d->RemoveWatch((int)index);
        [me.watchView setWatches:d->Watches() paused:me.session.isInBreak atStop:d->CurrentFrame() == 0];
    };
    self.memoryView.readMemory = ^NSData*(unsigned long long address, NSInteger length) {
        GRMainWindowController* me = weakSelf;
        if (me == nil || !me.session.isInBreak) return nil;
        std::vector<unsigned char> bytes = me.session.debugger->ReadMemory(address, (int)length);
        if (bytes.empty()) return nil;
        return [NSData dataWithBytes:bytes.data() length:bytes.size()];
    };
    self.localsView.keysLine = [self keysLine];
}

// ---- breakpoints -----------------------------------------------------------------------------
- (void)breakpointsChanged
{
    [self.session syncBreakpoints];
    [self.editorArea refreshGutters];
    [self refreshBreakpointsPane];
    std::shared_ptr<Services::DebugEngine> d = self.session.debugger;
    if (d && d->IsActive()) d->BreakpointsChanged();     // toggling while stopped resends them (RIDE)
}

- (void)refreshBreakpointsPane { [self.breakpointsNavigator fill:[self.session breakpointTable]]; }
@end
