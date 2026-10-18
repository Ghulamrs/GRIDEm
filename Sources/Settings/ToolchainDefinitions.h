// =============================================================================
//  ToolchainDefinitions.h - toolchain.json: every tool, its arguments, the
//  libraries and where things are looked for, read from the shipped file inside
//  GRIDEm.app with the user's file laid over it field by field. Nothing of the
//  toolchain's names stays in the code: RideToolchain and ToolchainSettings ask
//  here. The format is GRIDEd's docs/TOOLCHAIN-FORMAT.md, unchanged.
//  (GRIDEd's Settings/ToolchainDefinitions.h)
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"
#include "../Core/StringUtil.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GRIDEm { namespace Settings {

    typedef std::map<std::string, std::string> StringMap;
    typedef std::map<std::string, std::string, Core::CaseInsensitiveLess> StringMapNoCase;

    // one thing a tool does: the argument template and what each argument means
    class OperationDefinition
    {
    public:
        std::string Id;
        std::string Description;
        std::string Args;
        std::string Shown;        // the short line GRIDE prints for it, when it differs from Args
        std::string Output;
        StringMap Arguments;
    };
    typedef std::shared_ptr<OperationDefinition> OperationDefinitionPtr;

    class LookupDefinition
    {
    public:
        LookupDefinition() : OnPath(true), Vcvars(false) {}
        std::string Env;
        std::string InstallDir;
        std::vector<std::string> Dirs;   // folders of its own (%VAR% expanded), looked in before PATH
        bool OnPath;
        bool Vcvars;
        std::string Description;
    };

    // how a debugger is driven on its standard input: the dialect its answers are read in (gdb, lldb,
    // cdb - or shalimar, a program that stops itself), the preamble sent at start, the marker asked
    // for after every command, and the spelling of each command the IDE needs; on a pseudo-console
    // where the debugger flushes its prompt only to one (cdb); with the variables the child starts
    // with (SHM_DEBUG=1) and, for the Shalimar session, the words its lines start with
    class SessionDefinition
    {
    public:
        SessionDefinition() : Console(false) {}
        std::string Description;
        std::string Dialect;               // gdb / lldb / cdb / shalimar
        std::string Like;                  // the tool whose session this one starts from
        std::string Marker;                // the text that ends every answer
        std::string MarkerCommand;         // the command that makes the debugger print it
        bool Console;                      // driven on a pseudo-console
        std::string AfterReady;            // the command id sent once after the program's ready line
        std::vector<std::string> Preamble;
        StringMap Commands;
        StringMap Environment;             // NAME -> value the child starts with
        StringMap Protocol;                // file / ready / stop / at / exit -> the first word of that line

        std::string Command(const std::string& id) const;
        bool Has(const std::string& id) const;
        std::string ProtocolWord(const std::string& id) const;
        std::shared_ptr<SessionDefinition> Clone() const;
    };
    typedef std::shared_ptr<SessionDefinition> SessionDefinitionPtr;

    class ToolDefinition
    {
    public:
        explicit ToolDefinition(const std::string& id);
        SessionDefinitionPtr Session;      // for a tool that debugs (null for the others)
        std::string Id;
        std::string Name;
        std::string Description;
        std::string Role;
        std::vector<std::string> Languages;     // C, C++, Shalimar
        std::vector<std::string> Targets;       // arch words
        std::string Exe;
        std::vector<std::string> Aliases;
        std::vector<std::string> Words;         // what a project file may call it
        LookupDefinition Lookup;
        bool HasVersionArgs;                    // false: no version query (GRIDEd's null)
        std::string VersionArgs;
        std::string OutputEncoding;             // utf-8 / oem
        std::string Diagnostics;                // gnu / msvc / shalimar / masm / ti / none
        StringMap Environment;
        std::string ArchFlag;
        std::string IncludeFlag;
        std::string DefineFlag;
        std::vector<std::string> InstallIncludeDirs;
        StringMap LanguageFlags;
        StringMap ConfigFlags;
        std::map<std::string, StringMap> ConfigFlagsByTarget;
        StringMap AsmSyntaxFlags;
        std::map<std::string, OperationDefinitionPtr> Operations;
        std::vector<std::string> OperationOrder;
        std::vector<std::string> Options;       // the compilerOptions ids the tool takes, in flag order
        // a Visual Studio project's own settings (MSBuild's ClCompile names) -> value -> the switch;
        // "*" stands for any other value ($(Value) filled in); in the order of ProjectSettingOrder
        std::map<std::string, StringMapNoCase, Core::CaseInsensitiveLess> ProjectSettings;
        std::vector<std::string> ProjectSettingOrder;

        bool TakesOption(const std::string& id) const;
        // the switch for a project's setting: the value's own entry, else "*"; false when the table
        // names neither (an entry of "" is a setting read and given nothing)
        bool ProjectSettingFlag(const std::string& name, const std::string& value, std::string& flag) const;
        // the exe first, then the aliases: the names tried in order
        std::vector<std::string> AllNames() const;
        bool TakesArch() const;
        bool ServesTarget(const std::string& arch) const;
        bool CompilesLanguage(const std::string& languageWord) const;
        OperationDefinitionPtr Operation(const std::string& id) const;
        std::string OperationArgs(const std::string& id) const;
        // the configuration's flags: the exception for the arch when there is one
        std::string ConfigFlagsFor(const std::string& config, const std::string& arch) const;
        std::string AsmSyntaxFlag(const std::string& key) const;
        std::string LanguageFlag(const std::string& languageWord) const;
        bool HasName(const std::string& leafOrPath) const;
    };
    typedef std::shared_ptr<ToolDefinition> ToolDefinitionPtr;

    class LibraryDefinition
    {
    public:
        explicit LibraryDefinition(const std::string& id);
        std::string Id;
        std::string Name;
        std::string Description;
        std::string Kind;             // runtime / printf / shalimarRuntime / hostCrt
        std::string Target;           // arch
        std::string Directory;
        std::string DirectoryEnv;
        std::string Marker;
        StringMap Files;              // config -> file(s)
        std::vector<std::string> Aliases;
        StringMap AssemblyDirectories;
        StringMap AssemblyDirectoryEnv;
        std::string When;

        std::string FileFor(bool debug) const;
        std::string AssemblyDirectoryFor(bool debug) const;
        std::string AssemblyDirectoryEnvFor(bool debug) const;
        // a project's name for this library: an alias, or one of its own files ('*' at the end matches any ending)
        bool IsAlias(const std::string& lib) const;
    };
    typedef std::shared_ptr<LibraryDefinition> LibraryDefinitionPtr;

    class TargetDefinition
    {
    public:
        explicit TargetDefinition(const std::string& arch);
        std::string Arch;
        std::string IdeTarget;
        std::string Name;
        std::string Description;
        bool Host;
        bool Emulated;
        std::string DebugInfo;
        std::string AssemblyExtension;
        std::string ObjectExtension;
        std::string ProgramExtension;
        std::string EmulatedProgramExtension;
        std::string LibraryDirFlag;
        std::string LibraryFlag;
        StringMap Roles;                    // role -> tool id
        std::string Runtime;                // library id
        std::string LinkerCommandFile;      // linkerCommandFiles id
        std::string Run;                    // program / simulator / none
        std::string Debug;                  // debugger / simulator / none
        std::string ShownRun;
        std::string ShownEmulate;

        std::string ToolIdFor(const std::string& role) const;
    };
    typedef std::shared_ptr<TargetDefinition> TargetDefinitionPtr;

    class LinkerCommandFileDefinition
    {
    public:
        explicit LinkerCommandFileDefinition(const std::string& id) : Id(id), Name(id) {}
        std::string Id;
        std::string Name;
        std::string Description;
        std::vector<std::string> Lines;
        std::string Text() const;
    };
    typedef std::shared_ptr<LinkerCommandFileDefinition> LinkerCommandFileDefinitionPtr;

    class InstallationDefinition
    {
    public:
        std::string Description;
        std::vector<std::string> DefaultRoots;
        std::string BinDir;
        std::vector<std::string> Markers;
        std::string IncludeDir;
        std::string LibDir;
    };

    // where Visual Studio's tools and the Windows SDK are (Windows only; kept so the file round-trips)
    class VisualStudioDefinition
    {
    public:
        std::string Description;
        std::string MsvcVersion;
        std::string Vswhere;
        std::string VswhereArgs;
        std::vector<std::string> DefaultRoots;
        std::vector<std::string> Editions;
        std::string Vcvars;
        std::string MsvcTools;
        std::string HostBin;
        std::string Lib;
        std::string Include;
        std::string LlvmBin;
        std::string KitRegistryKey;
        std::string KitRegistryValue;
        std::vector<std::string> KitRoots;
        std::string KitVersionsUnder;
        std::vector<std::string> KitLibDirs;
        std::vector<std::string> KitIncludeDirs;
        std::string KitBinDir;
        StringMap Environment;
        bool Defined() const { return !MsvcTools.empty(); }
    };

    // one row of RIDE's Compiler Options table (options.cpp)
    class CompilerOptionDefinition
    {
    public:
        explicit CompilerOptionDefinition(const std::string& id);
        std::string Id;                       // c90.opt, general.jobs
        std::string Tab;
        std::string Label;
        std::string Hint;
        std::string Control;                  // check / choice / text
        std::vector<std::string> Choices;
        StringMap Defaults;                   // per configuration name, when the file gives them so
        bool HasDefaultValue;                 // false: GRIDEd's null
        std::string DefaultValue;             // one default for every configuration
        std::string FlagOn;
        std::string FlagOff;
        std::string Flag;                     // a choice's flag, $(Value) filled in
        bool HasSilent;                       // false: the first choice is silent
        std::string Silent;                   // the choice that writes nothing
        std::string FlagEach;                 // a text list's flag per entry, $(Item) filled in
        std::string EnvironmentVariable;
        std::string Needs;                    // lineTable / emulated / notEmulated / ""
        std::string Unavailable;              // the sentence said when it does not apply, $(Arch) filled in

        bool IsCheck() const;
        bool IsChoice() const;
        bool IsText() const;
        std::string DefaultFor(const std::string& config) const;
        std::string SilentChoice() const;
        // the compiler word in front of the dot (c90.opt -> c90); general.* belongs to no one compiler
        std::string ToolWord() const;
    };
    typedef std::shared_ptr<CompilerOptionDefinition> CompilerOptionDefinitionPtr;

    // reading CCS projects: TI's option definitions, which projects the toolchain builds, TI's runtime names
    class CcsDefinition
    {
    public:
        std::string Description;
        std::string DefinitionsDir;
        StringMap Definitions;           // major version -> file name
        std::string DeviceFamily;
        std::string DeviceVariant;
        std::string SiliconVersion;
        StringMap CompilerDirs;          // major version -> directory name prefix
        std::string RuntimeIndex;
        std::string RuntimeLibrary;
        std::string RuntimeFallback;
        std::vector<std::string> TiRoots;
        std::vector<std::string> TiUnder;
        std::string TiBin;
        std::string TiLib;
        bool Defined() const { return !Definitions.empty(); }
    };

    class ToolchainDefinitions;
    typedef std::shared_ptr<ToolchainDefinitions> ToolchainDefinitionsPtr;

    // -------------------------------------------------------------------------
    class ToolchainDefinitions
    {
    public:
        static const char* const FileName;               // "toolchain.json"
        static const char* const UserFileNamePortable;   // "toolchain.user.json"
        static const char* const EnvironmentOverride;    // "GRIDE_TOOLCHAIN"

        // the definitions in force; loaded from the shipped and user files on first use
        static ToolchainDefinitionsPtr Current();
        static void SetCurrent(const ToolchainDefinitionsPtr& d);

        // $GRIDE_TOOLCHAIN, GRIDEm.app/Contents/Resources, beside the program, else up to four folders above
        static std::string ShippedPath();
        static std::string UserPath(bool portable);
        // the program's folder (Contents/MacOS)
        static std::string ExeDirectory();
        // where a portable install keeps its files: the folder that holds GRIDEm.app
        // (beside the program when it is not in an app)
        static std::string PortableDirectory();

        // reads both files; never throws - every fault is a line in Problems and the entry falls back
        static ToolchainDefinitionsPtr Load(const std::string& shippedPath, const std::string& userPath);
        static ToolchainDefinitionsPtr Reload();

        ToolchainDefinitions();

        std::string ShippedFile;
        std::string UserFile;
        Core::JsonPtr ShippedJson;
        Core::JsonPtr UserJson;          // null until the user overrides something
        Core::JsonPtr MergedJson;
        std::vector<std::string> Problems;
        int Format;
        std::string Description;
        InstallationDefinition Installation;
        VisualStudioDefinition VisualStudio;
        CcsDefinition Ccs;
        std::vector<TargetDefinitionPtr> Targets;
        std::vector<ToolDefinitionPtr> Tools;
        std::vector<LibraryDefinitionPtr> Libraries;
        std::vector<LinkerCommandFileDefinitionPtr> LinkerCommandFiles;
        StringMap Placeholders;
        bool ShippedFound;
        std::string CompilerOptionsDescription;
        std::vector<std::string> CompilerOptionTabs;
        std::vector<CompilerOptionDefinitionPtr> CompilerOptions;

        CompilerOptionDefinitionPtr CompilerOption(const std::string& id) const;
        std::vector<CompilerOptionDefinitionPtr> CompilerOptionsOf(const std::string& tab) const;
        ToolDefinitionPtr ToolOfOption(const std::string& id) const;

        // ---- lookups --------------------------------------------------------
        TargetDefinitionPtr TargetByArch(const std::string& arch) const;
        TargetDefinitionPtr TargetByIde(const std::string& ideName) const;
        TargetDefinitionPtr TargetOf(Core::Target t) const;
        TargetDefinitionPtr HostTarget() const;
        ToolDefinitionPtr Tool(const std::string& id) const;
        ToolDefinitionPtr ToolByRole(const std::string& role) const;
        ToolDefinitionPtr ToolFor(Core::Target target, const std::string& role) const;
        ToolDefinitionPtr ToolForArch(const std::string& arch, const std::string& role) const;
        ToolDefinitionPtr ToolByWord(const std::string& word) const;
        ToolDefinitionPtr ToolByName(const std::string& exeOrPath) const;
        ToolDefinitionPtr CompilerOf(const std::string& languageWord) const;
        LibraryDefinitionPtr Library(const std::string& id) const;
        LibraryDefinitionPtr LibraryByKind(const std::string& arch, const std::string& kind) const;
        LibraryDefinitionPtr RuntimeOf(Core::Target target) const;
        LinkerCommandFileDefinitionPtr LinkerCommandFile(const std::string& id) const;
        LinkerCommandFileDefinitionPtr LinkerCommandFileOf(Core::Target target) const;

        // ---- the user's overrides -------------------------------------------
        bool IsOverridden(const std::string& section, const std::string& id) const;
        Core::JsonPtr UserEntry(const std::string& section, const std::string& id);
        // replaces the user's entry with the fields given (null or empty means: as shipped)
        void SetUserEntry(const std::string& section, const std::string& id, const Core::JsonPtr& fields);
        void RestoreShipped(const std::string& section, const std::string& id);
        Core::JsonPtr ShippedEntry(const std::string& section, const std::string& id) const;
        Core::JsonPtr MergedEntry(const std::string& section, const std::string& id) const;
        // writes the user's file (removes it when nothing is overridden)
        bool SaveUser(std::string& error);

        // ---- helpers for the pages and the check dialog ----------------------
        static std::vector<std::string> Roles();
        static Core::JsonPtr Clone(const Core::JsonPtr& v);
        // the user's fields laid over the shipped ones: objects recurse, everything else is replaced
        static void MergeInto(const Core::JsonPtr& into, const Core::JsonPtr& from);

    private:
        static ToolchainDefinitionsPtr current;

        std::string ShippedLabel() const;
        std::string UserLabel() const;
        Core::JsonPtr ReadFile(const std::string& path, const std::string& what, bool shipped);
        void CheckDocument(const Core::JsonPtr& doc, const std::string& file, bool shipped);
        void Build();
    };
}}
