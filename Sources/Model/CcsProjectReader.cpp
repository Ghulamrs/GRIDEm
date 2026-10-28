// =============================================================================
//  CcsProjectReader.cpp - see CcsProjectReader.h
// =============================================================================
#include "CcsProjectReader.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <algorithm>
#include <stdexcept>

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    static bool Has(const std::vector<std::string>& list, const std::string& one)
    {
        return std::find(list.begin(), list.end(), one) != list.end();
    }

    std::string CcsProjectReader::ResolveFolder(const std::string& pathOrFolder)
    {
        if (pathOrFolder.empty()) return std::string();
        std::string p = PathUtil::Normalize(pathOrFolder);
        if (Paths::IsDirectory(p)) return Paths::FullPath(p);
        if (Paths::IsFile(p)) return Paths::DirectoryName(Paths::FullPath(p));
        return std::string();
    }

    bool CcsProjectReader::IsCcsProject(const std::string& pathOrFolder)
    {
        std::string dir = ResolveFolder(pathOrFolder);
        if (dir.empty()) return false;
        return Paths::IsFile(Paths::Combine(dir, ".project"))
            && (Paths::IsFile(Paths::Combine(dir, ".ccsproject")) || Paths::IsFile(Paths::Combine(dir, ".cproject")));
    }

    bool CcsProjectReader::IsCcsWorkspace(const std::string& folder) { return CcsWorkspace::IsWorkspace(folder); }

    std::string CcsProjectReader::Relative(const std::string& dir, const std::string& absSlashed)
    {
        std::string abs = PathUtil::Normalize(absSlashed);
        if (Paths::IsRooted(abs))
        {
            std::string rel = PathUtil::MakeRelative(dir, abs);
            if (!StringUtil::StartsWith(rel, "..") && !Paths::IsRooted(rel)) return rel;
        }
        return abs;
    }

    GrideProjectPtr CcsProjectReader::Read(const std::string& pathOrFolder, const std::string& cgToolRoot)
    {
        std::string dir = ResolveFolder(pathOrFolder);
        if (dir.empty() || !IsCcsProject(dir)) throw std::runtime_error("Not a Code Composer Studio project: " + pathOrFolder);

        // the workspace the folder is registered in, for WORKSPACE_LOC, path variables and build macros
        CcsWorkspacePtr ws;
        std::string wsDir = CcsWorkspace::WorkspaceOf(dir);
        if (!wsDir.empty()) { std::string error; ws = CcsWorkspace::Read(wsDir, error); }
        CcsReadingPtr r = CcsReading::Read(dir, cgToolRoot, ws);

        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->Kind = "ccs7";
        p->IsReadOnly = true;
        p->FilePath = Paths::Combine(dir, ".project");
        p->Name = r->Name;
        p->Template = r->CcsVersion.empty() ? "CCS 5.5 project" : "CCS 7.4 project";
        p->Targets.push_back(Target::C6747);
        p->Ccs = std::make_shared<CcsInfo>();
        p->Ccs->DeviceVariant = r->Device;
        p->Ccs->DeviceFamily = r->Family;
        p->Ccs->CodegenVersion = r->CgtVersion;
        p->Ccs->OutputFormat = r->Elf ? "ELF" : "COFF";
        p->Ccs->Workspace = r->Workspace;
        p->Ccs->Reading = r;
        // the linker command file: the first .cmd of a configuration (the folder's, relative)
        for (const CcsConfigPtr& c : r->Configs)
            if (!c->Link.CmdFiles.empty()) { p->Ccs->LinkerCommandFile = Relative(dir, c->Link.CmdFiles[0]); break; }

        // the files, grouped by their folder relative to the project; linked ones under "(linked)"
        bool anyCpp = false;
        std::vector<std::string> all;
        for (const CcsConfigPtr& c : r->Configs)
        {
            for (const std::string& s : c->Sources) if (!Has(all, s)) all.push_back(s);
            for (const std::string& s : c->Link.CmdFiles) if (!Has(all, s)) all.push_back(s);
        }
        for (const std::string& h : r->Headers)
        {
            std::string abs = r->Dir + "/" + h;
            if (!Has(all, abs)) all.push_back(abs);
        }
        for (const std::string& n : r->NotBuilt)
        {
            std::string abs = Paths::IsRooted(n) ? n : r->Dir + "/" + n;
            if (!Has(all, abs)) all.push_back(abs);
        }
        // a file every configuration excludes is still shown, excluded; one some configuration excludes is excluded there
        for (const CcsConfigPtr& c : r->Configs)
            for (const std::string& e : c->Excluded)
            {
                std::string abs = r->Dir + "/" + e;
                if (Paths::IsFile(abs) && !Has(all, abs)) all.push_back(abs);
            }
        for (const std::string& absSlashed : all)
        {
            std::string abs = PathUtil::Normalize(absSlashed);
            std::string rel = PathUtil::MakeRelative(dir, abs);
            std::string folder = Paths::DirectoryName(rel);
            if (folder.empty()) folder = "(project root)";
            if (Paths::IsRooted(rel) || StringUtil::StartsWith(rel, "..")) folder = "(linked)";
            ProjectFolderPtr f = p->GetOrAddFolder(folder);
            if (!Has(f->Files, rel)) f->Files.push_back(rel);
            if (PathUtil::HasExtension(abs, ".cpp")) anyCpp = true;
        }
        p->Lang = anyCpp ? Language::Cpp11 : Language::C90;

        // the configurations as GRIDEm builds them: RIDE's mapping, into the fields the engine reads
        for (const CcsConfigPtr& c : r->Configs)
        {
            BuildConfigurationPtr b = std::make_shared<BuildConfiguration>(c->IsDebug ? "Debug" : "Release", Target::C6747);
            b->Defines.clear(); b->IncludeDirs.clear(); b->Libs.clear(); b->LibDirs.clear();
            b->OutDir = ".gride/out/" + c->Name;
            b->ObjDir = ".gride/obj/" + c->Name;
            b->Defines.insert(b->Defines.end(), c->Defines.begin(), c->Defines.end());
            for (const std::string& inc : c->Includes) b->IncludeDirs.push_back(Relative(dir, inc));
            std::string opt = c->Opt;
            while (!opt.empty() && opt[0] == '-') opt = opt.substr(1);
            b->Optimise = c->Opt.empty() ? std::string("O0") : opt;
            // the undefines and --no_compress go as the compiler's extra flags (the defines carry the define flag)
            std::vector<std::string> extra;
            for (const std::string& u : c->Undefines) extra.push_back("-U" + u);
            if (c->NoCompress) extra.push_back("--no_compress");
            b->ExtraCompileFlags = StringUtil::Join(" ", extra);
            for (const std::string& s : c->Link.SearchPaths) b->LibDirs.push_back(Relative(dir, s));
            b->Libs.insert(b->Libs.end(), c->Link.Libraries.begin(), c->Link.Libraries.end());
            std::vector<std::string> link;
            if (!c->Link.Heap.empty()) link.push_back("--heap_size=" + c->Link.Heap);
            if (!c->Link.Stack.empty()) link.push_back("--stack_size=" + c->Link.Stack);
            if (c->Link.RomModel == 1) link.push_back("--rom_model");
            else if (c->Link.RomModel == 0) link.push_back("--ram_model");
            b->ExtraLinkFlags = StringUtil::Join(" ", link);
            if (!c->Link.CmdFiles.empty()) b->LinkerCmd = Relative(dir, c->Link.CmdFiles[0]);
            p->Configurations.push_back(b);
            std::vector<std::string> excluded;
            for (const std::string& e : c->Excluded) excluded.push_back(StringUtil::ReplaceChar(e, '/', '\\'));
            p->ExcludedByConfig[b->Config] = excluded;
        }
        if (p->Configurations.empty())
        {
            p->Configurations.push_back(std::make_shared<BuildConfiguration>("Debug", Target::C6747));
            p->Configurations.push_back(std::make_shared<BuildConfiguration>("Release", Target::C6747));
            for (const BuildConfigurationPtr& c : p->Configurations)
            {
                c->OutDir = ".gride/out/" + c->Config;
                c->ObjDir = ".gride/obj/" + c->Config;
            }
        }
        if (!p->Ccs->LinkerCommandFile.empty())
            for (const BuildConfigurationPtr& c : p->Configurations)
                if (c->LinkerCmd.empty()) c->LinkerCmd = p->Ccs->LinkerCommandFile;

        // the lines the Output pane shows (RIDE's Messages and the sources line)
        for (const CcsConfigPtr& c : r->Configs) p->Ccs->ReportLines.push_back(r->Report(c->IsDebug));
        std::string sources = r->SourceReport();
        if (!sources.empty()) p->Ccs->ReportLines.push_back(sources);
        for (const std::string& refusal : r->Refusals)
            p->Ccs->ReportLines.push_back("CCS project " + r->Name + ": GRIDE would refuse it - " + refusal
                                          + "; read all the same, the build stands on what was read");
        p->IsDirty = false;
        p->ImportedBaseline = p->ImportedSettings();      // what the file says, before the workspace's own
        return p;
    }
}}
