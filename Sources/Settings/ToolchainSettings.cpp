// =============================================================================
//  ToolchainSettings.cpp - see ToolchainSettings.h
// =============================================================================
#include "ToolchainSettings.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include <algorithm>

namespace GRIDEm { namespace Settings {

    using namespace GRIDEm::Core;
    using namespace GRIDEm::Services;
    using Platform::Paths;
    using Platform::Environment;

    std::string ToolchainSettings::RideRoot;

    ToolchainSettings::ToolchainSettings(Target t) : TargetKind(t), OutputCodePage(0)
    {
        ApplyDefaults();
    }

    std::string ToolchainSettings::Name() const { return TargetInfo::Name(TargetKind); }
    std::string ToolchainSettings::Arch() const { return RideToolchain::Arch(TargetKind); }

    std::string ToolchainSettings::CompilerFor(Language l) const
    {
        switch (l)
        {
        case Language::Cpp11:    return CompilerCpp11;
        case Language::Shalimar: return CompilerShalimar;
        default:                 return CompilerC90;
        }
    }

    std::string ToolchainSettings::CompilerFor(RideTool t) const
    {
        switch (t)
        {
        case RideTool::Cpp11:    return CompilerCpp11;
        case RideTool::Shalimar: return CompilerShalimar;
        case RideTool::Cl:
        case RideTool::HostCxx:  return CompilerCl;
        default:                 return CompilerC90;
        }
    }

    std::string ToolchainSettings::Resolve(const std::string& tool) const { return ResolveFor(std::string(), tool); }

    std::string ToolchainSettings::ResolveFor(const std::string& role, const std::string& tool) const
    {
        if (tool.empty()) return std::string();
        std::string t = Environment::Expand(tool);
        if (Paths::IsRooted(t)) return Paths::ExpandHome(t);
        std::string dir = ToolDir.empty() ? std::string() : PathUtil::Normalize(Environment::Expand(ToolDir));
        std::string under = !dir.empty() ? Paths::Combine(dir, PathUtil::Normalize(t)) : t;
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        ToolDefinitionPtr d = role.empty() ? nullptr : defs->ToolFor(TargetKind, role);
        if (!d || !d->HasName(t)) d = defs->ToolByName(t);
        std::string found;
        if (d) found = RideToolchain::FindTool(d, dir, RideRoot);
        else
        {
            if (Paths::Exists(under)) return under;
            found = RideToolchain::Find(std::vector<std::string>(1, t), std::string(), dir, RideRoot);
        }
        return !found.empty() ? found : under;
    }

    bool ToolchainSettings::ToolExists(const std::string& tool) const
    {
        std::string p = Resolve(tool);
        return !p.empty() && Paths::IsFile(p);
    }

    std::string ToolchainSettings::EnvironmentVariableFor(const std::string& tool)
    {
        ToolDefinitionPtr d = ToolchainDefinitions::Current()->ToolByName(tool);
        return d ? d->Lookup.Env : std::string();
    }

    std::string ToolchainSettings::LookedIn(const std::string& tool) const
    {
        ToolDefinitionPtr d = ToolchainDefinitions::Current()->ToolByName(tool);
        std::string dir = ToolDir.empty() ? std::string() : Environment::Expand(ToolDir);
        return RideToolchain::SearchPlaces(d, dir, RideRoot);
    }

    std::string ToolchainSettings::ResolveRuntimeDir() const
    {
        if (!StringUtil::IsNullOrWhiteSpace(RuntimeDir))
        {
            std::string r = Resolve(RuntimeDir);
            if (Paths::IsRooted(r) && Paths::IsDirectory(r)) return r;
        }
        std::string found = RideToolchain::FindRuntimeDir(ToolDir, RideRoot);
        if (!found.empty()) return found;
        return StringUtil::IsNullOrWhiteSpace(RuntimeDir) ? std::string() : Resolve(RuntimeDir);
    }

    static ToolDefinitionPtr Role(const ToolchainDefinitionsPtr& defs, const TargetDefinitionPtr& td, const std::string& role)
    {
        return td ? defs->Tool(td->ToolIdFor(role)) : nullptr;
    }

