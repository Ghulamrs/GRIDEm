// =============================================================================
//  GrideProject.h - one project (.pro JSON file)
//
//  Layout on disk ("one step difference"):
//      Project1/Project1.pro        <- this file
//      Project1/src/*.c             <- sources, stored as "src/main.c"
//      Project1/inc/*.h
//  A read-only project imported from Code Composer Studio (Kind "ccs7") or
//  Visual Studio (Kind "vs") uses the same class - see CcsProjectReader.h and
//  VsProjectReader.h. A .pro written by GRIDEd opens here and is written back
//  byte for byte as GRIDEd writes it. (GRIDEd's Model/GrideProject.h)
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"
#include "../Core/StringUtil.h"
#include "../Settings/CompilerOptions.h"
#include "BuildConfiguration.h"
#include "CcsReading.h"
#include "VsSolution.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace GRIDEm { namespace Model {

    // A named group of files shown as a folder in Project Explorer
    class ProjectFolder
    {
    public:
        explicit ProjectFolder(const std::string& name) : Name(name) {}
        std::string Name;
        std::vector<std::string> Files;      // relative paths with '/'
    };
    typedef std::shared_ptr<ProjectFolder> ProjectFolderPtr;

    // Extra information read from a CCS 7.4 / 5.5 project
    class CcsInfo
    {
    public:
        std::string DeviceVariant;
        std::string DeviceFamily;
        std::string CodegenVersion;
        std::string OutputFormat;
        std::string LinkerCommandFile;
        std::string Workspace;                 // the CCS workspace the project is registered in, or ""
        std::vector<std::string> ReportLines;  // RIDE's Messages lines for the Output pane
        CcsReadingPtr Reading;                 // the reading behind it, for the mapping dialog
    };
    typedef std::shared_ptr<CcsInfo> CcsInfoPtr;

    // Extra information read from a Visual Studio project (VsProjectReader.h)
    class VsInfo
    {
    public:
        std::string SolutionPath;              // the .sln it was opened from, or ""
        std::string SolutionFolder;            // the solution folder it sits in ("Libs/Inner"), or ""
        std::string Version;                   // "Visual Studio 2022, ToolsVersion Current, toolset v143"
        std::string Toolset;
        std::string Guid;
        std::vector<std::string> Configurations;     // the file's own: Debug|x64 ...
        NoCaseMap MappedConfigs;                     // GRIDE's Debug / Release -> the file's configuration built from
        std::vector<std::string> ReportLines;  // what was read and what is not mapped, for the Output pane
        VsProjectPtr Reading;                  // the reading behind it, for the mapping dialog
    };
    typedef std::shared_ptr<VsInfo> VsInfoPtr;

    // a small map that keeps the order its keys were added and ignores their case
    // (GRIDEd's Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase), whose
    // enumeration follows insertion while nothing is removed)
    class OrderedNoCaseMap
    {
    public:
        bool Find(const std::string& key, std::string& value) const;
        void Set(const std::string& key, const std::string& value);
        void Remove(const std::string& key);
        bool Empty() const { return items.empty(); }
        int Count() const { return (int)items.size(); }
        const std::vector<std::pair<std::string, std::string> >& Items() const { return items; }
    private:
        std::vector<std::pair<std::string, std::string> > items;
    };

    class GrideProject;
    typedef std::shared_ptr<GrideProject> GrideProjectPtr;

    class GrideProject
    {
    public:
        GrideProject();

        // ---- identity ---------------------------------------------------------
        std::string FilePath;          // full path of the .pro (or CCS .project)
        std::string Name;
        Core::Language Lang;
        std::string Template;
        std::string Kind;              // "gride", "ccs7" or "vs"
        bool IsReadOnly;
        bool IsDirty;
        CcsInfoPtr Ccs;
        VsInfoPtr Vs;

        // ---- content ----------------------------------------------------------
        std::vector<Core::Target> Targets;
        std::vector<ProjectFolderPtr> Folders;
        std::vector<BuildConfigurationPtr> Configurations;
        std::vector<std::string> DependsOn;     // other project names in the workspace
        std::vector<std::string> ExcludedFiles;
        // a CCS project's exclusions are per configuration: configuration name -> relative files
        std::map<std::string, std::vector<std::string>, Core::CaseInsensitiveLess> ExcludedByConfig;
        // the compiler the project's C and C++ go to, as RIDE's .pro spells it: auto, c90, cpp11, shalimar, cl, c++
        std::string Toolchain;
        // a folder (RIDE's group) naming its own compiler, which takes every file in it
        OrderedNoCaseMap FolderToolchains;
        // RIDE's emulateOnly: a tms6747 build stops at the assembly, which Run hands to vm6747
        bool EmulateOnly;
        // RIDE's build.args: what the program is given when it is run, one argument per entry
        std::vector<std::string> ProgramArgs;
        // RIDE's Compiler Options, per configuration (the .pro's "options")
        Settings::CompilerOptionStorePtr Options;

        // the compiler word in force for a file in a folder: the folder's, else the project's
        std::string ToolchainFor(const std::string& folderName) const;
        void SetFolderToolchain(const std::string& folderName, const std::string& word);

        std::string ProjectDir() const;
        bool IsCcs() const { return Kind == "ccs7"; }
        bool IsVs() const { return Kind == "vs"; }
        // a project read from another IDE's files, shown and built but never written
        bool IsImported() const { return IsCcs() || IsVs(); }
        // the report lines of an imported project, for the Output pane
        std::vector<std::string> ImportReportLines() const;

        // ---- paths ------------------------------------------------------------
        std::string ToAbsolute(const std::string& rel) const;
        std::string ToRelative(const std::string& abs) const;
        // every file of the project as an absolute path
        std::vector<std::string> AllFiles() const;
        bool ContainsFile(const std::string& abs) const;
        bool IsExcluded(const std::string& abs) const;
        // excluded for a configuration: by the project's list, or by the configuration's own (a CCS project)
        bool IsExcludedIn(const std::string& abs, const std::string& configName) const;
        void SetExcluded(const std::string& abs, bool excluded);
        ProjectFolderPtr FindFolder(const std::string& name) const;
        ProjectFolderPtr GetOrAddFolder(const std::string& name);
        // default logical folder for a file, decided by its extension
        static std::string DefaultFolderFor(const std::string& path);
        bool AddFile(const std::string& abs, const std::string& folderName);
        bool RemoveFile(const std::string& abs);
        bool RenameFile(const std::string& oldAbs, const std::string& newAbs);
        // the folder holding the file; false when none does
        bool FolderOfFile(const std::string& abs, std::string& folder) const;
        bool SupportsTarget(Core::Target t) const;
        // returns the configuration, creating a default one when missing
        BuildConfigurationPtr GetConfiguration(const std::string& config, Core::Target t);

        // ---- load / save --------------------------------------------------------
        // GRIDE 2.0's own .pro ("formatVersion" and no "gride"): read here, never written back
        static bool IsGrey(const Core::JsonPtr& j);
        // the Visual Studio solution a GRIDE 2.0 .pro stands for ("visualStudio"), else ""
        static std::string GreySolution(const std::string& path);
        static GrideProjectPtr FromGrey(const std::string& path, const Core::JsonPtr& j);
        // throws std::runtime_error (Core::JsonException for a malformed file)
        static GrideProjectPtr Load(const std::string& path);
        Core::JsonPtr ToJson();
        // throws std::runtime_error when the file cannot be written
        void Save();
        // creates the folder skeleton + .pro object for File > New > Project (not yet saved)
        static GrideProjectPtr CreateNew(const std::string& parentDir, const std::string& name, Core::Language lang,
                                         const std::string& templ, const std::vector<Core::Target>& targets);

    private:
        Core::JsonPtr raw;             // the file as loaded, so unknown keys survive a save
    };
}}
