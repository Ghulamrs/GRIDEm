// =============================================================================
//  CcsReading.h - a CCS 7.4 or 5.5 project for the TMS320C6747, read as RIDE's
//  src/ccs reads it: TI's own option definitions as data (CcsOptionDefs), the
//  three files read afresh and nothing written back (CcsReading), the workspace
//  a project is registered in (CcsWorkspace). What RIDE's toolchain can honour
//  is mapped; the rest is named, never dropped in silence. Where RIDE refuses a
//  project outright (not a C674x, COFF, big-endian) GRIDEm reads on and says so
//  in a note, so the project still opens; the build then stands on what was read.
//  (GRIDEd's Model/CcsReading.h)
// =============================================================================
#pragma once
#include "XmlReader.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace GRIDEm { namespace Model {

    // ---- TI's option definitions, one row per option (ccsoptions) -------------
    class CcsOptionDef
    {
    public:
        std::string Id;            // the tail after the version prefix: compilerID.OPT_LEVEL
        std::string ValueType;     // string, boolean, enumerated, stringList, definedSymbols, includePath, libPaths, libs
        std::string Command;       // --heap_size=, -O, --define=
        std::string DefaultValue;  // a plain default, or an enumerated one's full id
        std::string SuperClass;    // OPT_LEVEL.release's is OPT_LEVEL
        std::vector<std::pair<std::string, std::string> > Enums;   // value suffix -> flag
        std::string DefaultEnum;   // the suffix marked (default), or ""
    };
    typedef std::shared_ptr<CcsOptionDef> CcsOptionDefPtr;

    class CcsOptionDefs
    {
    public:
        // a directory named outright (tests, a suite); empty: looked for as DefinitionsDir says
        static std::string NamedDir;

        std::string File;
        bool Loaded() const { return !defs.empty(); }

        // lib/ccs in the app's Resources, beside the program, above it, under the installation's root
        static std::string DefinitionsDir();
        // the file for a compiler version: its major's; false, with a reason, when it is not there
        bool Load(const std::string& version, std::string& why);
        CcsOptionDefPtr Find(const std::string& tail) const;
        // the command-line spelling of a stored value, from the definition
        std::string Spell(const std::string& tail, const std::string& value, const std::vector<std::string>& items) const;
        // the enumerated default's suffix, or the plain defaultValue, for an option the project left unstored
        std::string DefaultOf(const std::string& tail) const;

        // "com.ti.ccstudio.buildDefinitions.C6000_8.2.compilerID.OPT_LEVEL" -> "compilerID.OPT_LEVEL"
        static std::string IdTail(const std::string& fullId);
        // an enumerated value's id -> its suffix after the option's own id
        static std::string EnumSuffix(const std::string& optionId, const std::string& valueId);
        static std::string MajorOf(const std::string& version);

    private:
        std::map<std::string, CcsOptionDefPtr> defs;
        static bool Holds(const std::string& dir);
        static std::string ToolchainSettingsRoot();
    };
    typedef std::shared_ptr<CcsOptionDefs> CcsOptionDefsPtr;

    // ---- the link of one configuration, as lnk6x is handed it (TiLink) --------
    class CcsLink
    {
    public:
        CcsLink() : Given(false), RomModel(-1) {}
        bool Given;
        std::string Heap;
        std::string Stack;
        std::vector<std::string> SearchPaths;
        std::vector<std::string> Libraries;
        int RomModel;              // 1 --rom_model, 0 --ram_model, -1 unsaid
        std::vector<std::string> CmdFiles;
    };

    // ---- one configuration, mapped (Config) -----------------------------------
    class CcsConfig
    {
    public:
        CcsConfig() : IsDebug(true), Debug(false), DebugSaid(false), NoCompress(false) {}
        std::string Name;                       // CCS's: Debug, Release
        bool IsDebug;                           // RIDE's
        std::string Opt;                        // -O0, -O1 or -O2
        bool Debug;                             // -g (DWARF)
        bool DebugSaid;
        std::vector<std::string> Defines;       // NAME or NAME=value, CCS's outer quotes taken off
        std::vector<std::string> Undefines;
        std::vector<std::string> Includes;      // absolute, macros resolved, ${CG_TOOL_ROOT}/include left out
        bool NoCompress;
        CcsLink Link;
        std::vector<std::string> Sources;       // absolute, exclusions applied, .cmd files apart
        std::vector<std::string> Excluded;      // project-relative, as .cproject spells them
        std::vector<std::string> Unsupported;   // as cl6x would have been given them
        std::vector<std::string> OwnInstead;
        std::vector<std::string> Mapped;        // "cl6x -O3 -> cpp11 -O2", for the read-only dialog
        std::string Prebuild;
        std::string Postbuild;
    };
    typedef std::shared_ptr<CcsConfig> CcsConfigPtr;

    // ---- a workspace's registered project (ccsworkspace) ----------------------
    class CcsWorkspaceMember
    {
    public:
        CcsWorkspaceMember() : Inside(false) {}
        std::string Name;
        std::string Location;      // absolute, slashes one way
        bool Inside;               // in the workspace folder rather than imported from elsewhere
    };

    class CcsWorkspace;
    typedef std::shared_ptr<CcsWorkspace> CcsWorkspacePtr;

    class CcsWorkspace
    {
    public:
        std::string Dir;
        std::vector<CcsWorkspaceMember> Projects;
        std::vector<std::string> Notes;
        std::map<std::string, std::string> PathVariables;
        std::map<std::string, std::string> Macros;

        static const char* const Resources;         // ".metadata/.plugins/org.eclipse.core.resources"
        static const char* const RuntimeSettings;   // ".metadata/.plugins/org.eclipse.core.runtime/.settings"

        // false when there is no member of that name
        bool Project(const std::string& name, CcsWorkspaceMember& out) const;

        // a folder with .metadata/.plugins/org.eclipse.core.resources is a workspace
        static bool IsWorkspace(const std::string& dir);
        // the registry, the path variables and the build macros; nullptr when the folder is not a workspace
        static CcsWorkspacePtr Read(const std::string& where, std::string& error);
        // the workspace a project folder is registered in, by looking in its parent: "" when there is none
        static std::string WorkspaceOf(const std::string& projectDir);
        static bool IsProjectDir(const std::string& dir);
        static std::string WithSlashes(const std::string& p);
        // a file: URI as Eclipse writes it - file:/C:/x%20y - to a path; anything else as it is
        static std::string PathOfUri(const std::string& uri);
        // .location: a 16-byte chunk marker, then Java's writeUTF of the location
        static std::string LocationRecord(const std::string& bytes);
        // a Java properties file, as Eclipse's preference store writes one
        static std::map<std::string, std::string> Properties(const std::string& text);
        static std::string Attr(const XmlNodePtr& n, const std::string& name);
    };

    // ---- the project (Reading) ------------------------------------------------
    class CcsReading;
    typedef std::shared_ptr<CcsReading> CcsReadingPtr;

    class CcsReading
    {
    public:
        CcsReading() : Elf(true) {}
        std::string Dir;             // the project folder, absolute, slashes one way
        std::string Name;
        std::string Device;          // TMS320C67XX.TMS320C6747
        std::string Family;          // C6000
        std::string CcsVersion;      // 7.4.0, or "" for 5.5
        std::string CgtVersion;      // 8.2.2, 7.4.4
        std::string CgToolRoot;      // ${CG_TOOL_ROOT} as resolved here, or ""
        bool Elf;
        std::vector<CcsConfigPtr> Configs;      // Debug first where there is one, then Release
        std::vector<std::string> Linked;        // linked resources' absolute paths, files only
        std::vector<std::string> NotBuilt;      // sources of a kind RIDE does not build: .asm, .sa, .lib, .obj
        std::vector<std::string> Headers;       // the folder's own headers, relative
        std::vector<std::string> Notes;
        std::vector<std::string> Refusals;      // what RIDE refuses the project for; GRIDEm reads on
        std::map<std::string, std::string> Macros;
        std::string Workspace;
        std::map<std::string, std::string> PathVariables;
        std::map<std::string, std::string> Projects;
        std::vector<std::string> References;
        std::string DefinitionsFile;

        CcsConfigPtr Config(bool debug) const;

        static bool IsProject(const std::string& dir);
        static bool IsProjectFile(const std::string& file);
        // a directory named as TI names its compiler directories, or holding bin/lnk6x
        static bool IsCompilerDir(const std::string& dir);
        // ${CG_TOOL_ROOT}: under the CCS root, the compiler directory of that version; "" for none
        static std::string CgToolRootFor(const std::string& ccsRoot, const std::string& version);
        // ${ProjName}, ${PROJECT_ROOT}, ${CG_TOOL_ROOT}, ${workspace_loc:/P/x}, a build variable
        std::string ResolveMacros(const std::string& text) const;
        // WORKSPACE_LOC: the workspace the project was opened from, else the folder's parent
        std::string WorkspaceDir() const;

        // Reads the three files. Throws std::runtime_error only when a file is missing or will not
        // parse; RIDE's refusals are notes in Refusals and the reading goes on. workspace may be null.
        static CcsReadingPtr Read(const std::string& where, const std::string& ccsRootSetting,
                                  const CcsWorkspacePtr& workspace);

        // "CCS project K6747c (Release): 2 options not supported, using RIDE's defaults: ..."
        std::string Report(bool debug) const;
        // linked files not on this machine, kinds RIDE does not build, the notes
        std::string SourceReport() const;
        // the dialog's text for a configuration
        std::string MappingText(bool debug) const;

    private:
        struct Stored
        {
            std::string Tail;
            std::string Value;
            std::vector<std::string> Items;
        };
        typedef std::vector<Stored> StoredList;

        static std::vector<Stored> OptionsOf(const XmlNodePtr& tool);
        static const Stored* FindStored(const StoredList& list, const std::string& tail);
        static XmlNodePtr ToolNamed(const XmlNodePtr& toolChain, const std::string& word);
        std::string LocationOf(const XmlNodePtr& link) const;
        void MapConfig(const StoredList& compiler, const StoredList& linker, const CcsOptionDefs& defs, CcsConfig& out);
        static void PerFileOptions(const XmlNodePtr& fileInfo, const StoredList& folder, const CcsOptionDefs& defs, CcsConfig& out);
    };
}}
