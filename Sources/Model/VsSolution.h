// =============================================================================
//  VsSolution.h - Visual Studio solutions and C++ projects, read only
//
//    .sln      Visual Studio 2005 .. 2022 solutions: the projects, the solution folders
//              and their items, the dependencies, the configurations and platforms and
//              what each project builds for them
//    .vcxproj  Visual Studio 2010 and later (MSBuild): the configurations, the property
//              and item-definition groups with their conditions, the items (wildcards
//              expanded), the exclusions per configuration, the .filters beside it
//    .vcproj   Visual Studio 2005 / 2008 (the classic XML)
//
//  Nothing is ever written to these files. On the Mac they are read for the
//  explorer and for GRIDEm's own toolchain; a part given to Microsoft's cl is
//  Windows only. (GRIDEd's Model/VsSolution.h)
// =============================================================================
#pragma once
#include "../Core/StringUtil.h"
#include "XmlReader.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace GRIDEm { namespace Model {

    typedef std::set<std::string, Core::CaseInsensitiveLess> NoCaseSet;
    typedef std::map<std::string, std::string, Core::CaseInsensitiveLess> NoCaseMap;

    // ---- one Project(...) line of a .sln, with its sections -------------------
    class VsSolutionEntry
    {
    public:
        VsSolutionEntry() : Kind("other") {}
        std::string Name;
        std::string Path;                    // absolute; "" for a solution folder
        std::string RelativePath;            // as the .sln wrote it
        std::string Guid;                    // without braces, upper case
        std::string TypeGuid;
        std::string ParentGuid;              // the solution folder it is nested in, or ""
        std::string Kind;                    // vcxproj / vcproj / folder / other
        std::vector<std::string> Dependencies;   // GUIDs (ProjectDependencies)
        std::vector<std::string> Items;          // a solution folder's files, absolute
        NoCaseMap ConfigMap;                     // solution configuration -> the project's (ActiveCfg)
        NoCaseSet Builds;                        // the solution configurations the project builds in (Build.0)

        bool IsFolder() const { return Kind == "folder"; }
        bool IsCpp() const { return Kind == "vcxproj" || Kind == "vcproj"; }
    };
    typedef std::shared_ptr<VsSolutionEntry> VsSolutionEntryPtr;

    // ---- the .sln -------------------------------------------------------------
    class VsSolution;
    typedef std::shared_ptr<VsSolution> VsSolutionPtr;

    class VsSolution
    {
    public:
        static const char* const CppTypeGuid;      // "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"
        static const char* const FolderTypeGuid;   // "2150E333-8FDC-42A3-9474-1A3956D46DE8"

        std::string Path;
        std::string Dir;
        std::string Name;
        std::string FormatVersion;           // 12.00
        std::string VisualStudioVersion;     // "Visual Studio Version 17, version 17.0..."
        std::vector<VsSolutionEntryPtr> Entries;
        std::vector<std::string> Configurations;   // "Debug|x64" ...
        std::vector<std::string> Notes;

        static bool IsSolution(const std::string& path);
        // "Visual Studio 2010 (format 11.00)"
        std::string Version() const;
        std::vector<VsSolutionEntryPtr> CppProjects() const;
        VsSolutionEntryPtr ByGuid(const std::string& guid) const;
        // the solution folders an entry sits in, outermost first: "Libs/Inner"
        std::string FolderPathOf(const VsSolutionEntryPtr& e) const;
        // every solution folder path, outer ones first
        std::vector<std::string> FolderPaths() const;
        static std::string CleanGuid(const std::string& g);
        // throws std::runtime_error when the file cannot be read
        static VsSolutionPtr Read(const std::string& path);
    };

    // ---- a project's item (a file) --------------------------------------------
    class VsProjectItem
    {
    public:
        VsProjectItem() : FromWildcard(false) {}
        std::string Include;                 // as written (the pattern, for a wildcard item)
        std::string Path;                    // absolute
        std::string Type;                    // ClCompile, ClInclude, None, ResourceCompile, Text, ...
        std::string Filter;                  // the .filters folder, "" for the root
        bool FromWildcard;
        NoCaseSet Excluded;                  // configurations ("Debug|x64") excluded in; "*" for all
        std::vector<std::string> Settings;   // per-file settings not mapped (PrecompiledHeader ...)

        bool ExcludedIn(const std::string& config) const;
    };
    typedef std::shared_ptr<VsProjectItem> VsProjectItemPtr;

    // ---- one configuration|platform of a project ------------------------------
    class VsProjectConfig
    {
    public:
        explicit VsProjectConfig(const std::string& name);
        std::string Name;                    // Debug|x64
        std::string Configuration;           // Debug
        std::string Platform;                // x64
        std::vector<std::string> IncludeDirs;    // absolute, macros expanded
        std::vector<std::string> Defines;
        std::vector<std::string> Undefines;
        std::vector<std::string> Libs;           // AdditionalDependencies
        std::vector<std::string> LibDirs;        // AdditionalLibraryDirectories, absolute
        std::string Optimization;            // Disabled / MinSpace / MaxSpeed / Full, or "" when not set
        std::string AdditionalOptions;
        std::string OutDir;
        std::string IntDir;
        std::string TargetName;
        std::string ConfigurationType;       // Application / StaticLibrary / DynamicLibrary
        std::string Toolset;
        std::string CharacterSet;
        std::string CompileAs;               // CompileAsC / CompileAsCpp / Default
        std::string SubSystem;
        bool ClrSupport;
        bool UseDebugLibrariesSaid;          // GRIDEd's UseDebugLibraries != null
        std::string UseDebugLibraries;       // "true" / "false" when said
        // the rest of the ClCompile settings, MSBuild's names and values, the last group's winning
        NoCaseMap ClSettings;
        std::vector<std::string> ClSettingOrder;     // the order the names were first seen (GRIDEd's Dictionary order)
        std::vector<std::string> Notes;              // what was seen and not mapped, for this configuration

        bool IsDebugLike() const;
        void SetClSetting(const std::string& name, const std::string& value);
    };
    typedef std::shared_ptr<VsProjectConfig> VsProjectConfigPtr;

    // ---- the project file -----------------------------------------------------
    class VsProject;
    typedef std::shared_ptr<VsProject> VsProjectPtr;

    class VsProject
    {
    public:
        std::string Path;
        std::string Dir;
        std::string Name;
        std::string Kind;                    // vcxproj / vcproj
        std::string Guid;
        std::string ToolsVersion;
        std::string Toolset;                 // the first configuration's PlatformToolset
        std::string Version;                 // "Visual Studio 2022, toolset v143"
        std::string SolutionDir;             // for $(SolutionDir); "" when the project was opened alone
        std::string FiltersFile;             // the .filters read, or ""
        std::vector<VsProjectConfigPtr> Configurations;
        std::vector<VsProjectItemPtr> Items;
        std::vector<std::string> Filters;    // the filter paths, as written (Source\UI)
        std::vector<std::string> Notes;      // what is in the file and not mapped
        std::vector<std::string> References;         // assembly references (C++/CLI)
        std::vector<std::string> ProjectReferences;  // other projects referenced

        static bool IsProjectFile(const std::string& path);
        VsProjectConfigPtr Config(const std::string& name) const;
        std::vector<std::string> Platforms() const;
        std::vector<std::string> ConfigurationNames() const;
        std::vector<VsProjectItemPtr> ItemsOfType(const std::string& type) const;
        // cppparse's describeToolsVersion
        static std::string DescribeToolsVersion(const std::string& toolsVersion, const std::string& toolset);
        // MSBuild's macros a project file uses for its own paths; an unknown one is left as written
        std::string ExpandMacros(const std::string& s, const VsProjectConfigPtr& c) const;
        static bool HasMacro(const std::string& s);
        // throws std::runtime_error when the file is missing or not a C++ project
        static VsProjectPtr Read(const std::string& path, const std::string& solutionDir);
        // the dialog's text: the file, the version, every configuration's values, the items, the notes
        std::string MappingText() const;

    private:
        bool Matches(const std::string& condition, const VsProjectConfigPtr& c);
        std::string ExpandForCondition(const std::string& s, const VsProjectConfigPtr& c) const;
        void ReadVcxproj(const XmlDocument& doc);
        void NoteImport(const std::string& project);
        void ApplyDirs(std::vector<std::string>& into, const std::string& value, const VsProjectConfigPtr& c);
        void AddItems(const std::string& type, const std::string& include, const std::string& exclude,
                      const XmlNodePtr& e, const std::string& groupCondition);
        std::vector<std::string> Expand(const std::string& pattern) const;
        void ReadFilters();
        void ReadVcproj(const XmlDocument& doc);
        void ReadVcprojFiles(const XmlNodePtr& parent, const std::string& filter);
    };
}}