    static std::string ExeOf(const ToolDefinitionPtr& d) { return d ? d->Exe : std::string(); }

    // "<macro of the tool>" followed by the operation's arguments; nothing when the tool or the operation is not defined
    static std::string Template(const std::string& macro, const ToolDefinitionPtr& d, const std::string& operation)
    {
        if (!d || !d->Operation(operation)) return std::string();
        std::string args = StringUtil::Trim(d->OperationArgs(operation));
        return "\"$(" + macro + ")\"" + (!args.empty() ? " " + args : std::string());
    }

    void ToolchainSettings::ApplyDefaults()
    {
        IncludeDirs.clear();
        LibDirs.clear();
        EnvironmentVars.clear();
        SetupScript.clear();
        OutputCodePage = 0;
        ErrorPattern.clear();
        Runtime.clear();
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        TargetDefinitionPtr td = defs->TargetOf(TargetKind);
        ToolDefinitionPtr cc = Role(defs, td, "cCompiler");
        ToolDefinitionPtr cxx = Role(defs, td, "cppCompiler");
        ToolDefinitionPtr shc = Role(defs, td, "shalimarCompiler");
        ToolDefinitionPtr host = Role(defs, td, "hostCompiler");
        ToolDefinitionPtr as = Role(defs, td, "assembler");
        ToolDefinitionPtr ld = Role(defs, td, "linker");
        ToolDefinitionPtr das = Role(defs, td, "debugAssembler");
        ToolDefinitionPtr dld = Role(defs, td, "debugLinker");
        ToolDefinitionPtr sim = Role(defs, td, "simulator");
        ToolDefinitionPtr emu = Role(defs, td, "emulator");
        ToolDefinitionPtr dbg = Role(defs, td, "debugger");
        CompilerC90 = ExeOf(cc);
        CompilerCpp11 = ExeOf(cxx);
        CompilerShalimar = ExeOf(shc);
        CompilerCl = ExeOf(host);
        Assembler = ExeOf(as);
        Linker = ExeOf(ld);
        DebugAssembler = ExeOf(das);
        DebugLinker = ExeOf(dld);
        Simulator = ExeOf(sim);
        Emulator = ExeOf(emu);
        Debugger = ExeOf(dbg);
        LibraryDefinitionPtr rts = td ? defs->Library(td->Runtime) : nullptr;
        RuntimeDir = rts ? rts->Directory : std::string();
        AsmExt = td ? td->AssemblyExtension : std::string(".s");
        ObjExt = td ? td->ObjectExtension : std::string(".o");
        LibDirFlag = td ? td->LibraryDirFlag : std::string();
        LibFlag = td ? td->LibraryFlag : std::string();
        IncludeFlag = cc ? cc->IncludeFlag : std::string();
        DefineFlag = cc ? cc->DefineFlag : std::string();
        CompileCmd = Template("Compiler", cc, "compileToAssembly");
        CompileCmdCl = Template("Compiler", host, "compileToAssembly");
        CompileCmdShalimar = Template("Compiler", shc, "compileToAssembly");
        AssembleCmd = Template("Assembler", as, "assemble");
        LinkCmd = Template("Linker", ld, "link");
        DebugAssembleCmd = Template("DebugAssembler", das, "assemble");
        DebugLinkCmd = Template("DebugLinker", dld, "link");
        EmulateCmd = Template("Emulator", emu, "emulate");
        std::string run = td ? td->Run : std::string("none");
        if (run == "program") RunCmd = "\"$(Program)\"";
        else if (run == "simulator") RunCmd = Template("Simulator", sim, "simulate");
        else RunCmd.clear();
        std::string debug = td ? td->Debug : std::string("none");
        if (debug == "simulator") DebugCmd = Template("Simulator", sim, "debug");
        else if (debug == "debugger") DebugCmd = Template("Debugger", dbg, "debug");
        else DebugCmd.clear();
        std::string rideBin = RideToolchain::RideBin(RideRoot);
        ToolDir = !rideBin.empty() ? rideBin : RideToolchain::DefaultRideBin();
    }

