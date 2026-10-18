// =============================================================================
//  GrideSettings.h - everything stored in gride.settings.json
//
//    EnvironmentSettings  theme, fonts, editor options, folders
//    ToolchainSettings    one per target (see ToolchainSettings.h)
//    LanguageSettings     one per language: extensions, keywords, comments
//    ConverterSettings    C <-> Shalimar converter tools
//    LayoutSettings       window position, pane sizes, hidden panes/toolbars
//    RecentList           recent files / workspaces
//    ExternalTool         Tools > External Tools entries
//    KeyBindings          command id -> shortcut text ("Ctrl+Shift+B")
//  The same file GRIDEd writes: GRIDEm reads it and writes it back with the
//  same keys in the same order. (GRIDEd's Settings/GrideSettings.h)
// =============================================================================
#pragma once
#include "CompilerOptions.h"
#include "ToolchainSettings.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace GRIDEm { namespace Settings {

    // a string -> string table that keeps the order its keys were added in, as GRIDEd's
    // Dictionary does, so the file is written back in the same order
    class OrderedStringMap
    {
    public:
        bool Has(const std::string& key) const;
        bool Find(const std::string& key, std::string& value) const;
        std::string Get(const std::string& key) const;       // "" when absent
        void Set(const std::string& key, const std::string& value);
        void Remove(const std::string& key);
        void Clear() { items.clear(); }
        int Count() const { return (int)items.size(); }
        bool Empty() const { return items.empty(); }
        const std::vector<std::pair<std::string, std::string> >& Items() const { return items; }
    private:
        std::vector<std::pair<std::string, std::string> > items;
    };

    // -------------------------------------------------------------------------
    class EnvironmentSettings
    {
    public:
        EnvironmentSettings();
        std::string Theme;
        std::string FontName;
        float FontSize;
        int TabSize;
        bool InsertSpaces;
        bool LineNumbers;
        bool ShowWhitespace;
        bool ReopenLastWorkspace;
        bool SaveBeforeBuild;
        bool JumpToFirstError;        // a failed build puts the caret on its first error (RIDE)
        bool AskNativeTools;          // RIDE's askNative: offer the vendor's assembler/linker when the project's own fail
        bool RunInIde;                // Start Without Debugging runs in the Output pane (RIDE); off: Terminal
        std::string DefaultProjectLocation;
        std::string CcsToolRoot;      // value of ${CG_TOOL_ROOT} for CCS imports
        std::string HelpDirectory;
        std::string RideRoot;         // RIDE's installation (its bin folder, or the folder above it)
        std::string RideIncludeDir;   // "include": cpp11's headers, relative to the installation
        std::string RideLibDir;       // "lib": c90's headers
        std::string Vcvars;           // "vcvars": Windows only, kept and written back
        std::string TiCompilerDir;    // "ti": TI's C6000 compiler directory
        std::string TiLibDir;         // "tilib": a second directory with rts6740_elf_eh.lib
        std::string DefaultCompiler;  // "compiler": the word a new project starts with
        std::vector<std::string> SharedIncludes;   // "includes"
        std::vector<std::string> SharedLibraries;  // "libraries"
        bool CcsEnabled;              // "ccs.enabled": File > Open > CCS Project
        std::string CcsRoot;          // "ccs.root": the CCS installation ${CG_TOOL_ROOT} is found under
        std::string C6747Debugger;    // auto, sim6747, vm6747; RIDE_DEBUG_* at start win
        OrderedStringMap SyntaxColors;   // token -> "#RRGGBB" override

        // what ${CG_TOOL_ROOT} is resolved from, in RIDE's order
        std::string CcsCompilerRoot() const;
        // the C6747 debugger choice in force: RIDE_DEBUG_VM6747 / RIDE_DEBUG_SIM6747 win
        std::string EffectiveC6747Debugger() const;

        Core::JsonPtr ToJson() const;
        void FromJson(const Core::JsonPtr& o);
    };
    typedef std::shared_ptr<EnvironmentSettings> EnvironmentSettingsPtr;

    // -------------------------------------------------------------------------
    class LanguageSettings
    {
    public:
        explicit LanguageSettings(Core::Language l);
        Core::Language Lang;
        std::vector<std::string> Extensions;
        std::vector<std::string> Keywords;
        std::vector<std::string> Types;
        std::string LineComment;           // "//" (empty for C90)
        std::string BlockCommentStart;     // "/*"
        std::string BlockCommentEnd;       // "*/"
        bool Preprocessor;                 // highlight #include etc.

        std::string Name() const;
        bool OwnsExtension(const std::string& ext) const;
        void ApplyDefaults();
        Core::JsonPtr ToJson() const;
        void FromJson(const Core::JsonPtr& o);
    };
    typedef std::shared_ptr<LanguageSettings> LanguageSettingsPtr;

    // -------------------------------------------------------------------------
    // Tools > Convert. Empty tool and template (the default) mean toolchain.json's converter (c2s)
    class ConverterSettings
    {
    public:
        ConverterSettings();
        std::string CToShalimar;       // a converter program in place of c2s, C -> Shalimar
        std::string ShalimarToC;       // the same, Shalimar -> C
        std::string CommandTemplate;   // $(Tool) $(Input) $(Output); empty: the converter's operation
        bool OpenResult;

        // GRIDE 1.0's defaults, which named programs that never shipped: read back as "not set"
        static const char* const Gride1CToShalimar;
        static const char* const Gride1ShalimarToC;
        static const char* const Gride1Template;
        static std::string Unless(const std::string& value, const std::string& gride1);

        Core::JsonPtr ToJson() const;
        void FromJson(const Core::JsonPtr& o);
    };
    typedef std::shared_ptr<ConverterSettings> ConverterSettingsPtr;

    // -------------------------------------------------------------------------
    class LayoutSettings
    {
    public:
        LayoutSettings();
        int Left;
        int Top;
        int Width;
        int Height;
        bool Maximized;
        int LeftWidth;
        int RightWidth;
        int BottomHeight;
        std::vector<std::string> HiddenPanes;
        std::vector<std::string> HiddenToolbars;
        bool StatusBar;

        Core::JsonPtr ToJson() const;
        void FromJson(const Core::JsonPtr& o);
    };
    typedef std::shared_ptr<LayoutSettings> LayoutSettingsPtr;

    // -------------------------------------------------------------------------
    class RecentList
    {
    public:
        std::vector<std::string> Files;
        std::vector<std::string> Workspaces;

        static void Push(std::vector<std::string>& list, const std::string& path);
        Core::JsonPtr ToJson() const;
        void FromJson(const Core::JsonPtr& o);
    };
    typedef std::shared_ptr<RecentList> RecentListPtr;

    // -------------------------------------------------------------------------
    class ExternalTool
    {
    public:
        ExternalTool() : Title("New Tool"), InitialDir("$(ProjectDir)") {}
        std::string Title;
        std::string Command;
        std::string Arguments;
        std::string InitialDir;
    };
    typedef std::shared_ptr<ExternalTool> ExternalToolPtr;

    class GrideSettings;
    typedef std::shared_ptr<GrideSettings> GrideSettingsPtr;

    // -------------------------------------------------------------------------
    //  GrideSettings - the root object
    // -------------------------------------------------------------------------
    class GrideSettings
    {
    public:
        GrideSettings();

        EnvironmentSettingsPtr Env;
        std::vector<ToolchainSettingsPtr> Toolchains;
        std::vector<LanguageSettingsPtr> Languages;
        ConverterSettingsPtr Converter;
        LayoutSettingsPtr Layout;
        RecentListPtr Recent;
        std::vector<ExternalToolPtr> ExternalTools;
        OrderedStringMap KeyBindings;
        std::string KeySet;                          // "visualstudio", "ride" (GRIDEd) or "macos" (GRIDEm)
        OrderedStringMap Snippets;                   // name -> text (Tools > Code Snippets)
        CompilerOptionStorePtr CompilerOptions;      // what a new project starts with
        // RIDE's own state for a CCS project: absolute directory with forward slashes -> { args, emulateOnly, ... }
        Core::JsonPtr CcsProjects;
        std::string LastWorkspace;
        std::string FilePath;      // where this file lives
        bool IsPortable;           // true when it sits beside the app

        static std::string CcsKey(const std::string& dir);
        std::string CcsKeyOf(const std::string& dir) const;
        Core::JsonPtr CcsState(const std::string& dir) const;
        void RememberCcsState(const std::string& dir, const Core::JsonPtr& state);

        // the statics the toolchain code reads: RIDE's root, TI's directories, the named vcvars64.bat
        void PublishPaths() const;

        ToolchainSettingsPtr Toolchain(Core::Target t);
        LanguageSettingsPtr LanguageOf(Core::Language l) const;
        // the language of a file from its extension; headers (.h) follow the project language
        Core::Language DetectLanguage(const std::string& path, Core::Language projectLang) const;

        Core::JsonPtr ToJson() const;
        static GrideSettingsPtr FromJson(const Core::JsonPtr& o);
        static OrderedStringMap DefaultSnippets();
        GrideSettingsPtr Clone() const;
    };

    // -------------------------------------------------------------------------
    //  SettingsManager - finds, loads and saves gride.settings.json
    //    1. gride.settings.json beside GRIDEm.app       (portable install)
    //    2. ~/Library/Application Support/GRIDE/gride.settings.json
    // -------------------------------------------------------------------------
    class SettingsManager
    {
    public:
        static std::string PortablePath();
        static std::string UserPath();
        // warning receives a message when the file was unreadable (it is then backed up); the toolchain
        // definitions are loaded first, since the settings' defaults come from them
        static GrideSettingsPtr Load(std::string& warning);
        static void Save(const GrideSettingsPtr& s);
    };
}}
