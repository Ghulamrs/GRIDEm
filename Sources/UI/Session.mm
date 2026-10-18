// =============================================================================
//  Session.mm - see Session.h
// =============================================================================
#import "Session.h"
#import "Common.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <atomic>
#include <map>
#include <thread>

using namespace GRIDEm;

static NSString* SessionNS(const std::string& s)
{
    NSString* r = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    return r != nil ? r : @"";
}

static std::string SessionStd(NSString* s)
{
    if (s == nil) return std::string();
    const char* c = [s UTF8String];
    return c != nullptr ? std::string(c) : std::string();
}

typedef std::map<std::string, std::shared_ptr<std::set<int> >, Core::CaseInsensitiveLess> BreakpointSets;

@implementation GRSession
{
    Settings::GrideSettingsPtr _settings;
    Model::WorkspacePtr _workspace;
    std::unique_ptr<Services::BuildEngine> _engine;
    std::unique_ptr<Services::ProgramRunner> _runner;
    std::shared_ptr<Services::DebugEngine> _debugger;
    BreakpointSets _sets;
    Services::BreakpointMapPtr _map;
    std::atomic<unsigned long> _generation;    // which engine the debug events belong to
    std::atomic<bool> _closing;
    std::atomic<bool> _verifying;
    Model::DiskLocks _looseLocks;              // files of no project made writable
}

- (instancetype)init
{
    self = [super init];
    if (self == nil) return nil;
    _generation = 0;
    _closing = false;
    _verifying = false;
    _map = std::make_shared<Services::BreakpointMap>();

    // a first run: no settings file anywhere yet
    _firstRun = !Platform::Paths::Exists(Settings::SettingsManager::UserPath())
                && !Platform::Paths::Exists(Settings::SettingsManager::PortablePath());
    std::string warning;
    _settings = Settings::SettingsManager::Load(warning);
    if (_firstRun) _settings->KeySet = "macos";        // GRIDEm's own default; GRIDEd's is "visualstudio"
    _loadWarning = warning.empty() ? nil : SessionNS(warning);
    _settings->PublishPaths();

    __weak GRSession* weakSelf = self;
    _engine.reset(new Services::BuildEngine(_settings));
    _engine->OnOutput = [weakSelf](const std::string& line) {
        NSString* s = SessionNS(line);
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing) [me.delegate sessionBuildOutput:s];
        });
    };
    _engine->OnMessage = [weakSelf](const Services::BuildMessagePtr& m) {
        Services::BuildMessagePtr copy = m;
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing) [me.delegate sessionBuildMessage:copy];
        });
    };
    _engine->OnFinished = [weakSelf](const Services::BuildSummaryPtr& s) {
        Services::BuildSummaryPtr copy = s;
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing) [me.delegate sessionBuildFinished:copy];
        });
    };
    // the native-tools question: the build thread waits for the answer given on the main thread
    _engine->Asker = [weakSelf](const std::string& question) -> bool {
        GRSession* me = weakSelf;
        if (me == nil || me->_closing) return false;
        if ([NSThread isMainThread]) return [me.delegate sessionAskYesNo:SessionNS(question)];
        __block BOOL yes = NO;
        NSString* q = SessionNS(question);
        dispatch_sync(dispatch_get_main_queue(), ^{
            GRSession* again = weakSelf;
            if (again != nil && !again->_closing) yes = [again.delegate sessionAskYesNo:q];
        });
        return yes;
    };

    _runner.reset(new Services::ProgramRunner());
    _runner->OnOutput = [weakSelf](const std::string& line) {
        NSString* s = SessionNS(line);
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing) [me.delegate sessionRunOutput:s];
        });
    };
    _runner->OnExited = [weakSelf](int status, bool stopped) {
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing) [me.delegate sessionRunExited:status stopped:stopped];
        });
    };

    [self attachDebugger:std::make_shared<Services::PlaceholderDebugger>()];
    return self;
}

// ---- settings ---------------------------------------------------------------------
- (Settings::GrideSettingsPtr)settings { return _settings; }

- (void)replaceSettings:(const Settings::GrideSettingsPtr&)settings
{
    if (!settings) return;
    _settings = settings;
    _settings->PublishPaths();
    _engine->SetCurrentSettings(_settings);
    [self saveSettings];
}

- (void)saveSettings
{
    try { Settings::SettingsManager::Save(_settings); }
    catch (const std::exception& ex) { NSLog(@"GRIDEm: cannot save the settings: %s", ex.what()); }
}