    ToolchainSettingsPtr ToolchainSettings::Defaults() const { return std::make_shared<ToolchainSettings>(TargetKind); }

    static std::string Follow(const std::string& value, const std::string& before, const std::string& now)
    {
        return value == before ? now : value;
    }

    void ToolchainSettings::Rebase(const ToolchainSettingsPtr& before)
    {
        ToolchainSettingsPtr now = Defaults();
        if (!before) return;
        ToolDir = Follow(ToolDir, before->ToolDir, now->ToolDir);
        CompilerC90 = Follow(CompilerC90, before->CompilerC90, now->CompilerC90);
        CompilerCpp11 = Follow(CompilerCpp11, before->CompilerCpp11, now->CompilerCpp11);
        CompilerShalimar = Follow(CompilerShalimar, before->CompilerShalimar, now->CompilerShalimar);
        CompilerCl = Follow(CompilerCl, before->CompilerCl, now->CompilerCl);
        Assembler = Follow(Assembler, before->Assembler, now->Assembler);
        Linker = Follow(Linker, before->Linker, now->Linker);
        DebugAssembler = Follow(DebugAssembler, before->DebugAssembler, now->DebugAssembler);
        DebugLinker = Follow(DebugLinker, before->DebugLinker, now->DebugLinker);
        RuntimeDir = Follow(RuntimeDir, before->RuntimeDir, now->RuntimeDir);
        Simulator = Follow(Simulator, before->Simulator, now->Simulator);
        Emulator = Follow(Emulator, before->Emulator, now->Emulator);
        Debugger = Follow(Debugger, before->Debugger, now->Debugger);
        CompileCmd = Follow(CompileCmd, before->CompileCmd, now->CompileCmd);
        CompileCmdCl = Follow(CompileCmdCl, before->CompileCmdCl, now->CompileCmdCl);
        CompileCmdShalimar = Follow(CompileCmdShalimar, before->CompileCmdShalimar, now->CompileCmdShalimar);
        AssembleCmd = Follow(AssembleCmd, before->AssembleCmd, now->AssembleCmd);
        LinkCmd = Follow(LinkCmd, before->LinkCmd, now->LinkCmd);
        DebugAssembleCmd = Follow(DebugAssembleCmd, before->DebugAssembleCmd, now->DebugAssembleCmd);
        DebugLinkCmd = Follow(DebugLinkCmd, before->DebugLinkCmd, now->DebugLinkCmd);
        RunCmd = Follow(RunCmd, before->RunCmd, now->RunCmd);
        EmulateCmd = Follow(EmulateCmd, before->EmulateCmd, now->EmulateCmd);
        DebugCmd = Follow(DebugCmd, before->DebugCmd, now->DebugCmd);
        IncludeFlag = Follow(IncludeFlag, before->IncludeFlag, now->IncludeFlag);
        DefineFlag = Follow(DefineFlag, before->DefineFlag, now->DefineFlag);
        LibDirFlag = Follow(LibDirFlag, before->LibDirFlag, now->LibDirFlag);
        LibFlag = Follow(LibFlag, before->LibFlag, now->LibFlag);
        AsmExt = Follow(AsmExt, before->AsmExt, now->AsmExt);
        ObjExt = Follow(ObjExt, before->ObjExt, now->ObjExt);
    }

    static void Put(const JsonPtr& o, const std::string& key, const std::string& value, const std::string& def)
    {
        if (value != def) o->SetString(key, value);
    }

