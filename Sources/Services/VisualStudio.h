// =============================================================================
//  VisualStudio.h - where Visual Studio's tools are (cl, ml64, Microsoft's
//  link.exe, clang) and the directories their environment needs. Windows only:
//  on the Mac there is no Visual Studio, so every question here answers "not on
//  this machine". The types and the named vcvars stay, so settings and
//  toolchain.json keep their fields and write them back unchanged.
//  (GRIDEd's Services/VisualStudio.h)
// =============================================================================
#pragma once
#include "../Settings/ToolchainDefinitions.h"
#include <memory>
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    class VsInstallation
    {
    public:
        std::string Root;
        std::string MsvcDir;
        std::string MsvcVersion;
        std::string PinnedVersion;   // the file's msvcVersion when it chose the version, else ""
        std::string Note;            // why the pin was not followed, or ""
        std::string MsvcBin;
        std::string MsvcLib;
        std::string MsvcInclude;
        std::string LlvmBin;
        std::string KitRoot;
        std::string KitVersion;
        std::string KitBin;
        std::vector<std::string> KitLibs;
        std::vector<std::string> KitIncludes;
        std::string Vcvars;

        std::vector<std::string> BinDirs() const;
        std::vector<std::string> EnvironmentLines(const Settings::VisualStudioDefinition& d) const;
        std::string Describe() const;
    };
    typedef std::shared_ptr<VsInstallation> VsInstallationPtr;

    class VisualStudio
    {
    public:
        // RIDE's settings.json "vcvars": kept and written back; never used on the Mac
        static std::string NamedVcvars();
        static void SetNamedVcvars(const std::string& v);

        // always null on the Mac
        static VsInstallationPtr Find();
        static void Forget();
        // a tool of Visual Studio's by its names: false on the Mac
        static bool FindTool(const std::vector<std::string>& names, std::string& path);
        static std::vector<std::string> EnvironmentLines();
        static std::string Where();
        // what is said where Visual Studio would be needed
        static const char* const WindowsOnly;
    };
}}
