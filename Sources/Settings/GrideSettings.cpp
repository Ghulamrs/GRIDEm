// =============================================================================
//  GrideSettings.cpp - see GrideSettings.h
// =============================================================================
#include "GrideSettings.h"
#include <algorithm>
#include <stdexcept>
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include "../Services/RideToolchain.h"
#include "../Services/VisualStudio.h"

namespace GRIDEm { namespace Settings {

    using namespace GRIDEm::Core;
    using namespace GRIDEm::Services;
    using Platform::Paths;

    static std::string OwnHelpDirectory() { return Paths::Combine(Paths::ResourcesDirectory(), "docs"); }

    static void AddAll(std::vector<std::string>& into, const char* const* items, size_t n)
    {
        for (size_t i = 0; i < n; i++) into.push_back(items[i]);
    }

    // =========================================================================
    //  OrderedStringMap
    // =========================================================================
    bool OrderedStringMap::Has(const std::string& key) const
    {
        for (const std::pair<std::string, std::string>& kv : items) if (kv.first == key) return true;
        return false;
    }

    bool OrderedStringMap::Find(const std::string& key, std::string& value) const
    {
        for (const std::pair<std::string, std::string>& kv : items)
            if (kv.first == key) { value = kv.second; return true; }
        return false;
    }

    std::string OrderedStringMap::Get(const std::string& key) const
    {
        std::string v;
        Find(key, v);
        return v;
    }

    void OrderedStringMap::Set(const std::string& key, const std::string& value)
    {
        for (std::pair<std::string, std::string>& kv : items)
            if (kv.first == key) { kv.second = value; return; }
        items.push_back(std::make_pair(key, value));
    }

    void OrderedStringMap::Remove(const std::string& key)
    {
        for (size_t i = 0; i < items.size(); i++)
            if (items[i].first == key) { items.erase(items.begin() + (long)i); return; }
    }

    // =========================================================================
    //  EnvironmentSettings
    // =========================================================================
    EnvironmentSettings::EnvironmentSettings()
        : Theme("Dark"), FontName("Menlo"), FontSize(12.0f), TabSize(4), InsertSpaces(false), LineNumbers(true),
          ShowWhitespace(false), ReopenLastWorkspace(true), SaveBeforeBuild(true), JumpToFirstError(true),
          AskNativeTools(true), RunInIde(true), BuildJobs(0), CcsToolRoot("C:\\ti\\ccsv7\\tools\\compiler\\c6000_7.4.24"),
          DefaultCompiler("auto"), CcsEnabled(true), C6747Debugger("auto"),
          WindowsDebugger("auto"), WindowsRuntime("auto")
    {
        DefaultProjectLocation = Paths::Combine(Paths::HomeDirectory(), "Documents/GRIDE Projects");
        HelpDirectory = OwnHelpDirectory();
    }

    std::string EnvironmentSettings::CcsCompilerRoot() const
    {
        if (!StringUtil::IsNullOrWhiteSpace(CcsRoot)) return StringUtil::Trim(CcsRoot);
        if (!StringUtil::IsNullOrWhiteSpace(TiCompilerDir)) return StringUtil::Trim(TiCompilerDir);
        return CcsToolRoot;
    }

    std::string EnvironmentSettings::EffectiveC6747Debugger() const
    {
        std::string vm, sim;
        if (Paths::GetEnvironment("GRIDE_DEBUG_VM6747", vm) && !StringUtil::IsNullOrWhiteSpace(vm) && StringUtil::Trim(vm) != "0")
            return "vm6747";
        if (Paths::GetEnvironment("GRIDE_DEBUG_SIM6747", sim) && !StringUtil::IsNullOrWhiteSpace(sim) && StringUtil::Trim(sim) != "0")
            return "sim6747";
        return StringUtil::IsNullOrWhiteSpace(C6747Debugger) ? std::string("auto") : StringUtil::ToLower(StringUtil::Trim(C6747Debugger));
    }