    JsonPtr ToolchainSettings::ToJson() const
    {
        ToolchainSettingsPtr d = Defaults();
        JsonPtr o = JsonValue::MakeObject();
        Put(o, "toolDir", ToolDir, d->ToolDir);
        JsonPtr comp = JsonValue::MakeObject();
        Put(comp, "C90", CompilerC90, d->CompilerC90);
        Put(comp, "C++11", CompilerCpp11, d->CompilerCpp11);
        Put(comp, "Shalimar", CompilerShalimar, d->CompilerShalimar);
        Put(comp, "cl", CompilerCl, d->CompilerCl);
        if (comp->Count() > 0) o->SetValue("compilers", comp);
        Put(o, "assembler", Assembler, d->Assembler);
        Put(o, "linker", Linker, d->Linker);
        Put(o, "debugAssembler", DebugAssembler, d->DebugAssembler);
        Put(o, "debugLinker", DebugLinker, d->DebugLinker);
        Put(o, "runtime", Runtime, d->Runtime);
        Put(o, "runtimeDir", RuntimeDir, d->RuntimeDir);
        Put(o, "simulator", Simulator, d->Simulator);
        Put(o, "emulator", Emulator, d->Emulator);
        Put(o, "debugger", Debugger, d->Debugger);
        if (!IncludeDirs.empty()) o->SetList("includeDirs", IncludeDirs);
        if (!LibDirs.empty()) o->SetList("libDirs", LibDirs);
        JsonPtr cmd = JsonValue::MakeObject();
        Put(cmd, "compile", CompileCmd, d->CompileCmd);
        Put(cmd, "compileCl", CompileCmdCl, d->CompileCmdCl);
        Put(cmd, "compileShalimar", CompileCmdShalimar, d->CompileCmdShalimar);
        Put(cmd, "assemble", AssembleCmd, d->AssembleCmd);
        Put(cmd, "link", LinkCmd, d->LinkCmd);
        Put(cmd, "debugAssemble", DebugAssembleCmd, d->DebugAssembleCmd);
        Put(cmd, "debugLink", DebugLinkCmd, d->DebugLinkCmd);
        Put(cmd, "run", RunCmd, d->RunCmd);
        Put(cmd, "emulate", EmulateCmd, d->EmulateCmd);
        Put(cmd, "debug", DebugCmd, d->DebugCmd);
        if (cmd->Count() > 0) o->SetValue("commands", cmd);
        JsonPtr flags = JsonValue::MakeObject();
        Put(flags, "include", IncludeFlag, d->IncludeFlag);
        Put(flags, "define", DefineFlag, d->DefineFlag);
        Put(flags, "libDir", LibDirFlag, d->LibDirFlag);
        Put(flags, "lib", LibFlag, d->LibFlag);
        Put(flags, "asmExt", AsmExt, d->AsmExt);
        Put(flags, "objExt", ObjExt, d->ObjExt);
        if (flags->Count() > 0) o->SetValue("flags", flags);
        JsonPtr run = JsonValue::MakeObject();
        Put(run, "setupScript", SetupScript, std::string());
        if (!EnvironmentVars.empty()) run->SetList("environment", EnvironmentVars);
        if (OutputCodePage != 0) run->SetNumber("outputCodePage", OutputCodePage);
        Put(run, "errorPattern", ErrorPattern, std::string());
        if (run->Count() > 0) o->SetValue("run", run);
        return o;
    }

    static std::string Upgrade(const std::string& value, const std::string& oldDefault, const std::string& newDefault)
    {
        if (StringUtil::EqualsIgnoreCase(StringUtil::Trim(value), oldDefault)) return newDefault;
        return value;
    }

    static std::vector<std::string> UpgradeList(const std::vector<std::string>& value, const std::vector<std::string>& oldDefault,
                                                const std::vector<std::string>& newDefault)
    {
        if (value.size() != oldDefault.size()) return value;
        for (size_t i = 0; i < value.size(); i++)
            if (!StringUtil::EqualsIgnoreCase(value[i], oldDefault[i])) return value;
        return newDefault;
    }