// ---- the workspace ------------------------------------------------------------------
- (Model::WorkspacePtr)workspace { return _workspace; }

- (void)setWorkspace:(const Model::WorkspacePtr&)workspace
{
    if (_workspace && _workspace != workspace) _workspace->Locks.RelockAll();   // closed: read-only again
    _workspace = workspace;
    _sets.clear();
    if (_workspace)
        for (const std::pair<std::string, std::set<int> >& kv : _workspace->Breakpoints.Items())
            _sets[kv.first] = std::make_shared<std::set<int> >(kv.second);
    [self refillMap];
}

- (BOOL)mayWriteFile:(NSString*)path
{
    std::string abs = SessionStd(path);
    if (Model::DiskLocks::IsWritable(abs)) return YES;
    __weak GRSession* weakSelf = self;
    std::function<bool()> ask = [weakSelf, path]() -> bool {
        GRSession* me = weakSelf;
        NSString* q = [@"File on the disk is read only, are you sure you need to update?\n\n" stringByAppendingString:path];
        return me != nil && [me.delegate sessionAskYesNo:q];
    };
    if (_workspace) return _workspace->PrepareWrite(abs, ask) ? YES : NO;
    return _looseLocks.Prepare(abs, abs, ask) ? YES : NO;
}

- (NSString*)saveWorkspace
{
    if (!_workspace) return nil;
    [self syncBreakpoints];
    try
    {
        _workspace->SaveProjects();
        if (!_workspace->IsTemporary && _workspace->HasChanges()) _workspace->Save();
    }
    catch (const std::exception& ex) { return SessionNS(ex.what()); }
    return nil;
}

// ---- breakpoints ----------------------------------------------------------------------
- (std::shared_ptr<std::set<int> >)breakpointsFor:(NSString*)path
{
    std::string p = SessionStd(path);
    BreakpointSets::iterator it = _sets.find(p);
    if (it != _sets.end()) return it->second;
    std::shared_ptr<std::set<int> > s = std::make_shared<std::set<int> >();
    _sets[p] = s;
    return s;
}

- (void)moveBreakpointsFrom:(NSString*)oldPath to:(NSString*)newPath
{
    BreakpointSets::iterator it = _sets.find(SessionStd(oldPath));
    if (it == _sets.end()) return;
    std::shared_ptr<std::set<int> > s = it->second;
    _sets.erase(it);
    _sets[SessionStd(newPath)] = s;
    if (_workspace) _workspace->Breakpoints.Remove(SessionStd(oldPath));
    [self syncBreakpoints];
}

- (void)refillMap
{
    _map->clear();
    for (BreakpointSets::const_iterator it = _sets.begin(); it != _sets.end(); ++it)
        if (it->second && !it->second->empty()) (*_map)[it->first] = *it->second;
}

- (void)syncBreakpoints
{
    if (_workspace)
    {
        for (BreakpointSets::const_iterator it = _sets.begin(); it != _sets.end(); ++it)
        {
            if (it->second && !it->second->empty()) _workspace->Breakpoints.Set(it->first, *it->second);
            else _workspace->Breakpoints.Remove(it->first);
        }
        _workspace->IsDirty = true;
    }
    [self refillMap];
}

- (Model::BreakpointTable)breakpointTable
{
    Model::BreakpointTable t;
    for (BreakpointSets::const_iterator it = _sets.begin(); it != _sets.end(); ++it)
        if (it->second && !it->second->empty()) t.Set(it->first, *it->second);
    return t;
}

- (void)deleteAllBreakpoints
{
    for (BreakpointSets::iterator it = _sets.begin(); it != _sets.end(); ++it)
        if (it->second) it->second->clear();
    [self syncBreakpoints];
}

- (void)removeBreakpoint:(NSString*)path line:(NSInteger)line
{
    BreakpointSets::iterator it = _sets.find(SessionStd(path));
    if (it == _sets.end() || !it->second) return;
    it->second->erase((int)line);
    [self syncBreakpoints];
}

- (Services::BreakpointMapPtr)breakpointMap { return _map; }

// ---- the build -------------------------------------------------------------------------
- (Services::BuildEngine*)engine { return _engine.get(); }

- (BOOL)startBuild:(const Services::BuildRequestPtr&)request
{
    if (_engine->IsBuilding()) return NO;
    return _engine->StartAsync(request) ? YES : NO;
}

- (BOOL)startBatch:(const std::vector<Services::BuildRequestPtr>&)requests
{
    if (_engine->IsBuilding()) return NO;
    return _engine->StartBatchAsync(requests) ? YES : NO;
}

