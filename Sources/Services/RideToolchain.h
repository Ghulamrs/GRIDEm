// =============================================================================
//  RideToolchain.h - the calling conventions of RIDE's toolchain, as RIDE's
//  src/toolchain.cpp, compile.cpp and editor.cpp have them: which compiler takes
//  which language, what runs on this machine, the flags of a configuration, how a
//  tool is found and the command lines RIDE shows and runs. Every name, word,
//  flag and library comes from toolchain.json (ToolchainDefinitions); nothing of
//  the toolchain is spelled here. RideTool is the role a compiler plays.
//  (GRIDEd's Services/RideToolchain.h)
//
//  On the Mac a tool is looked for under each of its names as written and, for
//  a name ending in .exe, without it; inside GRIDEm.app the tools sit in
//  Contents/MacOS. A path answer of "" means "not found" (GRIDEd's null).
// =============================================================================
#pragma once
#include "../Core/LanguageTarget.h"
#include "../Settings/CompilerOptions.h"
#include "../Settings/ToolchainDefinitions.h"
#include "VisualStudio.h"
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    // RIDE's ToolchainKind: auto, cc1 (the C compiler), msvc (the host's compiler), shc (the Shalimar
    // compiler), cxx (the host's C++ elsewhere than Windows), cxx1 (the C++ compiler)
    enum class RideTool { Auto, C90, Cl, Shalimar, HostCxx, Cpp11 };

    class RideToolchain
    {
    public:
        // ---- the definitions behind a role ----------------------------------
        static std::string RoleOf(RideTool t);
        static RideTool ToolOfRole(const std::string& role);
        static Settings::ToolDefinitionPtr Definition(RideTool t);
        static Settings::TargetDefinitionPtr TargetDef(Core::Target t);

        // ---- words and names ------------------------------------------------
        static std::string Word(RideTool t);                 // toolchainWord
        static RideTool FromWord(const std::string& word);   // toolchainFrom
        static std::string Name(RideTool t);                 // toolchainName
        static std::vector<RideTool> Compilers();
        static std::vector<std::string> Aliases(RideTool t);
        static std::string EnvironmentVariable(RideTool t);
        static std::string LanguageWord(Core::Language l);

        // ---- targets --------------------------------------------------------
        static std::string Arch(Core::Target t);
        static Core::Target TargetOfArch(const std::string& arch);
        static std::string HostArch();
        static bool IsEmulated(Core::Target t);
        static bool IsHostArch(Core::Target t) { return !IsEmulated(t); }
        static Settings::TargetDefinitionPtr EmulatedTarget();
        static bool UsesArch(RideTool t);
        static std::string ArchFlag(RideTool t, Core::Target target);

        // ---- which compiler takes the file ----------------------------------
        static RideTool Resolve(RideTool chosen, Core::Language lang);
        static bool CanCompile(RideTool t, Core::Language lang);
        static std::string Refusal(RideTool t, Core::Language lang);

        // ---- what runs here -------------------------------------------------
        static bool RunsHere(RideTool t, Core::Target target);
        static std::string WhyNotRun(RideTool t, Core::Target target);
        static std::string AssemblyOnlyFlag();

        // ---- debug information ----------------------------------------------
        static bool EmitsDebugInfo(RideTool t, Core::Target target);
        static bool DebugsWithCodeView(RideTool t, Core::Target target, bool debug);
        // a Debug build of a compiler that writes its CodeView in the MASM spelling itself (asmSyntaxFlags.ownCodeView)
        static bool OwnCodeView(RideTool t, Core::Target target, bool debug);
        static std::string AssemblerFlag(RideTool t, Core::Target target, bool debug, bool assemblerNamed);
        static std::string ConfigFlags(RideTool t, bool debug, Core::Target target);
        static std::string ConfigFlags(RideTool t, bool debug, Core::Target target, const Settings::CompilerOptionStorePtr& store);

        // ---- RIDE's Compiler Options ----------------------------------------
        static bool OptionAvailable(const std::string& id, Core::Target target, std::string& why);
        static std::string OptionFlags(const Settings::CompilerOptionStorePtr& store, const Settings::ToolDefinitionPtr& tool,
                                       const std::string& config, Core::Target target);
        static std::vector<std::string> OptionEnvironment(const Settings::CompilerOptionStorePtr& store,
                                                          const Settings::ToolDefinitionPtr& tool, const std::string& config);
        static std::string LanguageFlags(RideTool t, Core::Language lang);
        static std::string IncludeFlagOf(RideTool t);
        static std::string DefineFlagOf(RideTool t);
        static std::string IncludeFlags(RideTool t, const std::vector<std::string>& projectDirs,
                                        const std::string& installInclude, const std::string& installLib);

        // ---- finding the tools ----------------------------------------------
        static std::string Quote(const std::string& s);
        // the folder holding the IDE's program (GRIDEm.app/Contents/MacOS)
        static std::string IdeDirectory();
        static std::string RideBin(const std::string& rideRoot);
        static std::string InstallRoot(const std::string& rideRoot);
        static std::string InstallDir(const std::string& rideRoot, const std::string& which, const std::string& named);
        static std::vector<std::string> InstallIncludeDirs(RideTool t, const std::string& rideRoot,
                                                           const std::string& namedInclude, const std::string& namedLib);

        // ---- TI's C6000 compiler directory (RIDE's settings ti / tilib) ------
        static std::string TiCompilerDir;
        static std::string TiLibDir;
        static std::string TiDir();
        static bool TiNamed();
        static std::string DetectedTiDir();
        static std::string DefaultRideBin();

        // where a program is: its environment variable, the tool folder, beside the IDE, the
        // installation's bin, then PATH. "" when nowhere.
        static std::string Find(const std::vector<std::string>& names, const std::string& envVar,
                                const std::string& toolDir, const std::string& rideRoot);
        static std::string Find(const std::vector<std::string>& names, const std::string& envVar,
                                const std::string& toolDir, const std::string& rideRoot, bool onPath);
        static std::string Find(const std::string& name, const std::string& envVar,
                                const std::string& toolDir, const std::string& rideRoot);
        static std::string FindTool(const Settings::ToolDefinitionPtr& d, const std::string& toolDir, const std::string& rideRoot);
        static std::string SearchPlaces(const Settings::ToolDefinitionPtr& d, const std::string& toolDir, const std::string& rideRoot);
        static bool IsTiTool(const Settings::ToolDefinitionPtr& d);
        static std::string TiBinFor(const Settings::ToolDefinitionPtr& d);
        static std::string FindLibraryDir(const Settings::LibraryDefinitionPtr& lib, const std::string& toolDir, const std::string& rideRoot);
        static std::string FindRuntimeDir(const std::string& toolDir, const std::string& rideRoot);
        static std::string FindShalimarRuntimeDir(const std::string& toolDir, const std::string& rideRoot, bool debug);

        // ---- the lines RIDE shows -------------------------------------------
        static std::string ShownCompileCommand(RideTool t, const std::string& program, const std::string& source, Core::Language lang,
                                               Core::Target target, const std::string& configFlags, const std::string& assemblerFlag,
                                               const std::string& includeFlags);
        static std::string ShownRunCommand(RideTool t, const std::string& program, const std::string& source, Core::Language lang,
                                           Core::Target target, const std::string& configFlags, const std::string& assemblerFlag,
                                           const std::string& includeFlags, bool emulate);
        static std::string LaunchCommand(const std::string& program, bool shalimar, const std::vector<std::string>& args,
                                         const std::string& emulator, const std::string& shalimarRuntimeDir);
        static std::string EmulateCommand(const std::string& emulator, const std::string& program, bool shalimar,
                                          const std::vector<std::string>& args, const std::string& shalimarRuntimeDir);
        static std::string SimulationMissing(const std::string& simulator, const std::string& out);
        static bool IsEmulatedProgram(const std::string& program);
        static std::string SimulateCommand(const std::string& simulator, const std::string& out);
        static std::string TiProgramOf(const std::string& program);
        static std::string RunnerLine(bool simulator, const std::string& program);
        static std::string ToolName(const Settings::TargetDefinitionPtr& td, const std::string& role);
        static std::string TiLinkCmd();
        static std::string TiLinkCmdName();
        static std::string RtsLibrary(bool debug);
        static bool IsRtsAlias(const std::string& lib);
        static std::string ShalimarRtsLibrary(bool debug);
        static std::string HostShalimarRuntime(Core::Target target, bool debug, const std::string& toolDir, const std::string& rideRoot);
        // the one built for that C runtime ("grt" / "msvc")
        static std::string HostShalimarRuntime(Core::Target target, bool debug, const std::string& toolDir, const std::string& rideRoot,
                                               const std::string& crt);
        static std::string CrtLibraries(Core::Target target, bool debug);
        static std::string WindowsCrtLibraries(bool debug);
        // the host C runtime's entry for a choice ("grt" GRIDE's, "msvc" Microsoft's)
        static Settings::LibraryDefinitionPtr HostCrt(Core::Target target, const std::string& crt);
        // its libraries as the link takes them: full paths where the entry names a directory, else the names;
        // false where the entry's directory or a file in it is not there
        static bool CrtLibraries(Core::Target target, bool debug, const std::string& crt, const std::string& toolDir,
                                 const std::string& rideRoot, std::string& libraries);
        // where GRIDE's runtime lies, for the messages: its directory, or the one it is looked for in
        static std::string CrtDirectory(Core::Target target, const std::string& crt, const std::string& toolDir, const std::string& rideRoot);

        // ---- small helpers others use ---------------------------------------
        // one space between words, none at the ends
        static std::string Tidy(const std::string& s);
        // the names tried for a program: each as written, then without a trailing .exe
        static std::vector<std::string> NamesHere(const std::vector<std::string>& names);

    private:
        static Settings::ToolchainDefinitionsPtr Defs();
        static void CompilersUnder(const std::string& root, const std::string& under, const Settings::CcsDefinition& cd,
                                   std::vector<std::string>& into);
        static std::vector<int> VersionOf(const std::string& name);
        static bool Newer(const std::vector<int>& a, const std::vector<int>& b);
        static bool HasMarker(const std::string& dir, const std::vector<std::string>& markers);
        static bool HasFile(const std::string& dir, const std::string& marker);
        static std::vector<std::string> SearchDirs(const std::string& toolDir, const std::string& rideRoot);
        static std::string Fill(const std::string& templ, RideTool t, const std::string& source, Core::Language lang,
                                Core::Target target, const std::string& configFlags, const std::string& assemblerFlag,
                                const std::string& includeFlags);
        static void Append(std::string& sb, const std::string& s);
    };
}}