    std::string EnvironmentSettings::EffectiveWindowsDebugger() const
    {
        return StringUtil::IsNullOrWhiteSpace(WindowsDebugger) ? std::string("auto") : StringUtil::ToLower(StringUtil::Trim(WindowsDebugger));
    }

    std::string EnvironmentSettings::EffectiveWindowsRuntime() const
    {
        std::string v;
        if (!Paths::GetEnvironment("GRIDE_WINDOWS_RUNTIME", v) || StringUtil::IsNullOrWhiteSpace(v)) v = WindowsRuntime;
        v = StringUtil::IsNullOrWhiteSpace(v) ? std::string("auto") : StringUtil::ToLower(StringUtil::Trim(v));
        return v == "grt" || v == "msvc" ? v : std::string("auto");
    }

    JsonPtr EnvironmentSettings::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetString("theme", Theme);
        o->SetString("font", FontName);
        o->SetNumber("fontSize", FontSize);
        o->SetNumber("tabSize", TabSize);
        o->SetBool("insertSpaces", InsertSpaces);
        o->SetBool("lineNumbers", LineNumbers);
        o->SetBool("showWhitespace", ShowWhitespace);
        o->SetBool("reopenLastWorkspace", ReopenLastWorkspace);
        o->SetBool("saveBeforeBuild", SaveBeforeBuild);
        o->SetBool("jumpToFirstError", JumpToFirstError);
        o->SetBool("askNativeTools", AskNativeTools);
        o->SetBool("runInIde", RunInIde);
        o->SetNumber("buildJobs", BuildJobs);
        o->SetString("defaultProjectLocation", DefaultProjectLocation);
        o->SetString("ccsToolRoot", CcsToolRoot);
        if (!PathUtil::SamePath(HelpDirectory, OwnHelpDirectory())) o->SetString("helpDirectory", HelpDirectory);
        o->SetString("grideRoot", RideRoot);
        o->SetString("grideIncludeDir", RideIncludeDir);
        o->SetString("grideLibDir", RideLibDir);
        o->SetString("vcvars", Vcvars);
        o->SetString("tiCompilerDir", TiCompilerDir);
        o->SetString("tiLibDir", TiLibDir);
        o->SetString("defaultCompiler", DefaultCompiler);
        o->SetList("sharedIncludes", SharedIncludes);
        o->SetList("sharedLibraries", SharedLibraries);
        o->SetBool("ccsEnabled", CcsEnabled);
        o->SetString("ccsRoot", CcsRoot);
        o->SetString("c6747Debugger", C6747Debugger);
        o->SetString("windowsDebugger", WindowsDebugger);
        o->SetString("windowsRuntime", WindowsRuntime);
        JsonPtr c = JsonValue::MakeObject();
        for (const std::pair<std::string, std::string>& kv : SyntaxColors.Items()) c->SetString(kv.first, kv.second);
        o->SetValue("syntaxColors", c);
        return o;
    }

