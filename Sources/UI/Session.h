// =============================================================================
//  Session.h - GRSession: everything the window works on that is not a view
//  (the state part of GRIDEd's MainForm: settings, workspace, BuildEngine,
//  ProgramRunner, DebugEngine, the breakpoint map)
//
//  The services call their handlers on threads of their own; GRSession moves
//  every one of them onto the main queue before the window hears of it
//  (GRIDEd's BeginInvoke), so no view ever touches a service directly and
//  every delegate method below is called on the main thread.
//
//  Breakpoints: one std::set<int> per file, held by shared_ptr, shared by the
//  editor's gutter (GRSourceDocument::breakpoints), written into the
//  workspace's .grws (BreakpointTable) and handed to the debugger
//  (BreakpointMap) by -syncBreakpoints - GRIDEd's one SortedSet per file.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../Settings/GrideSettings.h"
#include "../Model/Workspace.h"
#include "../Services/BuildEngine.h"
#include "../Services/ProgramRunner.h"
#include "../Services/Debugger.h"
#include <memory>
#include <set>
#include <string>

@protocol GRSessionDelegate <NSObject>
// ---- the build ----
- (void)sessionBuildOutput:(NSString*)line;
- (void)sessionBuildMessage:(const GRIDEm::Services::BuildMessagePtr&)message;
- (void)sessionBuildFinished:(const GRIDEm::Services::BuildSummaryPtr&)summary;
// the native-tools question, asked from the build thread; answered here on the main thread
- (BOOL)sessionAskYesNo:(NSString*)question;
// ---- a program running in the Output pane (also Verify's lines) ----
- (void)sessionRunOutput:(NSString*)line;
- (void)sessionRunExited:(int)status stopped:(BOOL)stopped;
// ---- the debugger ----
- (void)sessionDebugState:(GRIDEm::Services::DebugState)state;
- (void)sessionDebugStopped:(const GRIDEm::Services::StopInfoPtr&)info;
- (void)sessionDebugOutput:(NSString*)line;           // the engine's own lines
- (void)sessionDebugProgramOutput:(NSString*)line;    // the program's lines, heard through the debugger
@end

@interface GRSession : NSObject

@property (nonatomic, weak) id<GRSessionDelegate> delegate;

// loads gride.settings.json (and toolchain.json first); a first run - no settings file yet - takes
// the Mac's key set. warning: what the load wanted said (nil when nothing)
- (instancetype)init;
@property (nonatomic, readonly, copy) NSString* loadWarning;
@property (nonatomic, readonly) BOOL firstRun;

// ---- settings ------------------------------------------------------------------
- (GRIDEm::Settings::GrideSettingsPtr)settings;
// the settings the Settings window returned: kept, published to the services, saved
- (void)replaceSettings:(const GRIDEm::Settings::GrideSettingsPtr&)settings;
- (void)saveSettings;

// ---- the workspace ---------------------------------------------------------------
- (GRIDEm::Model::WorkspacePtr)workspace;
// takes a workspace (or nullptr): its saved breakpoints become the shared sets
- (void)setWorkspace:(const GRIDEm::Model::WorkspacePtr&)workspace;
// writes the projects, and the .grws when something in it changed (not a temporary one);
// nil, or what went wrong
- (NSString*)saveWorkspace;
// a file read-only on disk is written only after "File on the disk is read only, are you sure you
// need to update?" - asked once per project (once per file for a file of no project) - and is made
// read-only again when its workspace closes or GRIDEm quits (Model::DiskLocks)
- (BOOL)mayWriteFile:(NSString*)path;

// ---- breakpoints -----------------------------------------------------------------
// the set of a file (made when it has none); every holder of it sees the same lines
- (std::shared_ptr<std::set<int> >)breakpointsFor:(NSString*)path;
// a file renamed: its breakpoints go with it
- (void)moveBreakpointsFrom:(NSString*)oldPath to:(NSString*)newPath;
// the sets into the workspace (marked dirty) and into the map the debugger reads
- (void)syncBreakpoints;
// every breakpoint of the session, path -> lines (the Breakpoints navigator)
- (GRIDEm::Model::BreakpointTable)breakpointTable;
- (void)deleteAllBreakpoints;
- (void)removeBreakpoint:(NSString*)path line:(NSInteger)line;
// the map handed to a debug engine (refilled in place by -syncBreakpoints)
- (GRIDEm::Services::BreakpointMapPtr)breakpointMap;

// ---- the build -------------------------------------------------------------------
- (GRIDEm::Services::BuildEngine*)engine;
- (BOOL)startBuild:(const GRIDEm::Services::BuildRequestPtr&)request;
- (BOOL)startBatch:(const std::vector<GRIDEm::Services::BuildRequestPtr>&)requests;
- (BOOL)isBuilding;
- (void)cancelBuild;

// ---- a program running in the Output pane -----------------------------------------
- (BOOL)startProgram:(const std::string&)commandLine workDir:(const std::string&)workDir
         environment:(const std::vector<std::string>&)environment onTerminal:(BOOL)onTerminal;
- (BOOL)writeToProgram:(NSString*)line;
- (void)stopProgram;
- (BOOL)isRunning;
// Verify: both runners captured on a thread of their own; the lines come as run output, the end
// as sessionRunExited (0 agree, 1 differ, 2 missing)
- (void)startVerify:(const std::string&)emulateCommand simulate:(const std::string&)simulateCommand
            workDir:(const std::string&)workDir outName:(const std::string&)outName
            missing:(const std::string&)missing hasArguments:(BOOL)hasArguments;
@property (nonatomic, readonly) BOOL verifying;

// ---- the debugger ----------------------------------------------------------------
// the engine the window talks to (a PlaceholderDebugger until a session starts)
- (std::shared_ptr<GRIDEm::Services::DebugEngine>)debugger;
// a new engine: the one before is stopped and let go; its events go to the delegate
- (void)attachDebugger:(const std::shared_ptr<GRIDEm::Services::DebugEngine>&)engine;
- (BOOL)isDebugging;
- (BOOL)isInBreak;

// ---- leaving -----------------------------------------------------------------------
// stops whatever runs; nothing asks the window afterwards
- (void)shutDown;
@end
