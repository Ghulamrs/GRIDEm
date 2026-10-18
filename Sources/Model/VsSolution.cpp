// =============================================================================
//  VsSolution.cpp - see VsSolution.h
// =============================================================================
#include "VsSolution.h"
#include "../Core/PathUtil.h"
#include "../Core/TextFile.h"
#include "../Platform/Paths.h"
#include <algorithm>
#include <regex>
#include <stdexcept>
#include <utility>

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    static bool Has(const std::vector<std::string>& list, const std::string& one)
    {
        return std::find(list.begin(), list.end(), one) != list.end();
    }

    static void AddNote(std::vector<std::string>& notes, const std::string& note)
    {
        if (!Has(notes, note)) notes.push_back(note);
    }

    static std::string Join(const std::string& sep, const std::vector<std::string>& parts)
    {
        return StringUtil::Join(sep, parts);
    }

    static std::string JoinSet(const std::string& sep, const NoCaseSet& set)
    {
        std::vector<std::string> v(set.begin(), set.end());
        return Join(sep, v);
    }

    // =========================================================================
    //  VsSolution
    // =========================================================================
    const char* const VsSolution::CppTypeGuid = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942";
    const char* const VsSolution::FolderTypeGuid = "2150E333-8FDC-42A3-9474-1A3956D46DE8";

    bool VsSolution::IsSolution(const std::string& path) { return PathUtil::HasExtension(path, ".sln"); }

    std::string VsSolution::Version() const
    {
        std::string v = !VisualStudioVersion.empty() ? VisualStudioVersion : std::string("Visual Studio");
        return !FormatVersion.empty() ? v + " (format " + FormatVersion + ")" : v;
    }

    std::vector<VsSolutionEntryPtr> VsSolution::CppProjects() const
    {
        std::vector<VsSolutionEntryPtr> r;
        for (const VsSolutionEntryPtr& e : Entries)
            if (e->IsCpp()) r.push_back(e);
        return r;
    }

    VsSolutionEntryPtr VsSolution::ByGuid(const std::string& guid) const
    {
        std::string g = CleanGuid(guid);
        for (const VsSolutionEntryPtr& e : Entries)
            if (e->Guid == g) return e;
        return nullptr;
    }

    std::string VsSolution::FolderPathOf(const VsSolutionEntryPtr& e) const
    {
        std::vector<std::string> chain;
        VsSolutionEntryPtr at = e;
        int guard = 0;
        while (at && !at->ParentGuid.empty() && guard++ < 32)
        {
            at = ByGuid(at->ParentGuid);
            if (at) chain.insert(chain.begin(), at->Name);
        }
        return Join("/", chain);
    }

    std::vector<std::string> VsSolution::FolderPaths() const
    {
        std::vector<std::string> r;
        for (const VsSolutionEntryPtr& e : Entries)
            if (e->IsFolder())
            {
                std::string own = FolderPathOf(e);
                r.push_back(!own.empty() ? own + "/" + e->Name : e->Name);
            }
        return r;
    }

    std::string VsSolution::CleanGuid(const std::string& g)
    {
        std::string t = StringUtil::Trim(g);
        size_t b = 0, e = t.size();
        while (b < e && (t[b] == '{' || t[b] == '}')) b++;
        while (e > b && (t[e - 1] == '{' || t[e - 1] == '}')) e--;
        return StringUtil::ToUpper(t.substr(b, e - b));
    }

    VsSolutionPtr VsSolution::Read(const std::string& path)
    {
        VsSolutionPtr s = std::make_shared<VsSolution>();
        s->Path = Paths::FullPath(PathUtil::Normalize(path));
        s->Dir = Paths::DirectoryName(s->Path);
        s->Name = Paths::FileNameWithoutExtension(s->Path);
        std::string text, error;
        TextEncoding enc;
        if (!TextFile::Read(s->Path, text, enc, error)) throw std::runtime_error(error);
        std::vector<std::string> lines = StringUtil::SplitLines(text);
        static const std::regex projectLine(
            "^Project\\(\"\\{?([^\"}]+)\\}?\"\\)\\s*=\\s*\"([^\"]*)\"\\s*,\\s*\"([^\"]*)\"\\s*,\\s*\"\\{?([^\"}]+)\\}?\"",
            std::regex::icase);
        VsSolutionEntryPtr current;
        std::string section;            // the ProjectSection or GlobalSection being read
        for (const std::string& raw : lines)
        {
            std::string line = StringUtil::Trim(raw);
            if (line.empty()) continue;
            if (StringUtil::StartsWithIgnoreCase(line, "Microsoft Visual Studio Solution File"))
            {
                int at = StringUtil::IndexOfIgnoreCase(line, "Format Version", 0);
                s->FormatVersion = at < 0 ? std::string() : StringUtil::Trim(line.substr((size_t)at + 14));
                continue;
            }
            if (StringUtil::StartsWith(line, "# Visual Studio"))
            {
                if (s->VisualStudioVersion.empty()) s->VisualStudioVersion = StringUtil::Trim(line.substr(2));
                continue;
            }
            if (StringUtil::StartsWithIgnoreCase(line, "VisualStudioVersion"))
            {
                size_t eq = line.find('=');
                if (eq != std::string::npos && eq > 0)
                    s->VisualStudioVersion = (!s->VisualStudioVersion.empty() ? s->VisualStudioVersion + ", " : std::string())
                                           + "version " + StringUtil::Trim(line.substr(eq + 1));
                continue;
            }
            std::smatch m;
            if (std::regex_search(line, m, projectLine))
            {
                current = std::make_shared<VsSolutionEntry>();
                current->TypeGuid = CleanGuid(m[1].str());
                current->Name = m[2].str();
                current->RelativePath = m[3].str();
                current->Guid = CleanGuid(m[4].str());
                if (current->TypeGuid == FolderTypeGuid) current->Kind = "folder";
                else
                {
                    std::string ext = StringUtil::ToLower(Paths::Extension(PathUtil::Normalize(current->RelativePath)));
                    current->Kind = ext == ".vcxproj" ? "vcxproj" : ext == ".vcproj" ? "vcproj" : "other";
                    current->Path = PathUtil::MakeAbsolute(s->Dir, current->RelativePath);
                    if (current->Kind == "other")
                        s->Notes.push_back("skipped non-C++ project \"" + current->Name + "\" (" + current->RelativePath + ")");
                }
                s->Entries.push_back(current);
                continue;
            }
            if (StringUtil::StartsWithIgnoreCase(line, "EndProject") && !StringUtil::StartsWithIgnoreCase(line, "EndProjectSection"))
            {
                current.reset();
                section.clear();
                continue;
            }
            if (StringUtil::StartsWithIgnoreCase(line, "ProjectSection(") || StringUtil::StartsWithIgnoreCase(line, "GlobalSection("))
            {
                size_t open = line.find('('), close = line.find(')');
                section = (open != std::string::npos && close != std::string::npos && close > open)
                        ? line.substr(open + 1, close - open - 1) : std::string();
                continue;
            }
            if (StringUtil::StartsWithIgnoreCase(line, "EndProjectSection") || StringUtil::StartsWithIgnoreCase(line, "EndGlobalSection"))
            {
                section.clear();
                continue;
            }
            if (section.empty()) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = StringUtil::Trim(line.substr(0, eq));
            std::string value = StringUtil::Trim(line.substr(eq + 1));
            if (current && StringUtil::EqualsIgnoreCase(section, "ProjectDependencies")) current->Dependencies.push_back(CleanGuid(key));
            else if (current && StringUtil::EqualsIgnoreCase(section, "SolutionItems")) current->Items.push_back(PathUtil::MakeAbsolute(s->Dir, key));
            else if (StringUtil::EqualsIgnoreCase(section, "SolutionConfigurationPlatforms"))
            {
                if (!Has(s->Configurations, key)) s->Configurations.push_back(key);
            }
            else if (StringUtil::EqualsIgnoreCase(section, "ProjectConfigurationPlatforms"))
            {
                // {guid}.Debug|x64.ActiveCfg = Debug|x64 ; {guid}.Debug|x64.Build.0 = Debug|x64
                size_t dot = key.find('.');
                if (dot == std::string::npos) continue;
                VsSolutionEntryPtr e = s->ByGuid(key.substr(0, dot));
                std::string rest = key.substr(dot + 1);
                if (!e) continue;
                if (StringUtil::EndsWithIgnoreCase(rest, ".ActiveCfg")) e->ConfigMap[rest.substr(0, rest.size() - 10)] = value;
                else if (StringUtil::EndsWithIgnoreCase(rest, ".Build.0")) e->Builds.insert(rest.substr(0, rest.size() - 8));
            }
            else if (StringUtil::EqualsIgnoreCase(section, "NestedProjects"))
            {
                VsSolutionEntryPtr child = s->ByGuid(key);
                if (child) child->ParentGuid = CleanGuid(value);
            }
        }
        if (s->Entries.empty()) s->Notes.push_back("no project in " + s->Path);
        return s;
    }

    // =========================================================================
    //  VsProjectItem, VsProjectConfig
    // =========================================================================
    bool VsProjectItem::ExcludedIn(const std::string& config) const
    {
        return Excluded.count("*") > 0 || (!config.empty() && Excluded.count(config) > 0);
    }

    VsProjectConfig::VsProjectConfig(const std::string& name)
        : Name(name), ClrSupport(false), UseDebugLibrariesSaid(false)
    {
        size_t bar = name.find('|');
        Configuration = bar == std::string::npos ? name : name.substr(0, bar);
        Platform = bar == std::string::npos ? std::string() : name.substr(bar + 1);
    }

    bool VsProjectConfig::IsDebugLike() const
    {
        return StringUtil::EqualsIgnoreCase(Configuration, "Debug")
            || (UseDebugLibrariesSaid && StringUtil::EqualsIgnoreCase(UseDebugLibraries, "true"));
    }

    void VsProjectConfig::SetClSetting(const std::string& name, const std::string& value)
    {
        if (ClSettings.find(name) == ClSettings.end()) ClSettingOrder.push_back(name);
        ClSettings[name] = value;
    }

    // =========================================================================
    //  VsProject
    // =========================================================================
    static std::string Attr(const XmlNodePtr& n, const std::string& name) { return n ? n->Attr(name) : std::string(); }

    static std::vector<XmlNodePtr> Elements(const XmlNodePtr& parent)
    {
        if (!parent) return std::vector<XmlNodePtr>();
        return parent->Children;
    }

    static std::string ChildText(const XmlNodePtr& parent, const std::string& localName)
    {
        XmlNodePtr e = parent ? parent->Child(localName) : nullptr;
        return e ? StringUtil::Trim(e->InnerText()) : std::string();
    }

    static bool Named(const XmlNodePtr& e, const char* name)
    {
        return StringUtil::EqualsIgnoreCase(e->LocalName(), name);
    }

    // ';'-separated, blanks dropped, MSBuild's %(...) references dropped
    static std::vector<std::string> SplitList(const std::string& list)
    {
        std::vector<std::string> r;
        if (list.empty()) return r;
        for (const std::string& s : StringUtil::Split(list, ';'))
        {
            std::string t = StringUtil::Trim(s);
            if (t.empty() || StringUtil::StartsWith(t, "%(")) continue;
            r.push_back(t);
        }
        return r;
    }

    static bool Appends(const std::string& list) { return StringUtil::Contains(list, "%("); }

    // with %(...) in it the entries are added to what earlier groups gave, else they replace it (MSBuild)
    static void ApplyList(std::vector<std::string>& into, const std::string& value)
    {
        std::vector<std::string> items = SplitList(value);
        if (!Appends(value)) into.clear();
        for (const std::string& i : items)
            if (!Has(into, i)) into.push_back(i);
    }

    static std::string ItemKey(const std::string& include)
    {
        std::string k = StringUtil::ToLower(StringUtil::ReplaceChar(StringUtil::Trim(include), '/', '\\'));
        while (StringUtil::StartsWith(k, ".\\")) k = k.substr(2);
        return k;
    }

    static std::string ReplaceMacro(const std::string& s, const std::string& name, const std::string& value)
    {
        return StringUtil::ReplaceIgnoreCase(s, "$(" + name + ")", value);
    }

    bool VsProject::IsProjectFile(const std::string& path)
    {
        return PathUtil::HasExtension(path, ".vcxproj") || PathUtil::HasExtension(path, ".vcproj");
    }

    VsProjectConfigPtr VsProject::Config(const std::string& name) const
    {
        for (const VsProjectConfigPtr& c : Configurations)
            if (StringUtil::EqualsIgnoreCase(c->Name, name)) return c;
        return nullptr;
    }

    std::vector<std::string> VsProject::Platforms() const
    {
        std::vector<std::string> r;
        for (const VsProjectConfigPtr& c : Configurations)
            if (!Has(r, c->Platform)) r.push_back(c->Platform);
        return r;
    }

    std::vector<std::string> VsProject::ConfigurationNames() const
    {
        std::vector<std::string> r;
        for (const VsProjectConfigPtr& c : Configurations)
            if (!Has(r, c->Configuration)) r.push_back(c->Configuration);
        return r;
    }

    std::vector<VsProjectItemPtr> VsProject::ItemsOfType(const std::string& type) const
    {
        std::vector<VsProjectItemPtr> r;
        for (const VsProjectItemPtr& i : Items)
            if (StringUtil::EqualsIgnoreCase(i->Type, type)) r.push_back(i);
        return r;
    }

    std::string VsProject::DescribeToolsVersion(const std::string& toolsVersion, const std::string& toolset)
    {
        std::string vs;
        std::string ts = StringUtil::ToLower(toolset);
        if (ts == "v90") vs = "Visual Studio 2008";
        else if (ts == "v100") vs = "Visual Studio 2010";
        else if (ts == "v110") vs = "Visual Studio 2012";
        else if (ts == "v120") vs = "Visual Studio 2013";
        else if (ts == "v140") vs = "Visual Studio 2015";
        else if (ts == "v141") vs = "Visual Studio 2017";
        else if (ts == "v142") vs = "Visual Studio 2019";
        else if (ts == "v143") vs = "Visual Studio 2022";
        else if (ts == "v145") vs = "Visual Studio 2026";
        if (vs.empty())
        {
            if (toolsVersion == "4.0") vs = "Visual Studio 2010/2012";
            else if (toolsVersion == "12.0") vs = "Visual Studio 2013";
            else if (toolsVersion == "14.0") vs = "Visual Studio 2015";
            else if (toolsVersion == "15.0") vs = "Visual Studio 2017";
            else if (toolsVersion == "Current") vs = "Visual Studio 2019 or later";
            else vs = "Visual Studio (unknown version)";
        }
        std::string out = vs;
        if (!toolsVersion.empty()) out += ", ToolsVersion " + toolsVersion;
        if (!toolset.empty()) out += ", toolset " + toolset;
        return out;
    }

    // on the Mac a folder ends in '/', where Windows writes '\'
    std::string VsProject::ExpandMacros(const std::string& s, const VsProjectConfigPtr& c) const
    {
        if (s.empty()) return std::string();
        std::string out = s;
        out = ReplaceMacro(out, "ProjectDir", Dir + "/");
        out = ReplaceMacro(out, "MSBuildProjectDirectory", Dir);
        out = ReplaceMacro(out, "MSBuildThisFileDirectory", Dir + "/");
        out = ReplaceMacro(out, "ProjectName", Name);
        out = ReplaceMacro(out, "ProjectPath", Path);
        out = ReplaceMacro(out, "TargetName",
                           c && !c->TargetName.empty() && !StringUtil::Contains(c->TargetName, "$(") ? c->TargetName : Name);
        if (!SolutionDir.empty())
        {
            std::string sd = StringUtil::TrimEnd(StringUtil::TrimEnd(SolutionDir, '/'), '\\');
            out = ReplaceMacro(out, "SolutionDir", sd + "/");
            out = ReplaceMacro(out, "SolutionName", Paths::FileName(sd));
        }
        if (c)
        {
            out = ReplaceMacro(out, "Configuration", c->Configuration);
            out = ReplaceMacro(out, "ConfigurationName", c->Configuration);
            out = ReplaceMacro(out, "Platform", c->Platform);
            out = ReplaceMacro(out, "PlatformName", c->Platform);
        }
        return out;
    }

    bool VsProject::HasMacro(const std::string& s) { return StringUtil::Contains(s, "$("); }

    VsProjectPtr VsProject::Read(const std::string& path, const std::string& solutionDir)
    {
        VsProjectPtr p = std::make_shared<VsProject>();
        p->Path = Paths::FullPath(PathUtil::Normalize(path));
        p->Dir = Paths::DirectoryName(p->Path);
        p->Name = Paths::FileNameWithoutExtension(p->Path);
        p->SolutionDir = StringUtil::IsNullOrWhiteSpace(solutionDir) ? std::string() : Paths::FullPath(PathUtil::Normalize(solutionDir));
        if (!Paths::IsFile(p->Path)) throw std::runtime_error("not found: " + p->Path);
        XmlDocument doc;
        try { doc = XmlDocument::Load(p->Path); }
        catch (const std::exception& ex) { throw std::runtime_error(p->Path + ": " + ex.what()); }
        if (PathUtil::HasExtension(p->Path, ".vcxproj")) { p->Kind = "vcxproj"; p->ReadVcxproj(doc); }
        else if (PathUtil::HasExtension(p->Path, ".vcproj")) { p->Kind = "vcproj"; p->ReadVcproj(doc); }
        else throw std::runtime_error("not a Visual Studio C++ project: " + p->Path);
        return p;
    }

    std::string VsProject::MappingText() const
    {
        std::string t;
        t += "Visual Studio project " + Name + ", read from " + Path + "\n";
        t += Version + (!Guid.empty() ? "; " + Guid : std::string()) + "\n";
        if (!SolutionDir.empty()) t += "$(SolutionDir) = " + SolutionDir + "\n";
        if (!FiltersFile.empty()) t += "filters: " + FiltersFile + " (" + std::to_string(Filters.size()) + " folders)\n";
        t += "configurations: " + Join(", ", ConfigurationNames()) + "; platforms: " + Join(", ", Platforms()) + "\n";
        for (const VsProjectConfigPtr& c : Configurations)
        {
            t += "\n" + c->Name + ":\n";
            if (!c->ConfigurationType.empty())
                t += "  type " + c->ConfigurationType + (!c->Toolset.empty() ? ", toolset " + c->Toolset : std::string())
                   + (c->ClrSupport ? ", C++/CLI" : "") + "\n";
            if (!c->Defines.empty()) t += "  defines: " + Join(";", c->Defines) + "\n";
            if (!c->Undefines.empty()) t += "  undefines: " + Join(";", c->Undefines) + "\n";
            if (!c->IncludeDirs.empty()) t += "  include directories: " + Join("; ", c->IncludeDirs) + "\n";
            if (!c->Optimization.empty()) t += "  optimization: " + c->Optimization + "\n";
            if (!c->AdditionalOptions.empty()) t += "  additional options: " + c->AdditionalOptions + "\n";
            if (!c->ClSettingOrder.empty())
            {
                std::vector<std::string> said;
                for (const std::string& k : c->ClSettingOrder) said.push_back(k + " " + c->ClSettings.find(k)->second);
                t += "  cl's own settings (given to cl when it compiles the project): " + Join("; ", said) + "\n";
            }
            if (!c->Libs.empty()) t += "  libraries: " + Join(";", c->Libs) + "\n";
            if (!c->LibDirs.empty()) t += "  library directories: " + Join("; ", c->LibDirs) + "\n";
            if (!c->OutDir.empty()) t += "  output: " + c->OutDir + (!c->TargetName.empty() ? " " + c->TargetName : std::string()) + "\n";
            for (const std::string& n : c->Notes) t += "  not mapped: " + n + "\n";
        }
        std::vector<std::pair<std::string, int> > counts;
        for (const VsProjectItemPtr& i : Items)
        {
            bool found = false;
            for (std::pair<std::string, int>& kv : counts)
                if (kv.first == i->Type) { kv.second++; found = true; break; }
            if (!found) counts.push_back(std::make_pair(i->Type, 1));
        }
        std::vector<std::string> said;
        for (const std::pair<std::string, int>& kv : counts) said.push_back(std::to_string(kv.second) + " " + kv.first);
        t += "\nitems: " + Join(", ", said) + "\n";
        for (const VsProjectItemPtr& i : Items)
            if (!i->Excluded.empty())
                t += "  " + i->Include + " excluded from " + (i->Excluded.count("*") > 0 ? std::string("every configuration") : JoinSet(", ", i->Excluded)) + "\n";
        if (!Notes.empty())
        {
            t += "\nnot mapped:\n";
            for (const std::string& n : Notes) t += "  " + n + "\n";
        }
        t += "\nThese are read from the Visual Studio files and cannot be changed here - edit them in Visual Studio.\n";
        return t;
    }

    // ---- conditions: '$(Configuration)|$(Platform)'=='Debug|x64', exists(...), and / or ----
    static std::vector<std::string> SplitWord(const std::string& text, const char* word)
    {
        std::regex sep(std::string("\\s+") + word + "\\s+", std::regex::icase);
        std::vector<std::string> parts;
        std::sregex_token_iterator it(text.begin(), text.end(), sep, -1), end;
        for (; it != end; ++it) parts.push_back(it->str());
        return parts;
    }

    bool VsProject::Matches(const std::string& condition, const VsProjectConfigPtr& c)
    {
        if (StringUtil::IsNullOrWhiteSpace(condition)) return true;
        std::string text = StringUtil::Trim(condition);
        std::vector<std::string> ors = SplitWord(text, "or");
        if (ors.size() > 1)
        {
            for (const std::string& o : ors)
                if (Matches(o, c)) return true;
            return false;
        }
        std::vector<std::string> ands = SplitWord(text, "and");
        if (ands.size() > 1)
        {
            for (const std::string& a : ands)
                if (!Matches(a, c)) return false;
            return true;
        }
        std::string one = StringUtil::Trim(text);
        {
            size_t b = 0, e = one.size();
            while (b < e && (one[b] == '(' || one[b] == ')')) b++;
            while (e > b && (one[e - 1] == '(' || one[e - 1] == ')')) e--;
            one = StringUtil::Trim(one.substr(b, e - b));
        }
        bool negate = false;
        if (StringUtil::StartsWith(one, "!")) { negate = true; one = StringUtil::Trim(one.substr(1)); }
        bool result;
        static const std::regex exists("^exists\\s*\\(\\s*'?([^')]*)'?\\s*\\)$", std::regex::icase);
        static const std::regex compare("^'(.*)'\\s*(==|!=)\\s*'(.*)'$");
        std::smatch ex, cmp;
        if (std::regex_match(one, ex, exists))
        {
            std::string p = PathUtil::Normalize(ExpandForCondition(ex[1].str(), c));
            if (!p.empty() && !Paths::IsRooted(p)) p = Paths::Combine(Dir, p);
            result = !HasMacro(p) && !p.empty() && Paths::Exists(p);
        }
        else if (std::regex_match(one, cmp, compare))
        {
            std::string l = ExpandForCondition(cmp[1].str(), c);
            std::string r = ExpandForCondition(cmp[3].str(), c);
            bool same = StringUtil::EqualsIgnoreCase(l, r);
            result = cmp[2].str() == "==" ? same : !same;
        }
        else if (StringUtil::EqualsIgnoreCase(one, "true")) result = true;
        else if (StringUtil::EqualsIgnoreCase(one, "false")) result = false;
        else
        {
            AddNote(Notes, "condition not evaluated, taken as true: " + condition);
            result = true;
        }
        return negate ? !result : result;
    }

    // in a condition an unknown property is empty, as MSBuild has it
    std::string VsProject::ExpandForCondition(const std::string& s, const VsProjectConfigPtr& c) const
    {
        std::string in = ExpandMacros(s, c);
        std::string out;
        size_t i = 0;
        while (i < in.size())
        {
            if (in[i] == '$' && i + 1 < in.size() && in[i + 1] == '(')
            {
                size_t close = in.find(')', i + 2);
                if (close != std::string::npos) { i = close + 1; continue; }
            }
            out += in[i++];
        }
        return out;
    }

    // ---- .vcxproj ----
    void VsProject::ReadVcxproj(const XmlDocument& doc)
    {
        XmlNodePtr root = doc.DocumentElement;
        if (!root || !Named(root, "Project")) throw std::runtime_error(Path + " is not an MSBuild project");
        ToolsVersion = Attr(root, "ToolsVersion");
        // the configurations first, so the groups can be judged against them
        for (const XmlNodePtr& g : Elements(root))
            if (Named(g, "ItemGroup"))
                for (const XmlNodePtr& e : Elements(g))
                    if (Named(e, "ProjectConfiguration"))
                    {
                        std::string name = Attr(e, "Include");
                        if (!name.empty() && !Config(name)) Configurations.push_back(std::make_shared<VsProjectConfig>(name));
                    }
        if (Configurations.empty())
        {
            Configurations.push_back(std::make_shared<VsProjectConfig>("Debug|Win32"));
            Configurations.push_back(std::make_shared<VsProjectConfig>("Release|Win32"));
            Notes.push_back("no ProjectConfiguration in the file: Debug|Win32 and Release|Win32 assumed");
        }
        // the globals and every group in document order
        for (const XmlNodePtr& g : Elements(root))
        {
            std::string condition = Attr(g, "Condition");
            if (Named(g, "PropertyGroup"))
            {
                for (const VsProjectConfigPtr& c : Configurations)
                {
                    if (!Matches(condition, c)) continue;
                    for (const XmlNodePtr& e : Elements(g))
                    {
                        std::string n = e->LocalName();
                        std::string v = StringUtil::Trim(e->InnerText());
                        if (!Matches(Attr(e, "Condition"), c)) continue;
                        if (n == "ProjectGuid") Guid = VsSolution::CleanGuid(v);
                        else if (n == "ProjectName") { if (!v.empty()) Name = v; }
                        else if (n == "RootNamespace") { if (Name.empty()) Name = v; }
                        else if (n == "ConfigurationType") c->ConfigurationType = v;
                        else if (n == "PlatformToolset") c->Toolset = v;
                        else if (n == "CharacterSet") c->CharacterSet = v;
                        else if (n == "CLRSupport") c->ClrSupport = !StringUtil::EqualsIgnoreCase(v, "false") && !v.empty();
                        else if (n == "UseDebugLibraries") { c->UseDebugLibraries = v; c->UseDebugLibrariesSaid = true; }
                        else if (n == "OutDir") c->OutDir = v;
                        else if (n == "IntDir") c->IntDir = v;
                        else if (n == "TargetName") c->TargetName = v;
                    }
                }
            }
            else if (Named(g, "ItemDefinitionGroup"))
            {
                for (const VsProjectConfigPtr& c : Configurations)
                {
                    if (!Matches(condition, c)) continue;
                    for (const XmlNodePtr& tool : Elements(g))
                    {
                        std::string tn = tool->LocalName();
                        if (tn == "ClCompile")
                        {
                            for (const XmlNodePtr& e : Elements(tool))
                            {
                                std::string n = e->LocalName();
                                std::string v = StringUtil::Trim(e->InnerText());
                                if (!Matches(Attr(e, "Condition"), c)) continue;
                                if (n == "PreprocessorDefinitions") ApplyList(c->Defines, v);
                                else if (n == "UndefinePreprocessorDefinitions") ApplyList(c->Undefines, v);
                                else if (n == "AdditionalIncludeDirectories") ApplyDirs(c->IncludeDirs, v, c);
                                else if (n == "Optimization") c->Optimization = v;
                                else if (n == "AdditionalOptions") c->AdditionalOptions = StringUtil::Trim(StringUtil::Replace(v, "%(AdditionalOptions)", ""));
                                else if (n == "CompileAs") c->CompileAs = v;
                                else if (n == "PrecompiledHeader")
                                {
                                    if (!v.empty() && !StringUtil::EqualsIgnoreCase(v, "NotUsing")) AddNote(c->Notes, "precompiled header (" + v + ")");
                                }
                                else if (n == "ForcedIncludeFiles") AddNote(c->Notes, "forced include files: " + v);
                                else if (!v.empty()) c->SetClSetting(n, v);
                            }
                        }
                        else if (tn == "Link" || tn == "Lib")
                        {
                            for (const XmlNodePtr& e : Elements(tool))
                            {
                                std::string n = e->LocalName();
                                std::string v = StringUtil::Trim(e->InnerText());
                                if (!Matches(Attr(e, "Condition"), c)) continue;
                                if (n == "AdditionalDependencies") ApplyList(c->Libs, v);
                                else if (n == "AdditionalLibraryDirectories") ApplyDirs(c->LibDirs, v, c);
                                else if (n == "SubSystem") c->SubSystem = v;
                                else if (n == "ModuleDefinitionFile" || n == "DelayLoadDLLs" || n == "EntryPointSymbol")
                                    AddNote(c->Notes, tn + " " + n + " " + v);
                            }
                        }
                        else if (tn == "PreBuildEvent" || tn == "PreLinkEvent" || tn == "PostBuildEvent" || tn == "CustomBuildStep" || tn == "CustomBuild")
                            AddNote(c->Notes, tn + " (not run)");
                        else if (tn == "ResourceCompile" || tn == "Midl" || tn == "Manifest" || tn == "ProjectReference")
                            AddNote(c->Notes, tn + " settings");
                    }
                }
            }
            else if (Named(g, "ItemGroup"))
            {
                for (const XmlNodePtr& e : Elements(g))
                {
                    std::string type = e->LocalName();
                    std::string include = Attr(e, "Include");
                    if (type == "ProjectConfiguration") continue;
                    if (type == "Reference") { References.push_back(include); continue; }
                    if (type == "ProjectReference") { ProjectReferences.push_back(include); continue; }
                    if (type == "PackageReference" || type == "Filter") continue;
                    if (!Attr(e, "Remove").empty() || !Attr(e, "Update").empty())
                    {
                        AddNote(Notes, type + " with Remove/Update (SDK style) not applied: " + Attr(e, "Remove") + Attr(e, "Update"));
                        continue;
                    }
                    if (include.empty()) continue;
                    AddItems(type, include, Attr(e, "Exclude"), e, condition);
                }
            }
            else if (Named(g, "Import")) NoteImport(Attr(g, "Project"));
            else if (Named(g, "ImportGroup"))
            {
                for (const XmlNodePtr& e : Elements(g))
                    if (Named(e, "Import")) NoteImport(Attr(e, "Project"));
            }
            else if (Named(g, "Target")) AddNote(Notes, "target '" + Attr(g, "Name") + "' (not run)");
        }
        Toolset = !Configurations.empty() ? Configurations[0]->Toolset : std::string();
        Version = DescribeToolsVersion(ToolsVersion, Toolset);
        for (const VsProjectConfigPtr& c : Configurations)
        {
            if (c->ClrSupport) AddNote(Notes, "C++/CLI (CLRSupport) in " + c->Name + " - GRIDE's compilers build native C and C++ only");
            if (!c->OutDir.empty()) c->OutDir = ExpandMacros(c->OutDir, c);
            if (!c->IntDir.empty()) c->IntDir = ExpandMacros(c->IntDir, c);
            if (!c->TargetName.empty()) c->TargetName = ExpandMacros(c->TargetName, c);
        }
        if (!References.empty()) AddNote(Notes, "assembly references: " + Join(", ", References));
        if (!ProjectReferences.empty()) AddNote(Notes, "project references: " + Join(", ", ProjectReferences));
        ReadFilters();
    }

    void VsProject::NoteImport(const std::string& project)
    {
        if (project.empty()) return;
        bool props = StringUtil::EndsWithIgnoreCase(project, ".props");
        if (props && !HasMacro(project)) AddNote(Notes, "property sheet not evaluated: " + project);
        else if (props && !StringUtil::Contains(project, "$(VCTargetsPath)") && !StringUtil::Contains(project, "$(UserRootDir)"))
            AddNote(Notes, "property sheet not evaluated: " + project);
        else if (StringUtil::EndsWithIgnoreCase(project, ".targets") && !StringUtil::Contains(project, "$(VCTargetsPath)"))
            AddNote(Notes, "targets file not run: " + project);
    }

    void VsProject::ApplyDirs(std::vector<std::string>& into, const std::string& value, const VsProjectConfigPtr& c)
    {
        if (!Appends(value)) into.clear();
        for (const std::string& raw : SplitList(value))
        {
            std::string d = ExpandMacros(raw, c);
            if (HasMacro(d))
            {
                AddNote(c->Notes, "directory with an unresolved macro left out: " + raw
                        + (SolutionDir.empty() && StringUtil::Contains(raw, "$(SolutionDir)") ? " (open the solution to resolve $(SolutionDir))" : ""));
                continue;
            }
            std::string trimmed = StringUtil::Trim(d);
            while (!trimmed.empty() && (trimmed[trimmed.size() - 1] == '\\' || trimmed[trimmed.size() - 1] == '/')) trimmed.erase(trimmed.size() - 1);
            std::string abs = PathUtil::MakeAbsolute(Dir, trimmed);
            if (!Has(into, abs)) into.push_back(abs);
        }
    }

    void VsProject::AddItems(const std::string& type, const std::string& include, const std::string& exclude,
                             const XmlNodePtr& e, const std::string& groupCondition)
    {
        std::vector<std::string> paths;
        bool wildcard = include.find_first_of("*?") != std::string::npos;
        std::string expanded = ExpandMacros(include, nullptr);
        if (wildcard)
        {
            paths = Expand(expanded);
            if (paths.empty()) AddNote(Notes, type + " pattern matched nothing: " + include);
            if (!exclude.empty())
                for (const std::string& x : SplitList(exclude))
                    for (const std::string& gone : Expand(ExpandMacros(x, nullptr)))
                    {
                        std::vector<std::string>::iterator at = std::find(paths.begin(), paths.end(), gone);
                        if (at != paths.end()) paths.erase(at);
                    }
        }
        else if (HasMacro(expanded)) { AddNote(Notes, type + " item with an unresolved macro: " + include); return; }
        else paths.push_back(PathUtil::MakeAbsolute(Dir, expanded));
        for (const std::string& path : paths)
        {
            VsProjectItemPtr item = std::make_shared<VsProjectItem>();
            item->Include = include;
            item->Path = path;
            item->Type = type;
            item->FromWildcard = wildcard;
            for (const XmlNodePtr& m : Elements(e))
            {
                std::string n = m->LocalName();
                if (n == "ExcludedFromBuild")
                {
                    if (!StringUtil::EqualsIgnoreCase(StringUtil::Trim(m->InnerText()), "true")) continue;
                    std::string cond = Attr(m, "Condition");
                    if (cond.empty()) item->Excluded.insert("*");
                    else
                        for (const VsProjectConfigPtr& c : Configurations)
                            if (Matches(cond, c)) item->Excluded.insert(c->Name);
                }
                else if (n == "Filter") item->Filter = StringUtil::Trim(m->InnerText());
                else if (n != "FileType" && n != "SubType" && !Has(item->Settings, n)) item->Settings.push_back(n);
            }
            // an item group of a condition holds its items for those configurations only
            if (!groupCondition.empty())
                for (const VsProjectConfigPtr& c : Configurations)
                    if (!Matches(groupCondition, c)) item->Excluded.insert(c->Name);
            Items.push_back(item);
            if (!item->Settings.empty() && type == "ClCompile")
                AddNote(Notes, "per-file settings of " + include + " not mapped: " + Join(", ", item->Settings));
        }
    }

    // * and ? in a file name, ignoring case as Windows does
    static bool GlobMatch(const std::string& pattern, const std::string& name)
    {
        size_t p = 0, n = 0, star = std::string::npos, mark = 0;
        while (n < name.size())
        {
            if (p < pattern.size() && (pattern[p] == '?' || StringUtil::ToLowerChar(pattern[p]) == StringUtil::ToLowerChar(name[n])))
            {
                p++; n++;
            }
            else if (p < pattern.size() && pattern[p] == '*') { star = p++; mark = n; }
            else if (star != std::string::npos) { p = star + 1; n = ++mark; }
            else return false;
        }
        while (p < pattern.size() && pattern[p] == '*') p++;
        return p == pattern.size();
    }

    static void FindFiles(const std::string& dir, const std::string& pattern, bool recursive, std::vector<std::string>& out)
    {
        for (const std::string& name : Paths::List(dir))
        {
            std::string full = Paths::Combine(dir, name);
            if (Paths::IsDirectory(full)) { if (recursive) FindFiles(full, pattern, true, out); }
            else if (GlobMatch(pattern, name)) out.push_back(full);
        }
    }

    // a wildcard pattern expanded against the project folder (* and ? in the last part, ** for any depth)
    std::vector<std::string> VsProject::Expand(const std::string& pattern) const
    {
        std::vector<std::string> r;
        std::string p = StringUtil::ReplaceChar(pattern, '\\', '/');
        size_t star2 = p.find("**");
        std::string baseRel, filePattern;
        bool recursive = false;
        if (star2 != std::string::npos)
        {
            baseRel = StringUtil::TrimEnd(p.substr(0, star2), '/');
            filePattern = p.substr(star2 + 2);
            while (!filePattern.empty() && filePattern[0] == '/') filePattern = filePattern.substr(1);
            if (filePattern.empty()) filePattern = "*";
            recursive = true;
        }
        else
        {
            size_t slash = p.find_last_of('/');
            baseRel = slash == std::string::npos ? std::string() : p.substr(0, slash);
            filePattern = slash == std::string::npos ? p : p.substr(slash + 1);
        }
        if (filePattern.find('/') != std::string::npos)
        {
            size_t slash = filePattern.find_last_of('/');
            baseRel = baseRel + "/" + filePattern.substr(0, slash);
            filePattern = filePattern.substr(slash + 1);
        }
        std::string base = baseRel.empty() ? Dir : PathUtil::MakeAbsolute(Dir, baseRel);
        if (!Paths::IsDirectory(base)) return r;
        std::vector<std::string> found;
        FindFiles(base, filePattern, recursive, found);
        std::sort(found.begin(), found.end(),
                  [](const std::string& a, const std::string& b) { return StringUtil::CompareIgnoreCase(a, b) < 0; });
        for (const std::string& f : found) r.push_back(Paths::FullPath(f));
        return r;
    }

    // the .filters beside the project: the folders, and which one each item sits in
    void VsProject::ReadFilters()
    {
        std::string file = Path + ".filters";
        if (!Paths::IsFile(file)) return;
        FiltersFile = file;
        XmlDocument doc;
        try { doc = XmlDocument::Load(file); }
        catch (const std::exception& ex) { AddNote(Notes, std::string("the .filters file could not be read: ") + ex.what()); return; }
        std::map<std::string, std::string> byInclude;
        for (const XmlNodePtr& g : Elements(doc.DocumentElement))
        {
            if (!Named(g, "ItemGroup")) continue;
            for (const XmlNodePtr& e : Elements(g))
            {
                std::string include = Attr(e, "Include");
                if (Named(e, "Filter"))
                {
                    if (!include.empty() && !Has(Filters, include)) Filters.push_back(include);
                    continue;
                }
                std::string filter = ChildText(e, "Filter");
                if (!include.empty()) byInclude[ItemKey(include)] = filter;
            }
        }
        for (const VsProjectItemPtr& i : Items)
        {
            std::map<std::string, std::string>::const_iterator f = byInclude.find(ItemKey(i->Include));
            if (f != byInclude.end()) i->Filter = f->second;
        }
        // a filter an item names that the list does not: still a folder
        for (const VsProjectItemPtr& i : Items)
            if (!i->Filter.empty() && !Has(Filters, i->Filter)) Filters.push_back(i->Filter);
    }

    // what a classic file's item would be in MSBuild terms: by its extension
    static std::string TypeByExtension(const std::string& path)
    {
        std::string e = StringUtil::ToLower(Paths::Extension(PathUtil::Normalize(path)));
        if (e == ".cpp" || e == ".cxx" || e == ".cc" || e == ".c" || e == ".c++") return "ClCompile";
        if (e == ".h" || e == ".hpp" || e == ".hxx" || e == ".hh" || e == ".inl" || e == ".tli" || e == ".tlh" || e == ".ipp") return "ClInclude";
        if (e == ".rc") return "ResourceCompile";
        if (e == ".idl") return "Midl";
        return "None";
    }

    // ---- .vcproj (the classic XML) ----
    void VsProject::ReadVcproj(const XmlDocument& doc)
    {
        XmlNodePtr root = doc.DocumentElement;
        if (!root || !Named(root, "VisualStudioProject")) throw std::runtime_error(Path + " is not a Visual Studio project");
        std::string ver = Attr(root, "Version");
        if (!Attr(root, "Name").empty()) Name = Attr(root, "Name");
        Guid = VsSolution::CleanGuid(Attr(root, "ProjectGUID"));
        if (StringUtil::StartsWith(ver, "9")) Version = "Visual Studio 2008";
        else if (StringUtil::StartsWith(ver, "8")) Version = "Visual Studio 2005";
        else if (StringUtil::StartsWith(ver, "7.1")) Version = "Visual Studio .NET 2003";
        else if (StringUtil::StartsWith(ver, "7")) Version = "Visual Studio .NET 2002";
        else Version = "Visual Studio (classic .vcproj)";
        if (!ver.empty()) Version += ", format " + ver;
        ToolsVersion = ver;
        XmlNodePtr configs = root->Child("Configurations");
        for (const XmlNodePtr& ce : Elements(configs))
        {
            if (!Named(ce, "Configuration")) continue;
            std::string name = Attr(ce, "Name");
            if (name.empty()) continue;
            VsProjectConfigPtr c = std::make_shared<VsProjectConfig>(name);
            std::string type = Attr(ce, "ConfigurationType");
            c->ConfigurationType = type == "1" ? "Application" : type == "2" ? "DynamicLibrary" : type == "4" ? "StaticLibrary" : type;
            c->OutDir = ExpandMacros(Attr(ce, "OutputDirectory"), c);
            c->IntDir = ExpandMacros(Attr(ce, "IntermediateDirectory"), c);
            std::string cs = Attr(ce, "CharacterSet");
            c->CharacterSet = cs == "1" ? "Unicode" : cs == "2" ? "MultiByte" : cs;
            if (!Attr(ce, "ManagedExtensions").empty() && Attr(ce, "ManagedExtensions") != "0") c->ClrSupport = true;
            for (const XmlNodePtr& tool : Elements(ce))
            {
                std::string tn = Attr(tool, "Name");
                if (StringUtil::EqualsIgnoreCase(tn, "VCCLCompilerTool"))
                {
                    ApplyList(c->Defines, Attr(tool, "PreprocessorDefinitions"));
                    ApplyList(c->Undefines, Attr(tool, "UndefinePreprocessorDefinitions"));
                    ApplyDirs(c->IncludeDirs, StringUtil::Replace(Attr(tool, "AdditionalIncludeDirectories"), ",", ";"), c);
                    std::string opt = Attr(tool, "Optimization");
                    c->Optimization = opt == "0" ? "Disabled" : opt == "1" ? "MinSpace" : opt == "2" ? "MaxSpeed" : opt == "3" ? "Full" : opt;
                    c->AdditionalOptions = Attr(tool, "AdditionalOptions");
                    std::string pch = Attr(tool, "UsePrecompiledHeader");
                    if (!pch.empty() && pch != "0") AddNote(c->Notes, "precompiled header");
                    std::string as = Attr(tool, "CompileAs");
                    c->CompileAs = as == "1" ? "CompileAsC" : as == "2" ? "CompileAsCpp" : std::string();
                }
                else if (StringUtil::EqualsIgnoreCase(tn, "VCLinkerTool"))
                {
                    // the classic file separates libraries with spaces
                    for (const std::string& l : StringUtil::SplitNonEmpty(Attr(tool, "AdditionalDependencies"), " ;"))
                        if (!Has(c->Libs, l)) c->Libs.push_back(l);
                    ApplyDirs(c->LibDirs, StringUtil::Replace(Attr(tool, "AdditionalLibraryDirectories"), ",", ";"), c);
                    std::string sub = Attr(tool, "SubSystem");
                    c->SubSystem = sub == "1" ? "Console" : sub == "2" ? "Windows" : sub;
                }
                else if (StringUtil::EqualsIgnoreCase(tn, "VCPreBuildEventTool") || StringUtil::EqualsIgnoreCase(tn, "VCPostBuildEventTool")
                         || StringUtil::EqualsIgnoreCase(tn, "VCPreLinkEventTool") || StringUtil::EqualsIgnoreCase(tn, "VCCustomBuildTool"))
                {
                    if (!Attr(tool, "CommandLine").empty()) AddNote(c->Notes, tn + " (not run)");
                }
            }
            Configurations.push_back(c);
        }
        if (Configurations.empty())
        {
            Configurations.push_back(std::make_shared<VsProjectConfig>("Debug|Win32"));
            Configurations.push_back(std::make_shared<VsProjectConfig>("Release|Win32"));
            Notes.push_back("no Configuration in the file: Debug|Win32 and Release|Win32 assumed");
        }
        XmlNodePtr files = root->Child("Files");
        if (files) ReadVcprojFiles(files, "");
        Toolset.clear();
    }

    void VsProject::ReadVcprojFiles(const XmlNodePtr& parent, const std::string& filter)
    {
        for (const XmlNodePtr& e : Elements(parent))
        {
            if (Named(e, "Filter"))
            {
                std::string name = Attr(e, "Name");
                std::string path = filter.empty() ? name : filter + "\\" + name;
                if (!Has(Filters, path)) Filters.push_back(path);
                ReadVcprojFiles(e, path);
            }
            else if (Named(e, "File"))
            {
                std::string rel = Attr(e, "RelativePath");
                if (rel.empty()) continue;
                VsProjectItemPtr item = std::make_shared<VsProjectItem>();
                item->Include = rel;
                item->Path = PathUtil::MakeAbsolute(Dir, ExpandMacros(rel, nullptr));
                item->Type = TypeByExtension(rel);
                item->Filter = filter;
                for (const XmlNodePtr& fc : Elements(e))
                {
                    if (!Named(fc, "FileConfiguration")) continue;
                    if (StringUtil::EqualsIgnoreCase(Attr(fc, "ExcludedFromBuild"), "true")) item->Excluded.insert(Attr(fc, "Name"));
                    for (const XmlNodePtr& tool : Elements(fc))
                        if (!Attr(tool, "Name").empty() && tool->Attributes.size() > 1 && !Has(item->Settings, Attr(tool, "Name")))
                            item->Settings.push_back(Attr(tool, "Name"));
                }
                Items.push_back(item);
            }
        }
    }
}}
