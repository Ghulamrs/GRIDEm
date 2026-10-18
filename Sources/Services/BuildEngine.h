// =============================================================================
//  BuildEngine.h - turns projects into programs with the configured toolchain
//  (GRIDEd's Services/BuildEngine.h: BuildAction, BuildRequest, BuildSummary,
//  BuildEngine)
//
//    source (.c/.cpp/.shl) --compile--> .s --assemble--> .obj --link--> program
//    source (.asm)                      ------assemble--> .obj
//
//  Every step is a command template from ToolchainSettings (its defaults from
//  toolchain.json), expanded by MacroExpander and run by ToolRunner on a worker
//  thread. The rules are RIDE's: a compiler per folder, a Shalimar program
//  compiled as one, the C6747 program through asm6x and lnk6x against RTS6x
//  with RIDE's own linker command file, the environment the compilers read, the
//  native-tools question, and the refusals (a cross target only reaches
//  assembly, a directory that cannot be written to). Each progress line names
//  the program that runs and the file it writes.
//  The parts that need Microsoft's cl, link.exe or a Debug build's CodeView
//  (clang) are Windows only: on the Mac those tools are not found, and the
//  build says so as GRIDEd says it of any missing tool - it never crashes.
//
//  The events are called on the build thread; the UI's Session moves them to
//  the main queue (the library has no GCD in it).
// =============================================================================
#pragma once
#include "BuildMessage.h"
#include "MacroExpander.h"
#include "RideToolchain.h"
#include "ToolRunner.h"
#include "../Model/GrideProject.h"
#include "../Model/Workspace.h"
#include "../Settings/GrideSettings.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace GRIDEm { namespace Services {

    enum class BuildAction { Build, Rebuild, Clean, CompileFile, AssembleOnly, LinkOnly };

    class BuildRequest
    {
    public:
        BuildRequest() : Config("Debug"), TargetKind(Core::Target::Windows), Action(BuildAction::Build) {}
        std::vector<Model::GrideProjectPtr> Projects;   // already in build order
        std::string Config;
        Core::Target TargetKind;
        BuildAction Action;
        std::string SingleFile;                         // for CompileFile ("" = none)
        std::string WorkspaceDir;
    };
    typedef std::shared_ptr<BuildRequest> BuildRequestPtr;

    class BuildSummary
    {
    public:
        BuildSummary();
        int Succeeded;
        int Failed;
        int UpToDate;
        int Skipped;
        bool Cancelled;
        double ElapsedSeconds;
        BuildAction Action;
        std::string Program;           // the last program built (or linked), for a run

        int Errors() const { return errors; }
        int Warnings() const { return warnings; }
        bool Ok() const { return Failed == 0 && !Cancelled; }
        // the first error of the build, where RIDE puts the caret
        BuildMessagePtr FirstError() const;

        // called from the tool output threads
        void Count(Severity s);
        void Count(const BuildMessagePtr& m);

    private:
        std::atomic<int> errors;
        std::atomic<int> warnings;
        mutable std::mutex firstLock;
        BuildMessagePtr firstError;
    };
    typedef std::shared_ptr<BuildSummary> BuildSummaryPtr;

    typedef std::function<void(const BuildSummaryPtr& s)> BuildFinishedHandler;
    // the front end's answer to a yes/no question asked from the build thread (the native-tools question)
    typedef std::function<bool(const std::string& question)> BuildQuestionHandler;

    class BuildEngine
    {
    public:
        explicit BuildEngine(const Settings::GrideSettingsPtr& settings);
        ~BuildEngine();                       // cancels a build still running and waits for it
        BuildEngine(const BuildEngine&) = delete;
        BuildEngine& operator=(const BuildEngine&) = delete;

        TextLineHandler OnOutput;             // build thread
        BuildMessageHandler OnMessage;        // build thread
        BuildFinishedHandler OnFinished;      // build thread
        // who answers the native-tools question; empty means nothing here can ask, and the build stands
        BuildQuestionHandler Asker;

        bool IsBuilding() const { return busy; }
        Settings::GrideSettingsPtr CurrentSettings() const { return settings; }
        void SetCurrentSettings(const Settings::GrideSettingsPtr& s) { settings = s; }

        // ---- public API ---------------------------------------------------
        bool StartAsync(const BuildRequestPtr& r);
        // several requests (Build > Batch Build) run one after another
        bool StartBatchAsync(const std::vector<BuildRequestPtr>& requests);
        // the same on the calling thread (GRIDEm-build); the summary when done, nullptr when busy
        BuildSummaryPtr RunBatch(const std::vector<BuildRequestPtr>& requests);
        void Cancel() { runner.Cancel(); }
        // waits for a build running on its thread to end
        void Wait();

        // full path of the linked program for a project/config/target
        static std::string ProgramPath(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg);
        // include files are never compiled or assembled ( .inc is an assembler include )
        static bool IsHeader(const std::string& path);
        // source files that take part in a build; a CCS project's exclusions are per configuration
        std::vector<std::string> BuildableFiles(const Model::GrideProjectPtr& p) const;
        std::vector<std::string> BuildableFiles(const Model::GrideProjectPtr& p, const std::string& configName) const;

        typedef std::map<std::string, std::string, Core::CaseInsensitiveLess> StemTable;
        typedef std::set<std::string, Core::CaseInsensitiveLess> NameSet;
        // object / assembly file names: the base name of the source when it is unique in the build,
        // else a name made from its project-relative path (src/a.c -> src_a, lib/a.c -> lib_a)
        static StemTable StemMap(const Model::GrideProjectPtr& p, const std::vector<std::string>& sources);
        // src/drivers/uart.c -> src_drivers_uart (unique within this build)
        static std::string MakeStem(const std::string& rel, NameSet& used);
        // the .s the compiler writes for a source file (Disassembly pane)
        std::string AsmFileFor(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg,
                               const std::string& src) const;
        // the Shalimar files of a build: shalimar compiles a program as one
        std::vector<std::string> ShalimarUnit(const Model::GrideProjectPtr& p, const std::vector<std::string>& sources) const;
        static std::string ProgramStem(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg);
        // RIDE's refusal before any compiler runs: the program's directory must take a file; "" when it does
        static std::string Unwritable(const std::string& dir);

    private:
        // one step's tool: the path, the template and the name, chosen by RIDE's rules
        struct Step
        {
            std::string Path, Cmd, Name, Macro, Role;
        };

        void ThreadMain(std::vector<BuildRequestPtr> requests);
        void Say(const std::string& s);
        void Keep(const std::string& s);
        bool OutputHas(const std::string& part);
        void BeginTool(const std::string& source, const std::string& toolName);
        int RunTool(const std::string& command, const std::string& workDir, const ToolRunOptions& options,
                    const std::string& source, const std::string& toolName, const std::string& targetName, bool rideTool);
        void OnToolLine(const std::string& line);
        void Report(Severity sev, const std::string& file, const std::string& text);

        int NativeFallback(const Model::GrideProjectPtr& p, const BuildRequestPtr& r);
        static std::string ToolName(const std::string& tool);
        static std::string RoleName(Core::Target t, const std::string& role, const std::string& tool);
        static Settings::ToolDefinitionPtr ProgramDefinition(RideTool tool, const std::string& program);
        void PartsOf(const Model::GrideProjectPtr& p, const std::vector<std::string>& sources, bool& cl, bool& other) const;
        static std::string ProjectToolFlags(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg,
                                            const Settings::ToolDefinitionPtr& d, std::vector<std::string>* unmapped);
        Step AssemblerFor(const Settings::ToolchainSettingsPtr& tc, Core::Target t, bool debug, bool native, RideTool tool);
        Step LinkerFor(const Settings::ToolchainSettingsPtr& tc, Core::Target t, bool debug, bool native, bool codeView,
                       bool microsoft);
        int BuildProject(const Model::GrideProjectPtr& p, const BuildRequestPtr& r, bool native);
        int HostLink(const Model::GrideProjectPtr& p, const Settings::ToolchainSettingsPtr& tc, MacroExpander& mx,
                     const ToolRunOptions& run, const Step& ld, std::vector<std::string>& objects,
                     const std::string& program, bool shalimarProgram, bool debug);
        int TiLink(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg,
                   const Settings::ToolchainSettingsPtr& tc, MacroExpander& mx, const ToolRunOptions& run, Step ld,
                   std::vector<std::string>& objects, const std::string& program, bool debug, bool shalimarProgram,
                   bool native);
        static std::vector<std::string> Leaves(const std::vector<std::string>& files);
        static ToolRunOptions RunOptions(const Settings::ToolchainSettingsPtr& tc, Core::Target t);
        static ToolRunOptions CompileOptions(const ToolRunOptions& base, const Settings::ToolchainSettingsPtr& tc,
                                             bool codeView, const std::vector<std::string>& optionEnvironment);
        void SetCompilerMacros(MacroExpander& mx, RideTool tool, Core::Language lang, const Model::GrideProjectPtr& p,
                               const Model::BuildConfigurationPtr& cfg, const Settings::ToolchainSettingsPtr& tc);
        void KeepInstallHeadersFrom(std::vector<std::string>& inc);
        MacroExpander BaseMacros(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg,
                                 const Settings::ToolchainSettingsPtr& tc, const BuildRequestPtr& r);
        static bool IsC6747Library(const std::string& lib);
        long long NewestHeader(const Model::GrideProjectPtr& p) const;
        bool CanClean(const Model::GrideProjectPtr& p, const std::string& dir, const std::string& what);
        void CleanDir(const std::string& dir);

        Settings::GrideSettingsPtr settings;
        ToolRunner runner;
        std::thread worker;
        BuildSummaryPtr summary;
        Model::GrideProjectPtr currentProject;
        std::string currentWorkDir;
        MessagePatternPtr currentPattern;   // custom error pattern of the current toolchain
        std::shared_ptr<DiagnosticReader> reader;   // reads the running tool's lines; one per tool run
        std::mutex linesLock;
        std::vector<std::string> projectLines;      // every line of the current project's build
        std::string failedStep;             // compile / assemble / link, when the project failed
        bool placedError;                   // a diagnostic naming a file and line was seen
        bool codeViewWarned;
        bool clSettingsSaid;
        bool rideHeadersSaid;
        std::atomic<bool> busy;
    };
}}
