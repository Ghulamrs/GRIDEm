// =============================================================================
//  RideToolchain.cpp - see RideToolchain.h
// =============================================================================
#include "RideToolchain.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include <algorithm>

namespace GRIDEm { namespace Services {

    using namespace GRIDEm::Core;
    using namespace GRIDEm::Settings;
    using Platform::Paths;
    using Platform::Environment;

    std::string RideToolchain::TiCompilerDir;
    std::string RideToolchain::TiLibDir;

    ToolchainDefinitionsPtr RideToolchain::Defs() { return ToolchainDefinitions::Current(); }

    static bool Has(const std::vector<std::string>& list, const std::string& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    static std::string Trimmed(const std::string& s) { return StringUtil::Trim(s); }

    // ---- the definitions behind a role -------------------------------------------------
    std::string RideToolchain::RoleOf(RideTool t)
    {
        switch (t)
        {
        case RideTool::C90:      return "cCompiler";
        case RideTool::Cpp11:    return "cppCompiler";
        case RideTool::Shalimar: return "shalimarCompiler";
        case RideTool::Cl:       return "hostCompiler";
        case RideTool::HostCxx:  return "hostCxx";
        default:                 return std::string();
        }
    }

    RideTool RideToolchain::ToolOfRole(const std::string& role)
    {
        if (role == "cCompiler") return RideTool::C90;
        if (role == "cppCompiler") return RideTool::Cpp11;
        if (role == "shalimarCompiler") return RideTool::Shalimar;
        if (role == "hostCompiler") return RideTool::Cl;
        if (role == "hostCxx") return RideTool::HostCxx;
        return RideTool::Auto;
    }

    ToolDefinitionPtr RideToolchain::Definition(RideTool t)
    {
        std::string role = RoleOf(t);
        return role.empty() ? nullptr : Defs()->ToolByRole(role);
    }

    TargetDefinitionPtr RideToolchain::TargetDef(Target t) { return Defs()->TargetOf(t); }

    // ---- words and names ---------------------------------------------------------------
    std::string RideToolchain::Word(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d) return "auto";
        return d->Words.empty() ? d->Name : d->Words[0];
    }

    RideTool RideToolchain::FromWord(const std::string& word)
    {
        if (StringUtil::IsNullOrWhiteSpace(word)) return RideTool::Auto;
        std::string w = Trimmed(word);
        if (StringUtil::EqualsIgnoreCase(w, "auto")) return RideTool::Auto;
        ToolDefinitionPtr d = Defs()->ToolByWord(w);
        if (!d) d = Defs()->ToolByName(w);
        if (!d) return RideTool::Auto;
        RideTool t = ToolOfRole(d->Role);
        if (t == RideTool::HostCxx)
        {
            TargetDefinitionPtr host = Defs()->HostTarget();
            if (host && !host->ToolIdFor("hostCompiler").empty() && host->ToolIdFor("hostCompiler") != d->Id) return RideTool::Cl;
        }
        return t;
    }

    std::string RideToolchain::Name(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        return d ? d->Name : std::string("auto");
    }

    std::vector<RideTool> RideToolchain::Compilers()
    {
        std::vector<RideTool> r;
        const RideTool all[] = { RideTool::C90, RideTool::Cpp11, RideTool::Shalimar, RideTool::Cl };
        for (RideTool t : all) if (Definition(t)) r.push_back(t);
        return r;
    }

    std::vector<std::string> RideToolchain::Aliases(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        return d ? d->AllNames() : std::vector<std::string>();
    }

    std::string RideToolchain::EnvironmentVariable(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        return d ? d->Lookup.Env : std::string();
    }

    std::string RideToolchain::LanguageWord(Language l)
    {
        switch (l)
        {
        case Language::C90:      return "C";
        case Language::Cpp11:    return "C++";
        case Language::Shalimar: return "Shalimar";
        case Language::Assembly: return "assembly";
        case Language::Header:   return "a header";
        default:                 return "plain text";
        }
    }

    // ---- targets -----------------------------------------------------------------------
    std::string RideToolchain::Arch(Target t)
    {
        TargetDefinitionPtr d = TargetDef(t);
        return d ? d->Arch : std::string();
    }

    Target RideToolchain::TargetOfArch(const std::string& arch)
    {
        TargetDefinitionPtr d = Defs()->TargetByArch(arch);
        return d ? TargetInfo::Parse(d->IdeTarget) : Target::Windows;
    }

    std::string RideToolchain::HostArch()
    {
        TargetDefinitionPtr d = Defs()->HostTarget();
        return d ? d->Arch : std::string();
    }

    bool RideToolchain::IsEmulated(Target t)
    {
        TargetDefinitionPtr d = TargetDef(t);
        return d && d->Emulated;
    }

    TargetDefinitionPtr RideToolchain::EmulatedTarget()
    {
        for (const TargetDefinitionPtr& d : Defs()->Targets) if (d->Emulated) return d;
        return nullptr;
    }

    bool RideToolchain::UsesArch(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        return d && d->TakesArch();
    }

