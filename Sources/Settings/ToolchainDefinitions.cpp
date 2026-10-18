// =============================================================================
//  ToolchainDefinitions.cpp - see ToolchainDefinitions.h
// =============================================================================
#include "ToolchainDefinitions.h"
#include "../Core/PathUtil.h"
#include "../Platform/Paths.h"
#include <algorithm>

namespace GRIDEm { namespace Settings {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    static std::string ValueOf(const StringMap& m, const std::string& key)
    {
        StringMap::const_iterator it = m.find(key);
        return it == m.end() ? std::string() : it->second;
    }

    static bool Contains(const std::vector<std::string>& list, const std::string& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    // =========================================================================
    //  SessionDefinition
    // =========================================================================
    std::string SessionDefinition::Command(const std::string& id) const { return ValueOf(Commands, id); }

    bool SessionDefinition::Has(const std::string& id) const
    {
        StringMap::const_iterator it = Commands.find(id);
        return it != Commands.end() && !it->second.empty();
    }

    std::string SessionDefinition::ProtocolWord(const std::string& id) const { return ValueOf(Protocol, id); }

    SessionDefinitionPtr SessionDefinition::Clone() const
    {
        return std::make_shared<SessionDefinition>(*this);
    }

    // =========================================================================
    //  ToolDefinition
    // =========================================================================
    ToolDefinition::ToolDefinition(const std::string& id)
        : Id(id), Name(id), HasVersionArgs(false), OutputEncoding("utf-8"), Diagnostics("none") {}

    bool ToolDefinition::TakesOption(const std::string& id) const { return Contains(Options, id); }

    bool ToolDefinition::ProjectSettingFlag(const std::string& name, const std::string& value, std::string& flag) const
    {
        std::map<std::string, StringMapNoCase, CaseInsensitiveLess>::const_iterator m = ProjectSettings.find(name);
        if (name.empty() || m == ProjectSettings.end()) return false;
        std::string v = StringUtil::Trim(value);
        for (StringMapNoCase::const_iterator kv = m->second.begin(); kv != m->second.end(); ++kv)
            if (kv->first != "*" && StringUtil::EqualsIgnoreCase(kv->first, v)) { flag = kv->second; return true; }
        StringMapNoCase::const_iterator any = m->second.find("*");
        if (any == m->second.end()) return false;
        flag = StringUtil::Replace(any->second, "$(Value)", v);
        return true;
    }

    std::vector<std::string> ToolDefinition::AllNames() const
    {
        std::vector<std::string> r;
        if (!StringUtil::IsNullOrWhiteSpace(Exe)) r.push_back(Exe);
        for (const std::string& a : Aliases)
            if (!StringUtil::IsNullOrWhiteSpace(a) && !Contains(r, a)) r.push_back(a);
        return r;
    }

    bool ToolDefinition::TakesArch() const { return !StringUtil::IsNullOrWhiteSpace(ArchFlag); }

    bool ToolDefinition::ServesTarget(const std::string& arch) const { return Targets.empty() || Contains(Targets, arch); }

    bool ToolDefinition::CompilesLanguage(const std::string& languageWord) const { return Contains(Languages, languageWord); }

    OperationDefinitionPtr ToolDefinition::Operation(const std::string& id) const
    {
        std::map<std::string, OperationDefinitionPtr>::const_iterator it = Operations.find(id);
        return it == Operations.end() ? nullptr : it->second;
    }

    std::string ToolDefinition::OperationArgs(const std::string& id) const
    {
        OperationDefinitionPtr op = Operation(id);
        return op ? op->Args : std::string();
    }

    std::string ToolDefinition::ConfigFlagsFor(const std::string& config, const std::string& arch) const
    {
        std::map<std::string, StringMap>::const_iterator by = ConfigFlagsByTarget.find(arch);
        if (!arch.empty() && by != ConfigFlagsByTarget.end())
        {
            StringMap::const_iterator v = by->second.find(config);
            if (v != by->second.end()) return v->second;
        }
        return ValueOf(ConfigFlags, config);
    }

    std::string ToolDefinition::AsmSyntaxFlag(const std::string& key) const { return ValueOf(AsmSyntaxFlags, key); }

    std::string ToolDefinition::LanguageFlag(const std::string& languageWord) const { return ValueOf(LanguageFlags, languageWord); }

    // a name written on Windows ("C:\x\cpp11.exe") reads by its last part on the Mac too
    static std::string Leaf(const std::string& path)
    {
        return Paths::FileName(PathUtil::Normalize(path));
    }

    bool ToolDefinition::HasName(const std::string& leafOrPath) const
    {
        if (leafOrPath.empty()) return false;
        std::string leaf = Leaf(leafOrPath);
        for (const std::string& n : AllNames())
            if (StringUtil::EqualsIgnoreCase(n, leaf)
                || StringUtil::EqualsIgnoreCase(Paths::FileNameWithoutExtension(n), Paths::FileNameWithoutExtension(leaf)))
                return true;
        return false;
    }

    // =========================================================================
    //  LibraryDefinition
    // =========================================================================
    LibraryDefinition::LibraryDefinition(const std::string& id) : Id(id), Name(id), When("always") {}

    std::string LibraryDefinition::FileFor(bool debug) const { return ValueOf(Files, debug ? "Debug" : "Release"); }

    std::string LibraryDefinition::AssemblyDirectoryFor(bool debug) const
    {
        return ValueOf(AssemblyDirectories, debug ? "Debug" : "Release");
    }

    std::string LibraryDefinition::AssemblyDirectoryEnvFor(bool debug) const
    {
        return ValueOf(AssemblyDirectoryEnv, debug ? "Debug" : "Release");
    }

    bool LibraryDefinition::IsAlias(const std::string& lib) const
    {
        if (lib.empty()) return false;
        std::string l = StringUtil::ToLower(Leaf(lib));
        for (const std::string& a : Aliases)
        {
            std::string x = StringUtil::ToLower(a);
            if (StringUtil::EndsWith(x, "*")) { if (StringUtil::StartsWith(l, x.substr(0, x.size() - 1))) return true; }
            else if (x == l) return true;
        }
        for (StringMap::const_iterator kv = Files.begin(); kv != Files.end(); ++kv)
            for (const std::string& f : StringUtil::SplitNonEmpty(kv->second, " "))
                if (StringUtil::ToLower(f) == l) return true;
        return false;
    }

    // =========================================================================
    //  TargetDefinition, LinkerCommandFileDefinition, CompilerOptionDefinition
    // =========================================================================
    TargetDefinition::TargetDefinition(const std::string& arch)
        : Arch(arch), Name(arch), Host(false), Emulated(false), AssemblyExtension(".s"), ObjectExtension(".o"),
          LibraryDirFlag("-L"), LibraryFlag("-l"), Run("none"), Debug("debugger") {}

    std::string TargetDefinition::ToolIdFor(const std::string& role) const { return ValueOf(Roles, role); }

    std::string LinkerCommandFileDefinition::Text() const { return StringUtil::Join("\n", Lines) + "\n"; }

    CompilerOptionDefinition::CompilerOptionDefinition(const std::string& id)
        : Id(id), Label(id), Control("check"), HasDefaultValue(false), HasSilent(false) {}

    bool CompilerOptionDefinition::IsCheck() const  { return StringUtil::EqualsIgnoreCase(Control, "check"); }
    bool CompilerOptionDefinition::IsChoice() const { return StringUtil::EqualsIgnoreCase(Control, "choice"); }
    bool CompilerOptionDefinition::IsText() const   { return StringUtil::EqualsIgnoreCase(Control, "text"); }

    std::string CompilerOptionDefinition::DefaultFor(const std::string& config) const
    {
        StringMap::const_iterator it = Defaults.find(config);
        if (it != Defaults.end()) return it->second;
        if (HasDefaultValue) return DefaultValue;
        if (IsChoice()) return Choices.empty() ? std::string() : Choices[0];
        return IsCheck() ? "0" : std::string();
    }

    std::string CompilerOptionDefinition::SilentChoice() const
    {
        if (HasSilent) return Silent;
        return Choices.empty() ? std::string() : Choices[0];
    }

    std::string CompilerOptionDefinition::ToolWord() const
    {
        size_t dot = Id.find('.');
        return (dot != std::string::npos && dot > 0) ? Id.substr(0, dot) : std::string();
    }

    // =========================================================================
    //  ToolchainDefinitions
    // =========================================================================
    const char* const ToolchainDefinitions::FileName = "toolchain.json";
    const char* const ToolchainDefinitions::UserFileNamePortable = "toolchain.user.json";
    const char* const ToolchainDefinitions::EnvironmentOverride = "GRIDE_TOOLCHAIN";

    ToolchainDefinitionsPtr ToolchainDefinitions::current;

    ToolchainDefinitions::ToolchainDefinitions() : Format(0), ShippedFound(false) {}

    ToolchainDefinitionsPtr ToolchainDefinitions::Current()
    {
        if (!current) current = Load(ShippedPath(), UserPath(false));
        return current;
    }

    void ToolchainDefinitions::SetCurrent(const ToolchainDefinitionsPtr& d) { current = d; }

    std::string ToolchainDefinitions::ExeDirectory() { return Paths::AppDirectory(); }

    std::string ToolchainDefinitions::PortableDirectory()
    {
        std::string dir = ExeDirectory();
        // GRIDEm.app/Contents/MacOS -> the folder that holds GRIDEm.app
        if (Paths::FileName(dir) == "MacOS")
        {
            std::string contents = Paths::DirectoryName(dir);
            std::string app = Paths::DirectoryName(contents);
            if (Paths::FileName(contents) == "Contents" && StringUtil::EndsWithIgnoreCase(app, ".app"))
                return Paths::DirectoryName(app);
        }
        return dir;
    }

    std::string ToolchainDefinitions::ShippedPath()
    {
        std::string env;
        if (Paths::GetEnvironment(EnvironmentOverride, env) && !StringUtil::IsNullOrWhiteSpace(env) && Paths::IsFile(env))
            return Paths::FullPath(env);
        std::string resources = Paths::Combine(Paths::ResourcesDirectory(), FileName);
        if (Paths::IsFile(resources)) return resources;
        std::string dir = ExeDirectory();
        if (dir.empty()) return FileName;
        std::string beside = Paths::Combine(dir, FileName);
        if (Paths::IsFile(beside)) return beside;
        std::string up = dir;
        for (int i = 0; i < 4; i++)
        {
            std::string parent = Paths::DirectoryName(up);
            if (parent.empty() || parent == up) break;
            up = parent;
            const char* under[] = { "", "GRIDE", "Resources" };
            for (const char* u : under)
            {
                std::string candidate = Paths::Combine(Paths::Combine(up, u), FileName);
                if (Paths::IsFile(candidate)) return candidate;
            }
        }
        return Paths::IsFile(resources) || Paths::FileName(dir) == "MacOS" ? resources : beside;
    }

    std::string ToolchainDefinitions::UserPath(bool portable)
    {
        if (portable) return Paths::Combine(PortableDirectory(), UserFileNamePortable);
        return Paths::Combine(Paths::UserSettingsDirectory(), FileName);
    }

    ToolchainDefinitionsPtr ToolchainDefinitions::Load(const std::string& shippedPath, const std::string& userPath)
    {
        ToolchainDefinitionsPtr d = std::make_shared<ToolchainDefinitions>();
        d->ShippedFile = shippedPath;
        d->UserFile = userPath;
        d->ShippedJson = d->ReadFile(d->ShippedFile, std::string("the shipped ") + FileName, true);
        if (!d->ShippedJson) d->ShippedJson = JsonValue::MakeObject();
        d->CheckDocument(d->ShippedJson, d->ShippedLabel(), true);
        if (!d->UserFile.empty() && Paths::IsFile(d->UserFile))
            d->UserJson = d->ReadFile(d->UserFile, std::string("the user's ") + FileName, false);
        if (d->UserJson) d->CheckDocument(d->UserJson, d->UserLabel(), false);
        d->MergedJson = Clone(d->ShippedJson);
        if (d->UserJson) MergeInto(d->MergedJson, d->UserJson);
        d->Build();
        return d;
    }

    ToolchainDefinitionsPtr ToolchainDefinitions::Reload()
    {
        ToolchainDefinitionsPtr was = current;
        current = Load(was ? was->ShippedFile : ShippedPath(), was ? was->UserFile : UserPath(false));
        return current;
    }

    CompilerOptionDefinitionPtr ToolchainDefinitions::CompilerOption(const std::string& id) const
    {
        for (const CompilerOptionDefinitionPtr& o : CompilerOptions) if (o->Id == id) return o;
        return nullptr;
    }

    std::vector<CompilerOptionDefinitionPtr> ToolchainDefinitions::CompilerOptionsOf(const std::string& tab) const
    {
        std::vector<CompilerOptionDefinitionPtr> r;
        for (const CompilerOptionDefinitionPtr& o : CompilerOptions)
            if (StringUtil::EqualsIgnoreCase(o->Tab, tab)) r.push_back(o);
        return r;
    }

    ToolDefinitionPtr ToolchainDefinitions::ToolOfOption(const std::string& id) const
    {
        CompilerOptionDefinitionPtr o = CompilerOption(id);
        ToolDefinitionPtr t = o ? ToolByWord(o->ToolWord()) : nullptr;
        if (t) return t;
        for (const ToolDefinitionPtr& d : Tools) if (d->TakesOption(id)) return d;
        return nullptr;
    }

    TargetDefinitionPtr ToolchainDefinitions::TargetByArch(const std::string& arch) const
    {
        std::string a = StringUtil::Trim(arch);
        if (a.empty()) return nullptr;
        for (const TargetDefinitionPtr& t : Targets) if (StringUtil::EqualsIgnoreCase(t->Arch, a)) return t;
        return nullptr;
    }

    TargetDefinitionPtr ToolchainDefinitions::TargetByIde(const std::string& ideName) const
    {
        if (ideName.empty()) return nullptr;
        for (const TargetDefinitionPtr& t : Targets) if (StringUtil::EqualsIgnoreCase(t->IdeTarget, ideName)) return t;
        return nullptr;
    }

    TargetDefinitionPtr ToolchainDefinitions::TargetOf(Target t) const { return TargetByIde(TargetInfo::Name(t)); }

    TargetDefinitionPtr ToolchainDefinitions::HostTarget() const
    {
        for (const TargetDefinitionPtr& t : Targets) if (t->Host) return t;
        return nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::Tool(const std::string& id) const
    {
        if (id.empty()) return nullptr;
        for (const ToolDefinitionPtr& t : Tools) if (t->Id == id) return t;
        return nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::ToolByRole(const std::string& role) const
    {
        for (const ToolDefinitionPtr& t : Tools) if (t->Role == role) return t;
        return nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::ToolFor(Target target, const std::string& role) const
    {
        TargetDefinitionPtr t = TargetOf(target);
        return t ? Tool(t->ToolIdFor(role)) : nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::ToolForArch(const std::string& arch, const std::string& role) const
    {
        TargetDefinitionPtr t = TargetByArch(arch);
        return t ? Tool(t->ToolIdFor(role)) : nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::ToolByWord(const std::string& word) const
    {
        if (StringUtil::IsNullOrWhiteSpace(word)) return nullptr;
        std::string w = StringUtil::Trim(word);
        for (const ToolDefinitionPtr& t : Tools)
            for (const std::string& x : t->Words) if (StringUtil::EqualsIgnoreCase(x, w)) return t;
        return nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::ToolByName(const std::string& exeOrPath) const
    {
        for (const ToolDefinitionPtr& t : Tools) if (t->HasName(exeOrPath)) return t;
        return nullptr;
    }

    ToolDefinitionPtr ToolchainDefinitions::CompilerOf(const std::string& languageWord) const
    {
        const char* roles[] = { "cCompiler", "cppCompiler", "shalimarCompiler" };
        for (const char* role : roles)
        {
            ToolDefinitionPtr t = ToolByRole(role);
            if (t && t->CompilesLanguage(languageWord)) return t;
        }
        return nullptr;
    }

    LibraryDefinitionPtr ToolchainDefinitions::Library(const std::string& id) const
    {
        if (id.empty()) return nullptr;
        for (const LibraryDefinitionPtr& l : Libraries) if (l->Id == id) return l;
        return nullptr;
    }

    LibraryDefinitionPtr ToolchainDefinitions::LibraryByKind(const std::string& arch, const std::string& kind) const
    {
        for (const LibraryDefinitionPtr& l : Libraries)
            if (l->Kind == kind && (arch.empty() || StringUtil::EqualsIgnoreCase(l->Target, arch))) return l;
        return nullptr;
    }

    LibraryDefinitionPtr ToolchainDefinitions::RuntimeOf(Target target) const
    {
        TargetDefinitionPtr t = TargetOf(target);
        return t ? Library(t->Runtime) : nullptr;
    }

    LinkerCommandFileDefinitionPtr ToolchainDefinitions::LinkerCommandFile(const std::string& id) const
    {
        if (id.empty()) return nullptr;
        for (const LinkerCommandFileDefinitionPtr& f : LinkerCommandFiles) if (f->Id == id) return f;
        return nullptr;
    }

    LinkerCommandFileDefinitionPtr ToolchainDefinitions::LinkerCommandFileOf(Target target) const
    {
        TargetDefinitionPtr t = TargetOf(target);
        return t ? LinkerCommandFile(t->LinkerCommandFile) : nullptr;
    }

    // ---- the user's overrides ------------------------------------------------------
    bool ToolchainDefinitions::IsOverridden(const std::string& section, const std::string& id) const
    {
        if (!UserJson) return false;
        JsonPtr s = UserJson->GetObject(section);
        return s && s->Has(id);
    }

    JsonPtr ToolchainDefinitions::UserEntry(const std::string& section, const std::string& id)
    {
        if (!UserJson) UserJson = JsonValue::MakeObject();
        JsonPtr s = UserJson->GetObject(section);
        if (!s) { s = JsonValue::MakeObject(); UserJson->SetValue(section, s); }
        JsonPtr e = s->GetObject(id);
        if (!e) { e = JsonValue::MakeObject(); s->SetValue(id, e); }
        return e;
    }

    void ToolchainDefinitions::SetUserEntry(const std::string& section, const std::string& id, const JsonPtr& fields)
    {
        if (!UserJson) UserJson = JsonValue::MakeObject();
        JsonPtr s = UserJson->GetObject(section);
        if (!fields || fields->Count() == 0)
        {
            if (s)
            {
                s->Remove(id);
                if (s->Count() == 0) UserJson->Remove(section);
            }
            return;
        }
        if (!s) { s = JsonValue::MakeObject(); UserJson->SetValue(section, s); }
        s->SetValue(id, fields);
    }

    void ToolchainDefinitions::RestoreShipped(const std::string& section, const std::string& id)
    {
        SetUserEntry(section, id, nullptr);
    }

    JsonPtr ToolchainDefinitions::ShippedEntry(const std::string& section, const std::string& id) const
    {
        JsonPtr s = ShippedJson ? ShippedJson->GetObject(section) : nullptr;
        return s ? s->GetObject(id) : nullptr;
    }

    JsonPtr ToolchainDefinitions::MergedEntry(const std::string& section, const std::string& id) const
    {
        JsonPtr s = MergedJson ? MergedJson->GetObject(section) : nullptr;
        return s ? s->GetObject(id) : nullptr;
    }

    bool ToolchainDefinitions::SaveUser(std::string& error)
    {
        error.clear();
        try
        {
            if (UserFile.empty()) { error = "no user file is named"; return false; }
            if (!UserJson || UserJson->Count() == 0)
            {
                if (Paths::IsFile(UserFile)) Paths::DeleteFile(UserFile);
            }
            else
            {
                PathUtil::EnsureDirectory(Paths::DirectoryName(UserFile));
                if (!UserJson->Has("format")) UserJson->SetNumber("format", Format > 0 ? Format : 1);
                if (!UserJson->Has("description"))
                    UserJson->SetString("description", std::string("the user's overrides of the shipped ") + FileName
                                        + ": a field named here wins, every other field keeps the shipped value");
                JsonWriter::WriteFile(UserFile, UserJson);
            }
            return true;
        }
        catch (const std::exception& ex) { error = ex.what(); return false; }
    }

    std::vector<std::string> ToolchainDefinitions::Roles()
    {
        const char* r[] = { "cCompiler", "cppCompiler", "shalimarCompiler", "hostCompiler", "hostCxx", "assembler", "linker",
                            "debugAssembler", "debugLinker", "nativeAssembler", "nativeLinker", "simulator", "emulator", "debugger",
                            "converter" };
        return std::vector<std::string>(r, r + sizeof r / sizeof r[0]);
    }

    JsonPtr ToolchainDefinitions::Clone(const JsonPtr& v)
    {
        return v ? v->Clone() : nullptr;
    }

    void ToolchainDefinitions::MergeInto(const JsonPtr& into, const JsonPtr& from)
    {
        if (!into || !from || !into->IsObject() || !from->IsObject()) return;
        for (const std::string& k : from->KeyNames())
        {
            JsonPtr theirs = from->Get(k);
            JsonPtr mine = into->Get(k);
            if (theirs && theirs->IsObject() && mine && mine->IsObject()) MergeInto(mine, theirs);
            else into->SetValue(k, Clone(theirs));
        }
    }

    // ---- reading ---------------------------------------------------------------------
    std::string ToolchainDefinitions::ShippedLabel() const { return std::string(FileName) + " (shipped, " + ShippedFile + ")"; }
    std::string ToolchainDefinitions::UserLabel() const { return std::string(FileName) + " (user, " + UserFile + ")"; }

    JsonPtr ToolchainDefinitions::ReadFile(const std::string& path, const std::string& what, bool shipped)
    {
        if (path.empty() || !Paths::IsFile(path))
        {
            if (shipped)
                Problems.push_back(what + " was not found - looked for " + (path.empty() ? std::string(FileName) : path)
                                   + " (inside GRIDEm.app, or named by " + EnvironmentOverride
                                   + "); no tool is defined until it is there");
            return nullptr;
        }
        try
        {
            JsonPtr v = JsonParser::ParseFile(path);
            if (!v || !v->IsObject())
            {
                Problems.push_back(what + " (" + path + ") does not hold a JSON object - "
                                   + (shipped ? "no tool is defined" : "the shipped file is used alone"));
                return nullptr;
            }
            if (shipped) ShippedFound = true;
            return v;
        }
        catch (const std::exception& ex)
        {
            Problems.push_back(what + " (" + path + ") cannot be read: " + ex.what() + " - "
                               + (shipped ? "no tool is defined until it is fixed" : "the shipped file is used alone"));
            return nullptr;
        }
    }

    // ---- validation: a field of the wrong kind is dropped with a message ----------------
    static std::string KindName(JsonKind k)
    {
        switch (k)
        {
        case JsonKind::String: return "a string";
        case JsonKind::Number: return "a number";
        case JsonKind::Bool:   return "true or false";
        case JsonKind::Array:  return "a list";
        case JsonKind::Object: return "an object";
        default:               return "null";
        }
    }

    static bool Expect(const JsonPtr& obj, const std::string& key, JsonKind kind, const std::string& where,
                       const std::string& file, std::vector<std::string>& problems)
    {
        JsonPtr v = obj->Get(key);
        if (!v || v->Kind() == JsonKind::Null) return true;
        if (v->Kind() == kind) return true;
        problems.push_back(file + ": " + where + ": field '" + key + "' must be " + KindName(kind) + " - it is "
                           + KindName(v->Kind()) + " and is ignored (the other file's value, or the default, stands)");
        obj->Remove(key);
        return false;
    }

    static void ExpectAll(const JsonPtr& obj, const std::vector<std::string>& keys, JsonKind kind, const std::string& where,
                          const std::string& file, std::vector<std::string>& problems)
    {
        for (const std::string& k : keys) Expect(obj, k, kind, where, file, problems);
    }

    static void ExpectStringMap(const JsonPtr& obj, const std::string& key, const std::string& where, const std::string& file,
                                std::vector<std::string>& problems)
    {
        if (!Expect(obj, key, JsonKind::Object, where, file, problems)) return;
        JsonPtr m = obj->GetObject(key);
        if (!m) return;
        for (const std::string& k : m->KeyNames())
            if (k != "description") Expect(m, k, JsonKind::String, where + " > " + key, file, problems);
    }

    static std::vector<std::string> Words(const char* const* list, size_t n)
    {
        return std::vector<std::string>(list, list + n);
    }

    void ToolchainDefinitions::CheckDocument(const JsonPtr& doc, const std::string& file, bool shipped)
    {
        std::vector<std::string>& P = Problems;
        Expect(doc, "format", JsonKind::Number, "the document", file, P);
        Expect(doc, "description", JsonKind::String, "the document", file, P);
        static const char* const sections[] = { "installation", "visualStudio", "ccs", "compilerOptions", "placeholders",
                                                "targets", "tools", "libraries", "linkerCommandFiles" };
        ExpectAll(doc, Words(sections, 9), JsonKind::Object, "the document", file, P);

        JsonPtr co = doc->GetObject("compilerOptions");
        if (co)
        {
            Expect(co, "description", JsonKind::String, "compilerOptions", file, P);
            Expect(co, "tabs", JsonKind::Array, "compilerOptions", file, P);
            if (Expect(co, "options", JsonKind::Object, "compilerOptions", file, P) && co->GetObject("options"))
            {
                JsonPtr opts = co->GetObject("options");
                for (const std::string& id : opts->KeyNames())
                {
                    if (id == "description") continue;
                    std::string where = "compiler option '" + id + "'";
                    if (!Expect(opts, id, JsonKind::Object, "compilerOptions > options", file, P)) continue;
                    JsonPtr o = opts->GetObject(id);
                    static const char* const strs[] = { "tab", "label", "hint", "control", "flagOn", "flagOff", "flag", "silent",
                                                        "flagEach", "environment", "needs", "unavailable" };
                    ExpectAll(o, Words(strs, 12), JsonKind::String, where, file, P);
                    Expect(o, "choices", JsonKind::Array, where, file, P);
                    JsonPtr def = o->Get("default");
                    if (def && def->Kind() != JsonKind::Null && def->Kind() != JsonKind::String && def->Kind() != JsonKind::Object)
                    {
                        P.push_back(file + ": " + where + ": field 'default' must be a string or an object - it is "
                                    + KindName(def->Kind()) + " and is ignored (the other file's value, or the default, stands)");
                        o->Remove("default");
                    }
                    else if (def && def->Kind() == JsonKind::Object) ExpectStringMap(o, "default", where, file, P);
                    if (shipped && !o->Has("control"))
                        P.push_back(file + ": " + where + ": field 'control' is missing - the option is taken as a check box");
                }
            }
        }
        JsonPtr ccs = doc->GetObject("ccs");
        if (ccs)
        {
            static const char* const strs[] = { "description", "definitionsDir", "deviceFamily", "deviceVariant", "siliconVersion" };
            ExpectAll(ccs, Words(strs, 5), JsonKind::String, "ccs", file, P);
            static const char* const maps[] = { "definitions", "compilerDirs", "tiRuntime" };
            for (const char* k : maps) ExpectStringMap(ccs, k, "ccs", file, P);
            if (Expect(ccs, "tiCompiler", JsonKind::Object, "ccs", file, P) && ccs->GetObject("tiCompiler"))
            {
                JsonPtr ti = ccs->GetObject("tiCompiler");
                static const char* const lists[] = { "roots", "under" };
                ExpectAll(ti, Words(lists, 2), JsonKind::Array, "ccs > tiCompiler", file, P);
                static const char* const tstrs[] = { "bin", "lib", "description" };
                ExpectAll(ti, Words(tstrs, 3), JsonKind::String, "ccs > tiCompiler", file, P);
            }
        }
        JsonPtr inst = doc->GetObject("installation");
        if (inst)
        {
            Expect(inst, "defaultRoots", JsonKind::Array, "installation", file, P);
            Expect(inst, "markers", JsonKind::Array, "installation", file, P);
            static const char* const strs[] = { "binDir", "includeDir", "libDir", "description" };
            ExpectAll(inst, Words(strs, 4), JsonKind::String, "installation", file, P);
        }
        JsonPtr vs = doc->GetObject("visualStudio");
        if (vs)
        {
            static const char* const strs[] = { "description", "vswhere", "vswhereArgs", "vcvars", "msvcTools", "msvcVersion",
                                                "hostBin", "lib", "include", "llvmBin" };
            ExpectAll(vs, Words(strs, 10), JsonKind::String, "visualStudio", file, P);
            static const char* const lists[] = { "defaultRoots", "editions" };
            ExpectAll(vs, Words(lists, 2), JsonKind::Array, "visualStudio", file, P);
            ExpectStringMap(vs, "environment", "visualStudio", file, P);
            if (Expect(vs, "windowsKits", JsonKind::Object, "visualStudio", file, P) && vs->GetObject("windowsKits"))
            {
                JsonPtr k = vs->GetObject("windowsKits");
                static const char* const kstrs[] = { "registryKey", "registryValue", "versionsUnder", "binDir" };
                ExpectAll(k, Words(kstrs, 4), JsonKind::String, "visualStudio > windowsKits", file, P);
                static const char* const klists[] = { "roots", "libDirs", "includeDirs" };
                ExpectAll(k, Words(klists, 3), JsonKind::Array, "visualStudio > windowsKits", file, P);
            }
        }
        JsonPtr targets = doc->GetObject("targets");
        if (targets)
            for (const std::string& id : targets->KeyNames())
            {
                if (id == "description") continue;
                std::string where = "target '" + id + "'";
                if (!Expect(targets, id, JsonKind::Object, "targets", file, P)) continue;
                JsonPtr t = targets->GetObject(id);
                static const char* const strs[] = { "ideTarget", "name", "description", "debugInfo", "assemblyExtension",
                                                    "objectExtension", "programExtension", "emulatedProgramExtension",
                                                    "libraryDirFlag", "libraryFlag", "runtime", "linkerCommandFile", "run",
                                                    "debug", "shownRun", "shownEmulate" };
                ExpectAll(t, Words(strs, 16), JsonKind::String, where, file, P);
                static const char* const bools[] = { "host", "emulated" };
                ExpectAll(t, Words(bools, 2), JsonKind::Bool, where, file, P);
                ExpectStringMap(t, "roles", where, file, P);
                if (shipped && !t->Has("ideTarget"))
                    P.push_back(file + ": " + where + ": field 'ideTarget' is missing - the target cannot be matched to the IDE's and is left out");
            }
        JsonPtr tools = doc->GetObject("tools");
        if (tools)
            for (const std::string& id : tools->KeyNames())
            {
                if (id == "description") continue;
                std::string where = "tool '" + id + "'";
                if (!Expect(tools, id, JsonKind::Object, "tools", file, P)) continue;
                JsonPtr t = tools->GetObject(id);
                static const char* const strs[] = { "name", "description", "role", "exe", "outputEncoding", "diagnostics",
                                                    "archFlag", "includeFlag", "defineFlag" };
                ExpectAll(t, Words(strs, 9), JsonKind::String, where, file, P);
                static const char* const lists[] = { "languages", "targets", "aliases", "words", "installIncludeDirs", "options" };
                ExpectAll(t, Words(lists, 6), JsonKind::Array, where, file, P);
                if (Expect(t, "lookup", JsonKind::Object, where, file, P) && t->GetObject("lookup"))
                {
                    JsonPtr l = t->GetObject("lookup");
                    static const char* const lstrs[] = { "env", "installDir", "description" };
                    ExpectAll(l, Words(lstrs, 3), JsonKind::String, where + " > lookup", file, P);
                    static const char* const lbools[] = { "path", "vcvars" };
                    ExpectAll(l, Words(lbools, 2), JsonKind::Bool, where + " > lookup", file, P);
                }
                if (Expect(t, "version", JsonKind::Object, where, file, P) && t->GetObject("version"))
                    Expect(t->GetObject("version"), "args", JsonKind::String, where + " > version", file, P);
                static const char* const maps[] = { "environment", "languageFlags", "configFlags", "asmSyntaxFlags" };
                for (const char* k : maps) ExpectStringMap(t, k, where, file, P);
                JsonPtr sess = t->Get("session");
                if (sess && sess->Kind() != JsonKind::Null && sess->Kind() != JsonKind::Object && sess->Kind() != JsonKind::String)
                {
                    P.push_back(file + ": " + where + ": field 'session' must be an object or a tool id - it is "
                                + KindName(sess->Kind()) + " and is ignored (the other file's value, or the default, stands)");
                    t->Remove("session");
                }
                else if (sess && sess->IsObject())
                {
                    static const char* const sstrs[] = { "description", "dialect", "like", "marker", "markerCommand", "afterReady" };
                    ExpectAll(sess, Words(sstrs, 6), JsonKind::String, where + " > session", file, P);
                    Expect(sess, "console", JsonKind::Bool, where + " > session", file, P);
                    Expect(sess, "preamble", JsonKind::Array, where + " > session", file, P);
                    static const char* const smaps[] = { "commands", "environment", "protocol" };
                    for (const char* k : smaps) ExpectStringMap(sess, k, where + " > session", file, P);
                }
                if (Expect(t, "configFlagsByTarget", JsonKind::Object, where, file, P) && t->GetObject("configFlagsByTarget"))
                {
                    JsonPtr by = t->GetObject("configFlagsByTarget");
                    for (const std::string& arch : by->KeyNames()) ExpectStringMap(by, arch, where + " > configFlagsByTarget", file, P);
                }
                if (Expect(t, "projectSettings", JsonKind::Object, where, file, P) && t->GetObject("projectSettings"))
                {
                    JsonPtr ps = t->GetObject("projectSettings");
                    for (const std::string& name : ps->KeyNames())
                        if (name != "description") ExpectStringMap(ps, name, where + " > projectSettings", file, P);
                }
                if (Expect(t, "operations", JsonKind::Object, where, file, P) && t->GetObject("operations"))
                {
                    JsonPtr ops = t->GetObject("operations");
                    for (const std::string& op : ops->KeyNames())
                    {
                        if (op == "description") continue;
                        std::string w = where + " > operation '" + op + "'";
                        if (!Expect(ops, op, JsonKind::Object, where + " > operations", file, P)) continue;
                        JsonPtr o = ops->GetObject(op);
                        static const char* const ostrs[] = { "description", "args", "shown", "output" };
                        ExpectAll(o, Words(ostrs, 4), JsonKind::String, w, file, P);
                        ExpectStringMap(o, "arguments", w, file, P);
                        if (shipped && !o->Has("args")) P.push_back(file + ": " + w + ": field 'args' is missing - the operation has no arguments");
                    }
                }
                if (shipped)
                {
                    if (!t->Has("exe")) P.push_back(file + ": " + where + ": field 'exe' is missing - the tool has no program name and cannot be found");
                    if (!t->Has("role")) P.push_back(file + ": " + where + ": field 'role' is missing - the tool plays no part");
                }
            }
        JsonPtr libs = doc->GetObject("libraries");
        if (libs)
            for (const std::string& id : libs->KeyNames())
            {
                if (id == "description") continue;
                std::string where = "library '" + id + "'";
                if (!Expect(libs, id, JsonKind::Object, "libraries", file, P)) continue;
                JsonPtr l = libs->GetObject(id);
                static const char* const strs[] = { "name", "description", "kind", "target", "directory", "directoryEnv", "marker", "when" };
                ExpectAll(l, Words(strs, 8), JsonKind::String, where, file, P);
                Expect(l, "aliases", JsonKind::Array, where, file, P);
                static const char* const maps[] = { "files", "assemblyDirectories", "assemblyDirectoryEnv" };
                for (const char* k : maps) ExpectStringMap(l, k, where, file, P);
                if (shipped && !l->Has("files")) P.push_back(file + ": " + where + ": field 'files' is missing - the library names no file");
            }
        JsonPtr cmds = doc->GetObject("linkerCommandFiles");
        if (cmds)
            for (const std::string& id : cmds->KeyNames())
            {
                if (id == "description") continue;
                std::string where = "linker command file '" + id + "'";
                if (!Expect(cmds, id, JsonKind::Object, "linkerCommandFiles", file, P)) continue;
                JsonPtr c = cmds->GetObject(id);
                static const char* const strs[] = { "name", "description" };
                ExpectAll(c, Words(strs, 2), JsonKind::String, where, file, P);
                Expect(c, "lines", JsonKind::Array, where, file, P);
            }
    }

    // ---- the typed model from the merged document ---------------------------------------
    template <typename M>
    static void ReadMap(const JsonPtr& obj, const std::string& key, M& into)
    {
        into.clear();
        JsonPtr m = obj ? obj->GetObject(key) : nullptr;
        if (!m) return;
        for (const std::string& k : m->KeyNames())
            if (k != "description") into[k] = m->GetString(k, std::string());
    }

    static std::vector<std::string> ReadList(const JsonPtr& obj, const std::string& key)
    {
        std::vector<std::string> r;
        if (obj && obj->Has(key))
            for (const std::string& s : obj->GetStringList(key))
                if (!StringUtil::IsNullOrWhiteSpace(s)) r.push_back(StringUtil::Trim(s));
        return r;
    }

    void ToolchainDefinitions::Build()
    {
        JsonPtr doc = MergedJson;
        Format = doc->GetInt("format", 0);
        Description = doc->GetString("description", "");
        JsonPtr inst = doc->GetObject("installation");
        if (inst)
        {
            Installation.Description = inst->GetString("description", "");
            Installation.DefaultRoots = ReadList(inst, "defaultRoots");
            Installation.BinDir = inst->GetString("binDir", "");
            Installation.Markers = ReadList(inst, "markers");
            Installation.IncludeDir = inst->GetString("includeDir", "");
            Installation.LibDir = inst->GetString("libDir", "");
        }
        JsonPtr vs = doc->GetObject("visualStudio");
        if (vs)
        {
            VisualStudio.Description = vs->GetString("description", "");
            VisualStudio.Vswhere = vs->GetString("vswhere", "");
            VisualStudio.VswhereArgs = vs->GetString("vswhereArgs", "");
            VisualStudio.DefaultRoots = ReadList(vs, "defaultRoots");
            VisualStudio.Editions = ReadList(vs, "editions");
            VisualStudio.Vcvars = vs->GetString("vcvars", "");
            VisualStudio.MsvcTools = vs->GetString("msvcTools", "");
            VisualStudio.MsvcVersion = StringUtil::Trim(vs->GetString("msvcVersion", ""));
            VisualStudio.HostBin = vs->GetString("hostBin", "");
            VisualStudio.Lib = vs->GetString("lib", "");
            VisualStudio.Include = vs->GetString("include", "");
            VisualStudio.LlvmBin = vs->GetString("llvmBin", "");
            ReadMap(vs, "environment", VisualStudio.Environment);
            JsonPtr kits = vs->GetObject("windowsKits");
            if (kits)
            {
                VisualStudio.KitRegistryKey = kits->GetString("registryKey", "");
                VisualStudio.KitRegistryValue = kits->GetString("registryValue", "");
                VisualStudio.KitRoots = ReadList(kits, "roots");
                VisualStudio.KitVersionsUnder = kits->GetString("versionsUnder", "");
                VisualStudio.KitLibDirs = ReadList(kits, "libDirs");
                VisualStudio.KitIncludeDirs = ReadList(kits, "includeDirs");
                VisualStudio.KitBinDir = kits->GetString("binDir", "");
            }
        }
        JsonPtr ccs = doc->GetObject("ccs");
        if (ccs)
        {
            Ccs.Description = ccs->GetString("description", "");
            Ccs.DefinitionsDir = ccs->GetString("definitionsDir", "");
            ReadMap(ccs, "definitions", Ccs.Definitions);
            Ccs.DeviceFamily = ccs->GetString("deviceFamily", "");
            Ccs.DeviceVariant = ccs->GetString("deviceVariant", "");
            Ccs.SiliconVersion = ccs->GetString("siliconVersion", "");
            ReadMap(ccs, "compilerDirs", Ccs.CompilerDirs);
            JsonPtr rt = ccs->GetObject("tiRuntime");
            if (rt)
            {
                Ccs.RuntimeIndex = rt->GetString("index", "");
                Ccs.RuntimeLibrary = rt->GetString("library", "");
                Ccs.RuntimeFallback = rt->GetString("fallback", "");
            }
            JsonPtr ti = ccs->GetObject("tiCompiler");
            if (ti)
            {
                Ccs.TiRoots = ReadList(ti, "roots");
                Ccs.TiUnder.clear();
                if (ti->Has("under"))
                    for (const std::string& s : ti->GetStringList("under")) Ccs.TiUnder.push_back(StringUtil::Trim(s));
                Ccs.TiBin = ti->GetString("bin", "");
                Ccs.TiLib = ti->GetString("lib", "");
            }
        }
        JsonPtr co = doc->GetObject("compilerOptions");
        if (co)
        {
            CompilerOptionsDescription = co->GetString("description", "");
            CompilerOptionTabs = ReadList(co, "tabs");
            JsonPtr opts = co->GetObject("options");
            if (opts)
                for (const std::string& id : opts->KeyNames())
                {
                    JsonPtr o = opts->GetObject(id);
                    if (!o || id == "description") continue;
                    CompilerOptionDefinitionPtr d = std::make_shared<CompilerOptionDefinition>(id);
                    d->Tab = o->GetString("tab", "");
                    d->Label = o->GetString("label", id);
                    d->Hint = o->GetString("hint", "");
                    d->Control = o->GetString("control", "check");
                    d->Choices = ReadList(o, "choices");
                    JsonPtr def = o->Get("default");
                    if (def && def->Kind() == JsonKind::String) { d->HasDefaultValue = true; def->AsString(d->DefaultValue); }
                    else if (def && def->IsObject()) ReadMap(o, "default", d->Defaults);
                    d->FlagOn = o->GetString("flagOn", "");
                    d->FlagOff = o->GetString("flagOff", "");
                    d->Flag = o->GetString("flag", "");
                    d->HasSilent = o->Has("silent");
                    d->Silent = o->GetString("silent", "");
                    d->FlagEach = o->GetString("flagEach", "");
                    d->EnvironmentVariable = o->GetString("environment", "");
                    d->Needs = o->GetString("needs", "");
                    d->Unavailable = o->GetString("unavailable", "");
                    if (!Contains(CompilerOptionTabs, d->Tab) && !d->Tab.empty()) CompilerOptionTabs.push_back(d->Tab);
                    CompilerOptions.push_back(d);
                }
        }
        ReadMap(doc, "placeholders", Placeholders);
        JsonPtr targets = doc->GetObject("targets");
        if (targets)
            for (const std::string& arch : targets->KeyNames())
            {
                JsonPtr t = targets->GetObject(arch);
                if (!t || arch == "description") continue;
                TargetDefinitionPtr d = std::make_shared<TargetDefinition>(arch);
                d->IdeTarget = t->GetString("ideTarget", "");
                if (d->IdeTarget.empty()) continue;
                d->Name = t->GetString("name", arch);
                d->Description = t->GetString("description", "");
                d->Host = t->GetBool("host", false);
                d->Emulated = t->GetBool("emulated", false);
                d->DebugInfo = t->GetString("debugInfo", "");
                d->AssemblyExtension = t->GetString("assemblyExtension", d->AssemblyExtension);
                d->ObjectExtension = t->GetString("objectExtension", d->ObjectExtension);
                d->ProgramExtension = t->GetString("programExtension", d->ProgramExtension);
                d->EmulatedProgramExtension = t->GetString("emulatedProgramExtension", d->EmulatedProgramExtension);
                d->LibraryDirFlag = t->GetString("libraryDirFlag", d->LibraryDirFlag);
                d->LibraryFlag = t->GetString("libraryFlag", d->LibraryFlag);
                ReadMap(t, "roles", d->Roles);
                d->Runtime = t->GetString("runtime", "");
                d->LinkerCommandFile = t->GetString("linkerCommandFile", "");
                d->Run = t->GetString("run", d->Run);
                d->Debug = t->GetString("debug", d->Debug);
                d->ShownRun = t->GetString("shownRun", "");
                d->ShownEmulate = t->GetString("shownEmulate", "");
                Targets.push_back(d);
            }
        JsonPtr tools = doc->GetObject("tools");
        if (tools)
            for (const std::string& id : tools->KeyNames())
            {
                JsonPtr t = tools->GetObject(id);
                if (!t || id == "description") continue;
                ToolDefinitionPtr d = std::make_shared<ToolDefinition>(id);
                d->Name = t->GetString("name", id);
                d->Description = t->GetString("description", "");
                d->Role = t->GetString("role", "");
                d->Languages = ReadList(t, "languages");
                d->Targets = ReadList(t, "targets");
                d->Exe = t->GetString("exe", "");
                d->Aliases = ReadList(t, "aliases");
                d->Words = ReadList(t, "words");
                JsonPtr l = t->GetObject("lookup");
                if (l)
                {
                    d->Lookup.Env = l->GetString("env", "");
                    d->Lookup.InstallDir = l->GetString("installDir", "");
                    d->Lookup.Dirs = ReadList(l, "dirs");
                    d->Lookup.OnPath = l->GetBool("path", true);
                    d->Lookup.Vcvars = l->GetBool("vcvars", false);
                    d->Lookup.Description = l->GetString("description", "");
                }
                JsonPtr v = t->GetObject("version");
                d->HasVersionArgs = (bool)v;
                d->VersionArgs = v ? v->GetString("args", "") : std::string();
                d->OutputEncoding = t->GetString("outputEncoding", d->OutputEncoding);
                d->Diagnostics = t->GetString("diagnostics", d->Diagnostics);
                ReadMap(t, "environment", d->Environment);
                d->ArchFlag = t->GetString("archFlag", "");
                d->IncludeFlag = t->GetString("includeFlag", "");
                d->DefineFlag = t->GetString("defineFlag", "");
                d->InstallIncludeDirs = ReadList(t, "installIncludeDirs");
                d->Options = ReadList(t, "options");
                ReadMap(t, "languageFlags", d->LanguageFlags);
                ReadMap(t, "configFlags", d->ConfigFlags);
                ReadMap(t, "asmSyntaxFlags", d->AsmSyntaxFlags);
                JsonPtr by = t->GetObject("configFlagsByTarget");
                if (by)
                    for (const std::string& arch : by->KeyNames())
                    {
                        if (arch == "description" || !by->GetObject(arch)) continue;
                        StringMap m;
                        ReadMap(by, arch, m);
                        d->ConfigFlagsByTarget[arch] = m;
                    }
                JsonPtr ps = t->GetObject("projectSettings");
                if (ps)
                    for (const std::string& name : ps->KeyNames())
                    {
                        if (name == "description" || !ps->GetObject(name)) continue;
                        StringMapNoCase m;
                        ReadMap(ps, name, m);
                        d->ProjectSettings[name] = m;
                        d->ProjectSettingOrder.push_back(name);
                    }
                JsonPtr ops = t->GetObject("operations");
                if (ops)
                    for (const std::string& op : ops->KeyNames())
                    {
                        JsonPtr o = ops->GetObject(op);
                        if (!o || op == "description") continue;
                        OperationDefinitionPtr od = std::make_shared<OperationDefinition>();
                        od->Id = op;
                        od->Description = o->GetString("description", "");
                        od->Args = o->GetString("args", "");
                        od->Shown = o->GetString("shown", "");
                        od->Output = o->GetString("output", "");
                        ReadMap(o, "arguments", od->Arguments);
                        d->Operations[op] = od;
                        d->OperationOrder.push_back(op);
                    }
                JsonPtr sess = t->Get("session");
                if (sess && sess->Kind() == JsonKind::String)
                {
                    d->Session = std::make_shared<SessionDefinition>();
                    sess->AsString(d->Session->Like);
                }
                else if (sess && sess->IsObject())
                {
                    d->Session = std::make_shared<SessionDefinition>();
                    d->Session->Description = sess->GetString("description", "");
                    d->Session->Dialect = sess->GetString("dialect", "");
                    d->Session->Like = sess->GetString("like", "");
                    d->Session->Marker = sess->GetString("marker", "");
                    d->Session->MarkerCommand = sess->GetString("markerCommand", "");
                    d->Session->Console = sess->GetBool("console", false);
                    d->Session->AfterReady = sess->GetString("afterReady", "");
                    d->Session->Preamble = ReadList(sess, "preamble");
                    ReadMap(sess, "commands", d->Session->Commands);
                    ReadMap(sess, "environment", d->Session->Environment);
                    ReadMap(sess, "protocol", d->Session->Protocol);
                }
                Tools.push_back(d);
            }
        // a session that starts from another tool's: that one's fields, this one's over them
        for (const ToolDefinitionPtr& d : Tools)
        {
            if (!d->Session || d->Session->Like.empty()) continue;
            ToolDefinitionPtr from = Tool(d->Session->Like);
            if (!from || !from->Session)
            {
                Problems.push_back(std::string(FileName) + ": tool '" + d->Id + "': session 'like' names a tool '"
                                   + d->Session->Like + "' with no session - the session is its own fields alone");
                continue;
            }
            SessionDefinitionPtr merged = from->Session->Clone();
            SessionDefinitionPtr own = d->Session;
            merged->Like = own->Like;
            if (!own->Description.empty()) merged->Description = own->Description;
            if (!own->Dialect.empty()) merged->Dialect = own->Dialect;
            if (!own->Marker.empty()) merged->Marker = own->Marker;
            if (!own->MarkerCommand.empty()) merged->MarkerCommand = own->MarkerCommand;
            if (own->Console) merged->Console = true;
            if (!own->AfterReady.empty()) merged->AfterReady = own->AfterReady;
            if (!own->Preamble.empty()) merged->Preamble = own->Preamble;
            for (StringMap::const_iterator kv = own->Commands.begin(); kv != own->Commands.end(); ++kv) merged->Commands[kv->first] = kv->second;
            for (StringMap::const_iterator kv = own->Environment.begin(); kv != own->Environment.end(); ++kv) merged->Environment[kv->first] = kv->second;
            for (StringMap::const_iterator kv = own->Protocol.begin(); kv != own->Protocol.end(); ++kv) merged->Protocol[kv->first] = kv->second;
            d->Session = merged;
        }
        JsonPtr libs = doc->GetObject("libraries");
        if (libs)
            for (const std::string& id : libs->KeyNames())
            {
                JsonPtr l = libs->GetObject(id);
                if (!l || id == "description") continue;
                LibraryDefinitionPtr d = std::make_shared<LibraryDefinition>(id);
                d->Name = l->GetString("name", id);
                d->Description = l->GetString("description", "");
                d->Kind = l->GetString("kind", "");
                d->Target = l->GetString("target", "");
                d->Directory = l->GetString("directory", "");
                d->DirectoryEnv = l->GetString("directoryEnv", "");
                d->Marker = l->GetString("marker", "");
                ReadMap(l, "files", d->Files);
                d->Aliases = ReadList(l, "aliases");
                ReadMap(l, "assemblyDirectories", d->AssemblyDirectories);
                ReadMap(l, "assemblyDirectoryEnv", d->AssemblyDirectoryEnv);
                d->When = l->GetString("when", d->When);
                Libraries.push_back(d);
            }
        JsonPtr cmds = doc->GetObject("linkerCommandFiles");
        if (cmds)
            for (const std::string& id : cmds->KeyNames())
            {
                JsonPtr c = cmds->GetObject(id);
                if (!c || id == "description") continue;
                LinkerCommandFileDefinitionPtr d = std::make_shared<LinkerCommandFileDefinition>(id);
                d->Name = c->GetString("name", id);
                d->Description = c->GetString("description", "");
                d->Lines = c->GetStringList("lines");
                LinkerCommandFiles.push_back(d);
            }
        // the targets' roles must name tools that are there
        for (const TargetDefinitionPtr& t : Targets)
            for (StringMap::const_iterator kv = t->Roles.begin(); kv != t->Roles.end(); ++kv)
                if (!kv->second.empty() && !Tool(kv->second))
                    Problems.push_back(std::string(FileName) + ": target '" + t->Arch + "': role '" + kv->first
                                       + "' names a tool '" + kv->second + "' that is not defined - the role is empty");
    }
}}