    void EnvironmentSettings::FromJson(const JsonPtr& o)
    {
        if (!o) return;
        Theme = o->GetString("theme", Theme);
        FontName = o->GetString("font", FontName);
        FontSize = (float)o->GetNumber("fontSize", FontSize);
        TabSize = o->GetInt("tabSize", TabSize);
        InsertSpaces = o->GetBool("insertSpaces", InsertSpaces);
        LineNumbers = o->GetBool("lineNumbers", LineNumbers);
        ShowWhitespace = o->GetBool("showWhitespace", ShowWhitespace);
        ReopenLastWorkspace = o->GetBool("reopenLastWorkspace", ReopenLastWorkspace);
        SaveBeforeBuild = o->GetBool("saveBeforeBuild", SaveBeforeBuild);
        JumpToFirstError = o->GetBool("jumpToFirstError", JumpToFirstError);
        AskNativeTools = o->GetBool("askNativeTools", AskNativeTools);
        RunInIde = o->GetBool("runInIde", RunInIde);
        BuildJobs = std::max(0, o->GetInt("buildJobs", BuildJobs));
        DefaultProjectLocation = o->GetString("defaultProjectLocation", DefaultProjectLocation);
        CcsToolRoot = o->GetString("ccsToolRoot", CcsToolRoot);
        std::string help = o->GetString("helpDirectory", HelpDirectory);
        if (!StringUtil::IsNullOrWhiteSpace(help) && Paths::IsDirectory(Paths::ExpandHome(help))) HelpDirectory = help;
        RideRoot = o->GetString("grideRoot", o->GetString("rideRoot", RideRoot));
        RideIncludeDir = o->GetString("grideIncludeDir", o->GetString("rideIncludeDir", RideIncludeDir));
        RideLibDir = o->GetString("grideLibDir", o->GetString("rideLibDir", RideLibDir));
        Vcvars = o->GetString("vcvars", Vcvars);
        TiCompilerDir = o->GetString("tiCompilerDir", TiCompilerDir);
        TiLibDir = o->GetString("tiLibDir", TiLibDir);
        DefaultCompiler = o->GetString("defaultCompiler", DefaultCompiler);
        if (StringUtil::IsNullOrWhiteSpace(DefaultCompiler)) DefaultCompiler = "auto";
        if (o->Has("sharedIncludes")) SharedIncludes = o->GetStringList("sharedIncludes");
        if (o->Has("sharedLibraries")) SharedLibraries = o->GetStringList("sharedLibraries");
        CcsEnabled = o->GetBool("ccsEnabled", CcsEnabled);
        CcsRoot = o->GetString("ccsRoot", CcsRoot);
        C6747Debugger = o->GetString("c6747Debugger", C6747Debugger);
        WindowsDebugger = o->GetString("windowsDebugger", WindowsDebugger);
        WindowsRuntime = o->GetString("windowsRuntime", WindowsRuntime);
        JsonPtr c = o->GetObject("syntaxColors");
        if (c)
            for (const std::string& k : c->KeyNames()) SyntaxColors.Set(k, c->GetString(k, ""));
    }

    // =========================================================================
    //  LanguageSettings
    // =========================================================================
    LanguageSettings::LanguageSettings(Language l) : Lang(l), Preprocessor(true)
    {
        ApplyDefaults();
    }

    std::string LanguageSettings::Name() const { return LanguageInfo::Name(Lang); }

    bool LanguageSettings::OwnsExtension(const std::string& ext) const
    {
        for (const std::string& e : Extensions)
            if (StringUtil::EqualsIgnoreCase(e, ext)) return true;
        return false;
    }

