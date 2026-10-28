// =============================================================================
//  GrideProject.cpp - see GrideProject.h
// =============================================================================
#include "GrideProject.h"
#include "../Core/PathUtil.h"
#include "../Platform/Paths.h"
#include <algorithm>

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    // ---- OrderedNoCaseMap -------------------------------------------------------
    bool OrderedNoCaseMap::Find(const std::string& key, std::string& value) const
    {
        for (const std::pair<std::string, std::string>& kv : items)
            if (StringUtil::EqualsIgnoreCase(kv.first, key)) { value = kv.second; return true; }
        return false;
    }

    void OrderedNoCaseMap::Set(const std::string& key, const std::string& value)
    {
        for (std::pair<std::string, std::string>& kv : items)
            if (StringUtil::EqualsIgnoreCase(kv.first, key)) { kv.second = value; return; }
        items.push_back(std::make_pair(key, value));
    }

    void OrderedNoCaseMap::Remove(const std::string& key)
    {
        for (size_t i = 0; i < items.size(); i++)
            if (StringUtil::EqualsIgnoreCase(items[i].first, key)) { items.erase(items.begin() + (long)i); return; }
    }

    // ---- GrideProject -----------------------------------------------------------
    GrideProject::GrideProject()
        : Name("Project"), Lang(Language::C90), Template("Empty"), Kind("gride"), IsReadOnly(false), IsDirty(false),
          Toolchain("auto"), EmulateOnly(false), Options(std::make_shared<Settings::CompilerOptionStore>()),
          BuildGroupsSaid(false) {}

    std::string GrideProject::ProgramName() const
    {
        return StringUtil::IsNullOrWhiteSpace(TargetName) ? Name : TargetName;
    }

    bool GrideProject::IsBuilt(const std::string& abs) const
    {
        if (!BuildGroupsSaid) return true;
        std::string folder;
        if (!FolderOfFile(abs, folder)) return true;
        return StringUtil::ContainsIgnoreCase(BuildGroups, folder);
    }

    bool GrideProject::FileToOpen(std::string& path) const
    {
        std::string named = raw && raw->IsObject() ? raw->GetString("open", "") : std::string();
        if (!StringUtil::IsNullOrWhiteSpace(named) && Paths::IsFile(ToAbsolute(named))) { path = ToAbsolute(named); return true; }
        std::vector<std::string> files = AllFiles();
        if (MainSource::Find(files, path)) return true;
        for (const std::string& f : files)
            if (Paths::IsFile(f)) { path = f; return true; }
        return false;
    }

    // ---- MainSource -------------------------------------------------------------------
    static std::string Uncommented(const std::string& line, bool shalimar, bool& inComment)
    {
        std::string out;
        for (size_t i = 0; i < line.size(); i++)
        {
            if (inComment)
            {
                if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') { inComment = false; i++; }
                continue;
            }
            if (!shalimar && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') { inComment = true; i++; continue; }
            if (!shalimar && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') break;
            out += line[i];
        }
        return out;
    }

    static bool DefinesMain(const std::string& line, bool shalimar)
    {
        size_t at = line.find("main");
        if (at == std::string::npos) return false;
        if (shalimar) return line.find("fun") != std::string::npos && line.compare(at, 6, "main()") == 0;
        if (at > 0 && StringUtil::IsIdentifierChar(line[at - 1])) return false;
        size_t paren = at + 4;
        while (paren < line.size() && (line[paren] == ' ' || line[paren] == '\t')) paren++;
        if (paren >= line.size() || line[paren] != '(') return false;
        std::string t = StringUtil::TrimEnd(line);
        return t.empty() || t[t.size() - 1] != ';';
    }

    bool MainSource::Find(const std::vector<std::string>& files, std::string& found)
    {
        for (const std::string& f : files)
        {
            std::string ext = StringUtil::ToLower(Paths::Extension(f));
            bool shalimar = ext == ".shl" || ext == ".shm";
            if (!shalimar && ext != ".c" && ext != ".cpp" && ext != ".cc" && ext != ".cxx") continue;
            std::string text, error;
            if (!Paths::ReadFile(f, text, error)) continue;
            bool inComment = false;
            for (const std::string& line : StringUtil::SplitLines(text))
                if (DefinesMain(Uncommented(line, shalimar, inComment), shalimar)) { found = f; return true; }
        }
        return false;
    }

    std::string GrideProject::ToolchainFor(const std::string& folderName) const
    {
        std::string w;
        if (!folderName.empty() && FolderToolchains.Find(folderName, w) && !StringUtil::IsNullOrWhiteSpace(w)) return w;
        return StringUtil::IsNullOrWhiteSpace(Toolchain) ? std::string("auto") : Toolchain;
    }

    void GrideProject::SetFolderToolchain(const std::string& folderName, const std::string& word)
    {
        if (StringUtil::IsNullOrWhiteSpace(word) || StringUtil::EqualsIgnoreCase(StringUtil::Trim(word), "auto"))
            FolderToolchains.Remove(folderName);
        else FolderToolchains.Set(folderName, StringUtil::Trim(word));
        IsDirty = true;
    }

    std::string GrideProject::ProjectDir() const
    {
        return FilePath.empty() ? std::string() : Paths::DirectoryName(FilePath);
    }

    std::vector<std::string> GrideProject::ImportReportLines() const
    {
        if (IsCcs() && Ccs) return Ccs->ReportLines;
        if (IsVs() && Vs) return Vs->ReportLines;
        if (IsRide()) return RideReportLines;
        return std::vector<std::string>();
    }

    std::string GrideProject::ToAbsolute(const std::string& rel) const { return PathUtil::MakeAbsolute(ProjectDir(), rel); }
    std::string GrideProject::ToRelative(const std::string& abs) const { return PathUtil::MakeRelative(ProjectDir(), abs); }

    std::vector<std::string> GrideProject::AllFiles() const
    {
        std::vector<std::string> r;
        for (const ProjectFolderPtr& f : Folders)
            for (const std::string& rel : f->Files) r.push_back(ToAbsolute(rel));
        return r;
    }

    bool GrideProject::ContainsFile(const std::string& abs) const
    {
        for (const std::string& f : AllFiles())
            if (PathUtil::SamePath(f, abs)) return true;
        return false;
    }

    bool GrideProject::IsExcluded(const std::string& abs) const
    {
        std::string rel = ToRelative(abs);
        for (const std::string& e : ExcludedFiles)
            if (StringUtil::EqualsIgnoreCase(PathUtil::Normalize(e), rel)) return true;
        return false;
    }

    bool GrideProject::IsExcludedIn(const std::string& abs, const std::string& configName) const
    {
        if (IsExcluded(abs)) return true;
        if (configName.empty()) return false;
        std::map<std::string, std::vector<std::string>, CaseInsensitiveLess>::const_iterator own = ExcludedByConfig.find(configName);
        if (own == ExcludedByConfig.end()) return false;
        std::string rel = StringUtil::ReplaceChar(ToRelative(abs), '/', '\\');
        for (const std::string& e : own->second)
        {
            std::string x = StringUtil::ReplaceChar(e, '/', '\\');
            if (StringUtil::EqualsIgnoreCase(x, rel)) return true;
            if (rel.size() > x.size() && StringUtil::StartsWithIgnoreCase(rel, x) && rel[x.size()] == '\\') return true;
        }
        return false;
    }

    void GrideProject::SetExcluded(const std::string& abs, bool excluded)
    {
        std::string rel = ToRelative(abs);
        for (size_t i = ExcludedFiles.size(); i > 0; i--)
            if (StringUtil::EqualsIgnoreCase(ExcludedFiles[i - 1], rel)) ExcludedFiles.erase(ExcludedFiles.begin() + (long)(i - 1));
        if (excluded) ExcludedFiles.push_back(rel);
        IsDirty = true;
    }

    ProjectFolderPtr GrideProject::FindFolder(const std::string& name) const
    {
        for (const ProjectFolderPtr& f : Folders)
            if (StringUtil::EqualsIgnoreCase(f->Name, name)) return f;
        return nullptr;
    }

    ProjectFolderPtr GrideProject::GetOrAddFolder(const std::string& name)
    {
        ProjectFolderPtr f = FindFolder(name);
        if (!f)
        {
            f = std::make_shared<ProjectFolder>(name);
            Folders.push_back(f);
            IsDirty = true;
            if (BuildGroupsSaid) BuildGroups.push_back(name);      // a folder made here is built
        }
        return f;
    }

    std::string GrideProject::DefaultFolderFor(const std::string& path)
    {
        std::string e = StringUtil::ToLower(Paths::Extension(PathUtil::Normalize(path)));
        if (e == ".h" || e == ".hpp" || e == ".hh" || e == ".inc") return "inc";
        if (e == ".lib" || e == ".a" || e == ".obj") return "lib";
        if (e == ".cmd" || e == ".ld") return "linker";
        return "src";
    }

    bool GrideProject::AddFile(const std::string& abs, const std::string& folderName)
    {
        if (IsReadOnly || ContainsFile(abs)) return false;
        std::string folder = folderName.empty() ? DefaultFolderFor(abs) : folderName;
        GetOrAddFolder(folder)->Files.push_back(ToRelative(abs));
        IsDirty = true;
        return true;
    }

    bool GrideProject::RemoveFile(const std::string& abs)
    {
        if (IsReadOnly) return false;
        for (const ProjectFolderPtr& f : Folders)
            for (size_t i = 0; i < f->Files.size(); i++)
                if (PathUtil::SamePath(ToAbsolute(f->Files[i]), abs))
                {
                    f->Files.erase(f->Files.begin() + (long)i);
                    IsDirty = true;
                    return true;
                }
        return false;
    }

    bool GrideProject::RenameFile(const std::string& oldAbs, const std::string& newAbs)
    {
        for (const ProjectFolderPtr& f : Folders)
            for (size_t i = 0; i < f->Files.size(); i++)
                if (PathUtil::SamePath(ToAbsolute(f->Files[i]), oldAbs))
                {
                    f->Files[i] = ToRelative(newAbs);
                    IsDirty = true;
                    return true;
                }
        return false;
    }

    bool GrideProject::FolderOfFile(const std::string& abs, std::string& folder) const
    {
        for (const ProjectFolderPtr& f : Folders)
            for (const std::string& rel : f->Files)
                if (PathUtil::SamePath(ToAbsolute(rel), abs)) { folder = f->Name; return true; }
        return false;
    }

    bool GrideProject::SupportsTarget(Target t) const
    {
        return Targets.empty() || std::find(Targets.begin(), Targets.end(), t) != Targets.end();
    }

    BuildConfigurationPtr GrideProject::GetConfiguration(const std::string& config, Target t)
    {
        std::string key = ConfigName::Make(config, t);
        for (const BuildConfigurationPtr& c : Configurations)
            if (StringUtil::EqualsIgnoreCase(c->Key(), key)) return c;
        BuildConfigurationPtr n = std::make_shared<BuildConfiguration>(config, t);
        Configurations.push_back(n);
        if (!IsReadOnly) IsDirty = true;
        return n;
    }

    bool GrideProject::IsGrey(const JsonPtr& j)
    {
        return !j->Has("gride") && j->Has("formatVersion");
    }

    std::string GrideProject::GreySolution(const std::string& path)
    {
        JsonPtr j = JsonParser::ParseFile(path);
        if (!IsGrey(j)) return std::string();
        std::string sln = j->GetString("visualStudio", "");
        return !sln.empty() ? PathUtil::MakeAbsolute(Paths::DirectoryName(Paths::FullPath(path)), sln) : std::string();
    }

    GrideProjectPtr GrideProject::FromGrey(const std::string& path, const JsonPtr& j)
    {
        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->FilePath = Paths::FullPath(PathUtil::Normalize(path));
        p->Name = j->GetString("name", Paths::FileNameWithoutExtension(p->FilePath));
        p->Lang = LanguageInfo::Parse(j->GetString("language", "C90"));
        p->Template = "Program";
        p->Targets.push_back(Target::Windows);
        p->Targets.push_back(Target::C6747);
        ProjectFolderPtr f = std::make_shared<ProjectFolder>("Sources");
        f->Files = j->GetStringList("sourceFiles");
        p->Folders.push_back(f);
        JsonPtr cfgs = j->Get("configurations");
        bool listed = cfgs && cfgs->IsArray() && cfgs->Count() > 0;
        for (Target t : p->Targets)
        {
            if (!listed)
            {
                p->Configurations.push_back(std::make_shared<BuildConfiguration>("Debug", t));
                p->Configurations.push_back(std::make_shared<BuildConfiguration>("Release", t));
                continue;
            }
            for (int i = 0; i < cfgs->Count(); i++)
            {
                BuildConfigurationPtr c = std::make_shared<BuildConfiguration>(cfgs->At(i)->GetString("name", "Debug"), t);
                c->Defines = cfgs->At(i)->GetStringList("defines");
                p->Configurations.push_back(c);
            }
        }
        p->Toolchain = j->GetString("toolchain", "auto");
        if (j->Has("programArgs")) p->ProgramArgs = j->GetStringList("programArgs");
        p->IsReadOnly = true;
        p->IsDirty = false;
        return p;
    }


    // ---- the one format -----------------------------------------------------------
    bool GrideProject::IsRideForm(const JsonPtr& j)
    {
        if (!j || !j->IsObject()) return false;
        if (j->Has("gride") || j->Has("folders") || j->Has("formatVersion") || j->Has("sourceFiles")) return false;
        return !j->GetObject("ccs");          // a CCS workspace project's .pro is read by CcsProjectReader
    }

    Language GrideProject::LanguageOfFiles(const std::vector<ProjectFolderPtr>& folders)
    {
        bool shalimar = false;
        for (const ProjectFolderPtr& f : folders)
            for (const std::string& file : f->Files)
            {
                std::string e = StringUtil::ToLower(Paths::Extension(file));
                if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".hpp" || e == ".hh") return Language::Cpp11;
                if (e == ".shl" || e == ".shm") shalimar = true;
            }
        return shalimar ? Language::Shalimar : Language::C90;
    }

    std::string GrideProject::ArchOf(Target t)
    {
        switch (t)
        {
        case Target::Linux: return "x86_64-linux";
        case Target::MacOS: return "arm64-darwin";
        case Target::C6747: return "tms6747";
        default:            return "x86_64-windows";
        }
    }

    Target GrideProject::TargetOfArch(const std::string& arch)
    {
        std::string a = StringUtil::ToLower(StringUtil::Trim(arch));
        if (StringUtil::EndsWith(a, "-linux")) return Target::Linux;
        if (StringUtil::EndsWith(a, "-darwin")) return Target::MacOS;
        if (StringUtil::Contains(a, "6747") || StringUtil::StartsWith(a, "tms")) return Target::C6747;
        if (StringUtil::EndsWith(a, "-windows")) return Target::Windows;
        return TargetInfo::Parse(arch);
    }

    Target GrideProject::TargetOfProjectArch(const std::string& arch)
    {
        Target t = TargetOfArch(arch);
        return t == Target::Windows || t == Target::Linux ? Target::MacOS : t;
    }

    static void AddTarget(std::vector<Target>& targets, Target t)
    {
        if (std::find(targets.begin(), targets.end(), t) == targets.end()) targets.push_back(t);
    }

    GrideProjectPtr GrideProject::FromRide(const std::string& path, const JsonPtr& j)
    {
        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->FilePath = Paths::FullPath(PathUtil::Normalize(path));
        p->Kind = "ride";
        p->Name = j->GetString("name", Paths::FileName(Paths::DirectoryName(p->FilePath)));
        p->Toolchain = j->GetString("toolchain", "auto");

        // groups: name -> [files], or name -> { "files": [...], "toolchain": word }
        JsonPtr groups = j->GetObject("groups");
        if (groups)
            for (const std::string& name : groups->KeyNames())
            {
                ProjectFolderPtr f = std::make_shared<ProjectFolder>(name);
                JsonPtr entry = groups->Get(name);
                JsonPtr files = entry && entry->IsObject() ? entry->Get("files") : entry;
                if (entry && entry->IsObject())
                {
                    std::string word = entry->GetString("toolchain", "auto");
                    if (!StringUtil::IsNullOrWhiteSpace(word) && !StringUtil::EqualsIgnoreCase(word, "auto")) p->FolderToolchains.Set(name, word);
                }
                if (files && files->IsArray())
                    for (const std::string& file : files->ToStringList())
                    {
                        std::string rel = PathUtil::Normalize(file);
                        if (!rel.empty()) f->Files.push_back(rel);
                    }
                p->Folders.push_back(f);
            }
        if (p->Folders.empty()) p->Folders.push_back(std::make_shared<ProjectFolder>("Sources"));
        p->Lang = j->Has("language") ? LanguageInfo::Parse(j->GetString("language", "C90")) : LanguageOfFiles(p->Folders);
        p->Template = j->GetString("template", "Program");
        p->Includes = j->GetStringList("include");
        p->Libraries = j->GetStringList("libraries");

        // the build: no "build" at all builds Sources (RIDE's projects from before 2026-09-16)
        JsonPtr build = j->GetObject("build");
        if (build)
        {
            p->TargetName = build->GetString("target", "");
            if (build->Has("groups")) { p->BuildGroupsSaid = true; p->BuildGroups = build->GetStringList("groups"); }
            p->ProgramArgs = build->GetStringList("args");
        }
        else if (!j->Has("build") && p->FindFolder("Sources"))
        {
            p->BuildGroupsSaid = true;
            p->BuildGroups.push_back(p->FindFolder("Sources")->Name);
        }

        // targets: GRIDEd's list, else the Mac and the C6747 as RIDE offers them; the file's arch first
        for (const std::string& t : j->GetStringList("targets")) AddTarget(p->Targets, TargetInfo::Parse(t));
        if (p->Targets.empty()) { p->Targets.push_back(Target::MacOS); p->Targets.push_back(Target::C6747); }
        p->ArchWritten = j->GetString("arch", "");
        if (!StringUtil::IsNullOrWhiteSpace(p->ArchWritten))
        {
            Target first = TargetOfProjectArch(p->ArchWritten);
            p->Targets.erase(std::remove(p->Targets.begin(), p->Targets.end(), first), p->Targets.end());
            p->Targets.insert(p->Targets.begin(), first);
        }

        JsonPtr cfgs = j->GetObject("configurations");
        if (cfgs)
            for (const std::string& key : cfgs->KeyNames())
                p->Configurations.push_back(BuildConfiguration::FromJson(key, cfgs->Get(key)));
        else
            for (Target t : p->Targets)
            {
                p->Configurations.push_back(std::make_shared<BuildConfiguration>("Debug", t));
                p->Configurations.push_back(std::make_shared<BuildConfiguration>("Release", t));
            }
        p->ExcludedFiles = j->GetStringList("excludeFromBuild");
        p->DependsOn = j->GetStringList("dependsOn");
        p->EmulateOnly = j->GetBool("emulateOnly", false);
        p->Options->FromJson(j->GetObject("options"));

        bool gridesOwn = j->Has("language") || j->Has("targets") || j->Has("configurations") || j->Has("excludeFromBuild")
                         || j->Has("dependsOn") || j->Has("template");
        if (!gridesOwn)
        {
            std::vector<std::string> built, skipped;
            for (const ProjectFolderPtr& f : p->Folders)
                (!p->BuildGroupsSaid || StringUtil::ContainsIgnoreCase(p->BuildGroups, f->Name) ? built : skipped).push_back(f->Name);
            std::string line = "RIDE project " + p->Name + ": " + std::to_string(p->Folders.size()) + " groups; builds "
                               + (built.empty() ? std::string("nothing") : StringUtil::Join(", ", built));
            if (!skipped.empty()) line += "; not built: " + StringUtil::Join(", ", skipped);
            p->RideReportLines.push_back(line);
        }
        p->raw = j;
        p->rideRead = p->RideFields();
        p->IsDirty = false;
        return p;
    }

    // what GRIDEm keeps of a .pro of the one format; Save writes only the entries that differ from the
    // baseline taken when it was read, so what GRIDEm did not change stays as the file had it
    JsonPtr GrideProject::RideFields() const
    {
        JsonPtr f = JsonValue::MakeObject();
        f->SetString("name", Name);
        f->SetString("toolchain", Toolchain);
        JsonPtr groups = JsonValue::MakeObject();
        for (const ProjectFolderPtr& g : Folders)
        {
            std::string word;
            if (FolderToolchains.Find(g->Name, word) && !StringUtil::EqualsIgnoreCase(word, "auto"))
            {
                JsonPtr named = JsonValue::MakeObject();
                named->SetList("files", g->Files);
                named->SetString("toolchain", word);
                groups->SetValue(g->Name, named);
            }
            else groups->SetList(g->Name, g->Files);
        }
        f->SetValue("groups", groups);
        f->SetList("include", Includes);
        f->SetList("libraries", Libraries);
        f->SetBool("emulateOnly", EmulateOnly);
        f->SetValue("options", Options->ToJson());
        std::vector<std::string> built = BuildGroups;
        if (!BuildGroupsSaid)
        {
            built.clear();
            for (const ProjectFolderPtr& g : Folders) built.push_back(g->Name);
        }
        f->SetList("buildGroups", built);
        f->SetList("buildArgs", ProgramArgs);
        f->SetString("buildTarget", TargetName);
        f->SetString("language", LanguageInfo::Name(Lang));
        f->SetString("template", Template);
        std::vector<std::string> t;
        for (Target x : Targets) t.push_back(TargetInfo::Name(x));
        f->SetList("targets", t);
        if (!Targets.empty())
            f->SetString("arch", !StringUtil::IsNullOrWhiteSpace(ArchWritten) && TargetOfProjectArch(ArchWritten) == Targets[0]
                                     ? ArchWritten : ArchOf(Targets[0]));
        JsonPtr cfgs = JsonValue::MakeObject();
        for (const BuildConfigurationPtr& c : Configurations) cfgs->SetValue(c->Key(), c->ToJson());
        f->SetValue("configurations", cfgs);
        f->SetList("excludeFromBuild", ExcludedFiles);
        f->SetList("dependsOn", DependsOn);
        return f;
    }

    static bool SameJson(const JsonPtr& a, const JsonPtr& b)
    {
        return JsonWriter::WriteRide(a ? a : JsonValue::MakeNull()) == JsonWriter::WriteRide(b ? b : JsonValue::MakeNull());
    }

    void GrideProject::SaveRide()
    {
        JsonPtr now = RideFields();
        JsonPtr was = rideRead ? rideRead : JsonValue::MakeObject();
        if (SameJson(now, was)) { IsDirty = false; return; }
        JsonPtr out = raw ? raw->Clone() : JsonValue::MakeObject();
        const char* const keys[] = { "name", "toolchain", "arch", "groups", "include", "libraries", "emulateOnly", "options",
                                     "language", "template", "targets", "configurations", "excludeFromBuild", "dependsOn" };
        for (const char* k : keys)
        {
            std::string key = k;
            JsonPtr v = now->Get(key);
            if (!v || SameJson(v, was->Get(key))) continue;
            bool says = true;
            if (key == "include" || key == "libraries" || key == "excludeFromBuild" || key == "dependsOn") says = !v->ToStringList().empty();
            else if (key == "emulateOnly") says = v->AsBool();
            else if (key == "options") says = !Options->Empty();
            else if (key == "language") says = Lang != LanguageOfFiles(Folders) || out->Has("language");
            else if (key == "template") says = Template != "Empty";
            else if (key == "targets") says = Targets.size() > 1;
            else if (key == "configurations") says = !Configurations.empty();
            if (says) out->SetValue(key, v);
            else out->Remove(key);
        }
        if (!SameJson(now->Get("buildGroups"), was->Get("buildGroups")) || !SameJson(now->Get("buildArgs"), was->Get("buildArgs"))
            || !SameJson(now->Get("buildTarget"), was->Get("buildTarget")))
        {
            JsonPtr old = out->GetObject("build");
            JsonPtr build = old ? old->Clone() : JsonValue::MakeObject();
            if (!StringUtil::IsNullOrWhiteSpace(TargetName) || !build->Has("target")) build->SetString("target", ProgramName());
            build->SetList("groups", now->GetStringList("buildGroups"));
            if (!ProgramArgs.empty()) build->SetList("args", ProgramArgs);
            else build->Remove("args");
            out->SetValue("build", build);
        }
        JsonWriter::WriteRideFile(FilePath, out);
        raw = out;
        rideRead = now;
        IsDirty = false;
    }

    GrideProjectPtr GrideProject::Load(const std::string& path)
    {
        JsonPtr j = JsonParser::ParseFile(path);
        if (IsGrey(j)) return FromGrey(path, j);
        if (IsRideForm(j)) return FromRide(path, j);
        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->FilePath = Paths::FullPath(PathUtil::Normalize(path));
        p->raw = j;
        p->Name = j->GetString("name", Paths::FileNameWithoutExtension(p->FilePath));
        p->Lang = LanguageInfo::Parse(j->GetString("language", "C90"));
        p->Template = j->GetString("template", "Empty");

        for (const std::string& t : j->GetStringList("targets")) p->Targets.push_back(TargetInfo::Parse(t));

        JsonPtr folders = j->GetObject("folders");
        if (folders)
            for (const std::string& name : folders->KeyNames())
            {
                ProjectFolderPtr f = std::make_shared<ProjectFolder>(name);
                f->Files = folders->GetStringList(name);
                p->Folders.push_back(f);
            }

        JsonPtr cfgs = j->GetObject("configurations");
        if (cfgs)
            for (const std::string& key : cfgs->KeyNames())
                p->Configurations.push_back(BuildConfiguration::FromJson(key, cfgs->Get(key)));

        p->DependsOn = j->GetStringList("dependsOn");
        p->ExcludedFiles = j->GetStringList("excludeFromBuild");
        p->Toolchain = j->GetString("toolchain", "auto");
        p->EmulateOnly = j->GetBool("emulateOnly", false);
        if (j->Has("programArgs")) p->ProgramArgs = j->GetStringList("programArgs");
        p->Options->FromJson(j->GetObject("options"));
        JsonPtr ft = j->GetObject("folderToolchains");
        if (ft)
            for (const std::string& name : ft->KeyNames())
            {
                std::string w = ft->GetString(name, "");
                if (!StringUtil::IsNullOrWhiteSpace(w)) p->FolderToolchains.Set(name, w);
            }
        p->IsDirty = false;
        return p;
    }

    JsonPtr GrideProject::ToJson()
    {
        // from the file as loaded, so keys GRIDEm does not know survive (White's "indent", "tabs", "open")
        JsonPtr j = raw ? raw : JsonValue::MakeObject();
        bool saidLanguage = j->Has("language");
        const char* const former[] = { "gride", "type", "folders", "folderToolchains", "programArgs" };
        for (const char* k : former) j->Remove(k);

        j->SetString("name", Name);
        j->SetString("toolchain", StringUtil::IsNullOrWhiteSpace(Toolchain) ? std::string("auto") : Toolchain);
        if (!Targets.empty())
            j->SetString("arch", !StringUtil::IsNullOrWhiteSpace(ArchWritten) && TargetOfProjectArch(ArchWritten) == Targets[0]
                                     ? ArchWritten : ArchOf(Targets[0]));
        JsonPtr groups = JsonValue::MakeObject();
        for (const ProjectFolderPtr& f : Folders)
        {
            std::string w;
            if (FolderToolchains.Find(f->Name, w) && !StringUtil::IsNullOrWhiteSpace(w))
            {
                JsonPtr g = JsonValue::MakeObject();
                g->SetList("files", f->Files);
                g->SetString("toolchain", w);
                groups->SetValue(f->Name, g);
            }
            else groups->SetList(f->Name, f->Files);
        }
        j->SetValue("groups", groups);
        if (!Includes.empty()) j->SetList("include", Includes); else j->Remove("include");
        if (!Libraries.empty()) j->SetList("libraries", Libraries); else j->Remove("libraries");
        if (EmulateOnly) j->SetBool("emulateOnly", true); else j->Remove("emulateOnly");
        if (!Options->Empty()) j->SetValue("options", Options->ToJson()); else j->Remove("options");
        JsonPtr build = JsonValue::MakeObject();
        build->SetString("target", ProgramName());
        std::vector<std::string> built = BuildGroups;
        if (!BuildGroupsSaid)
        {
            built.clear();
            for (const ProjectFolderPtr& f : Folders) built.push_back(f->Name);
        }
        build->SetList("groups", built);
        if (!ProgramArgs.empty()) build->SetList("args", ProgramArgs);
        j->SetValue("build", build);

        if (saidLanguage || Lang != LanguageOfFiles(Folders)) j->SetString("language", LanguageInfo::Name(Lang));
        if (!Template.empty() && Template != "Empty") j->SetString("template", Template); else j->Remove("template");
        if (Targets.size() > 1)
        {
            std::vector<std::string> t;
            for (Target x : Targets) t.push_back(TargetInfo::Name(x));
            j->SetList("targets", t);
        }
        else j->Remove("targets");
        if (!Configurations.empty())
        {
            JsonPtr cfgs = JsonValue::MakeObject();
            for (const BuildConfigurationPtr& c : Configurations) cfgs->SetValue(c->Key(), c->ToJson());
            j->SetValue("configurations", cfgs);
        }
        else j->Remove("configurations");
        if (!ExcludedFiles.empty()) j->SetList("excludeFromBuild", ExcludedFiles); else j->Remove("excludeFromBuild");
        if (!DependsOn.empty()) j->SetList("dependsOn", DependsOn); else j->Remove("dependsOn");
        return j;
    }

    JsonPtr GrideProject::ImportedSettings() const
    {
        JsonPtr s = JsonValue::MakeObject();
        std::vector<std::string> t;
        for (Target x : Targets) t.push_back(TargetInfo::Name(x));
        s->SetList("targets", t);
        s->SetList("dependsOn", DependsOn);
        JsonPtr cfgs = JsonValue::MakeObject();
        for (const BuildConfigurationPtr& c : Configurations) cfgs->SetValue(c->Key(), c->ToJson());
        s->SetValue("configurations", cfgs);
        s->SetValue("options", Options->ToJson());
        return s;
    }

    void GrideProject::ApplyImportedSettings(const JsonPtr& s)
    {
        if (!s || !s->IsObject()) return;
        if (s->Has("targets"))
        {
            Targets.clear();
            for (const std::string& name : s->GetStringList("targets")) Targets.push_back(TargetInfo::Parse(name));
        }
        if (s->Has("dependsOn")) DependsOn = s->GetStringList("dependsOn");
        JsonPtr cfgs = s->GetObject("configurations");
        if (cfgs)
            for (const std::string& key : cfgs->KeyNames())
            {
                BuildConfigurationPtr c = BuildConfiguration::FromJson(key, cfgs->Get(key));
                if (!c) continue;
                bool replaced = false;
                for (BuildConfigurationPtr& x : Configurations)
                    if (StringUtil::EqualsIgnoreCase(x->Key(), key)) { x = c; replaced = true; break; }
                if (!replaced) Configurations.push_back(c);
            }
        if (s->Has("options")) Options->FromJson(s->GetObject("options"));
    }

    std::function<bool(const std::string& path)> GrideProject::BeforeWrite;

    void GrideProject::Save()
    {
        if (IsReadOnly) return;
        if (BeforeWrite && !BeforeWrite(FilePath)) return;
        if (IsRide()) { SaveRide(); return; }
        // the older form, saved in the one format: from here on it is that format's project
        JsonPtr j = ToJson();
        JsonWriter::WriteFile(FilePath, j);
        raw = j;
        Kind = "ride";
        rideRead = RideFields();
        IsDirty = false;
    }

    GrideProjectPtr GrideProject::CreateNew(const std::string& parentDir, const std::string& name, Language lang,
                                            const std::string& templ, const std::vector<Target>& targets)
    {
        std::string dir = Paths::Combine(parentDir, name);
        PathUtil::EnsureDirectory(dir);
        PathUtil::EnsureDirectory(Paths::Combine(dir, "src"));
        PathUtil::EnsureDirectory(Paths::Combine(dir, "inc"));
        PathUtil::EnsureDirectory(Paths::Combine(dir, "lib"));

        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->FilePath = Paths::Combine(dir, name + ".pro");
        p->Name = name;
        p->Lang = lang;
        p->Template = templ;
        p->Targets = targets;
        p->Folders.push_back(std::make_shared<ProjectFolder>("src"));
        p->Folders.push_back(std::make_shared<ProjectFolder>("inc"));
        p->Folders.push_back(std::make_shared<ProjectFolder>("lib"));
        for (Target t : targets)
        {
            p->Configurations.push_back(std::make_shared<BuildConfiguration>("Debug", t));
            p->Configurations.push_back(std::make_shared<BuildConfiguration>("Release", t));
        }
        return p;
    }
}}
