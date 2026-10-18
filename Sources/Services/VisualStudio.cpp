// =============================================================================
//  VisualStudio.cpp - see VisualStudio.h
// =============================================================================
#include "VisualStudio.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"

namespace GRIDEm { namespace Services {

    using namespace GRIDEm::Core;
    using Platform::Paths;

    const char* const VisualStudio::WindowsOnly = "Visual Studio is Windows only - not on this machine";

    static std::string namedVcvars;

    std::vector<std::string> VsInstallation::BinDirs() const
    {
        std::vector<std::string> r;
        const std::string* dirs[] = { &MsvcBin, &LlvmBin, &KitBin };
        for (const std::string* d : dirs)
            if (!d->empty() && Paths::IsDirectory(*d)) r.push_back(*d);
        return r;
    }

    std::vector<std::string> VsInstallation::EnvironmentLines(const Settings::VisualStudioDefinition& d) const
    {
        std::vector<std::string> r;
        for (Settings::StringMap::const_iterator kv = d.Environment.begin(); kv != d.Environment.end(); ++kv)
        {
            std::vector<std::string> dirs;
            std::string name = StringUtil::ToUpper(kv->first);
            if (name == "INCLUDE")
            {
                if (!MsvcInclude.empty()) dirs.push_back(MsvcInclude);
                dirs.insert(dirs.end(), KitIncludes.begin(), KitIncludes.end());
            }
            else if (name == "LIB")
            {
                if (!MsvcLib.empty()) dirs.push_back(MsvcLib);
                dirs.insert(dirs.end(), KitLibs.begin(), KitLibs.end());
            }
            else if (name == "PATH")
            {
                std::vector<std::string> b = BinDirs();
                dirs.insert(dirs.end(), b.begin(), b.end());
            }
            if (dirs.empty()) continue;
            r.push_back(kv->first + "=" + StringUtil::Join(";", dirs) + ";%" + kv->first + "%");
        }
        return r;
    }

    std::string VsInstallation::Describe() const
    {
        return "Visual Studio " + Root + " (MSVC " + MsvcVersion
               + (PinnedVersion.empty() ? std::string() : " as msvcVersion " + PinnedVersion + " asks")
               + (LlvmBin.empty() ? std::string() : ", clang")
               + (KitVersion.empty() ? std::string(", no Windows SDK") : ", Windows SDK " + KitVersion) + ")"
               + (Note.empty() ? std::string() : " - " + Note);
    }

    std::string VisualStudio::NamedVcvars() { return namedVcvars; }

    void VisualStudio::SetNamedVcvars(const std::string& v) { namedVcvars = v; }

    VsInstallationPtr VisualStudio::Find() { return nullptr; }

    void VisualStudio::Forget() {}

    bool VisualStudio::FindTool(const std::vector<std::string>& names, std::string& path)
    {
        (void)names;
        path.clear();
        return false;
    }

    std::vector<std::string> VisualStudio::EnvironmentLines() { return std::vector<std::string>(); }

    std::string VisualStudio::Where() { return "no Visual Studio was found (Windows only)"; }
}}
