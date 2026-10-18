// =============================================================================
//  Workspace.cpp - see Workspace.h
// =============================================================================
#include "Workspace.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <algorithm>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    static bool Has(const std::vector<std::string>& list, const std::string& one)
    {
        return std::find(list.begin(), list.end(), one) != list.end();
    }

    // ---- BreakpointTable --------------------------------------------------------
    std::set<int>* BreakpointTable::Find(const std::string& path)
    {
        for (std::pair<std::string, std::set<int> >& e : entries)
            if (StringUtil::EqualsIgnoreCase(e.first, path)) return &e.second;
        return nullptr;
    }

    const std::set<int>* BreakpointTable::Find(const std::string& path) const
    {
        for (const std::pair<std::string, std::set<int> >& e : entries)
            if (StringUtil::EqualsIgnoreCase(e.first, path)) return &e.second;
        return nullptr;
    }

    std::set<int>& BreakpointTable::GetOrAdd(const std::string& path)
    {
        std::set<int>* found = Find(path);
        if (found != nullptr) return *found;
        entries.push_back(std::make_pair(path, std::set<int>()));
        return entries.back().second;
    }

    void BreakpointTable::Set(const std::string& path, const std::set<int>& lines)
    {
        GetOrAdd(path) = lines;
    }

    void BreakpointTable::Remove(const std::string& path)
    {
        for (size_t i = 0; i < entries.size(); i++)
            if (StringUtil::EqualsIgnoreCase(entries[i].first, path)) { entries.erase(entries.begin() + (long)i); return; }
    }

    // ---- Workspace ----------------------------------------------------------------
    Workspace::Workspace()
        : Name("Workspace"), ActiveConfig("Debug"), ActiveTarget(Target::Windows), IsTemporary(false), IsDirty(false),
          hasSavedText(false) {}

    WorkspacePtr Workspace::ForSolution(const VsSolutionPtr& s)
    {
        WorkspacePtr w = std::make_shared<Workspace>();
        w->Name = s->Name;
        w->FilePath = Paths::Combine(s->Dir, s->Name + ".grws");
        w->IsTemporary = true;
        w->SolutionPath = s->Path;
        w->ActiveTarget = Target::Windows;
        for (const std::string& f : s->FolderPaths()) w->SolutionFolders.push_back(f);
        for (const VsSolutionEntryPtr& e : s->Entries)
        {
            if (!e->IsFolder() || e->Items.empty()) continue;
            std::string chain = s->FolderPathOf(e);
            std::string own = !chain.empty() ? chain + "/" + e->Name : e->Name;
            bool found = false;
            for (std::pair<std::string, std::vector<std::string> >& kv : w->SolutionItems)
                if (kv.first == own)
                {
                    kv.second.insert(kv.second.end(), e->Items.begin(), e->Items.end());
                    found = true;
                }
            if (!found) w->SolutionItems.push_back(std::make_pair(own, e->Items));
        }
        std::map<std::string, GrideProjectPtr> byGuid;
        for (const VsSolutionEntryPtr& e : s->CppProjects())
        {
            try
            {
                GrideProjectPtr p = VsProjectReader::Read(e->Path, s->Path);
                if (p->Name.empty() || StringUtil::EqualsIgnoreCase(p->Name, Paths::FileNameWithoutExtension(e->Path))) p->Name = e->Name;
                p->Vs->SolutionFolder = s->FolderPathOf(e);
                w->Projects.push_back(p);
                byGuid[e->Guid] = p;
            }
            catch (const std::exception& ex) { w->LoadErrors.push_back(e->RelativePath + ": " + ex.what()); }
        }
        for (const VsSolutionEntryPtr& e : s->CppProjects())
        {
            std::map<std::string, GrideProjectPtr>::const_iterator p = byGuid.find(e->Guid);
            if (p == byGuid.end()) continue;
            for (const std::string& dep : e->Dependencies)
            {
                std::map<std::string, GrideProjectPtr>::const_iterator d = byGuid.find(dep);
                if (d != byGuid.end() && d->second != p->second && !Has(p->second->DependsOn, d->second->Name))
                    p->second->DependsOn.push_back(d->second->Name);
            }
        }
        for (const std::string& n : s->Notes) w->LoadErrors.push_back(n);
        w->StartupName = !w->Projects.empty() ? w->Projects[0]->Name : std::string();
        w->IsDirty = false;
        return w;
    }

    std::string Workspace::Directory() const
    {
        return FilePath.empty() ? std::string() : Paths::DirectoryName(FilePath);
    }

    GrideProjectPtr Workspace::StartupProject() const
    {
        GrideProjectPtr p = FindProject(StartupName);
        if (!p && !Projects.empty()) p = Projects[0];
        return p;
    }

    GrideProjectPtr Workspace::FindProject(const std::string& name) const
    {
        if (name.empty()) return nullptr;
        for (const GrideProjectPtr& p : Projects)
            if (StringUtil::EqualsIgnoreCase(p->Name, name)) return p;
        return nullptr;
    }

    GrideProjectPtr Workspace::FindProjectForFile(const std::string& abs) const
    {
        for (const GrideProjectPtr& p : Projects)
            if (p->ContainsFile(abs)) return p;
        return nullptr;
    }

    bool Workspace::IsDocumentReadOnly(const std::string& abs) const
    {
        (void)abs;
        return false;
    }

    GrideProjectPtr Workspace::FindProjectOwning(const std::string& abs) const
    {
        GrideProjectPtr p = FindProjectForFile(abs);
        if (p) return p;
        for (const GrideProjectPtr& x : Projects)
            if (PathUtil::SamePath(x->FilePath, abs)) return x;
        return nullptr;
    }

    bool Workspace::PrepareWrite(const std::string& abs, const std::function<bool()>& ask)
    {
        GrideProjectPtr p = FindProjectOwning(abs);
        return Locks.Prepare(abs, p ? p->FilePath : abs, ask);
    }

    bool Workspace::AddProject(const GrideProjectPtr& p)
    {
        for (const GrideProjectPtr& x : Projects)
            if (PathUtil::SamePath(x->FilePath, p->FilePath)) return false;
        Projects.push_back(p);
        if (StartupName.empty()) StartupName = p->Name;
        IsDirty = true;
        return true;
    }

    void Workspace::RemoveProject(const GrideProjectPtr& p)
    {
        std::vector<GrideProjectPtr>::iterator at = std::find(Projects.begin(), Projects.end(), p);
        if (at != Projects.end()) Projects.erase(at);
        for (const GrideProjectPtr& x : Projects)
        {
            std::vector<std::string>::iterator d = std::find(x->DependsOn.begin(), x->DependsOn.end(), p->Name);
            if (d != x->DependsOn.end()) x->DependsOn.erase(d);
        }
        if (StringUtil::EqualsIgnoreCase(StartupName, p->Name))
            StartupName = !Projects.empty() ? Projects[0]->Name : std::string();
        IsDirty = true;
    }

    void Workspace::Visit(const GrideProjectPtr& p, std::vector<GrideProjectPtr>& result, std::vector<GrideProject*>& visiting) const
    {
        if (!p || std::find(result.begin(), result.end(), p) != result.end()
            || std::find(visiting.begin(), visiting.end(), p.get()) != visiting.end())
            return;
        visiting.push_back(p.get());
        for (const std::string& dep : p->DependsOn) Visit(FindProject(dep), result, visiting);
        result.push_back(p);
    }

    std::vector<GrideProjectPtr> Workspace::BuildOrder() const
    {
        std::vector<GrideProjectPtr> result;
        std::vector<GrideProject*> visiting;
        for (const GrideProjectPtr& p : Projects) Visit(p, result, visiting);
        return result;
    }

    std::vector<GrideProjectPtr> Workspace::BuildOrderFor(const GrideProjectPtr& root) const
    {
        std::vector<GrideProjectPtr> result;
        std::vector<GrideProject*> visiting;
        Visit(root, result, visiting);
        return result;
    }

    WorkspacePtr Workspace::Load(const std::string& path, const std::string& cgToolRoot)
    {
        JsonPtr j = JsonParser::ParseFile(path);
        WorkspacePtr w = std::make_shared<Workspace>();
        w->FilePath = Paths::FullPath(PathUtil::Normalize(path));
        w->Name = j->GetString("name", Paths::FileNameWithoutExtension(w->FilePath));
        w->StartupName = j->GetString("startup", "");
        w->ActiveConfig = j->GetString("activeConfig", "Debug");
        w->ActiveTarget = TargetInfo::Parse(j->GetString("activeTarget", "Windows"));
        std::string sln = j->GetString("solution", "");
        if (!sln.empty()) w->SolutionPath = PathUtil::MakeAbsolute(w->Directory(), sln);
        if (j->Has("solutionFolders")) w->SolutionFolders = j->GetStringList("solutionFolders");
        JsonPtr items = j->GetObject("solutionItems");
        if (items)
            for (const std::string& folder : items->KeyNames())
            {
                std::vector<std::string> abs;
                for (const std::string& rel : items->GetStringList(folder)) abs.push_back(PathUtil::MakeAbsolute(w->Directory(), rel));
                w->SolutionItems.push_back(std::make_pair(folder, abs));
            }

        JsonPtr list = j->Get("projects");
        if (list && list->IsArray())
            for (int i = 0; i < list->Count(); i++)
            {
                JsonPtr e = list->At(i);
                std::string rel = e->GetString("path", "");
                std::string kind = e->GetString("kind", "gride");
                std::string abs = PathUtil::MakeAbsolute(w->Directory(), rel);
                try
                {
                    GrideProjectPtr p;
                    if (kind == "ccs7") p = CcsProjectReader::Read(abs, cgToolRoot);
                    else if (kind == "vs")
                    {
                        std::string own = e->GetString("solution", "");
                        p = VsProjectReader::Read(abs, !own.empty() ? PathUtil::MakeAbsolute(w->Directory(), own) : w->SolutionPath);
                        p->Vs->SolutionFolder = e->GetString("solutionFolder", "");
                    }
                    else p = GrideProject::Load(abs);
                    if (p->IsImported()) ReadImportedState(e, p);
                    w->Projects.push_back(p);
                }
                catch (const std::exception& ex)
                {
                    w->LoadErrors.push_back(rel + ": " + ex.what());
                }
            }
        JsonPtr bps = j->GetObject("breakpoints");
        if (bps)
            for (const std::string& rel : bps->KeyNames())
            {
                std::set<int> lines;
                JsonPtr arr = bps->Get(rel);
                if (!arr || !arr->IsArray()) continue;   // hand-edited / malformed entry
                for (int i = 0; i < arr->Count(); i++) lines.insert((int)arr->At(i)->AsNumber());
                if (!lines.empty()) w->Breakpoints.Set(PathUtil::MakeAbsolute(w->Directory(), rel), lines);
            }
        w->IsDirty = false;
        w->savedText = JsonWriter::Write(w->ToJson());
        w->hasSavedText = true;
        return w;
    }

    WorkspacePtr Workspace::ForSingleProject(const GrideProjectPtr& p)
    {
        WorkspacePtr w = std::make_shared<Workspace>();
        w->Name = p->Name;
        w->FilePath = Paths::Combine(p->ProjectDir(), p->Name + ".grws");
        w->Projects.push_back(p);
        w->StartupName = p->Name;
        if (!p->Targets.empty()) w->ActiveTarget = p->Targets[0];
        w->IsTemporary = true;
        return w;
    }

    bool Workspace::HasChanges() const
    {
        if (IsDirty || IsTemporary || !hasSavedText) return true;
        return JsonWriter::Write(ToJson()) != savedText;
    }

    void Workspace::Save()
    {
        JsonPtr j = ToJson();
        JsonWriter::WriteFile(FilePath, j);
        savedText = JsonWriter::Write(j);
        hasSavedText = true;
        IsTemporary = false;
        IsDirty = false;
    }

    JsonPtr Workspace::ToJson() const
    {
        std::string dir = Directory();
        JsonPtr j = JsonValue::MakeObject();
        j->SetString("gride", "1.0");
        j->SetString("type", "workspace");
        j->SetString("name", Name);
        JsonPtr list = JsonValue::MakeArray();
        for (const GrideProjectPtr& p : Projects)
        {
            JsonPtr e = JsonValue::MakeObject();
            if (p->IsCcs())
            {
                e->SetString("path", PathUtil::MakeRelative(dir, p->ProjectDir()));
                e->SetString("kind", "ccs7");
                e->SetBool("readOnly", true);
            }
            else if (p->IsVs())
            {
                e->SetString("path", PathUtil::MakeRelative(dir, p->FilePath));
                e->SetString("kind", "vs");
                e->SetBool("readOnly", true);
                if (p->Vs && !p->Vs->SolutionPath.empty()) e->SetString("solution", PathUtil::MakeRelative(dir, p->Vs->SolutionPath));
                if (p->Vs && !p->Vs->SolutionFolder.empty()) e->SetString("solutionFolder", p->Vs->SolutionFolder);
            }
            else e->SetString("path", PathUtil::MakeRelative(dir, p->FilePath));
            if (p->IsImported()) WriteImportedState(e, p);
            list->Add(e);
        }
        j->SetValue("projects", list);
        if (!SolutionPath.empty()) j->SetString("solution", PathUtil::MakeRelative(dir, SolutionPath));
        if (!SolutionFolders.empty()) j->SetList("solutionFolders", SolutionFolders);
        if (!SolutionItems.empty())
        {
            JsonPtr items = JsonValue::MakeObject();
            for (const std::pair<std::string, std::vector<std::string> >& kv : SolutionItems)
            {
                std::vector<std::string> rel;
                for (const std::string& abs : kv.second) rel.push_back(PathUtil::MakeRelative(dir, abs));
                items->SetList(kv.first, rel);
            }
            j->SetValue("solutionItems", items);
        }
        j->SetString("startup", StartupName);
        j->SetString("activeConfig", ActiveConfig);
        j->SetString("activeTarget", TargetInfo::Name(ActiveTarget));
        JsonPtr bps = JsonValue::MakeObject();
        for (const std::pair<std::string, std::set<int> >& kv : Breakpoints.Items())
        {
            if (kv.second.empty()) continue;
            JsonPtr arr = JsonValue::MakeArray();
            for (int l : kv.second) arr->Add(JsonValue::FromNumber(l));
            bps->SetValue(PathUtil::MakeRelative(dir, kv.first), arr);
        }
        j->SetValue("breakpoints", bps);
        return j;
    }

    void Workspace::SaveProjects()
    {
        for (const GrideProjectPtr& p : Projects)
            if (p->IsDirty && !p->IsReadOnly) p->Save();
    }

    void Workspace::WriteImportedState(const JsonPtr& e, const GrideProjectPtr& p)
    {
        if (!StringUtil::IsNullOrWhiteSpace(p->Toolchain) && !StringUtil::EqualsIgnoreCase(StringUtil::Trim(p->Toolchain), "auto"))
            e->SetString("toolchain", StringUtil::Trim(p->Toolchain));
        if (!p->FolderToolchains.Empty())
        {
            JsonPtr ft = JsonValue::MakeObject();
            for (const std::pair<std::string, std::string>& kv : p->FolderToolchains.Items()) ft->SetString(kv.first, kv.second);
            e->SetValue("folderToolchains", ft);
        }
        if (!p->ProgramArgs.empty()) e->SetList("programArgs", p->ProgramArgs);
        if (p->EmulateOnly) e->SetBool("emulateOnly", true);
        JsonPtr settings = ImportedSettingsChanged(p);
        if (settings) e->SetValue("settings", settings);
    }

    JsonPtr Workspace::ImportedSettingsChanged(const GrideProjectPtr& p)
    {
        JsonPtr now = p->ImportedSettings();
        JsonPtr settings = JsonValue::MakeObject();
        for (const std::string& k : now->KeyNames())
        {
            JsonPtr was = p->ImportedBaseline ? p->ImportedBaseline->Get(k) : JsonPtr();
            if (!was || JsonWriter::Write(was) != JsonWriter::Write(now->Get(k))) settings->SetValue(k, now->Get(k));
        }
        return settings->KeyNames().empty() ? JsonPtr() : settings;
    }

    void Workspace::ReadImportedState(const JsonPtr& e, const GrideProjectPtr& p)
    {
        p->Toolchain = e->GetString("toolchain", "auto");
        JsonPtr ft = e->GetObject("folderToolchains");
        if (ft)
            for (const std::string& name : ft->KeyNames())
            {
                std::string w = ft->GetString(name, "");
                if (!StringUtil::IsNullOrWhiteSpace(w) && !StringUtil::EqualsIgnoreCase(StringUtil::Trim(w), "auto"))
                    p->FolderToolchains.Set(name, StringUtil::Trim(w));
            }
        if (e->Has("programArgs")) p->ProgramArgs = e->GetStringList("programArgs");
        p->EmulateOnly = e->GetBool("emulateOnly", false);
        p->ApplyImportedSettings(e->GetObject("settings"));
        p->IsDirty = false;
    }
    // ---- DiskLocks ----------------------------------------------------------------
    bool DiskLocks::IsWritable(const std::string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return true;           // not there yet: nothing to unlock
        return access(path.c_str(), W_OK) == 0;
    }

    bool DiskLocks::Prepare(const std::string& path, const std::string& key, const std::function<bool()>& ask)
    {
        if (IsWritable(path)) return true;
        bool known = false;
        for (const std::string& k : agreed)
            if (PathUtil::SamePath(k, key)) { known = true; break; }
        if (!known)
        {
            if (!ask || !ask()) return false;
            agreed.push_back(key);
        }
        return Unlock(path);
    }

    bool DiskLocks::Unlock(const std::string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return false;
        bool listed = false;
        for (const std::pair<std::string, unsigned>& e : entries)
            if (PathUtil::SamePath(e.first, path)) { listed = true; break; }
        if (!listed) entries.push_back(std::make_pair(path, (unsigned)(st.st_mode & 07777)));
        return chmod(path.c_str(), (st.st_mode & 07777) | S_IWUSR) == 0 && access(path.c_str(), W_OK) == 0;
    }

    void DiskLocks::RelockAll()
    {
        // a save may have replaced the file (written beside it, then renamed): its mode is set again
        for (const std::pair<std::string, unsigned>& e : entries) chmod(e.first.c_str(), (mode_t)e.second);
        entries.clear();
        agreed.clear();
    }
}}