    std::string RideToolchain::ArchFlag(RideTool t, Target target)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d || !d->TakesArch()) return std::string();
        return StringUtil::Replace(d->ArchFlag, "$(Arch)", Arch(target));
    }

    // ---- which compiler takes the file -------------------------------------------------
    RideTool RideToolchain::Resolve(RideTool chosen, Language lang)
    {
        if (chosen != RideTool::Auto) return chosen;
        ToolDefinitionPtr d = Defs()->CompilerOf(LanguageWord(lang));
        RideTool t = d ? ToolOfRole(d->Role) : RideTool::Auto;
        return t == RideTool::Auto ? RideTool::C90 : t;
    }

    bool RideToolchain::CanCompile(RideTool t, Language lang)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d) return false;
        if (lang != Language::C90 && lang != Language::Cpp11 && lang != Language::Shalimar) return false;
        return d->CompilesLanguage(LanguageWord(lang));
    }

    std::string RideToolchain::Refusal(RideTool t, Language lang)
    {
        ToolDefinitionPtr takes = Defs()->CompilerOf(LanguageWord(lang));
        std::string picks = takes ? ", and it picks " + takes->Name : std::string();
        if (lang == Language::Shalimar && t != RideTool::Shalimar)
            return Name(t) + " does not compile Shalimar - choose the automatic compiler" + picks;
        if (lang == Language::Other)
            return "nothing to compile: no extension names a language - .c, .cpp or .shl (.shm) picks the compiler";
        if (lang != Language::C90 && lang != Language::Cpp11 && lang != Language::Shalimar)
            return "nothing to compile: this is " + LanguageWord(lang) + ", not C or C++";
        ToolDefinitionPtr d = Definition(t);
        if (d && !d->CompilesLanguage(LanguageWord(lang)))
            return d->Name + " compiles " + StringUtil::Join(" and ", d->Languages) + ", not " + LanguageWord(lang)
                   + " - choose the automatic compiler" + picks;
        return "cannot compile this file";
    }

    // ---- what runs here ----------------------------------------------------------------
    bool RideToolchain::RunsHere(RideTool t, Target target)
    {
        if (!UsesArch(t)) return true;          // cl builds for the machine it is on, whatever the target says (RIDE)
        if (IsEmulated(target)) return true;
        return StringUtil::EqualsIgnoreCase(Arch(target), HostArch());
    }

    std::string RideToolchain::WhyNotRun(RideTool t, Target target)
    {
        if (RunsHere(t, target)) return std::string();
        if (IsEmulated(target))
        {
            std::vector<std::string> owners;
            for (const ToolDefinitionPtr& d : Defs()->Tools)
                if (d->TakesArch() && d->ServesTarget(Arch(target))) owners.push_back(d->Name + "'s");
            std::string list;
            if (owners.empty()) list = "no compiler's";
            else if (owners.size() == 1) list = owners[0];
            else
            {
                std::vector<std::string> head(owners.begin(), owners.end() - 1);
                list = StringUtil::Join(", ", head) + " and " + owners.back();
            }
            return Name(t) + " has no " + Arch(target) + " target - it is " + list;
        }
        return Arch(target) + " only reaches " + AssemblyOnlyFlag() + " here - switch to " + HostArch() + " to run it";
    }

    std::string RideToolchain::AssemblyOnlyFlag()
    {
        ToolDefinitionPtr d = Definition(RideTool::C90);
        std::string args = d ? Trimmed(d->OperationArgs("compileToAssembly")) : std::string();
        size_t sp = args.find(' ');
        return (sp != std::string::npos && sp > 0) ? args.substr(0, sp) : args;
    }

    // ---- debug information -------------------------------------------------------------
    bool RideToolchain::EmitsDebugInfo(RideTool t, Target target)
    {
        if (!UsesArch(t)) return true;
        TargetDefinitionPtr d = TargetDef(target);
        return d && !d->DebugInfo.empty();
    }

    bool RideToolchain::DebugsWithCodeView(RideTool t, Target target, bool debug)
    {
        TargetDefinitionPtr d = TargetDef(target);
        return debug && d && StringUtil::EqualsIgnoreCase(d->DebugInfo, "codeview") && UsesArch(t);
    }

    std::string RideToolchain::AssemblerFlag(RideTool t, Target target, bool debug, bool assemblerNamed)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d) return std::string();
        if (DebugsWithCodeView(t, target, debug)) return d->AsmSyntaxFlag("codeview");
        TargetDefinitionPtr td = TargetDef(target);
        if (td && td->Host && assemblerNamed) return d->AsmSyntaxFlag("assemblerNamed");
        return std::string();
    }

    std::string RideToolchain::ConfigFlags(RideTool t, bool debug, Target target)
    {
        return ConfigFlags(t, debug, target, nullptr);
    }

    std::string RideToolchain::ConfigFlags(RideTool t, bool debug, Target target, const CompilerOptionStorePtr& store)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d) return std::string();
        if (!d->Options.empty() && !Defs()->CompilerOptions.empty())
            return OptionFlags(store ? store : std::make_shared<CompilerOptionStore>(), d, debug ? "Debug" : "Release", target);
        std::string flags = d->ConfigFlagsFor(debug ? "Debug" : "Release", Arch(target));
        if (debug && !EmitsDebugInfo(t, target))
        {
            std::vector<std::string> kept;
            for (const std::string& f : StringUtil::SplitNonEmpty(flags, " "))
                if (f != "-g") kept.push_back(f);
            flags = StringUtil::Join(" ", kept);
        }
        return flags;
    }

    // ---- RIDE's Compiler Options -------------------------------------------------------
    bool RideToolchain::OptionAvailable(const std::string& id, Target target, std::string& why)
    {
        why.clear();
        CompilerOptionDefinitionPtr o = Defs()->CompilerOption(id);
        if (!o || o->Needs.empty()) return true;
        bool ok;
        if (StringUtil::EqualsIgnoreCase(o->Needs, "lineTable"))
        {
            ToolDefinitionPtr tool = Defs()->ToolOfOption(id);
            ok = tool ? EmitsDebugInfo(ToolOfRole(tool->Role), target) : EmitsDebugInfo(RideTool::C90, target);
        }
        else if (StringUtil::EqualsIgnoreCase(o->Needs, "emulated")) ok = IsEmulated(target);
        else if (StringUtil::EqualsIgnoreCase(o->Needs, "notEmulated")) ok = !IsEmulated(target);
        else ok = true;
        if (!ok) why = StringUtil::Replace(o->Unavailable, "$(Arch)", Arch(target));
        return ok;
    }

    std::string RideToolchain::OptionFlags(const CompilerOptionStorePtr& store, const ToolDefinitionPtr& tool,
                                           const std::string& config, Target target)
    {
        std::string sb;
        if (!store || !tool) return std::string();
        std::string why;
        for (const std::string& id : tool->Options)
        {
            CompilerOptionDefinitionPtr o = Defs()->CompilerOption(id);
            if (!o || !o->EnvironmentVariable.empty()) continue;
            std::string value = store->Value(config, id);
            if (o->IsCheck())
            {
                if (value == "1" && !o->FlagOn.empty() && OptionAvailable(id, target, why)) Append(sb, o->FlagOn);
                else if (value == "0" && !o->FlagOff.empty() && OptionAvailable(id, target, why)) Append(sb, o->FlagOff);
            }
            else if (o->IsChoice())
            {
                if (value != o->SilentChoice() && !value.empty() && !o->Flag.empty() && OptionAvailable(id, target, why))
                    Append(sb, StringUtil::Replace(o->Flag, "$(Value)", value));
            }
            else if (!o->FlagEach.empty() && OptionAvailable(id, target, why))
            {
                for (const std::string& item : CompilerOptionStore::Entries(value))
                {
                    bool plain = item.find_first_of(" \t\"") == std::string::npos;
                    Append(sb, StringUtil::Replace(o->FlagEach, "$(Item)", plain ? item : "\"" + item + "\""));
                }
            }
        }
        return sb;
    }

    std::vector<std::string> RideToolchain::OptionEnvironment(const CompilerOptionStorePtr& store, const ToolDefinitionPtr& tool,
                                                              const std::string& config)
    {
        std::vector<std::string> r;
        if (!tool) return r;
        for (const std::string& id : tool->Options)
        {
            CompilerOptionDefinitionPtr o = Defs()->CompilerOption(id);
            if (!o || o->EnvironmentVariable.empty()) continue;
            std::string value = store ? store->Value(config, id) : o->DefaultFor(config);
            r.push_back(o->EnvironmentVariable + "=" + (o->IsCheck() ? (value == "1" ? std::string("1") : std::string()) : value));
        }
        return r;
    }

    std::string RideToolchain::LanguageFlags(RideTool t, Language lang)
    {
        ToolDefinitionPtr d = Definition(t);
        return d ? d->LanguageFlag(LanguageWord(lang)) : std::string();
    }

    std::string RideToolchain::IncludeFlagOf(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        return d ? d->IncludeFlag : std::string();
    }

    std::string RideToolchain::DefineFlagOf(RideTool t)
    {
        ToolDefinitionPtr d = Definition(t);
        return d ? d->DefineFlag : std::string();
    }

    std::string RideToolchain::IncludeFlags(RideTool t, const std::vector<std::string>& projectDirs,
                                            const std::string& installInclude, const std::string& installLib)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d || d->IncludeFlag.empty()) return std::string();
        std::string flag = d->IncludeFlag;
        std::string sb;
        for (const std::string& dir : projectDirs)
            if (!StringUtil::IsNullOrWhiteSpace(dir)) Append(sb, flag + Quote(dir));
        const InstallationDefinition& inst = Defs()->Installation;
        for (const std::string& which : d->InstallIncludeDirs)
        {
            std::string dir;
            if (StringUtil::EqualsIgnoreCase(which, inst.LibDir) || StringUtil::EqualsIgnoreCase(which, "lib")) dir = installLib;
            else if (StringUtil::EqualsIgnoreCase(which, inst.IncludeDir) || StringUtil::EqualsIgnoreCase(which, "include")) dir = installInclude;
            if (!dir.empty()) Append(sb, flag + Quote(dir));
        }
        return sb;
    }

    // ---- finding the tools -------------------------------------------------------------
    std::string RideToolchain::Quote(const std::string& s) { return "\"" + s + "\""; }

    std::string RideToolchain::IdeDirectory() { return Paths::AppDirectory(); }

    std::vector<std::string> RideToolchain::NamesHere(const std::vector<std::string>& names)
    {
        std::vector<std::string> r;
        for (const std::string& n : names)
        {
            if (n.empty()) continue;
            if (!Has(r, n)) r.push_back(n);
        }
        for (const std::string& n : names)
            if (StringUtil::EndsWithIgnoreCase(n, ".exe") && n.size() > 4)
            {
                std::string bare = n.substr(0, n.size() - 4);
                if (!Has(r, bare)) r.push_back(bare);
            }
        return r;
    }

    // the program at dir/name, a path written on Windows read with '/'
    static bool ProgramAt(const std::string& dir, const std::string& name, std::string& full)
    {
        std::string p = Paths::Combine(PathUtil::Normalize(dir), PathUtil::Normalize(name));
        if (Paths::IsExecutable(p)) { full = Paths::FullPath(p); return true; }
        return false;
    }

    static bool NamedByVariable(const std::string& envVar, std::string& full)
    {
        if (envVar.empty()) return false;
        std::string named;
        if (!Paths::GetEnvironment(envVar, named) || StringUtil::IsNullOrWhiteSpace(named)) return false;
        named = PathUtil::Normalize(Trimmed(named));
        if (!Paths::IsExecutable(named)) return false;
        full = Paths::FullPath(named);
        return true;
    }

    std::string RideToolchain::RideBin(const std::string& rideRoot)
    {
        const InstallationDefinition& inst = Defs()->Installation;
        std::vector<std::string> roots;
        if (!StringUtil::IsNullOrWhiteSpace(rideRoot)) roots.push_back(PathUtil::Normalize(Environment::Expand(Trimmed(rideRoot))));
        for (const std::string& r : inst.DefaultRoots)
        {
            std::string x = PathUtil::Normalize(Environment::Expand(r));
            if (!StringUtil::Contains(x, "%") && !Has(roots, x)) roots.push_back(x);
        }
        for (const std::string& r : roots)
        {
            if (HasMarker(r, inst.Markers)) return r;
            std::string bin = inst.BinDir.empty() ? r : Paths::Combine(r, inst.BinDir);
            if (HasMarker(bin, inst.Markers)) return bin;
        }
        return std::string();
    }

    std::string RideToolchain::InstallRoot(const std::string& rideRoot)
    {
        std::string bin = RideBin(rideRoot);
        if (bin.empty()) return std::string();
        const InstallationDefinition& inst = Defs()->Installation;
        if (inst.BinDir.empty()) return bin;
        std::string up = Paths::DirectoryName(bin);
        return (!up.empty() && StringUtil::EqualsIgnoreCase(Paths::FileName(bin), inst.BinDir)) ? up : bin;
    }

    std::string RideToolchain::InstallDir(const std::string& rideRoot, const std::string& which, const std::string& named)
    {
        std::string root = InstallRoot(rideRoot);
        if (!StringUtil::IsNullOrWhiteSpace(named))
        {
            std::string n = PathUtil::Normalize(Environment::Expand(Trimmed(named)));
            std::string dir = Paths::IsRooted(n) ? n : (root.empty() ? n : Paths::Combine(root, n));
            return Paths::IsDirectory(dir) ? Paths::FullPath(dir) : std::string();
        }
        if (root.empty() || which.empty()) return std::string();
        std::string dir = Paths::Combine(root, which);
        return Paths::IsDirectory(dir) ? dir : std::string();
    }

    std::vector<std::string> RideToolchain::InstallIncludeDirs(RideTool t, const std::string& rideRoot,
                                                               const std::string& namedInclude, const std::string& namedLib)
    {
        std::vector<std::string> r;
        ToolDefinitionPtr d = Definition(t);
        if (!d || d->IncludeFlag.empty()) return r;
        const InstallationDefinition& inst = Defs()->Installation;
        for (const std::string& which : d->InstallIncludeDirs)
        {
            std::string dir;
            if (StringUtil::EqualsIgnoreCase(which, inst.LibDir) || StringUtil::EqualsIgnoreCase(which, "lib"))
                dir = InstallDir(rideRoot, inst.LibDir, namedLib);
            else if (StringUtil::EqualsIgnoreCase(which, inst.IncludeDir) || StringUtil::EqualsIgnoreCase(which, "include"))
                dir = InstallDir(rideRoot, inst.IncludeDir, namedInclude);
            if (!dir.empty() && !Has(r, dir)) r.push_back(dir);
        }
        return r;
    }

    // ---- TI's C6000 compiler directory -------------------------------------------------
    std::string RideToolchain::TiDir()
    {
        if (!StringUtil::IsNullOrWhiteSpace(TiCompilerDir))
        {
            std::string d = PathUtil::Normalize(Environment::Expand(Trimmed(TiCompilerDir)));
            if (Paths::IsDirectory(d)) return Paths::FullPath(d);
        }
        return DetectedTiDir();
    }

    bool RideToolchain::TiNamed()
    {
        if (StringUtil::IsNullOrWhiteSpace(TiCompilerDir)) return false;
        return Paths::IsDirectory(PathUtil::Normalize(Environment::Expand(Trimmed(TiCompilerDir))));
    }

    std::string RideToolchain::DetectedTiDir()
    {
        const CcsDefinition& cd = Defs()->Ccs;
        TargetDefinitionPtr emu = EmulatedTarget();
        ToolDefinitionPtr ld = emu ? Defs()->Tool(emu->ToolIdFor("nativeLinker")) : nullptr;
        if (!ld || cd.TiRoots.empty()) return std::string();
        std::string found;
        std::vector<std::string> candidates;
        for (const std::string& r : cd.TiRoots)
        {
            std::string root = PathUtil::Normalize(Environment::Expand(r));
            if (StringUtil::Contains(root, "%") || !Paths::IsDirectory(root)) continue;
            for (const std::string& under : cd.TiUnder) CompilersUnder(root, under, cd, candidates);
        }
        for (const std::string& c : candidates)
        {
            bool hasLinker = false;
            std::string full;
            for (const std::string& n : NamesHere(ld->AllNames()))
                if (ProgramAt(Paths::Combine(c, cd.TiBin), n, full)) { hasLinker = true; break; }
            if (!hasLinker) continue;
            if (found.empty() || Newer(VersionOf(Paths::FileName(c)), VersionOf(Paths::FileName(found)))) found = c;
        }
        return found;
    }

    std::string RideToolchain::DefaultRideBin()
    {
        const InstallationDefinition& inst = Defs()->Installation;
        // the first root that means something here: a Windows root (%ProgramFiles%\RIDE) stays
        // unexpanded on the Mac and is passed over
        for (const std::string& root : inst.DefaultRoots)
        {
            std::string r = PathUtil::Normalize(Environment::Expand(root));
            if (StringUtil::Contains(r, "%")) continue;
            return inst.BinDir.empty() ? r : Paths::Combine(r, inst.BinDir);
        }
        // else the folder of the IDE itself, as the file says (GRIDEm.app/Contents/MacOS)
        return IdeDirectory();
    }

    std::string RideToolchain::Find(const std::vector<std::string>& names, const std::string& envVar,
                                    const std::string& toolDir, const std::string& rideRoot)
    {
        return Find(names, envVar, toolDir, rideRoot, true);
    }

    std::string RideToolchain::Find(const std::vector<std::string>& names, const std::string& envVar,
                                    const std::string& toolDir, const std::string& rideRoot, bool onPath)
    {
        std::string full;
        if (NamedByVariable(envVar, full)) return full;
        std::vector<std::string> tried = NamesHere(names);
        for (const std::string& d : SearchDirs(toolDir, rideRoot))
            for (const std::string& n : tried)
                if (ProgramAt(d, n, full)) return full;
        if (!onPath) return std::string();
        for (const std::string& d : Paths::SearchPath())
        {
            if (d.empty() || d == ".") continue;
            for (const std::string& n : tried)
                if (ProgramAt(d, n, full)) return full;
        }
        return std::string();
    }

    std::string RideToolchain::Find(const std::string& name, const std::string& envVar,
                                    const std::string& toolDir, const std::string& rideRoot)
    {
        std::vector<std::string> names;
        names.push_back(name);
        if (!StringUtil::EndsWithIgnoreCase(name, ".exe")) names.push_back(name + ".exe");
        return Find(names, envVar, toolDir, rideRoot);
    }

    std::string RideToolchain::FindTool(const ToolDefinitionPtr& d, const std::string& toolDir, const std::string& rideRoot)
    {
        if (!d) return std::string();
        std::vector<std::string> names = NamesHere(d->AllNames());
        std::string full;
        if (NamedByVariable(d->Lookup.Env, full)) return full;
#if !defined(_WIN32)
        // found through Visual Studio's environment (link.exe, ml64, clang for CodeView): Windows only.
        // A same-named program on this PATH (/usr/bin/link, Xcode's clang) is not that tool.
        if (d->Lookup.Vcvars) return std::string();
#endif
        std::vector<std::string> dirs;
        if (!d->Lookup.InstallDir.empty())
        {
            std::vector<std::string> s = SearchDirs(toolDir, rideRoot);
            dirs.insert(dirs.end(), s.begin(), s.end());
        }
        if (d->Lookup.Vcvars)
        {
            VsInstallationPtr vs = VisualStudio::Find();
            if (vs)
            {
                std::vector<std::string> b = vs->BinDirs();
                dirs.insert(dirs.end(), b.begin(), b.end());
            }
        }
        std::string tiBin = TiBinFor(d);
        if (!tiBin.empty()) dirs.push_back(tiBin);
        for (const std::string& own : d->Lookup.Dirs) dirs.push_back(PathUtil::Normalize(Environment::Expand(own)));
        for (const std::string& dir : dirs)
            for (const std::string& n : names)
                if (ProgramAt(dir, n, full)) return full;
        if (!d->Lookup.OnPath) return std::string();
        return Find(names, std::string(), std::string(), std::string(), true);
    }

    std::string RideToolchain::SearchPlaces(const ToolDefinitionPtr& d, const std::string& toolDir, const std::string& rideRoot)
    {
        std::vector<std::string> places;
        std::string envVar = d ? d->Lookup.Env : std::string();
        if (!envVar.empty())
        {
            std::string v;
            bool set = Paths::GetEnvironment(envVar, v) && !StringUtil::IsNullOrWhiteSpace(v);
            places.push_back("$" + envVar + (set ? " = " + v : std::string(" (not set)")));
        }
        if (!d || !d->Lookup.InstallDir.empty())
        {
            if (!StringUtil::IsNullOrWhiteSpace(toolDir)) places.push_back("the tool folder " + Environment::Expand(toolDir));
            std::string ide = IdeDirectory();
            if (!ide.empty()) places.push_back("inside GRIDEm (" + ide + ")");
            std::string bin = RideBin(rideRoot);
            places.push_back(!bin.empty() ? "the installation's " + bin : "the installation (not found: " + DefaultRideBin() + ")");
        }
        if (d && d->Lookup.Vcvars)
        {
            VsInstallationPtr vs = VisualStudio::Find();
            places.push_back(vs ? "Visual Studio's " + StringUtil::Join(", ", vs->BinDirs()) : std::string("Visual Studio (Windows only)"));
        }
        if (d && IsTiTool(d))
        {
            std::string tiBin = TiBinFor(d);
            places.push_back(!tiBin.empty() ? "TI's compiler directory's " + tiBin
                                            : "TI's compiler directory (none named under Settings > Projects, none found under "
                                              + StringUtil::Join(", ", Defs()->Ccs.TiRoots) + ")");
        }
        if (d)
            for (const std::string& own : d->Lookup.Dirs) places.push_back(Environment::Expand(own));
#if !defined(_WIN32)
        if (d && d->Lookup.Vcvars) return StringUtil::Join(", ", places);    // not PATH: see FindTool
#endif
        if (!d || d->Lookup.OnPath) places.push_back("PATH");
        return StringUtil::Join(", ", places);
    }

    bool RideToolchain::IsTiTool(const ToolDefinitionPtr& d)
    {
        TargetDefinitionPtr emu = EmulatedTarget();
        return d && emu && (d->Role == "nativeLinker" || d->Role == "nativeAssembler") && d->ServesTarget(emu->Arch);
    }

    std::string RideToolchain::TiBinFor(const ToolDefinitionPtr& d)
    {
        if (!IsTiTool(d)) return std::string();
        std::string ti = TiDir();
        if (ti.empty()) return std::string();
        std::string bin = Defs()->Ccs.TiBin.empty() ? ti : Paths::Combine(ti, Defs()->Ccs.TiBin);
        return Paths::IsDirectory(bin) ? bin : std::string();
    }

    std::string RideToolchain::FindLibraryDir(const LibraryDefinitionPtr& lib, const std::string& toolDir, const std::string& rideRoot)
    {
        if (!lib || lib->Directory.empty()) return std::string();
        if (!lib->DirectoryEnv.empty())
        {
            std::string env;
            if (Paths::GetEnvironment(lib->DirectoryEnv, env) && !StringUtil::IsNullOrWhiteSpace(env))
            {
                env = PathUtil::Normalize(env);
                if (Paths::IsDirectory(env) && HasFile(env, lib->Marker)) return Paths::FullPath(env);
            }
        }
        std::vector<std::string> dirs = SearchDirs(toolDir, rideRoot);
        // inside GRIDEm.app the libraries are resources: Contents/Resources/lib/...
        std::string resources = Paths::ResourcesDirectory();
        if (!resources.empty() && !Has(dirs, resources)) dirs.push_back(resources);
        for (const std::string& d : dirs)
        {
            std::string dir = Paths::Combine(d, PathUtil::Normalize(lib->Directory));
            if (Paths::IsDirectory(dir) && HasFile(dir, lib->Marker)) return dir;
        }
        return std::string();
    }

    std::string RideToolchain::FindRuntimeDir(const std::string& toolDir, const std::string& rideRoot)
    {
        TargetDefinitionPtr t = EmulatedTarget();
        return t ? FindLibraryDir(Defs()->Library(t->Runtime), toolDir, rideRoot) : std::string();
    }

    std::string RideToolchain::FindShalimarRuntimeDir(const std::string& toolDir, const std::string& rideRoot, bool debug)
    {
        TargetDefinitionPtr t = EmulatedTarget();
        LibraryDefinitionPtr lib = t ? Defs()->LibraryByKind(t->Arch, "shalimarRuntime") : nullptr;
        if (!lib) return std::string();
        std::string envName = lib->AssemblyDirectoryEnvFor(debug);
        if (!envName.empty())
        {
            std::string env;
            if (Paths::GetEnvironment(envName, env) && !StringUtil::IsNullOrWhiteSpace(env))
            {
                env = PathUtil::Normalize(env);
                if (Paths::IsDirectory(env)) return Paths::FullPath(env);
            }
        }
        std::vector<std::string> dirs = SearchDirs(toolDir, rideRoot);
        std::string resources = Paths::ResourcesDirectory();
        if (!resources.empty() && !Has(dirs, resources)) dirs.push_back(resources);
        for (const std::string& d : dirs)
        {
            const bool which[] = { debug, false };
            for (bool w : which)
            {
                std::string rel = lib->AssemblyDirectoryFor(w);
                if (rel.empty()) continue;
                std::string dir = Paths::Combine(d, PathUtil::Normalize(rel));
                if (Paths::IsDirectory(dir)) return dir;
            }
        }
        return std::string();
    }

    // ---- the lines RIDE shows ----------------------------------------------------------
    std::string RideToolchain::ShownCompileCommand(RideTool t, const std::string& program, const std::string& source, Language lang,
                                                   Target target, const std::string& configFlags, const std::string& assemblerFlag,
                                                   const std::string& includeFlags)
    {
        ToolDefinitionPtr d = Definition(t);
        if (!d) return program + " " + source;
        OperationDefinitionPtr op = d->Operation("compileToAssembly");
        std::string shown = op ? (!op->Shown.empty() ? op->Shown : op->Args) : std::string();
        return Tidy(program + " " + Fill(shown, t, source, lang, target, configFlags, assemblerFlag, includeFlags));
    }

    std::string RideToolchain::ShownRunCommand(RideTool t, const std::string& program, const std::string& source, Language lang,
                                               Target target, const std::string& configFlags, const std::string& assemblerFlag,
                                               const std::string& includeFlags, bool emulate)
    {
        TargetDefinitionPtr td = TargetDef(target);
        ToolDefinitionPtr d = Definition(t);
        if (td && td->Emulated && UsesArch(t))
        {
            std::string chain = (emulate && !td->ShownEmulate.empty()) ? td->ShownEmulate : td->ShownRun;
            chain = StringUtil::Replace(chain, "$(Compiler)", program);
            chain = StringUtil::Replace(chain, "$(Assembler)", ToolName(td, "assembler"));
            chain = StringUtil::Replace(chain, "$(Linker)", ToolName(td, "linker"));
            chain = StringUtil::Replace(chain, "$(Simulator)", ToolName(td, "simulator"));
            chain = StringUtil::Replace(chain, "$(Emulator)", ToolName(td, "emulator"));
            std::string shmrt;
            if (t == RideTool::Shalimar)
            {
                LibraryDefinitionPtr lib = Defs()->LibraryByKind(td->Arch, "shalimarRuntime");
                if (lib) shmrt = StringUtil::ReplaceChar(lib->AssemblyDirectoryFor(false), '\\', '/');
            }
            chain = StringUtil::Replace(chain, "$(ShalimarRuntimeDir)", shmrt);
            return Tidy(Fill(chain, t, source, lang, target, configFlags, assemblerFlag, includeFlags));
        }
        OperationDefinitionPtr op = d ? d->Operation("compileAndLink") : nullptr;
        std::string shown = (op && !op->Shown.empty()) ? op->Shown
                            : (td ? StringUtil::Replace(td->ShownRun, "$(Compiler)", "") : std::string());
        return Tidy(program + " " + Fill(shown, t, source, lang, target, configFlags, assemblerFlag, includeFlags));
    }

    std::string RideToolchain::LaunchCommand(const std::string& program, bool shalimar, const std::vector<std::string>& args,
                                             const std::string& emulator, const std::string& shalimarRuntimeDir)
    {
        if (IsEmulatedProgram(program)) return EmulateCommand(emulator, program, shalimar, args, shalimarRuntimeDir);
        std::string quoted;
        for (const std::string& a : args) Append(quoted, Quote(a));
        return Tidy(Quote(program) + " " + quoted);
    }

    std::string RideToolchain::EmulateCommand(const std::string& emulator, const std::string& program, bool shalimar,
                                              const std::vector<std::string>& args, const std::string& shalimarRuntimeDir)
    {
        std::string quoted;
        for (const std::string& a : args) Append(quoted, Quote(a));
        TargetDefinitionPtr td = EmulatedTarget();
        ToolDefinitionPtr vm = td ? Defs()->Tool(td->ToolIdFor("emulator")) : nullptr;
        std::string templ = (!vm || !vm->Operation("launch")) ? std::string("\"$(Program)\" $(ShalimarRuntimeDir) $(Arguments)")
                                                               : vm->OperationArgs("launch");
        std::string line = StringUtil::Replace(templ, "$(Program)", program);
        line = StringUtil::Replace(line, "$(ShalimarRuntimeDir)",
                                   (shalimar && !shalimarRuntimeDir.empty()) ? Quote(shalimarRuntimeDir) : std::string());
        line = StringUtil::Replace(line, "$(Arguments)", quoted);
        return Tidy(Quote(emulator) + " " + line);
    }

    std::string RideToolchain::SimulationMissing(const std::string& simulator, const std::string& out)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        std::string sim = ToolName(td, "simulator");
        if (sim.empty()) sim = "the simulator";
        if (simulator.empty() || !Paths::IsFile(simulator))
            return "no " + sim + " beside GRIDEm - the C6747 simulator ships with it from 5.0";
        if (!Paths::IsFile(out))
        {
            LibraryDefinitionPtr rts = td ? Defs()->Library(td->Runtime) : nullptr;
            return "no " + Paths::FileName(out) + ": a " + (td ? td->Arch : std::string("tms6747")) + " build links one only with "
                   + ToolName(td, "assembler") + " and " + ToolName(td, "linker") + " beside GRIDEm and a runtime - "
                   + (rts ? rts->Name : std::string("RTS6x")) + " in "
                   + (rts ? StringUtil::ReplaceChar(rts->Directory, '\\', '/') : std::string("lib/rts6x-tms6747"))
                   + " beside it, or TI's named under Settings";
        }
        return std::string();
    }

    bool RideToolchain::IsEmulatedProgram(const std::string& program)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        if (!td || program.empty()) return false;
        std::string stripped = program;
        while (!stripped.empty() && (stripped.back() == '/' || stripped.back() == '\\')) stripped.pop_back();
        std::string leaf = Paths::FileName(PathUtil::Normalize(stripped));
        return (!td->EmulatedProgramExtension.empty() && StringUtil::EndsWithIgnoreCase(leaf, td->EmulatedProgramExtension))
               || (!td->AssemblyExtension.empty() && StringUtil::EndsWithIgnoreCase(leaf, td->AssemblyExtension));
    }

    std::string RideToolchain::SimulateCommand(const std::string& simulator, const std::string& out)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        ToolDefinitionPtr sim = td ? Defs()->Tool(td->ToolIdFor("simulator")) : nullptr;
        std::string args = sim ? sim->OperationArgs("simulate") : std::string("\"$(Program)\"");
        return Tidy(Quote(simulator) + " " + StringUtil::Replace(args, "$(Program)", out));
    }

    std::string RideToolchain::TiProgramOf(const std::string& program)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        TargetDefinitionPtr host = Defs()->HostTarget();
        std::string stem = program;
        while (!stem.empty() && (stem.back() == '/' || stem.back() == '\\')) stem.pop_back();
        std::vector<std::string> exts;
        if (td)
        {
            exts.push_back(td->EmulatedProgramExtension);
            exts.push_back(td->AssemblyExtension);
            exts.push_back(td->ProgramExtension);
        }
        if (host) exts.push_back(host->ProgramExtension);
        for (const std::string& e : exts)
            if (!e.empty() && StringUtil::EndsWithIgnoreCase(stem, e)) { stem = stem.substr(0, stem.size() - e.size()); break; }
        return stem + (td ? td->ProgramExtension : std::string(".out"));
    }

    std::string RideToolchain::RunnerLine(bool simulator, const std::string& program)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        ToolDefinitionPtr tool = td ? Defs()->Tool(td->ToolIdFor(simulator ? "simulator" : "emulator")) : nullptr;
        std::string stripped = program;
        while (!stripped.empty() && (stripped.back() == '/' || stripped.back() == '\\')) stripped.pop_back();
        std::string file = Paths::FileName(PathUtil::Normalize(simulator ? TiProgramOf(program) : stripped));
        if (!tool) return "$ " + file;
        OperationDefinitionPtr op = tool->Operation(simulator ? "simulate" : "emulate");
        std::string shown = op ? (!op->Shown.empty() ? op->Shown : op->Args) : std::string("$(Program)");
        shown = StringUtil::Replace(StringUtil::Replace(shown, "$(Program)", file), "$(AsmFiles)", file);
        return Tidy("$ " + tool->Name + " " + shown);
    }

    std::string RideToolchain::ToolName(const TargetDefinitionPtr& td, const std::string& role)
    {
        ToolDefinitionPtr d = td ? Defs()->Tool(td->ToolIdFor(role)) : nullptr;
        return d ? d->Name : std::string();
    }

    std::string RideToolchain::TiLinkCmd()
    {
        TargetDefinitionPtr td = EmulatedTarget();
        LinkerCommandFileDefinitionPtr f = td ? Defs()->LinkerCommandFile(td->LinkerCommandFile) : nullptr;
        return f ? f->Text() : std::string();
    }

    std::string RideToolchain::TiLinkCmdName()
    {
        TargetDefinitionPtr td = EmulatedTarget();
        LinkerCommandFileDefinitionPtr f = td ? Defs()->LinkerCommandFile(td->LinkerCommandFile) : nullptr;
        return f ? f->Name : std::string();
    }

    std::string RideToolchain::RtsLibrary(bool debug)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        LibraryDefinitionPtr lib = td ? Defs()->Library(td->Runtime) : nullptr;
        return lib ? lib->FileFor(debug) : std::string();
    }

    bool RideToolchain::IsRtsAlias(const std::string& lib)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        LibraryDefinitionPtr rts = td ? Defs()->Library(td->Runtime) : nullptr;
        return rts && rts->IsAlias(lib);
    }

    std::string RideToolchain::ShalimarRtsLibrary(bool debug)
    {
        TargetDefinitionPtr td = EmulatedTarget();
        LibraryDefinitionPtr lib = td ? Defs()->LibraryByKind(td->Arch, "shalimarRuntime") : nullptr;
        return lib ? lib->FileFor(debug) : std::string();
    }

    std::string RideToolchain::HostShalimarRuntime(Target target, bool debug, const std::string& toolDir, const std::string& rideRoot)
    {
        LibraryDefinitionPtr lib = Defs()->LibraryByKind(Arch(target), "shalimarRuntime");
        if (!lib || lib->FileFor(debug).empty()) return std::string();
        std::string dir = FindLibraryDir(lib, toolDir, rideRoot);
        if (dir.empty()) return std::string();
        std::string file = Paths::Combine(dir, lib->FileFor(debug));
        return Paths::IsFile(file) ? file : std::string();
    }

    std::string RideToolchain::CrtLibraries(Target target, bool debug)
    {
        LibraryDefinitionPtr lib = Defs()->LibraryByKind(Arch(target), "hostCrt");
        return lib ? lib->FileFor(debug) : std::string();
    }

    std::string RideToolchain::WindowsCrtLibraries(bool debug)
    {
        // the CRT of the Windows target, wherever GRIDEm runs
        TargetDefinitionPtr win = Defs()->TargetOf(Target::Windows);
        if (!win) return std::string();
        return CrtLibraries(Target::Windows, debug);
    }

    // ---- private ------------------------------------------------------------------------
    void RideToolchain::CompilersUnder(const std::string& root, const std::string& under, const CcsDefinition& cd,
                                       std::vector<std::string>& into)
    {
        std::vector<std::string> dirs;
        dirs.push_back(root);
        for (const std::string& part : StringUtil::SplitNonEmpty(under, "\\/"))
        {
            std::vector<std::string> next;
            for (const std::string& d : dirs)
            {
                if (!Paths::IsDirectory(d)) continue;
                if (StringUtil::EndsWith(part, "*"))
                {
                    std::string prefix = part.substr(0, part.size() - 1);
                    for (const std::string& sub : Paths::ListDirectories(d))
                        if (StringUtil::StartsWithIgnoreCase(sub, prefix)) next.push_back(Paths::Combine(d, sub));
                }
                else next.push_back(Paths::Combine(d, part));
            }
            dirs = next;
        }
        for (const std::string& d : dirs)
        {
            if (!Paths::IsDirectory(d)) continue;
            for (const std::string& leaf : Paths::ListDirectories(d))
                for (StringMap::const_iterator kv = cd.CompilerDirs.begin(); kv != cd.CompilerDirs.end(); ++kv)
                    if (!kv->second.empty() && StringUtil::StartsWithIgnoreCase(leaf, kv->second))
                    {
                        into.push_back(Paths::Combine(d, leaf));
                        break;
                    }
        }
    }

    std::vector<int> RideToolchain::VersionOf(const std::string& name)
    {
        std::vector<int> parts;
        size_t at = name.find_last_of('_');
        size_t i = at == std::string::npos ? 0 : at + 1;
        while (i < name.size())
        {
            if (!StringUtil::IsDigit(name[i])) { i++; continue; }
            int n = 0;
            while (i < name.size() && StringUtil::IsDigit(name[i])) n = n * 10 + (name[i++] - '0');
            parts.push_back(n);
        }
        return parts;
    }

    bool RideToolchain::Newer(const std::vector<int>& a, const std::vector<int>& b)
    {
        size_t n = std::max(a.size(), b.size());
        for (size_t i = 0; i < n; i++)
        {
            int x = i < a.size() ? a[i] : -1, y = i < b.size() ? b[i] : -1;
            if (x != y) return x > y;
        }
        return false;
    }

    bool RideToolchain::HasMarker(const std::string& dir, const std::vector<std::string>& markers)
    {
        if (dir.empty() || !Paths::IsDirectory(dir)) return false;
        for (const std::string& m : markers)
        {
            if (Paths::IsFile(Paths::Combine(dir, m))) return true;
            // a marker named for Windows ("c90.exe") is there on the Mac without .exe
            if (StringUtil::EndsWithIgnoreCase(m, ".exe") && Paths::IsFile(Paths::Combine(dir, m.substr(0, m.size() - 4)))) return true;
        }
        return false;
    }

    bool RideToolchain::HasFile(const std::string& dir, const std::string& marker)
    {
        return marker.empty() || Paths::IsFile(Paths::Combine(dir, marker));
    }

    std::vector<std::string> RideToolchain::SearchDirs(const std::string& toolDir, const std::string& rideRoot)
    {
        std::vector<std::string> dirs;
        if (!StringUtil::IsNullOrWhiteSpace(toolDir)) dirs.push_back(PathUtil::Normalize(Environment::Expand(toolDir)));
        std::string ide = IdeDirectory();
        if (!ide.empty()) dirs.push_back(ide);
        std::string ride = RideBin(rideRoot);
        if (!ride.empty()) dirs.push_back(ride);
        return dirs;
    }

    std::string RideToolchain::Fill(const std::string& templ, RideTool t, const std::string& source, Language lang,
                                    Target target, const std::string& configFlags, const std::string& assemblerFlag,
                                    const std::string& includeFlags)
    {
        std::string s = StringUtil::Replace(templ, "$(Source)", source);
        s = StringUtil::Replace(s, "$(ArchFlag)", ArchFlag(t, target));
        s = StringUtil::Replace(s, "$(ConfigFlags)", configFlags);
        s = StringUtil::Replace(s, "$(AsmSyntaxFlag)", assemblerFlag);
        s = StringUtil::Replace(s, "$(IncludeFlags)", includeFlags);
        s = StringUtil::Replace(s, "$(LangFlags)", LanguageFlags(t, lang));
        s = StringUtil::Replace(s, "$(DefineFlags)", "");
        s = StringUtil::Replace(s, "$(CompileFlags)", "");
        s = StringUtil::Replace(s, "$(LibFlags)", "");
        return s;
    }

    std::string RideToolchain::Tidy(const std::string& s)
    {
        return StringUtil::Join(" ", StringUtil::SplitNonEmpty(s, " "));
    }

    void RideToolchain::Append(std::string& sb, const std::string& s)
    {
        if (s.empty()) return;
        if (!sb.empty()) sb += ' ';
        sb += s;
    }
}}