    ToolchainSettingsPtr ToolchainSettings::FromJson(Target t, const JsonPtr& o)
    {
        ToolchainSettingsPtr s = std::make_shared<ToolchainSettings>(t);
        if (!o || !o->IsObject()) return s;
        ToolchainSettingsPtr old = OldDefaults(t);
        s->ToolDir = Upgrade(o->GetString("toolDir", s->ToolDir), old->ToolDir, s->ToolDir);
        JsonPtr comp = o->GetObject("compilers");
        if (comp)
        {
            s->CompilerC90 = Upgrade(comp->GetString("C90", s->CompilerC90), old->CompilerC90, s->CompilerC90);
            s->CompilerCpp11 = Upgrade(comp->GetString("C++11", s->CompilerCpp11), old->CompilerCpp11, s->CompilerCpp11);
            s->CompilerShalimar = comp->GetString("Shalimar", s->CompilerShalimar);
            s->CompilerCl = comp->GetString("cl", s->CompilerCl);
        }
        s->Assembler = Upgrade(o->GetString("assembler", s->Assembler), old->Assembler, s->Assembler);
        s->Linker = Upgrade(o->GetString("linker", s->Linker), old->Linker, s->Linker);
        std::string debugAs = o->GetString("debugAssembler", s->DebugAssembler);
        if (!StringUtil::IsNullOrWhiteSpace(debugAs)) s->DebugAssembler = debugAs;
        std::string debugLd = o->GetString("debugLinker", s->DebugLinker);
        if (!StringUtil::IsNullOrWhiteSpace(debugLd)) s->DebugLinker = debugLd;
        s->Runtime = Upgrade(o->GetString("runtime", s->Runtime), old->Runtime, s->Runtime);
        s->RuntimeDir = o->GetString("runtimeDir", s->RuntimeDir);
        s->Simulator = Upgrade(o->GetString("simulator", s->Simulator), old->Simulator, s->Simulator);
        s->Emulator = o->GetString("emulator", s->Emulator);
        s->Debugger = o->GetString("debugger", s->Debugger);
        if (o->Has("includeDirs")) s->IncludeDirs = UpgradeList(o->GetStringList("includeDirs"), old->IncludeDirs, s->IncludeDirs);
        if (o->Has("libDirs")) s->LibDirs = UpgradeList(o->GetStringList("libDirs"), old->LibDirs, s->LibDirs);
        JsonPtr cmd = o->GetObject("commands");
        if (cmd)
        {
            s->CompileCmd = Upgrade(cmd->GetString("compile", s->CompileCmd), old->CompileCmd, s->CompileCmd);
            s->CompileCmdCl = cmd->GetString("compileCl", s->CompileCmdCl);
            s->CompileCmdShalimar = cmd->GetString("compileShalimar", s->CompileCmdShalimar);
            s->AssembleCmd = Upgrade(cmd->GetString("assemble", s->AssembleCmd), old->AssembleCmd, s->AssembleCmd);
            s->LinkCmd = Upgrade(cmd->GetString("link", s->LinkCmd), old->LinkCmd, s->LinkCmd);
            s->DebugAssembleCmd = cmd->GetString("debugAssemble", s->DebugAssembleCmd);
            s->DebugLinkCmd = cmd->GetString("debugLink", s->DebugLinkCmd);
            s->RunCmd = Upgrade(cmd->GetString("run", s->RunCmd), old->RunCmd, s->RunCmd);
            s->EmulateCmd = cmd->GetString("emulate", s->EmulateCmd);
            s->DebugCmd = Upgrade(cmd->GetString("debug", s->DebugCmd), old->DebugCmd, s->DebugCmd);
        }
        JsonPtr flags = o->GetObject("flags");
        if (flags)
        {
            s->IncludeFlag = flags->GetString("include", s->IncludeFlag);
            s->DefineFlag = flags->GetString("define", s->DefineFlag);
            s->LibDirFlag = Upgrade(flags->GetString("libDir", s->LibDirFlag), old->LibDirFlag, s->LibDirFlag);
            s->LibFlag = Upgrade(flags->GetString("lib", s->LibFlag), old->LibFlag, s->LibFlag);
            s->AsmExt = Upgrade(flags->GetString("asmExt", s->AsmExt), old->AsmExt, s->AsmExt);
            s->ObjExt = flags->GetString("objExt", s->ObjExt);
        }
        JsonPtr run = o->GetObject("run");
        if (run)
        {
            s->SetupScript = run->GetString("setupScript", s->SetupScript);
            if (run->Has("environment")) s->EnvironmentVars = run->GetStringList("environment");
            s->OutputCodePage = std::max(0, run->GetInt("outputCodePage", s->OutputCodePage));
            s->ErrorPattern = run->GetString("errorPattern", s->ErrorPattern);
        }
        return s;
    }

