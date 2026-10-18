// =============================================================================
//  CcsReading.cpp - see CcsReading.h
// =============================================================================
#include "CcsReading.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Core/TextFile.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include "../Settings/ToolchainDefinitions.h"
#include <algorithm>
#include <stdexcept>

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;
    using namespace GRIDEm::Settings;
    using Platform::Paths;

    // ---- small helpers ---------------------------------------------------------
    static bool Has(const std::vector<std::string>& list, const std::string& one)
    {
        return std::find(list.begin(), list.end(), one) != list.end();
    }

    static void AddUnique(std::vector<std::string>& into, const std::string& one)
    {
        if (!Has(into, one)) into.push_back(one);
    }

    static std::string ReadText(const std::string& path)
    {
        std::string text, error;
        TextEncoding enc;
        if (!TextFile::Read(path, text, enc, error)) return std::string();
        return text;
    }

    static std::string Unquoted(const std::string& s)
    {
        if (s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"') return s.substr(1, s.size() - 2);
        return s;
    }

    static bool Rooted(const std::string& p)
    {
        return !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
    }

    static std::string Parent(const std::string& dir)
    {
        return Paths::DirectoryName(dir);
    }

    // =========================================================================
    //  CcsOptionDefs
    // =========================================================================
    std::string CcsOptionDefs::NamedDir;

    bool CcsOptionDefs::Holds(const std::string& dir)
    {
        const CcsDefinition& d = ToolchainDefinitions::Current()->Ccs;
        if (dir.empty() || !Paths::IsDirectory(dir)) return false;
        for (StringMap::const_iterator it = d.Definitions.begin(); it != d.Definitions.end(); ++it)
            if (!Paths::IsFile(Paths::Combine(dir, it->second))) return false;
        return !d.Definitions.empty();
    }

    std::string CcsOptionDefs::ToolchainSettingsRoot()
    {
        const InstallationDefinition& inst = ToolchainDefinitions::Current()->Installation;
        for (const std::string& r : inst.DefaultRoots)
        {
            std::string x = Platform::Environment::Expand(r);
            if (x.find('%') == std::string::npos && Paths::IsDirectory(x)) return x;
        }
        return std::string();
    }

    std::string CcsOptionDefs::DefinitionsDir()
    {
        const CcsDefinition& d = ToolchainDefinitions::Current()->Ccs;
        if (!NamedDir.empty()) return Holds(NamedDir) ? NamedDir : std::string();
        std::string rel = PathUtil::Normalize(d.DefinitionsDir);
        std::vector<std::string> tried;
        // the app's Resources first: where GRIDEm.app keeps lib/
        tried.push_back(Paths::Combine(Paths::ResourcesDirectory(), rel));
        std::string program = ToolchainDefinitions::ExeDirectory();
        if (!program.empty())
        {
            tried.push_back(Paths::Combine(program, rel));
            std::string up = program;
            for (int i = 0; i < 4; i++)
            {
                std::string p = Parent(up);
                if (p.empty() || p == up) break;
                up = p;
                tried.push_back(Paths::Combine(up, rel));
                tried.push_back(Paths::Combine(Paths::Combine(up, "GRIDE"), rel));
                tried.push_back(Paths::Combine(Paths::Combine(up, "Resources"), rel));
            }
        }
        std::string root = ToolchainSettingsRoot();
        if (!root.empty())
        {
            tried.push_back(Paths::Combine(root, rel));
            tried.push_back(Paths::Combine(Parent(root), rel));
        }
        for (const std::string& t : tried)
            if (Holds(t)) return t;
        return std::string();
    }

    bool CcsOptionDefs::Load(const std::string& version, std::string& why)
    {
        defs.clear();
        File.clear();
        why.clear();
        const CcsDefinition& d = ToolchainDefinitions::Current()->Ccs;
        std::string dir = DefinitionsDir();
        if (dir.empty())
        {
            why = "TI's option definitions are not here - " + d.DefinitionsDir
                + " in GRIDEm's Resources, or docs/ccs-reference/ti-option-definitions";
            return false;
        }
        std::string major = MajorOf(version);
        std::string name;
        StringMap::const_iterator found = d.Definitions.find(major);
        if (found != d.Definitions.end()) name = found->second;
        else
        {
            // the nearest major: 8 and up take the newest file, anything older the oldest
            std::string best;
            bool haveBest = false;
            int bestMajor = -1;
            int wanted = 0;
            StringUtil::TryParseInt(major, wanted);
            for (StringMap::const_iterator it = d.Definitions.begin(); it != d.Definitions.end(); ++it)
            {
                int m;
                if (!StringUtil::TryParseInt(it->first, m)) continue;
                if (!haveBest || (m <= wanted && m > bestMajor) || (bestMajor > wanted && m < bestMajor))
                {
                    best = it->second;
                    bestMajor = m;
                    haveBest = true;
                }
            }
            name = best;
        }
        if (name.empty()) { why = "no option definition file is named for compiler version " + version; return false; }
        std::string file = Paths::Combine(dir, name);
        if (!Paths::IsFile(file)) { why = "cannot read " + file; return false; }
        std::vector<std::string> lines = StringUtil::SplitLines(ReadText(file));
        if (!lines.empty() && lines.back().empty()) lines.pop_back();
        for (size_t i = 1; i < lines.size(); i++)
        {
            std::vector<std::string> cols = StringUtil::Split(lines[i], '\t');
            if (cols.size() < 7) continue;
            CcsOptionDefPtr def = std::make_shared<CcsOptionDef>();
            def->Id = IdTail(StringUtil::Trim(cols[1]));
            def->ValueType = StringUtil::Trim(cols[2]);
            def->Command = StringUtil::Trim(cols[3]);
            def->DefaultValue = StringUtil::Trim(cols[5]);
            def->SuperClass = IdTail(StringUtil::Trim(cols[6]));
            if (cols.size() > 7)
                for (const std::string& raw : StringUtil::Split(cols[7], '|'))
                {
                    std::string one = StringUtil::Trim(raw);
                    if (one.empty()) continue;
                    bool isDefault = false;
                    const std::string mark = " (default)";
                    if (StringUtil::EndsWith(one, mark)) { one = one.substr(0, one.size() - mark.size()); isDefault = true; }
                    size_t eq = one.find('=');
                    std::string suffix = eq == std::string::npos ? one : one.substr(0, eq);
                    std::string flag = eq == std::string::npos ? std::string() : one.substr(eq + 1);
                    def->Enums.push_back(std::make_pair(suffix, flag));
                    if (isDefault) def->DefaultEnum = suffix;
                }
            // the first row of a tail wins: an instance (compilerRelease's OPT_LEVEL.release) has its own id
            if (defs.find(def->Id) == defs.end()) defs[def->Id] = def;
        }
        File = file;
        if (defs.empty()) { why = file + " holds no definitions"; return false; }
        return true;
    }

    CcsOptionDefPtr CcsOptionDefs::Find(const std::string& tail) const
    {
        std::map<std::string, CcsOptionDefPtr>::const_iterator it = defs.find(tail);
        return it == defs.end() ? nullptr : it->second;
    }

    std::string CcsOptionDefs::Spell(const std::string& tail, const std::string& value,
                                     const std::vector<std::string>& items) const
    {
        CcsOptionDefPtr def = Find(tail);
        // an instance carries its superClass's shape: OPT_LEVEL.release spells as OPT_LEVEL does
        CcsOptionDefPtr shape = (def && def->ValueType.empty() && !def->SuperClass.empty()) ? Find(def->SuperClass) : def;
        if (!shape)
        {
            std::string said = tail;
            if (!value.empty()) said += "=" + value;
            for (size_t i = 0; i < items.size(); i++) said += (i > 0 ? "," : "=") + items[i];
            return said;
        }
        if (shape->ValueType == "boolean") return value == "true" ? shape->Command : shape->Command + "=false";
        if (shape->ValueType == "enumerated")
        {
            std::string suffix = EnumSuffix("", value);
            for (const std::pair<std::string, std::string>& e : shape->Enums)
                if (e.first == suffix) return e.second.empty() ? shape->Command : e.second;
            return shape->Command + "=" + suffix;
        }
        if (shape->ValueType == "string") return shape->Command + value;
        std::string said;
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i > 0) said += ' ';
            said += shape->Command + items[i];
        }
        return said.empty() ? shape->Command : said;
    }

    std::string CcsOptionDefs::DefaultOf(const std::string& tail) const
    {
        CcsOptionDefPtr def = Find(tail);
        if (!def) return std::string();
        if (!def->DefaultValue.empty())
        {
            CcsOptionDefPtr shape = !def->ValueType.empty() ? def : Find(def->SuperClass);
            if (shape && shape->ValueType == "enumerated") return EnumSuffix("", def->DefaultValue);
            return def->DefaultValue;
        }
        if (!def->DefaultEnum.empty()) return def->DefaultEnum;
        if (def->ValueType.empty() && !def->SuperClass.empty()) return DefaultOf(def->SuperClass);
        return std::string();
    }

    std::string CcsOptionDefs::IdTail(const std::string& fullId)
    {
        const std::string prefix = "com.ti.ccstudio.buildDefinitions.";
        if (!StringUtil::StartsWith(fullId, prefix)) return fullId;
        std::string rest = fullId.substr(prefix.size());
        // past "C6000_8.2." - the version carries a dot of its own - or "core."
        if (StringUtil::StartsWith(rest, "C6000_"))
        {
            size_t at = 6;
            while (at < rest.size() && (StringUtil::IsDigit(rest[at]) || rest[at] == '.')) at++;
            return rest.substr(at);
        }
        return rest;
    }

    std::string CcsOptionDefs::EnumSuffix(const std::string& optionId, const std::string& valueId)
    {
        if (!optionId.empty() && StringUtil::StartsWith(valueId, optionId) && valueId.size() > optionId.size()
            && valueId[optionId.size()] == '.')
            return valueId.substr(optionId.size() + 1);
        size_t dot = valueId.find_last_of('.');
        return dot == std::string::npos ? valueId : valueId.substr(dot + 1);
    }

    std::string CcsOptionDefs::MajorOf(const std::string& version)
    {
        if (version.empty()) return std::string();
        size_t dot = version.find('.');
        return dot == std::string::npos ? StringUtil::Trim(version) : StringUtil::Trim(version.substr(0, dot));
    }

    // =========================================================================
    //  CcsWorkspace
    // =========================================================================
    const char* const CcsWorkspace::Resources = ".metadata/.plugins/org.eclipse.core.resources";
    const char* const CcsWorkspace::RuntimeSettings = ".metadata/.plugins/org.eclipse.core.runtime/.settings";

    bool CcsWorkspace::Project(const std::string& name, CcsWorkspaceMember& out) const
    {
        for (const CcsWorkspaceMember& m : Projects)
            if (m.Name == name) { out = m; return true; }
        return false;
    }

    bool CcsWorkspace::IsWorkspace(const std::string& dir)
    {
        return !dir.empty() && Paths::IsDirectory(Paths::Combine(dir, Resources));
    }

    std::string CcsWorkspace::WithSlashes(const std::string& p)
    {
        return StringUtil::ReplaceChar(p, '\\', '/');
    }

    CcsWorkspacePtr CcsWorkspace::Read(const std::string& where, std::string& error)
    {
        error.clear();
        std::string dir = StringUtil::TrimEnd(WithSlashes(Paths::FullPath(where)), '/');
        if (!IsWorkspace(dir)) { error = dir + " is not a CCS workspace (no .metadata)"; return nullptr; }
        CcsWorkspacePtr out = std::make_shared<CcsWorkspace>();
        out->Dir = dir;
        std::string registry = Paths::Combine(dir, std::string(Resources) + "/.projects");
        if (Paths::IsDirectory(registry))
            for (const std::string& name : Paths::ListDirectories(registry))
            {
                if (name.empty() || name[0] == '.') continue;
                std::string sub = Paths::Combine(registry, name);
                CcsWorkspaceMember member;
                member.Name = name;
                std::string record = Paths::Combine(sub, ".location");
                if (Paths::IsFile(record))
                {
                    std::string bytes, err;
                    Paths::ReadFile(record, bytes, err);
                    member.Location = LocationRecord(bytes);
                    if (member.Location.empty()) { out->Notes.push_back(name + ": its .location could not be read"); continue; }
                }
                else { member.Location = dir + "/" + name; member.Inside = true; }
                if (!Paths::IsDirectory(member.Location))
                {
                    out->Notes.push_back(name + " is registered at " + member.Location + ", which is not on this machine");
                    continue;
                }
                if (!IsProjectDir(member.Location)) continue;   // RemoteSystemsTempFiles and other non-CCS projects
                out->Projects.push_back(member);
            }
        std::sort(out->Projects.begin(), out->Projects.end(),
                  [](const CcsWorkspaceMember& a, const CcsWorkspaceMember& b) { return a.Name < b.Name; });
        std::string settingsDir = Paths::Combine(dir, RuntimeSettings);
        std::string resources = Paths::Combine(settingsDir, "org.eclipse.core.resources.prefs");
        if (Paths::IsFile(resources))
        {
            std::map<std::string, std::string> props = Properties(ReadText(resources));
            for (std::map<std::string, std::string>::const_iterator it = props.begin(); it != props.end(); ++it)
                if (StringUtil::StartsWith(it->first, "pathvariable."))
                    out->PathVariables[it->first.substr(13)] = PathOfUri(it->second);
        }
        std::string cdt = Paths::Combine(settingsDir, "org.eclipse.cdt.core.prefs");
        if (Paths::IsFile(cdt))
        {
            std::map<std::string, std::string> prefs = Properties(ReadText(cdt));
            std::map<std::string, std::string>::const_iterator xml = prefs.find("macros/workspace");
            if (xml != prefs.end())
            {
                try
                {
                    XmlDocument d = XmlDocument::Parse(xml->second);
                    for (const XmlNodePtr& m : d.SelectNodes("//stringMacro"))
                        out->Macros[Attr(m, "name")] = Attr(m, "value");
                }
                catch (const std::exception& ex)
                {
                    out->Notes.push_back(std::string("the workspace's build macros could not be read: ") + ex.what());
                }
            }
        }
        return out;
    }

    std::string CcsWorkspace::WorkspaceOf(const std::string& projectDir)
    {
        std::string dir = StringUtil::TrimEnd(WithSlashes(Paths::FullPath(projectDir)), '/');
        std::string parent = Parent(dir);
        if (parent.empty() || !IsWorkspace(parent)) return std::string();
        std::string error;
        CcsWorkspacePtr ws = Read(parent, error);
        if (!ws) return std::string();
        for (const CcsWorkspaceMember& m : ws->Projects)
            if (PathUtil::SamePath(m.Location, dir)) return ws->Dir;
        return std::string();
    }

    bool CcsWorkspace::IsProjectDir(const std::string& dir)
    {
        return Paths::IsDirectory(dir) && Paths::IsFile(Paths::Combine(dir, ".project"))
            && Paths::IsFile(Paths::Combine(dir, ".ccsproject"));
    }

    static bool IsHex(char c)
    {
        return StringUtil::IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    std::string CcsWorkspace::PathOfUri(const std::string& uri)
    {
        if (!StringUtil::StartsWith(uri, "file:")) return uri;
        std::string text = uri.substr(5);
        std::string out;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '%' && i + 2 < text.size() && IsHex(text[i + 1]) && IsHex(text[i + 2]))
            {
                unsigned long long v = 0;
                StringUtil::TryParseHex(text.substr(i + 1, 2), v);
                out += (char)v;          // the bytes of UTF-8, as the URI escapes them
                i += 2;
            }
            else out += text[i];
        }
        std::string s = out;
        while (s.size() > 1 && s[0] == '/' && s[1] == '/') s = s.substr(1);
        if (s.size() > 2 && s[0] == '/' && s[2] == ':') s = s.substr(1);
        return WithSlashes(s);
    }

    std::string CcsWorkspace::LocationRecord(const std::string& bytes)
    {
        if (bytes.size() < 18) return std::string();
        size_t length = ((size_t)(unsigned char)bytes[16] << 8) | (size_t)(unsigned char)bytes[17];
        if (length == 0 || 18 + length > bytes.size()) return std::string();
        std::string text = bytes.substr(18, length);
        if (StringUtil::StartsWith(text, "URI//")) return PathOfUri(text.substr(5));
        return WithSlashes(text);
    }

    static void AppendUtf8(std::string& out, unsigned int cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    std::map<std::string, std::string> CcsWorkspace::Properties(const std::string& text)
    {
        std::map<std::string, std::string> out;
        std::vector<std::string> lines;
        std::string line;
        for (size_t i = 0; i <= text.size(); i++)
        {
            if (i == text.size() || text[i] == '\n')
            {
                std::string l = StringUtil::TrimEnd(line, '\r');
                size_t slashes = 0;
                while (slashes < l.size() && l[l.size() - 1 - slashes] == '\\') slashes++;
                if (slashes % 2 == 1 && i < text.size()) { line = l.substr(0, l.size() - 1); continue; }
                lines.push_back(l);
                line.clear();
            }
            else line += text[i];
        }
        for (const std::string& raw : lines)
        {
            size_t b = 0;
            while (b < raw.size() && (raw[b] == ' ' || raw[b] == '\t')) b++;
            std::string t = raw.substr(b);
            if (t.empty() || t[0] == '#' || t[0] == '!') continue;
            std::string key, value;
            bool inKey = true;
            for (size_t i = 0; i < t.size(); i++)
            {
                char c = t[i];
                std::string& into = inKey ? key : value;
                if (c == '\\' && i + 1 < t.size())
                {
                    char e = t[++i];
                    if (e == 'n') into += '\n';
                    else if (e == 'r') into += '\r';
                    else if (e == 't') into += '\t';
                    else if (e == 'u' && i + 4 < t.size())
                    {
                        unsigned long long v = 0;
                        StringUtil::TryParseHex(t.substr(i + 1, 4), v);
                        AppendUtf8(into, (unsigned int)v);
                        i += 4;
                    }
                    else into += e;
                    continue;
                }
                if (inKey && (c == '=' || c == ':')) { inKey = false; continue; }
                into += c;
            }
            out[key] = value;
        }
        return out;
    }

    std::string CcsWorkspace::Attr(const XmlNodePtr& n, const std::string& name)
    {
        return n ? n->Attr(name) : std::string();
    }

    // =========================================================================
    //  CcsReading
    // =========================================================================
    enum FileKind { KindCompiled = 0, KindCommand = 1, KindNotBuilt = 2, KindHeader = 3, KindOther = 4 };
    static const char* const CgToolRootMacro = "${CG_TOOL_ROOT}";

    static std::string Attr(const XmlNodePtr& n, const std::string& name) { return CcsWorkspace::Attr(n, name); }

    static XmlDocument LoadXml(const std::string& file)
    {
        try { return XmlDocument::Load(file); }
        catch (const std::exception& ex) { throw std::runtime_error(file + ": " + ex.what()); }
    }

    // what CCS's makefile would take a file for, by its extension (case as written)
    static int KindOf(const std::string& name)
    {
        std::string ext = Paths::Extension(name);
        if (!ext.empty()) ext = ext.substr(1);
        if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "C" || ext == "c++") return KindCompiled;
        if (ext == "cmd") return KindCommand;
        if (ext == "asm" || ext == "s" || ext == "sa" || ext == "lib" || ext == "a" || ext == "obj") return KindNotBuilt;
        if (ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx") return KindHeader;
        return KindOther;
    }

    // the files under a folder, recursively, in name order; the build directories and the dot directories skipped
    static void Walk(const std::string& dir, const std::string& relative, const std::vector<std::string>& skip,
                     std::vector<std::string>& into)
    {
        if (!Paths::IsDirectory(dir)) return;
        std::vector<std::string> names = Paths::List(dir);        // sorted ordinally
        for (const std::string& name : names)
        {
            if (name.empty() || name[0] == '.') continue;
            std::string rel = relative.empty() ? name : relative + "/" + name;
            std::string full = Paths::Combine(dir, name);
            if (Paths::IsDirectory(full))
            {
                if (relative.empty() && Has(skip, name)) continue;
                if (StringUtil::EndsWith(name, ".vm")) continue;
                Walk(full, rel, skip, into);
            }
            else if (Paths::IsFile(full)) into.push_back(rel);
        }
    }

    static bool ExcludedBy(const std::string& relative, const std::vector<std::string>& excluding)
    {
        for (const std::string& one : excluding)
        {
            if (one.empty()) continue;
            if (relative == one) return true;
            if (relative.size() > one.size() && StringUtil::StartsWith(relative, one) && relative[one.size()] == '/') return true;
        }
        return false;
    }

    static bool SameList(const std::vector<std::string>& a, const std::vector<std::string>& b)
    {
        return a == b;
    }

    CcsConfigPtr CcsReading::Config(bool debug) const
    {
        for (const CcsConfigPtr& c : Configs)
            if (c->IsDebug == debug) return c;
        return nullptr;
    }

    bool CcsReading::IsProject(const std::string& dir) { return CcsWorkspace::IsProjectDir(dir); }

    bool CcsReading::IsProjectFile(const std::string& file)
    {
        std::string leaf = Paths::FileName(PathUtil::Normalize(file));
        return leaf == ".project" || leaf == ".ccsproject" || leaf == ".cproject";
    }

    bool CcsReading::IsCompilerDir(const std::string& dir)
    {
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        const CcsDefinition& cd = defs->Ccs;
        std::string trimmed = dir;
        while (!trimmed.empty() && (trimmed[trimmed.size() - 1] == '/' || trimmed[trimmed.size() - 1] == '\\'))
            trimmed.erase(trimmed.size() - 1);
        std::string leaf = Paths::FileName(PathUtil::Normalize(trimmed));
        for (StringMap::const_iterator it = cd.CompilerDirs.begin(); it != cd.CompilerDirs.end(); ++it)
            if (!it->second.empty() && StringUtil::StartsWithIgnoreCase(leaf, it->second)) return true;
        std::string bin = cd.TiBin.empty() ? dir : Paths::Combine(dir, PathUtil::Normalize(cd.TiBin));
        ToolDefinitionPtr ld;
        for (const TargetDefinitionPtr& t : defs->Targets)
            if (t->Emulated) { ld = defs->Tool(t->ToolIdFor("nativeLinker")); break; }
        if (ld && Paths::IsDirectory(bin))
            for (const std::string& n : ld->AllNames())
                if (Paths::IsFile(Paths::Combine(bin, n))) return true;
        return false;
    }

    std::string CcsReading::CgToolRootFor(const std::string& ccsRoot, const std::string& version)
    {
        if (StringUtil::IsNullOrWhiteSpace(ccsRoot)) return std::string();
        std::string root = StringUtil::TrimEnd(CcsWorkspace::WithSlashes(StringUtil::Trim(ccsRoot)), '/');
        // the compiler directory itself, told by its place, its name or the linker in its bin
        if (StringUtil::Contains(root, "/tools/compiler/") || IsCompilerDir(root)) return root;
        std::string compilers = root + "/tools/compiler";
        std::string found;
        if (Paths::IsDirectory(compilers))
            for (const std::string& name : Paths::ListDirectories(compilers))
            {
                if (StringUtil::EndsWith(name, "_" + version)) return compilers + "/" + name;
                if (found.empty() && StringUtil::Contains(name, version)) found = compilers + "/" + name;
            }
        if (!found.empty()) return found;
        std::string prefix;
        const StringMap& dirs = ToolchainDefinitions::Current()->Ccs.CompilerDirs;
        StringMap::const_iterator it = dirs.find(CcsOptionDefs::MajorOf(version));
        if (it != dirs.end()) prefix = it->second;
        else
        {
            int major = 0;
            StringUtil::TryParseInt(CcsOptionDefs::MajorOf(version), major);
            prefix = major >= 8 ? "ti-cgt-c6000_" : "c6000_";
        }
        return compilers + "/" + prefix + version;
    }

    std::string CcsReading::ResolveMacros(const std::string& text) const
    {
        std::string out;
        size_t i = 0;
        while (i < text.size())
        {
            if (text[i] != '$' || i + 1 >= text.size() || text[i + 1] != '{') { out += text[i++]; continue; }
            size_t close = text.find('}', i);
            if (close == std::string::npos) { out += text.substr(i); break; }
            std::string macro = text.substr(i + 2, close - i - 2);
            std::string value;
            bool known = true;
            if (macro == "ProjName") value = Name;
            else if (macro == "PROJECT_ROOT" || macro == "PROJECT_LOC") value = Dir;
            else if (macro == "CG_TOOL_ROOT") value = CgToolRoot;
            else if (macro == "CG_TOOL_CL") value = "\"" + CgToolRoot + "/bin/cl6x\"";
            else if (macro == "CG_CLEAN_CMD") value = "DEL /F";
            else if (macro == "BuildArtifactFileName") value = Name + ".out";
            else if (macro == "BuildDirectory") value = Dir;
            else if (macro == "workspace_loc") value = WorkspaceDir();
            else if (StringUtil::StartsWith(macro, "workspace_loc:"))
            {
                std::string rest = macro.substr(14);
                while (!rest.empty() && rest[0] == '/') rest = rest.substr(1);
                size_t slash = rest.find('/');
                std::string first = slash == std::string::npos ? rest : rest.substr(0, slash);
                std::map<std::string, std::string>::const_iterator member = Projects.find(first);
                if (member != Projects.end())
                    value = slash == std::string::npos ? member->second : member->second + "/" + rest.substr(slash + 1);
                else value = WorkspaceDir() + "/" + rest;
            }
            else
            {
                std::map<std::string, std::string>::const_iterator v = Macros.find(macro);
                if (v != Macros.end()) value = v->second;
                else known = false;
            }
            if (!known) { out += text.substr(i, close - i + 1); i = close + 1; continue; }
            out += value;
            i = close + 1;
        }
        return out;
    }

    std::string CcsReading::WorkspaceDir() const
    {
        if (!Workspace.empty()) return Workspace;
        std::string parent = Parent(Dir);
        return parent.empty() ? Dir : CcsWorkspace::WithSlashes(parent);
    }

    std::vector<CcsReading::Stored> CcsReading::OptionsOf(const XmlNodePtr& tool)
    {
        std::vector<Stored> out;
        for (const XmlNodePtr& o : tool->SelectNodes("option"))
        {
            Stored one;
            std::string sc = Attr(o, "superClass");
            one.Tail = CcsOptionDefs::IdTail(!sc.empty() ? sc : Attr(o, "id"));
            one.Value = Attr(o, "value");
            for (const XmlNodePtr& item : o->SelectNodes("listOptionValue")) one.Items.push_back(Attr(item, "value"));
            out.push_back(one);
        }
        return out;
    }

    const CcsReading::Stored* CcsReading::FindStored(const StoredList& list, const std::string& tail)
    {
        for (const Stored& s : list)
            if (s.Tail == tail) return &s;
        return nullptr;
    }

    // the tool whose superClass says so; a hand-made project may leave superClass out: then the id, else the name
    XmlNodePtr CcsReading::ToolNamed(const XmlNodePtr& toolChain, const std::string& word)
    {
        std::vector<XmlNodePtr> tools = toolChain->SelectNodes("tool");
        for (const XmlNodePtr& t : tools)
            if (StringUtil::Contains(Attr(t, "superClass"), word)) return t;
        for (const XmlNodePtr& t : tools)
            if (Attr(t, "superClass").empty() && StringUtil::Contains(Attr(t, "id"), word)) return t;
        std::string bare = word;
        while (!bare.empty() && bare[0] == '.') bare = bare.substr(1);
        for (const XmlNodePtr& t : tools)
            if (Attr(t, "superClass").empty() && StringUtil::ContainsIgnoreCase(Attr(t, "name"), bare)) return t;
        return nullptr;
    }

    // Eclipse's locationURI: PROJECT_LOC/x, PARENT-n-PROJECT_LOC/x, WORKSPACE_LOC/x, a path variable, file:/...
    std::string CcsReading::LocationOf(const XmlNodePtr& link) const
    {
        XmlNodePtr location = link->SelectSingleNode("location");
        if (location && !StringUtil::Trim(location->InnerText()).empty())
            return CcsWorkspace::WithSlashes(StringUtil::Trim(location->InnerText()));
        XmlNodePtr uri = link->SelectSingleNode("locationURI");
        if (!uri) return std::string();
        std::string text = StringUtil::Trim(uri->InnerText());
        if (StringUtil::StartsWith(text, "file:/")) return CcsWorkspace::PathOfUri(text);
        size_t slash = text.find('/');
        std::string variable = slash == std::string::npos ? text : text.substr(0, slash);
        std::string rest = slash == std::string::npos ? std::string() : text.substr(slash + 1);
        std::string base;
        if (variable == "PROJECT_LOC") base = Dir;
        else if (variable == "WORKSPACE_LOC") base = WorkspaceDir();
        else if (StringUtil::StartsWith(variable, "PARENT-") && StringUtil::EndsWith(variable, "-PROJECT_LOC")
                 && variable.size() >= 19)
        {
            int up = 0;
            StringUtil::TryParseInt(variable.substr(7, variable.size() - 7 - 12), up);
            base = Dir;
            for (int i = 0; i < up; i++)
            {
                std::string p = Parent(base);
                if (!p.empty()) base = CcsWorkspace::WithSlashes(p);
            }
        }
        else
        {
            std::map<std::string, std::string>::const_iterator v = PathVariables.find(variable);
            if (v == PathVariables.end()) return CcsWorkspace::WithSlashes(text);
            base = v->second;
        }
        return rest.empty() ? base : base + "/" + rest;
    }

    static std::string JoinWith(const std::string& sep, const std::vector<std::string>& parts)
    {
        return StringUtil::Join(sep, parts);
    }

    void CcsReading::MapConfig(const StoredList& compiler, const StoredList& linker, const CcsOptionDefs& defs, CcsConfig& out)
    {
        const CcsDefinition& cd = ToolchainDefinitions::Current()->Ccs;
        bool release = !out.IsDebug;
        for (const Stored& o : compiler)
        {
            const std::string& t = o.Tail;
            std::string flag = defs.Spell(t, o.Value, o.Items);
            if (t == "compilerID.OPT_LEVEL" || t == "compilerID.OPT_LEVEL.release")
            {
                std::string level = CcsOptionDefs::EnumSuffix("", o.Value);
                if (level == "off" || level == "0") out.Opt = "-O0";
                else if (level == "1") out.Opt = "-O1";
                else if (level == "2" || level == "3") out.Opt = "-O2";
                else { out.Unsupported.push_back(flag); continue; }
                out.Mapped.push_back(flag + " -> " + out.Opt + (level == "3" ? " (cpp11 and c90 have no -O3)" : ""));
            }
            else if (t == "compilerID.DEBUGGING_MODEL")
            {
                out.Debug = CcsOptionDefs::EnumSuffix("", o.Value) == "SYMDEBUG__DWARF";
                out.DebugSaid = true;
                out.Mapped.push_back(flag + " -> " + (out.Debug ? "-g" : "no -g"));
            }
            else if (t == "compilerID.DEFINE")
            {
                for (const std::string& i : o.Items) out.Defines.push_back(Unquoted(i));
                out.Mapped.push_back(flag + " -> -D" + JoinWith(" -D", out.Defines));
            }
            else if (t == "compilerID.UNDEFINE")
            {
                for (const std::string& i : o.Items) out.Undefines.push_back(Unquoted(i));
                out.Mapped.push_back(flag + " -> -U" + JoinWith(" -U", out.Undefines));
            }
            else if (t == "compilerID.INCLUDE_PATH")
            {
                std::vector<std::string> dropped;
                for (const std::string& i : o.Items)
                {
                    std::string raw = Unquoted(i);
                    if (StringUtil::StartsWith(raw, CgToolRootMacro)) { dropped.push_back(raw); continue; }
                    std::string d = CcsWorkspace::WithSlashes(ResolveMacros(raw));
                    if (!Rooted(d)) d = Dir + "/" + d;
                    AddUnique(out.Includes, d);
                }
                out.Mapped.push_back(flag + " -> -I" + JoinWith(" -I", out.Includes)
                    + (dropped.empty() ? std::string() : " (" + JoinWith(", ", dropped) + " is the compiler's own: RIDE's headers instead)"));
            }
            else if (t == "compilerID.NO_COMPRESS")
            {
                out.NoCompress = o.Value == "true";
                out.Mapped.push_back(flag + " -> " + (out.NoCompress ? "--no_compress" : "compact instructions kept"));
            }
            else if (t == "compilerID.SILICON_VERSION" || t == "compilerID.ABI" || t == "compilerID.EXCEPTIONS" || t == "compilerID.RTTI"
                     || StringUtil::StartsWith(t, "compilerID.DIAG_") || t == "compilerID.DISPLAY_ERROR_NUMBER"
                     || StringUtil::StartsWith(t, "compilerID.PREPROC_") || t == "compilerID.OBJ_DIRECTORY"
                     || t == "compilerID.OBJ_EXTENSION" || t == "compilerID.ASM_DIRECTORY" || t == "compilerID.TEMP_DIRECTORY"
                     || t == "compilerID.OUTPUT_FILE" || t == "compilerID.LIST_DIRECTORY")
                out.OwnInstead.push_back(flag);
            else out.Unsupported.push_back(flag);
        }
        if (FindStored(compiler, "compilerID.OPT_LEVEL") == nullptr && FindStored(compiler, "compilerID.OPT_LEVEL.release") == nullptr)
        {
            std::string level = defs.DefaultOf(release ? "compilerID.OPT_LEVEL.release" : "compilerID.OPT_LEVEL");
            if (level == "off" || level == "0") out.Opt = "-O0";
            else if (level == "1") out.Opt = "-O1";
            else if (level == "2" || level == "3") out.Opt = "-O2";
            out.Mapped.push_back(level.empty() ? "optimization not set in CCS -> RIDE's default"
                                               : "-O" + level + " (CCS's default for " + out.Name + ") -> " + out.Opt);
        }

        out.Link.Given = true;
        for (const Stored& o : linker)
        {
            const std::string& t = o.Tail;
            std::string flag = defs.Spell(t, o.Value, o.Items);
            if (t == "linkerID.HEAP_SIZE") { out.Link.Heap = Unquoted(o.Value); out.Mapped.push_back(flag + " -> lnk6x " + flag); }
            else if (t == "linkerID.STACK_SIZE") { out.Link.Stack = Unquoted(o.Value); out.Mapped.push_back(flag + " -> lnk6x " + flag); }
            else if (t == "linkerID.SEARCH_PATH")
            {
                std::vector<std::string> dropped;
                for (const std::string& i : o.Items)
                {
                    std::string raw = Unquoted(i);
                    if (StringUtil::StartsWith(raw, CgToolRootMacro)) { dropped.push_back(raw); continue; }
                    std::string d = CcsWorkspace::WithSlashes(ResolveMacros(raw));
                    if (!Rooted(d)) d = Dir + "/" + d;
                    AddUnique(out.Link.SearchPaths, d);
                }
                out.Mapped.push_back(flag + " -> -i " + JoinWith(" -i ", out.Link.SearchPaths)
                    + (dropped.empty() ? std::string() : " (" + JoinWith(", ", dropped) + " is the compiler's own: RIDE's runtime directory instead)"));
            }
            else if (t == "linkerID.LIBRARY")
            {
                std::vector<std::string> said;
                for (const std::string& i : o.Items)
                {
                    std::string lib = Unquoted(i);
                    if (lib == cd.RuntimeIndex)
                    {
                        said.push_back(lib + " -> " + cd.RuntimeLibrary + " (TI's index, which LNK6x does not read)");
                        lib = cd.RuntimeLibrary;
                    }
                    else said.push_back(lib);
                    AddUnique(out.Link.Libraries, lib);
                }
                out.Mapped.push_back(flag + " -> -l " + JoinWith(", -l ", said));
            }
            else if (t == "linkerID.INITIALIZATION_MODEL")
            {
                out.Link.RomModel = CcsOptionDefs::EnumSuffix("", o.Value) == "RAM_MODEL" ? 0 : 1;
                out.Mapped.push_back(flag + " -> lnk6x " + flag);
            }
            else if (t == "linkerID.Z" || t == "linkerID.MAP_FILE" || t == "linkerID.OUTPUT_FILE" || t == "linkerID.XML_LINK_INFO"
                     || StringUtil::StartsWith(t, "linkerID.DIAG_") || t == "linkerID.DISPLAY_ERROR_NUMBER"
                     || t == "linkerID.REREAD_LIBS" || t == "linkerID.WARN_SECTIONS")
                out.OwnInstead.push_back(flag);
            else out.Unsupported.push_back(flag);
        }
        if (FindStored(linker, "linkerID.INITIALIZATION_MODEL") == nullptr)
        {
            std::string model = defs.DefaultOf("linkerID.INITIALIZATION_MODEL");
            if (model == "RAM_MODEL") { out.Link.RomModel = 0; out.Mapped.push_back("--ram_model (CCS's default) -> lnk6x --ram_model"); }
            else if (model == "ROM_MODEL") { out.Link.RomModel = 1; out.Mapped.push_back("--rom_model (CCS's default) -> lnk6x --rom_model"); }
        }
    }

    void CcsReading::PerFileOptions(const XmlNodePtr& fileInfo, const StoredList& folder, const CcsOptionDefs& defs, CcsConfig& out)
    {
        std::vector<std::string> differ;
        for (const XmlNodePtr& tool : fileInfo->SelectNodes("tool"))
            for (const Stored& own : OptionsOf(tool))
            {
                const Stored* base = FindStored(folder, own.Tail);
                if (base != nullptr && base->Value == own.Value && SameList(base->Items, own.Items)) continue;
                differ.push_back(defs.Spell(own.Tail, own.Value, own.Items));
            }
        if (!differ.empty())
            out.Unsupported.push_back(Attr(fileInfo, "resourcePath") + ": " + JoinWith(", ", differ) + " (per-file options)");
    }

    CcsReadingPtr CcsReading::Read(const std::string& where, const std::string& ccsRootSetting,
                                   const CcsWorkspacePtr& workspace)
    {
        CcsReadingPtr out = std::make_shared<CcsReading>();
        std::string dir = CcsWorkspace::WithSlashes(Paths::FullPath(PathUtil::Normalize(where)));
        if (Paths::IsFile(dir) && IsProjectFile(dir)) dir = CcsWorkspace::WithSlashes(Parent(dir));
        dir = StringUtil::TrimEnd(dir, '/');
        if (!IsProject(dir)) throw std::runtime_error(dir + " is not a CCS project (no .project and .ccsproject)");
        out->Dir = dir;
        if (workspace)
        {
            out->Workspace = workspace->Dir;
            for (std::map<std::string, std::string>::const_iterator it = workspace->PathVariables.begin();
                 it != workspace->PathVariables.end(); ++it)
                out->PathVariables[it->first] = it->second;
            for (const CcsWorkspaceMember& m : workspace->Projects) out->Projects[m.Name] = m.Location;
        }
        XmlDocument project = LoadXml(Paths::Combine(dir, ".project"));
        XmlDocument ccsproject = LoadXml(Paths::Combine(dir, ".ccsproject"));
        std::string cprojectFile = Paths::Combine(dir, ".cproject");
        if (!Paths::IsFile(cprojectFile)) throw std::runtime_error(dir + ": a CCS project needs a .cproject beside .project");
        XmlDocument cproject = LoadXml(cprojectFile);
        const CcsDefinition& cd = ToolchainDefinitions::Current()->Ccs;

        XmlNodePtr nameNode = project.SelectSingleNode("/projectDescription/name");
        out->Name = nameNode ? StringUtil::Trim(nameNode->InnerText()) : std::string();
        if (out->Name.empty()) out->Name = Paths::FileName(dir);
        for (const XmlNodePtr& r : project.SelectNodes("/projectDescription/projects/project"))
            if (!StringUtil::Trim(r->InnerText()).empty()) out->References.push_back(StringUtil::Trim(r->InnerText()));
        for (const std::string& r : out->References)
            out->Notes.push_back("depends on project " + r + ", which RIDE does not build - build it in CCS, or open it in RIDE first");

        out->Device = Attr(ccsproject.SelectSingleNode("//deviceVariant"), "value");
        out->Family = Attr(ccsproject.SelectSingleNode("//deviceFamily"), "value");
        out->CgtVersion = Attr(ccsproject.SelectSingleNode("//codegenToolVersion"), "value");
        out->CcsVersion = Attr(ccsproject.SelectSingleNode("//ccsVersion"), "value");
        std::string elf = Attr(ccsproject.SelectSingleNode("//isElfFormat"), "value");
        out->Elf = elf != "false";
        std::string endianness = Attr(ccsproject.SelectSingleNode("//deviceEndianness"), "value");

        // only the C674x: the C6747 is what the toolchain is built for; GRIDEm notes and reads on
        if (out->Family != cd.DeviceFamily || !StringUtil::Contains(out->Device, cd.DeviceVariant))
            out->Refusals.push_back(out->Name + " is for " + (out->Device.empty() ? std::string("an unnamed device") : out->Device)
                + (out->Family.empty() ? std::string() : " (" + out->Family + ")")
                + ", and RIDE builds only C6000 C674x/C6747 projects");
        if (!out->Elf) out->Refusals.push_back(out->Name + " builds COFF, and RIDE's C6000 toolchain makes ELF only");
        if (endianness == "big") out->Refusals.push_back(out->Name + " is big-endian, and RIDE's C6000 toolchain is little-endian only");

        out->CgToolRoot = CgToolRootFor(ccsRootSetting, out->CgtVersion);
        CcsOptionDefs defs;
        std::string why;
        if (!defs.Load(out->CgtVersion, why)) out->Notes.push_back(why + "; options CCS left unstored take RIDE's defaults");
        out->DefinitionsFile = defs.File;

        // .project: the linked resources - a file, or a folder walked like the project's own
        std::vector<std::pair<std::string, std::string> > linkedFiles;   // project-relative name, absolute
        for (const XmlNodePtr& link : project.SelectNodes("//link"))
        {
            XmlNodePtr linkName = link->SelectSingleNode("name");
            XmlNodePtr type = link->SelectSingleNode("type");
            std::string location = out->LocationOf(link);
            if (!linkName || location.empty()) continue;
            std::string ln = StringUtil::Trim(linkName->InnerText());
            if (type && StringUtil::Trim(type->InnerText()) == "2")
            {
                std::vector<std::string> under;
                Walk(location, "", std::vector<std::string>(), under);
                for (const std::string& u : under) linkedFiles.push_back(std::make_pair(ln + "/" + u, location + "/" + u));
            }
            else linkedFiles.push_back(std::make_pair(ln, location));
        }
        for (const std::pair<std::string, std::string>& lf : linkedFiles) out->Linked.push_back(lf.second);

        // .cproject: one cconfiguration per build configuration
        std::vector<std::string> configNames;
        for (const XmlNodePtr& cc : cproject.SelectNodes("//cconfiguration"))
        {
            XmlNodePtr configuration = cc->SelectSingleNode(".//configuration");
            if (!configuration) continue;
            CcsConfigPtr config = std::make_shared<CcsConfig>();
            config->Name = Attr(configuration, "name");
            configNames.push_back(config->Name);
            std::string parent = Attr(configuration, "parent");
            if (StringUtil::EndsWith(parent, ".Debug") || (parent.empty() && config->Name == "Debug")) config->IsDebug = true;
            else if (StringUtil::EndsWith(parent, ".Release") || (parent.empty() && config->Name == "Release")) config->IsDebug = false;
            else { out->Notes.push_back("configuration " + config->Name + " is neither Debug nor Release, and RIDE builds those two"); continue; }
            if (out->Config(config->IsDebug))
            {
                out->Notes.push_back("configuration " + config->Name + " is a second " + (config->IsDebug ? "Debug" : "Release")
                                     + " one, and the first is taken");
                continue;
            }

            for (const XmlNodePtr& m : cc->SelectNodes(".//stringMacro"))
                if (out->Macros.find(Attr(m, "name")) == out->Macros.end()) out->Macros[Attr(m, "name")] = Attr(m, "value");
            if (workspace)
                for (std::map<std::string, std::string>::const_iterator it = workspace->Macros.begin(); it != workspace->Macros.end(); ++it)
                    if (out->Macros.find(it->first) == out->Macros.end()) out->Macros[it->first] = it->second;
            config->Prebuild = out->ResolveMacros(Attr(configuration, "prebuildStep"));
            config->Postbuild = out->ResolveMacros(Attr(configuration, "postbuildStep"));

            XmlNodePtr toolChain = configuration->SelectSingleNode(".//toolChain");
            if (!toolChain) { out->Notes.push_back("configuration " + config->Name + " has no toolChain"); continue; }
            StoredList chain = OptionsOf(toolChain);
            const Stored* tags = FindStored(chain, "core.OPT_TAGS");
            if (tags != nullptr)
                for (const std::string& tag : tags->Items)
                {
                    if (tag == "OUTPUT_FORMAT=COFF")
                        out->Refusals.push_back(out->Name + " (" + config->Name + ") builds COFF, and RIDE's C6000 toolchain makes ELF only");
                    else if (tag == "DEVICE_ENDIANNESS=big")
                        out->Refusals.push_back(out->Name + " (" + config->Name + ") is big-endian, and RIDE's C6000 toolchain is little-endian only");
                    else if (StringUtil::StartsWith(tag, "OUTPUT_TYPE=") && tag != "OUTPUT_TYPE=executable")
                        out->Notes.push_back(config->Name + " builds " + tag.substr(12) + ", and RIDE builds an executable of it");
                }
            XmlNodePtr compilerTool = ToolNamed(toolChain, ".compiler");
            XmlNodePtr linkerTool = ToolNamed(toolChain, ".linker");
            StoredList compiler = compilerTool ? OptionsOf(compilerTool) : StoredList();
            StoredList linker = linkerTool ? OptionsOf(linkerTool) : StoredList();
            const Stored* silicon = FindStored(compiler, "compilerID.SILICON_VERSION");
            if (silicon != nullptr)
            {
                // CCS stores the core as a string; a project written with it enumerated means its suffix
                std::string core = StringUtil::StartsWith(silicon->Value, "com.ti.") ? CcsOptionDefs::EnumSuffix("", silicon->Value) : silicon->Value;
                if (core != cd.SiliconVersion)
                    out->Refusals.push_back(out->Name + " (" + config->Name + ") is built for -mv" + core
                                            + ", and RIDE's C6000 toolchain is the C674x's (-mv" + cd.SiliconVersion + ")");
            }
            const Stored* abi = FindStored(compiler, "compilerID.ABI");
            if (abi != nullptr && CcsOptionDefs::EnumSuffix("", abi->Value) == "coffabi")
                out->Refusals.push_back(out->Name + " (" + config->Name + ") uses the COFF ABI, and RIDE's C6000 toolchain makes ELF only");
            const Stored* big = FindStored(compiler, "compilerID.BIG_ENDIAN");
            if (big != nullptr && big->Value == "true")
                out->Refusals.push_back(out->Name + " (" + config->Name + ") is big-endian, and RIDE's C6000 toolchain is little-endian only");

            out->MapConfig(compiler, linker, defs, *config);
            for (const XmlNodePtr& fi : configuration->SelectNodes(".//fileInfo")) PerFileOptions(fi, compiler, defs, *config);
            for (const XmlNodePtr& entry : configuration->SelectNodes(".//sourceEntries/entry"))
                for (const std::string& n : StringUtil::SplitNonEmpty(Attr(entry, "excluding"), "|"))
                    config->Excluded.push_back(CcsWorkspace::WithSlashes(n));
            out->Configs.push_back(config);
        }
        if (out->Configs.empty()) out->Refusals.push_back(out->Name + ": no Debug or Release configuration in .cproject");
        std::stable_sort(out->Configs.begin(), out->Configs.end(),
                         [](const CcsConfigPtr& a, const CcsConfigPtr& b) { return a->IsDebug && !b->IsDebug; });

        // the sources: every file in the folder by extension, plus the linked ones, minus each configuration's exclusions
        std::vector<std::string> under;
        Walk(dir, "", configNames, under);
        std::vector<std::pair<std::string, std::string> > candidates;
        for (const std::string& u : under) candidates.push_back(std::make_pair(u, dir + "/" + u));
        for (const std::pair<std::string, std::string>& lf : linkedFiles) candidates.push_back(lf);
        for (size_t i = 0; i < candidates.size(); i++)
        {
            const std::string& rel = candidates[i].first;
            const std::string& abs = candidates[i].second;
            int kind = KindOf(rel);
            if (kind == KindNotBuilt) { AddUnique(out->NotBuilt, rel); continue; }
            if (kind == KindHeader) { if (i < under.size()) AddUnique(out->Headers, rel); continue; }
            if (kind == KindOther) continue;
            for (const CcsConfigPtr& config : out->Configs)
            {
                if (ExcludedBy(rel, config->Excluded)) continue;
                if (kind == KindCommand) AddUnique(config->Link.CmdFiles, abs);
                else AddUnique(config->Sources, abs);
            }
        }
        return out;
    }

    std::string CcsReading::Report(bool debug) const
    {
        CcsConfigPtr config = Config(debug);
        std::string head = "CCS project " + Name;
        if (!config) return head + ": no " + (debug ? "Debug" : "Release") + " configuration in it";
        head += " (" + config->Name + "): ";
        std::string said = config->Unsupported.empty() ? std::string("no unsupported options")
            : std::to_string(config->Unsupported.size()) + " " + (config->Unsupported.size() == 1 ? "option" : "options")
              + " not supported, using RIDE's defaults: " + JoinWith(", ", config->Unsupported);
        if (!config->OwnInstead.empty()) said += "; RIDE's own instead of: " + JoinWith(", ", config->OwnInstead);
        return head + said;
    }

    std::string CcsReading::SourceReport() const
    {
        std::vector<std::string> gone;
        for (const std::string& l : Linked)
            if (!Paths::IsFile(l)) gone.push_back(l);
        std::string said;
        if (!gone.empty()) said += std::string("linked ") + (gone.size() == 1 ? "file" : "files") + " not on this machine: " + JoinWith(", ", gone);
        if (!NotBuilt.empty()) said += (said.empty() ? std::string() : "; ") + "not built by RIDE: " + JoinWith(", ", NotBuilt);
        for (const std::string& n : Notes) said += (said.empty() ? std::string() : "; ") + n;
        return said.empty() ? said : "CCS project " + Name + ": " + said;
    }

    std::string CcsReading::MappingText(bool debug) const
    {
        CcsConfigPtr config = Config(debug);
        std::string text;
        text += "CCS project " + Name + ", read from " + Dir + "\n";
        text += "device " + Device + ", compiler " + CgtVersion + (CcsVersion.empty() ? std::string(" (CCS 5.5)") : " (CCS " + CcsVersion + ")") + "\n";
        for (const std::string& r : Refusals) text += "RIDE would refuse this project: " + r + "\n";
        if (!config) return text + "no " + (debug ? "Debug" : "Release") + " configuration\n";
        text += "\n" + config->Name + " - what CCS set, and what RIDE builds with:\n";
        for (const std::string& m : config->Mapped) text += "  " + m + "\n";
        if (!config->Link.CmdFiles.empty()) text += "  linker command file: " + JoinWith(", ", config->Link.CmdFiles) + "\n";
        if (!config->Unsupported.empty())
        {
            text += "\nnot supported, RIDE's defaults instead:\n";
            for (const std::string& u : config->Unsupported) text += "  " + u + "\n";
        }
        if (!config->OwnInstead.empty())
        {
            text += "\nRIDE's own instead of:\n";
            for (const std::string& o : config->OwnInstead) text += "  " + o + "\n";
        }
        if (!config->Excluded.empty()) text += "\nexcluded from the build: " + JoinWith(", ", config->Excluded) + "\n";
        text += "\nThese are read from the CCS project and cannot be changed here - edit them in CCS.\n";
        return text;
    }
}}
