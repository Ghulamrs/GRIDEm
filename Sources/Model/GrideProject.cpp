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
          Toolchain("auto"), EmulateOnly(false), Options(std::make_shared<Settings::CompilerOptionStore>()) {}

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


    // ---- RIDE's own .pro ----------------------------------------------------------
    bool GrideProject::IsRideForm(const JsonPtr& j)
    {
        if (!j || !j->IsObject()) return false;
        if (j->Has("gride") || j->Has("folders") || j->Has("formatVersion") || j->Has("sourceFiles")) return false;
        if (j->GetObject("ccs")) return false;       // a CCS workspace project's .pro: not this form
        return j->Has("groups") || j->Has("build") || j->Has("arch") || j->Has("toolchain");
    }

    // the language a RIDE project's files say most of: Shalimar, C++ or C
    static Language LanguageOfFiles(const std::vector<ProjectFolderPtr>& folders)
    {
        int c = 0, cpp = 0, shl = 0;
        for (const ProjectFolderPtr& f : folders)
            for (const std::string& file : f->Files)
            {
                std::string ext = StringUtil::ToLower(Paths::Extension(file));
                if (ext == ".shl" || ext == ".shm") shl++;
                else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++") cpp++;
                else if (ext == ".c") c++;
            }
        if (shl > 0 && shl >= cpp && shl >= c) return Language::Shalimar;
        if (cpp > 0 && cpp >= c) return Language::Cpp11;
        return Language::C90;
    }

    static bool TargetOfArch(const std::string& arch, Target& t)
    {
        std::string a = StringUtil::ToLower(arch);
        if (a == "tms6747") { t = Target::C6747; return true; }
        if (StringUtil::EndsWith(a, "-windows")) { t = Target::Windows; return true; }
        if (StringUtil::EndsWith(a, "-linux")) { t = Target::Linux; return true; }
        if (StringUtil::EndsWith(a, "-darwin")) { t = Target::MacOS; return true; }
        return false;
    }

    GrideProjectPtr GrideProject::FromRide(const std::string& path, const JsonPtr& j)
    {
        GrideProjectPtr p = std::make_shared<GrideProject>();
        p->FilePath = Paths::FullPath(PathUtil::Normalize(path));
        p->Kind = "ride";
        p->Template = "Program";
        std::string dirName = Paths::FileName(Paths::DirectoryName(p->FilePath));
        p->Name = j->GetString("name", dirName);
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
                    if (!StringUtil::IsNullOrWhiteSpace(word) && word != "auto") p->FolderToolchains.Set(name, word);
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
        p->Lang = LanguageOfFiles(p->Folders);

        // the build: the groups compiled into the program and its arguments. No "build" entry at all
        // builds Sources (RIDE's projects from before 2026-09-16); an empty one builds nothing.
        std::vector<std::string> built;
        JsonPtr build = j->GetObject("build");
        if (build)
        {
            built = build->GetStringList("groups");
            p->ProgramArgs = build->GetStringList("args");
        }
        else if (!j->Has("build")) built.push_back("Sources");
        // a group the build does not take (RIDE's Input: sample.cpp) is shown and never compiled
        std::vector<std::string> skipped;
        for (const ProjectFolderPtr& f : p->Folders)
        {
            if (StringUtil::ContainsIgnoreCase(built, f->Name)) continue;
            bool any = false;
            for (const std::string& file : f->Files)
                if (DefaultFolderFor(file) == "src")      // a source; headers and libraries are never compiled anyway
                {
                    p->ExcludedFiles.push_back(file);
                    any = true;
                }
            if (any) skipped.push_back(f->Name);
        }

        // targets: the file's arch first (another Mac's or PC's host arch means this host), then the
        // Mac and the C6747, so both are offered as RIDE offers them
        Target first = Target::MacOS;
        std::string arch = j->GetString("arch", "");
        if (TargetOfArch(arch, first) && (first == Target::Windows || first == Target::Linux)) first = Target::MacOS;
        p->Targets.push_back(first);
        if (first != Target::MacOS) p->Targets.push_back(Target::MacOS);
        if (first != Target::C6747) p->Targets.push_back(Target::C6747);
        for (Target t : p->Targets)
        {
            p->Configurations.push_back(std::make_shared<BuildConfiguration>("Debug", t));
            p->Configurations.push_back(std::make_shared<BuildConfiguration>("Release", t));
        }
        for (const std::string& inc : j->GetStringList("include"))
            for (const BuildConfigurationPtr& c : p->Configurations) c->IncludeDirs.push_back(PathUtil::Normalize(inc));
        p->EmulateOnly = j->GetBool("emulateOnly", false);
        p->Options->FromJson(j->GetObject("options"));

        std::string line = "RIDE project " + p->Name + ": read as RIDE writes it (" + std::to_string(p->Folders.size())
                           + " groups), never rewritten; builds " + (built.empty() ? std::string("nothing")
                                                                                  : StringUtil::Join(", ", built));
        if (!skipped.empty()) line += "; not built: " + StringUtil::Join(", ", skipped);
        p->RideReportLines.push_back(line);
        p->IsReadOnly = true;
        p->IsDirty = false;
        return p;
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
        // start from the file we loaded so unknown keys survive a save
        JsonPtr j = raw ? raw : JsonValue::MakeObject();
        j->SetString("gride", "1.0");
        j->SetString("type", "project");
        j->SetString("name", Name);
        j->SetString("language", LanguageInfo::Name(Lang));
        j->SetString("template", Template);
        std::vector<std::string> t;
        for (Target x : Targets) t.push_back(TargetInfo::Name(x));
        j->SetList("targets", t);

        JsonPtr folders = JsonValue::MakeObject();
        for (const ProjectFolderPtr& f : Folders) folders->SetList(f->Name, f->Files);
        j->SetValue("folders", folders);

        JsonPtr cfgs = JsonValue::MakeObject();
        for (const BuildConfigurationPtr& c : Configurations) cfgs->SetValue(c->Key(), c->ToJson());
        j->SetValue("configurations", cfgs);

        j->SetList("excludeFromBuild", ExcludedFiles);
        j->SetList("dependsOn", DependsOn);
        // written only when they say something, so a file written before the port is unchanged
        if (!StringUtil::IsNullOrWhiteSpace(Toolchain) && !StringUtil::EqualsIgnoreCase(Toolchain, "auto")) j->SetString("toolchain", Toolchain);
        else if (j->Has("toolchain")) j->Remove("toolchain");
        if (!FolderToolchains.Empty())
        {
            JsonPtr ft = JsonValue::MakeObject();
            for (const std::pair<std::string, std::string>& kv : FolderToolchains.Items()) ft->SetString(kv.first, kv.second);
            j->SetValue("folderToolchains", ft);
        }
        else if (j->Has("folderToolchains")) j->Remove("folderToolchains");
        if (EmulateOnly) j->SetBool("emulateOnly", true);
        else if (j->Has("emulateOnly")) j->Remove("emulateOnly");
        if (!ProgramArgs.empty()) j->SetList("programArgs", ProgramArgs);
        else if (j->Has("programArgs")) j->Remove("programArgs");
        if (!Options->Empty()) j->SetValue("options", Options->ToJson());
        else if (j->Has("options")) j->Remove("options");
        return j;
    }

    std::function<bool(const std::string& path)> GrideProject::BeforeWrite;

    void GrideProject::Save()
    {
        if (IsReadOnly) return;
        if (BeforeWrite && !BeforeWrite(FilePath)) return;
        JsonPtr j = ToJson();
        JsonWriter::WriteFile(FilePath, j);
        raw = j;
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
