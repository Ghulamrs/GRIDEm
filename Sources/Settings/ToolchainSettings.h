// =============================================================================
//  ToolchainSettings.h - tool paths and command templates for ONE target
//
//  Every build step is a command-line template with $(Macros), e.g.
//     "$(Compiler)" -S "$(Source)" -o "$(AsmFile)" $(ArchFlag) $(ConfigFlags)
//  The defaults come from toolchain.json (ToolchainDefinitions): the tool each role names
//  for the target, its operations' argument templates, the target's extensions and flag
//  spellings, the runtime library's directory. A value still at its default is not written
//  to the settings file, so it follows the definitions; a value the user set is kept.
//  (GRIDEd's Settings/ToolchainSettings.h)
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"
#include "../Services/RideToolchain.h"
#include "ToolchainDefinitions.h"
#include <memory>
#include <string>
#include <vector>

namespace GRIDEm { namespace Settings {

    class ToolchainSettings;
    typedef std::shared_ptr<ToolchainSettings> ToolchainSettingsPtr;

    class ToolchainSettings
    {
    public:
        explicit ToolchainSettings(Core::Target t);

        // RIDE's installation root or bin folder, when the settings name one (GrideSettings sets it)
        static std::string RideRoot;

        Core::Target TargetKind;
        std::string ToolDir;           // folder used for tool names that are not absolute
        std::string CompilerC90;
        std::string CompilerCpp11;
        std::string CompilerShalimar;
        std::string CompilerCl;        // the machine's own compiler (Windows only)
        std::string Assembler;         // masm (x86_64-windows), asm6x (tms6747), as (darwin)
        std::string Linker;            // link (x86_64-windows), lnk6x (tms6747), cc (darwin)
        std::string DebugAssembler;    // clang, for a Debug build's CodeView on x86_64-windows
        std::string DebugLinker;       // Microsoft's link.exe
        std::string Runtime;           // a runtime library file handed to the link
        std::string RuntimeDir;        // RTS6x's directory, lnk6x's -i (lib/rts6x-tms6747)
        std::string Simulator;         // sim6747: runs the linked .out
        std::string Emulator;          // vm6747: runs the assembly, and is the tms6747 debugger (--debug)
        std::string Debugger;          // cdb for x86_64-windows, lldb on the Mac
        std::vector<std::string> IncludeDirs;
        std::vector<std::string> LibDirs;
        // command templates
        std::string CompileCmd;        // c90, cpp11
        std::string CompileCmdCl;      // cl
        std::string CompileCmdShalimar; // shalimar: every file of the program in one command ($(Sources)); empty = CompileCmd
        std::string AssembleCmd;
        std::string LinkCmd;
        std::string DebugAssembleCmd;  // the CodeView path of a Debug build (x86_64-windows)
        std::string DebugLinkCmd;
        std::string RunCmd;
        std::string EmulateCmd;        // tms6747: the assembly on vm6747
        std::string DebugCmd;
        // flag spellings, because MASM/LINK and TI style tools differ
        std::string IncludeFlag;       // -I
        std::string DefineFlag;        // -D
        std::string LibDirFlag;        // -L  or /LIBPATH:
        std::string LibFlag;           // -l  or (empty)
        std::string AsmExt;            // .s / .asm
        std::string ObjExt;            // .obj / .o
        // how the tools of this target are started
        std::string SetupScript;       // vcvars64.bat on Windows; kept and written back, not run on the Mac
        std::vector<std::string> EnvironmentVars;   // "NAME=value" (%VAR% expanded), added to the tool environment
        int OutputCodePage;            // code page of the tool output, 0 = the system's (UTF-8 on the Mac)
        std::string ErrorPattern;      // extra regex with groups file, line, col, sev, code, msg

        std::string Name() const;
        std::string Arch() const;

        std::string CompilerFor(Core::Language l) const;
        std::string CompilerFor(Services::RideTool t) const;

        // tool name -> full path: an absolute name as it is; a name toolchain.json knows is looked
        // for as its lookup says; another name in the tool folder, beside the IDE, the installation,
        // PATH; when nowhere, the name under ToolDir, so a "not found" status still names a place
        std::string Resolve(const std::string& tool) const;
        // the same for a role: the role's own tool definition is asked first ("" role: none)
        std::string ResolveFor(const std::string& role, const std::string& tool) const;
        bool ToolExists(const std::string& tool) const;
        static std::string EnvironmentVariableFor(const std::string& tool);
        std::string LookedIn(const std::string& tool) const;
        std::string ResolveRuntimeDir() const;

        // everything from toolchain.json
        void ApplyDefaults();
        ToolchainSettingsPtr Defaults() const;
        // after the definitions changed: every value still at the defaults they gave before follows the new ones
        void Rebase(const ToolchainSettingsPtr& before);
        // only what differs from the definitions' defaults is written
        Core::JsonPtr ToJson() const;
        static ToolchainSettingsPtr FromJson(Core::Target t, const Core::JsonPtr& o);
        ToolchainSettingsPtr Clone() const;

    private:
        static ToolchainSettingsPtr OldDefaults(Core::Target t);
    };
}}