    ToolchainSettingsPtr ToolchainSettings::Clone() const { return FromJson(TargetKind, ToJson()); }

    // the defaults of the release before the RIDE port, so an untouched settings file follows the port
    ToolchainSettingsPtr ToolchainSettings::OldDefaults(Target t)
    {
        ToolchainSettingsPtr o = std::make_shared<ToolchainSettings>(t);
        o->CompilerC90 = "grc90.exe";
        o->CompilerCpp11 = "grcpp11.exe";
        o->Runtime.clear();
        o->Simulator.clear();
        o->Assembler.clear();
        o->Linker.clear();
        o->RunCmd.clear();
        o->DebugCmd.clear();
        o->AsmExt = ".asm";
        o->LibDirFlag = "-L";
        o->LibFlag = "-l";
        o->IncludeDirs.clear();
        o->LibDirs.clear();
        switch (t)
        {
        case Target::Windows:
            o->ToolDir = "C:\\GRIDE\\bin\\win";
            o->Assembler = "C:\\masm32\\bin\\ml.exe";
            o->Linker = "C:\\masm32\\bin\\link.exe";
            o->IncludeDirs.push_back("C:\\GRIDE\\inc\\win");
            o->LibDirs.push_back("C:\\GRIDE\\lib\\win");
            o->LibDirFlag = "/LIBPATH:";
            o->LibFlag.clear();
            o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=win $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
            o->AssembleCmd = "\"$(Assembler)\" /nologo /c /coff /Fo\"$(ObjFile)\" \"$(AsmFile)\"";
            o->LinkCmd = "\"$(Linker)\" /nologo /SUBSYSTEM:CONSOLE /OUT:\"$(Output)\" $(Objects) $(LibDirFlags) $(LibFlags) $(LinkFlags)";
            o->RunCmd = "\"$(Program)\"";
            break;
        case Target::Linux:
            o->ToolDir = "C:\\GRIDE\\bin\\linux";
            o->IncludeDirs.push_back("C:\\GRIDE\\inc\\linux");
            o->LibDirs.push_back("C:\\GRIDE\\lib\\linux");
            o->AsmExt = ".s";
            o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=linux $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
            o->AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" -o \"$(ObjFile)\"";
            o->LinkCmd = "\"$(Linker)\" $(Objects) -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
            break;
        case Target::MacOS:
            o->ToolDir = "C:\\GRIDE\\bin\\macos";
            o->IncludeDirs.push_back("C:\\GRIDE\\inc\\macos");
            o->LibDirs.push_back("C:\\GRIDE\\lib\\macos");
            o->AsmExt = ".s";
            o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=macos $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
            o->AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" -o \"$(ObjFile)\"";
            o->LinkCmd = "\"$(Linker)\" $(Objects) -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
            break;
        default:
            o->ToolDir = "C:\\GRIDE\\bin\\c6x";
            o->Assembler = "asm6x.exe";
            o->Linker = "lnk6x.exe";
            o->Runtime = "C:\\GRIDE\\lib\\c6x\\rts6747.lib";
            o->Simulator = "vm6747.exe";
            o->IncludeDirs.push_back("C:\\GRIDE\\inc\\c6x");
            o->LibDirs.push_back("C:\\GRIDE\\lib\\c6x");
            o->LibDirFlag = "-i";
            o->LibFlag = "-l";
            o->CompileCmd = "\"$(Compiler)\" \"$(Source)\" --target=c6747 $(IncludeFlags) $(DefineFlags) -$(Optimise) $(CompileFlags) -o \"$(AsmFile)\"";
            o->AssembleCmd = "\"$(Assembler)\" \"$(AsmFile)\" --output_file=\"$(ObjFile)\"";
            o->LinkCmd = "\"$(Linker)\" $(Objects) \"$(LinkerCmd)\" -o \"$(Output)\" $(LibDirFlags) $(LibFlags) $(LinkFlags)";
            o->RunCmd = "\"$(Simulator)\" \"$(Program)\"";
            o->DebugCmd = "\"$(Simulator)\" --debug \"$(Program)\"";
            break;
        }
        return o;
    }
}}