    void LanguageSettings::ApplyDefaults()
    {
        Extensions.clear();
        Keywords.clear();
        Types.clear();
        BlockCommentStart = "/*";
        BlockCommentEnd = "*/";
        LineComment = "//";
        Preprocessor = true;
        static const char* const c90kw[] = { "auto", "break", "case", "const", "continue", "default", "do", "else", "enum",
                                             "extern", "for", "goto", "if", "register", "return", "sizeof", "static", "struct",
                                             "switch", "typedef", "union", "volatile", "while" };
        static const char* const c90ty[] = { "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void" };
        const size_t nkw = sizeof c90kw / sizeof c90kw[0];
        const size_t nty = sizeof c90ty / sizeof c90ty[0];
        switch (Lang)
        {
        case Language::C90:
        {
            static const char* const ext[] = { ".c", ".h" };
            AddAll(Extensions, ext, 2);
            AddAll(Keywords, c90kw, nkw);
            AddAll(Types, c90ty, nty);
            LineComment.clear();          // C90 has no // comments
            break;
        }
        case Language::Cpp11:
        {
            static const char* const ext[] = { ".cpp", ".cc", ".cxx", ".hpp", ".hh" };
            static const char* const kw[] = { "alignas", "alignof", "asm", "catch", "class", "constexpr",
                "const_cast", "decltype", "delete", "dynamic_cast", "explicit", "export", "false", "final",
                "friend", "inline", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override",
                "private", "protected", "public", "reinterpret_cast", "static_assert", "static_cast", "template",
                "this", "thread_local", "throw", "true", "try", "typeid", "typename", "using", "virtual" };
            static const char* const ty[] = { "bool", "wchar_t", "char16_t", "char32_t", "auto" };
            AddAll(Extensions, ext, 5);
            AddAll(Keywords, c90kw, nkw);
            AddAll(Keywords, kw, sizeof kw / sizeof kw[0]);
            AddAll(Types, c90ty, nty);
            AddAll(Types, ty, 5);
            break;
        }
        case Language::Shalimar:
        {
            Extensions.push_back(".shl");
            Extensions.push_back(".shm");          // RIDE's older programs
            AddAll(Keywords, c90kw, nkw);
            AddAll(Types, c90ty, nty);
            break;
        }
        case Language::Assembly:
        {
            static const char* const ext[] = { ".asm", ".s", ".sa", ".inc" };
            static const char* const kw[] = { ".text", ".data", ".bss", ".sect", ".global", ".def", ".ref",
                ".word", ".half", ".byte", ".float", ".space", ".align", ".equ", ".set", ".include", ".end",
                "PROC", "ENDP", "SEGMENT", "ENDS", "END", "DB", "DW", "DD", "DQ", "MACRO", "ENDM", "INCLUDE",
                "PUBLIC", "EXTERN", ".MODEL", ".CODE", ".DATA", ".STACK" };
            AddAll(Extensions, ext, 4);
            AddAll(Keywords, kw, sizeof kw / sizeof kw[0]);
            LineComment = ";";
            BlockCommentStart.clear();
            BlockCommentEnd.clear();
            Preprocessor = false;
            break;
        }
        case Language::LinkerCmd:
        {
            static const char* const ext[] = { ".cmd", ".ld" };
            static const char* const kw[] = { "MEMORY", "SECTIONS", "ORIGIN", "LENGTH", "org", "len",
                "-stack", "-heap", "-l", "-o", "-m", "-c", "-cr", "-x" };
            AddAll(Extensions, ext, 2);
            AddAll(Keywords, kw, sizeof kw / sizeof kw[0]);
            Preprocessor = false;
            break;
        }
        default:
            break;
        }
    }

