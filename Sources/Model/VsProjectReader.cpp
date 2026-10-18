// =============================================================================
//  VsProjectReader.cpp - see VsProjectReader.h
// =============================================================================
#include "VsProjectReader.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <algorithm>

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    static bool Has(const std::vector<std::string>& list, const std::string& one)
    {
        return std::find(list.begin(), list.end(), one) != list.end();
    }

    bool VsProjectReader::IsVsProject(const std::string& path)
    {
        return VsProject::IsProjectFile(path) && Paths::IsFile(PathUtil::Normalize(path));
    }

    bool VsProjectReader::IsSolution(const std::string& path)
    {
        return VsSolution::IsSolution(path) && Paths::IsFile(PathUtil::Normalize(path));
    }

    VsProjectConfigPtr VsProjectReader::Pick(const VsProjectPtr& r, const std::string& name)
    {
        const char* platforms[] = { "x64", "Win32", "" };
        for (const char* platform : platforms)
            for (const VsProjectConfigPtr& c : r->Configurations)
                if (StringUtil::EqualsIgnoreCase(c->Configuration, name)
                    && (platform[0] == '\0' || StringUtil::EqualsIgnoreCase(c->Platform, platform)))
                    return c;
        return nullptr;
    }

    std::string VsProjectReader::OptimiseOf(const VsProjectConfigPtr& c)
    {
        const std::string& o = c->Optimization;
        if (StringUtil::EqualsIgnoreCase(o, "Disabled")) return "O0";
        if (StringUtil::EqualsIgnoreCase(o, "MinSpace")) return "O1";
        if (StringUtil::EqualsIgnoreCase(o, "MaxSpeed")) return "O2";
        if (StringUtil::EqualsIgnoreCase(o, "Full")) return "O3";
        return c->IsDebugLike() ? "O0" : "O2";
    }

    std::string VsProjectReader::Relative(const std::string& dir, const std::string& abs)
    {
        std::string rel = PathUtil::MakeRelative(dir, abs);
        if (!StringUtil::StartsWith(rel, "..") && !Paths::IsRooted(rel)) return rel;
        return abs;
    }

    GrideProjectPtr VsProjectReader::Read(const std::string& path, const std::string& solutionPath)
    {
        std::string sln = StringUtil::IsNullOrWhiteSpace(solutionPath) ? std::string() : Paths::FullPath(PathUtil::Normalize(solutionPath));
        std::string solutionDir = sln.empty() ? std::string() : Paths::DirectoryName(sln);
        VsProjectPtr r = VsProject::Read(path, solutionDir);
        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->Kind = "vs";
        p->IsReadOnly = true;
        p->FilePath = r->Path;
        p->Name = r->Name;
        p->Template = "Visual Studio project (" + r->Kind + (!r->Toolset.empty() ? ", " + r->Toolset : std::string()) + ")";
        p->Targets.push_back(Target::Windows);
        p->Vs = std::make_shared<VsInfo>();
        p->Vs->Reading = r;
        p->Vs->SolutionPath = sln;
        p->Vs->Version = r->Version;
        p->Vs->Toolset = r->Toolset;
        p->Vs->Guid = r->Guid;
        for (const VsProjectConfigPtr& c : r->Configurations) p->Vs->Configurations.push_back(c->Name);

        // the files, in their filters; a file outside the project folder under "(outside)" when no filter names it
        bool anyCpp = false, anyC = false;
        for (const VsProjectItemPtr& i : r->Items)
        {
            std::string rel = PathUtil::MakeRelative(p->ProjectDir(), i->Path);
            std::string folder = i->Filter;
            if (folder.empty())
            {
                std::string d = StringUtil::ReplaceChar(Paths::DirectoryName(rel), '/', '\\');
                folder = (Paths::IsRooted(rel) || StringUtil::StartsWith(rel, "..")) ? std::string("(outside)")
                       : d.empty() ? std::string("(project root)") : d;
            }
            ProjectFolderPtr f = p->GetOrAddFolder(folder);
            if (!Has(f->Files, rel)) f->Files.push_back(rel);
            std::string e = StringUtil::ToLower(Paths::Extension(i->Path));
            bool source = e == ".cpp" || e == ".cxx" || e == ".cc" || e == ".c" || e == ".c++";
            if (StringUtil::EqualsIgnoreCase(i->Type, "ClCompile")) { if (e == ".c") anyC = true; else anyCpp = true; }
            // a source the project does not compile (a None item) is shown and left out of the build
            if (source && !StringUtil::EqualsIgnoreCase(i->Type, "ClCompile") && !Has(p->ExcludedFiles, rel)) p->ExcludedFiles.push_back(rel);
            if (i->Excluded.count("*") > 0 && !Has(p->ExcludedFiles, rel)) p->ExcludedFiles.push_back(rel);
        }
        p->Lang = anyCpp || !anyC ? Language::Cpp11 : Language::C90;

        // Debug and Release for Windows from the project's configurations: x64 before Win32; others are said
        std::vector<std::string> mappedFrom;
        const char* names[] = { "Debug", "Release" };
        for (const char* n : names)
        {
            std::string name = n;
            VsProjectConfigPtr c = Pick(r, name);
            BuildConfigurationPtr b = std::make_shared<BuildConfiguration>(name, Target::Windows);
            b->Defines.clear(); b->IncludeDirs.clear(); b->Libs.clear(); b->LibDirs.clear();
            b->OutDir = ".gride/out/" + name;
            b->ObjDir = ".gride/obj/" + name;
            b->LinkerCmd.clear();
            if (c)
            {
                mappedFrom.push_back(name + " from " + c->Name);
                p->Vs->MappedConfigs[name] = c->Name;
                b->Defines.insert(b->Defines.end(), c->Defines.begin(), c->Defines.end());
                for (const std::string& inc : c->IncludeDirs) b->IncludeDirs.push_back(Relative(p->ProjectDir(), inc));
                for (const std::string& lib : c->Libs) b->Libs.push_back(lib);
                for (const std::string& d : c->LibDirs) b->LibDirs.push_back(Relative(p->ProjectDir(), d));
                b->Optimise = OptimiseOf(c);
                std::vector<std::string> extra;
                for (const std::string& u : c->Undefines) extra.push_back("-U" + u);
                if (!c->AdditionalOptions.empty())
                    p->Vs->ReportLines.push_back("VS project " + r->Name + " (" + c->Name
                        + "): cl's additional options are given to cl only (Project > Properties > Compiler), not to RIDE's compilers: "
                        + c->AdditionalOptions);
                b->ExtraCompileFlags = StringUtil::Join(" ", extra);
                if (!c->TargetName.empty() && !VsProject::HasMacro(c->TargetName) && !StringUtil::EqualsIgnoreCase(c->TargetName, r->Name))
                    b->OutputName = c->TargetName;
                std::vector<std::string> excluded;
                for (const VsProjectItemPtr& i : r->Items)
                    if (i->ExcludedIn(c->Name) && i->Excluded.count("*") == 0)
                        excluded.push_back(StringUtil::ReplaceChar(PathUtil::MakeRelative(p->ProjectDir(), i->Path), '/', '\\'));
                p->ExcludedByConfig[name] = excluded;
                for (const std::string& note : c->Notes)
                    p->Vs->ReportLines.push_back("VS project " + r->Name + " (" + c->Name + "): not mapped - " + note);
            }
            else
            {
                b->Optimise = name == "Debug" ? "O0" : "O2";
                p->Vs->ReportLines.push_back("VS project " + r->Name + ": no " + name + " configuration in the file - GRIDE's defaults for " + name);
            }
            p->Configurations.push_back(b);
        }
        // what the file has beyond Debug and Release for x64 / Win32
        std::vector<std::string> others;
        for (const VsProjectConfigPtr& c : r->Configurations)
        {
            bool mapped = false;
            for (NoCaseMap::const_iterator it = p->Vs->MappedConfigs.begin(); it != p->Vs->MappedConfigs.end(); ++it)
                if (it->second == c->Name) mapped = true;
            if (!mapped) others.push_back(c->Name);
        }
        p->Vs->ReportLines.insert(p->Vs->ReportLines.begin(),
            "VS project " + r->Name + ": " + r->Version + "; " + StringUtil::Join(", ", mappedFrom)
            + (!others.empty() ? "; not built here: " + StringUtil::Join(", ", others) : std::string()));
        for (const std::string& note : r->Notes) p->Vs->ReportLines.push_back("VS project " + r->Name + ": not mapped - " + note);
        p->IsDirty = false;
        return p;
    }
}}