- (BOOL)isBuilding { return _engine->IsBuilding() ? YES : NO; }
- (void)cancelBuild { _engine->Cancel(); }

// ---- a program in the Output pane -------------------------------------------------------
- (BOOL)startProgram:(const std::string&)commandLine workDir:(const std::string&)workDir
         environment:(const std::vector<std::string>&)environment onTerminal:(BOOL)onTerminal
{
    if (_runner->IsRunning()) return NO;
    return _runner->Start(commandLine, workDir, environment, 0, onTerminal ? true : false) ? YES : NO;
}

- (BOOL)writeToProgram:(NSString*)line
{
    if (!_runner->IsRunning()) return NO;
    return _runner->Write(SessionStd(line)) ? YES : NO;
}

- (void)stopProgram { if (_runner->IsRunning()) _runner->Stop(); }
- (BOOL)isRunning { return _runner->IsRunning() ? YES : NO; }
- (BOOL)verifying { return _verifying ? YES : NO; }

- (void)startVerify:(const std::string&)emulateCommand simulate:(const std::string&)simulateCommand
            workDir:(const std::string&)workDir outName:(const std::string&)outName
            missing:(const std::string&)missing hasArguments:(BOOL)hasArguments
{
    if (_verifying) return;
    _verifying = true;
    __weak GRSession* weakSelf = self;
    bool args = hasArguments ? true : false;
    std::thread job([weakSelf, emulateCommand, simulateCommand, workDir, outName, missing, args]() {
        std::function<void(const std::string&)> say = [weakSelf](const std::string& line) {
            NSString* s = SessionNS(line);
            dispatch_async(dispatch_get_main_queue(), ^{
                GRSession* me = weakSelf;
                if (me != nil && !me->_closing) [me.delegate sessionRunOutput:s];
            });
        };
        int status = Services::ProgramRunner::Verify(emulateCommand, simulateCommand, workDir, 65001, outName,
                                                     missing, args, say);
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me == nil) return;
            me->_verifying = false;
            if (!me->_closing) [me.delegate sessionRunExited:status stopped:NO];
        });
    });
    job.detach();
}

// ---- the debugger -------------------------------------------------------------------------
- (std::shared_ptr<Services::DebugEngine>)debugger { return _debugger; }

- (void)attachDebugger:(const std::shared_ptr<Services::DebugEngine>&)engine
{
    if (!engine) return;
    std::shared_ptr<Services::DebugEngine> old = _debugger;
    unsigned long generation = ++_generation;
    _debugger = engine;
    if (old && old != engine)
    {
        old->OnStateChanged = nullptr;
        old->OnStopped = nullptr;
        old->OnOutput = nullptr;
        old->OnProgramOutput = nullptr;
        if (old->IsActive()) old->Stop();
    }
    __weak GRSession* weakSelf = self;
    engine->OnStateChanged = [weakSelf, generation](Services::DebugState s) {
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing && me->_generation == generation) [me.delegate sessionDebugState:s];
        });
    };
    engine->OnStopped = [weakSelf, generation](const Services::StopInfoPtr& info) {
        Services::StopInfoPtr copy = info;
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing && me->_generation == generation) [me.delegate sessionDebugStopped:copy];
        });
    };
    engine->OnOutput = [weakSelf, generation](const std::string& line) {
        NSString* s = SessionNS(line);
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing && me->_generation == generation) [me.delegate sessionDebugOutput:s];
        });
    };
    engine->OnProgramOutput = [weakSelf, generation](const std::string& line) {
        NSString* s = SessionNS(line);
        dispatch_async(dispatch_get_main_queue(), ^{
            GRSession* me = weakSelf;
            if (me != nil && !me->_closing && me->_generation == generation) [me.delegate sessionDebugProgramOutput:s];
        });
    };
    engine->SetBreakpoints(_map);
}

- (BOOL)isDebugging { return _debugger && _debugger->IsActive() ? YES : NO; }
- (BOOL)isInBreak { return _debugger && _debugger->State() == Services::DebugState::Break ? YES : NO; }

// ---- leaving --------------------------------------------------------------------------------
- (void)shutDown
{
    _closing = true;
    if (_engine->IsBuilding()) _engine->Cancel();
    _engine->Wait();
    if (_runner->IsRunning()) _runner->Stop();
    if (_debugger && _debugger->IsActive()) _debugger->Stop();
    if (_workspace) _workspace->Locks.RelockAll();
    _looseLocks.RelockAll();
}
@end