    JsonPtr LanguageSettings::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetList("ext", Extensions);
        o->SetList("keywords", Keywords);
        o->SetList("types", Types);
        o->SetString("lineComment", LineComment);
        o->SetString("blockCommentStart", BlockCommentStart);
        o->SetString("blockCommentEnd", BlockCommentEnd);
        o->SetBool("preprocessor", Preprocessor);
        return o;
    }

    void LanguageSettings::FromJson(const JsonPtr& o)
    {
        if (!o) return;
        if (o->Has("ext")) Extensions = o->GetStringList("ext");
        // Shalimar is .shl and RIDE's older .shm, both always
        if (Lang == Language::Shalimar)
        {
            const char* const both[] = { ".shl", ".shm" };
            for (const char* e : both)
                if (!OwnsExtension(e)) Extensions.push_back(e);
        }
        if (o->Has("keywords")) Keywords = o->GetStringList("keywords");
        if (o->Has("types")) Types = o->GetStringList("types");
        LineComment = o->GetString("lineComment", LineComment);
        BlockCommentStart = o->GetString("blockCommentStart", BlockCommentStart);
        BlockCommentEnd = o->GetString("blockCommentEnd", BlockCommentEnd);
        Preprocessor = o->GetBool("preprocessor", Preprocessor);
    }

    // =========================================================================
    //  ConverterSettings
    // =========================================================================
    const char* const ConverterSettings::Gride1CToShalimar = "C:\\GRIDE\\bin\\c2shl.exe";
    const char* const ConverterSettings::Gride1ShalimarToC = "C:\\GRIDE\\bin\\shl2c.exe";
    const char* const ConverterSettings::Gride1Template = "\"$(Tool)\" \"$(Input)\" -o \"$(Output)\"";

    ConverterSettings::ConverterSettings() : OpenResult(true) {}

    std::string ConverterSettings::Unless(const std::string& value, const std::string& gride1)
    {
        return StringUtil::EqualsIgnoreCase(StringUtil::Trim(value), gride1) ? std::string() : value;
    }

    JsonPtr ConverterSettings::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetString("CToShalimar", CToShalimar);
        o->SetString("ShalimarToC", ShalimarToC);
        o->SetString("command", CommandTemplate);
        o->SetBool("openResult", OpenResult);
        return o;
    }

    void ConverterSettings::FromJson(const JsonPtr& o)
    {
        if (!o) return;
        CToShalimar = Unless(o->GetString("CToShalimar", CToShalimar), Gride1CToShalimar);
        ShalimarToC = Unless(o->GetString("ShalimarToC", ShalimarToC), Gride1ShalimarToC);
        CommandTemplate = Unless(o->GetString("command", CommandTemplate), Gride1Template);
        OpenResult = o->GetBool("openResult", OpenResult);
    }

    // =========================================================================
    //  LayoutSettings
    // =========================================================================
    LayoutSettings::LayoutSettings()
        : Left(80), Top(60), Width(1400), Height(880), Maximized(true), LeftWidth(270), RightWidth(270),
          BottomHeight(220), StatusBar(true)
    {
        HiddenToolbars.push_back("Text Editor");
    }

    JsonPtr LayoutSettings::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetNumber("left", Left);
        o->SetNumber("top", Top);
        o->SetNumber("width", Width);
        o->SetNumber("height", Height);
        o->SetBool("maximized", Maximized);
        o->SetNumber("explorer", LeftWidth);
        o->SetNumber("properties", RightWidth);
        o->SetNumber("output", BottomHeight);
        o->SetList("hiddenPanes", HiddenPanes);
        o->SetList("hiddenToolbars", HiddenToolbars);
        o->SetBool("statusBar", StatusBar);
        return o;
    }

    void LayoutSettings::FromJson(const JsonPtr& o)
    {
        if (!o) return;
        Left = o->GetInt("left", Left);
        Top = o->GetInt("top", Top);
        Width = o->GetInt("width", Width);
        Height = o->GetInt("height", Height);
        Maximized = o->GetBool("maximized", Maximized);
        LeftWidth = o->GetInt("explorer", LeftWidth);
        RightWidth = o->GetInt("properties", RightWidth);
        BottomHeight = o->GetInt("output", BottomHeight);
        if (o->Has("hiddenPanes")) HiddenPanes = o->GetStringList("hiddenPanes");
        if (o->Has("hiddenToolbars")) HiddenToolbars = o->GetStringList("hiddenToolbars");
        StatusBar = o->GetBool("statusBar", StatusBar);
    }

    // =========================================================================
    //  RecentList
    // =========================================================================
    void RecentList::Push(std::vector<std::string>& list, const std::string& path)
    {
        for (size_t i = list.size(); i-- > 0;)
            if (StringUtil::EqualsIgnoreCase(list[i], path)) list.erase(list.begin() + (long)i);
        list.insert(list.begin(), path);
        while (list.size() > 10) list.pop_back();
    }

    JsonPtr RecentList::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetList("files", Files);
        o->SetList("workspaces", Workspaces);
        return o;
    }

    void RecentList::FromJson(const JsonPtr& o)
    {
        if (!o) return;
        Files = o->GetStringList("files");
        Workspaces = o->GetStringList("workspaces");
    }

    // =========================================================================
    //  GrideSettings
    // =========================================================================
    GrideSettings::GrideSettings()
        : Env(std::make_shared<EnvironmentSettings>()), Converter(std::make_shared<ConverterSettings>()),
          Layout(std::make_shared<LayoutSettings>()), Recent(std::make_shared<RecentList>()), KeySet("visualstudio"),
          CompilerOptions(std::make_shared<CompilerOptionStore>()), CcsProjects(JsonValue::MakeObject()), IsPortable(false)
    {
        for (Target t : TargetInfo::All()) Toolchains.push_back(std::make_shared<ToolchainSettings>(t));
        const Language langs[] = { Language::C90, Language::Cpp11, Language::Shalimar, Language::Assembly, Language::LinkerCmd };
        for (Language l : langs) Languages.push_back(std::make_shared<LanguageSettings>(l));
        Snippets = DefaultSnippets();
    }

    std::string GrideSettings::CcsKey(const std::string& dir)
    {
        std::string full = Paths::FullPath(PathUtil::Normalize(dir));
        while (full.size() > 1 && full.back() == '/') full.pop_back();
        return full;
    }

    std::string GrideSettings::CcsKeyOf(const std::string& dir) const
    {
        std::string key = CcsKey(dir);
        if (CcsProjects)
            for (const std::string& k : CcsProjects->KeyNames())
                if (StringUtil::EqualsIgnoreCase(k, key)) return k;
        return key;
    }

    JsonPtr GrideSettings::CcsState(const std::string& dir) const
    {
        return CcsProjects ? CcsProjects->GetObject(CcsKeyOf(dir)) : nullptr;
    }

    void GrideSettings::RememberCcsState(const std::string& dir, const JsonPtr& state)
    {
        if (!CcsProjects) CcsProjects = JsonValue::MakeObject();
        std::string key = CcsKeyOf(dir);
        if (!state || state->Count() == 0) CcsProjects->Remove(key);
        else CcsProjects->SetValue(key, state);
    }

    void GrideSettings::PublishPaths() const
    {
        ToolchainSettings::RideRoot = Env->RideRoot;
        RideToolchain::TiCompilerDir = Env->TiCompilerDir;
        RideToolchain::TiLibDir = Env->TiLibDir;
        VisualStudio::SetNamedVcvars(Env->Vcvars);
    }

    ToolchainSettingsPtr GrideSettings::Toolchain(Target t)
    {
        for (const ToolchainSettingsPtr& s : Toolchains)
            if (s->TargetKind == t) return s;
        ToolchainSettingsPtr n = std::make_shared<ToolchainSettings>(t);
        Toolchains.push_back(n);
        return n;
    }

    LanguageSettingsPtr GrideSettings::LanguageOf(Language l) const
    {
        for (const LanguageSettingsPtr& s : Languages)
            if (s->Lang == l) return s;
        return nullptr;
    }

    Language GrideSettings::DetectLanguage(const std::string& path, Language projectLang) const
    {
        std::string ext = Paths::Extension(PathUtil::Normalize(path));
        if (StringUtil::EqualsIgnoreCase(ext, ".h"))
            return (projectLang == Language::Cpp11 || projectLang == Language::Shalimar) ? projectLang : Language::C90;
        for (const LanguageSettingsPtr& s : Languages)
            if (s->OwnsExtension(ext)) return s->Lang;
        return Language::Other;
    }

    JsonPtr GrideSettings::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetString("gride", "1.0");
        o->SetValue("environment", Env->ToJson());

        JsonPtr tc = JsonValue::MakeObject();
        for (const ToolchainSettingsPtr& t : Toolchains) tc->SetValue(t->Name(), t->ToJson());
        o->SetValue("toolchains", tc);

        JsonPtr langs = JsonValue::MakeObject();
        for (const LanguageSettingsPtr& l : Languages) langs->SetValue(l->Name(), l->ToJson());
        o->SetValue("languages", langs);

        o->SetValue("converters", Converter->ToJson());

        JsonPtr tools = JsonValue::MakeArray();
        for (const ExternalToolPtr& x : ExternalTools)
        {
            JsonPtr e = JsonValue::MakeObject();
            e->SetString("title", x->Title);
            e->SetString("command", x->Command);
            e->SetString("arguments", x->Arguments);
            e->SetString("initialDir", x->InitialDir);
            tools->Add(e);
        }
        o->SetValue("externalTools", tools);

        JsonPtr keys = JsonValue::MakeObject();
        for (const std::pair<std::string, std::string>& kv : KeyBindings.Items()) keys->SetString(kv.first, kv.second);
        o->SetValue("keyBindings", keys);
        o->SetString("keySet", KeySet);

        JsonPtr snip = JsonValue::MakeObject();
        for (const std::pair<std::string, std::string>& kv : Snippets.Items()) snip->SetString(kv.first, kv.second);
        o->SetValue("snippets", snip);

        o->SetValue("compilerOptions", CompilerOptions->ToJson());
        if (CcsProjects && CcsProjects->Count() > 0) o->SetValue("ccsProjects", CcsProjects);
        o->SetValue("recent", Recent->ToJson());
        o->SetString("lastWorkspace", LastWorkspace);
        o->SetValue("layout", Layout->ToJson());
        return o;
    }

    GrideSettingsPtr GrideSettings::FromJson(const JsonPtr& o)
    {
        GrideSettingsPtr s = std::make_shared<GrideSettings>();
        if (!o || !o->IsObject()) return s;
        s->Env->FromJson(o->GetObject("environment"));
        // the toolchains' defaults and tool search read RIDE's root as soon as it is known
        s->PublishPaths();

        JsonPtr tc = o->GetObject("toolchains");
        if (tc)
        {
            s->Toolchains.clear();
            for (Target t : TargetInfo::All())
                s->Toolchains.push_back(ToolchainSettings::FromJson(t, tc->GetObject(TargetInfo::Name(t))));
        }

        JsonPtr langs = o->GetObject("languages");
        if (langs)
            for (const LanguageSettingsPtr& l : s->Languages) l->FromJson(langs->GetObject(l->Name()));

        s->Converter->FromJson(o->GetObject("converters"));

        JsonPtr tools = o->Get("externalTools");
        if (tools && tools->IsArray())
            for (int i = 0; i < tools->Count(); i++)
            {
                JsonPtr e = tools->At(i);
                ExternalToolPtr x = std::make_shared<ExternalTool>();
                x->Title = e->GetString("title", x->Title);
                x->Command = e->GetString("command", x->Command);
                x->Arguments = e->GetString("arguments", x->Arguments);
                x->InitialDir = e->GetString("initialDir", x->InitialDir);
                s->ExternalTools.push_back(x);
            }

        JsonPtr keys = o->GetObject("keyBindings");
        if (keys)
            for (const std::string& k : keys->KeyNames()) s->KeyBindings.Set(k, keys->GetString(k, ""));
        s->KeySet = o->GetString("keySet", s->KeySet);
        if (StringUtil::EqualsIgnoreCase(s->KeySet, "ride")) s->KeySet = "gride";

        JsonPtr snip = o->GetObject("snippets");
        if (snip)
        {
            s->Snippets.Clear();
            for (const std::string& k : snip->KeyNames()) s->Snippets.Set(k, snip->GetString(k, ""));
        }

        s->CompilerOptions->FromJson(o->GetObject("compilerOptions"));
        JsonPtr ccs = o->GetObject("ccsProjects");
        if (ccs) s->CcsProjects = ccs;
        s->Recent->FromJson(o->GetObject("recent"));
        s->LastWorkspace = o->GetString("lastWorkspace", "");
        s->Layout->FromJson(o->GetObject("layout"));
        return s;
    }

    OrderedStringMap GrideSettings::DefaultSnippets()
    {
        OrderedStringMap d;
        d.Set("for loop (C90)", "for (i = 0; i < n; i++)\n{\n    \n}\n");
        d.Set("if / else", "if ()\n{\n}\nelse\n{\n}\n");
        d.Set("switch", "switch (x)\n{\ncase 0:\n    break;\ndefault:\n    break;\n}\n");
        d.Set("file header", "/*\n * File   : \n * Author : \n * Date   : \n * Brief  : \n */\n");
        d.Set("C6747 interrupt handler", "interrupt void isr_name(void)\n{\n    /* clear the interrupt flag here */\n}\n");
        d.Set("include guard", "#ifndef NAME_H\n#define NAME_H\n\n\n#endif /* NAME_H */\n");
        return d;
    }

    GrideSettingsPtr GrideSettings::Clone() const
    {
        GrideSettingsPtr c = FromJson(JsonParser::Parse(JsonWriter::Write(ToJson())));
        c->FilePath = FilePath;
        c->IsPortable = IsPortable;
        return c;
    }

    // =========================================================================
    //  SettingsManager
    // =========================================================================
    const char* const SettingsManager::FileName = "settings.json";
    const char* const SettingsManager::FormerFileName = "gride.settings.json";

    std::string SettingsManager::PortablePath()
    {
        return Paths::Combine(ToolchainDefinitions::PortableDirectory(), FileName);
    }

    std::string SettingsManager::UserPath()
    {
        return Paths::Combine(Paths::UserSettingsDirectory(), FileName);
    }

    bool SettingsManager::Holds(const JsonPtr& o)
    {
        return o && o->IsObject() && (o->Has("gride") || o->Has("environment"));
    }

    GrideSettingsPtr SettingsManager::Load(std::string& warning)
    {
        warning.clear();
        bool portable = Paths::IsFile(PortablePath())
                        || Paths::IsFile(Paths::Combine(ToolchainDefinitions::PortableDirectory(), FormerFileName));
        std::string path = portable ? PortablePath() : UserPath();
        std::string former = Paths::Combine(Paths::DirectoryName(path), FormerFileName);
        ToolchainDefinitions::SetCurrent(ToolchainDefinitions::Load(ToolchainDefinitions::ShippedPath(),
                                                                    ToolchainDefinitions::UserPath(portable)));
        JsonPtr shared;
        if (Paths::IsFile(path))
        {
            try
            {
                shared = JsonParser::ParseFile(path);
                if (!shared || !shared->IsObject()) throw std::runtime_error("the file is not a JSON object");
            }
            catch (const std::exception& ex)
            {
                std::string backup = path + ".bad";
                std::string error;
                Paths::CopyFile(path, backup, error);
                warning = std::string("Settings file could not be read and was reset to defaults.\n") + ex.what()
                          + "\nA copy was kept as: " + backup;
                shared = nullptr;
            }
        }

        JsonPtr mine = Holds(shared) ? shared : nullptr;
        bool moving = false;
        if (!mine && Paths::IsFile(former))
        {
            try { mine = JsonParser::ParseFile(former); moving = mine && mine->IsObject(); }
            catch (const std::exception&) { mine = nullptr; }
            if (!moving) mine = nullptr;
        }
        GrideSettingsPtr s = std::make_shared<GrideSettings>();
        if (mine)
        {
            try { s = GrideSettings::FromJson(mine); }
            catch (const std::exception& ex)
            {
                warning = std::string("Settings could not be read and were reset to defaults.\n") + ex.what();
                s = std::make_shared<GrideSettings>();
            }
        }

        s->FilePath = path;
        s->IsPortable = portable;
        if (moving || !Holds(shared))
            if (Save(s) && moving && !PathUtil::SamePath(former, path)) Paths::DeleteFile(former);
        return s;
    }

    bool SettingsManager::Save(const GrideSettingsPtr& s)
    {
        try
        {
            JsonPtr doc;
            if (Paths::IsFile(s->FilePath))
                try { doc = JsonParser::ParseFile(s->FilePath); } catch (const std::exception&) { doc = nullptr; }
            if (!doc || !doc->IsObject()) doc = JsonValue::MakeObject();
            JsonPtr own = s->ToJson();
            for (const std::string& k : own->KeyNames()) doc->SetValue(k, own->Get(k));
            JsonWriter::WriteFile(s->FilePath, doc);
            return true;
        }
        catch (const std::exception&) { return false; }
    }
}}
