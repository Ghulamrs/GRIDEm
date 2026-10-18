// =============================================================================
//  BuildConfiguration.h - settings of one "Config|Target" pair of a project,
//  e.g. "Debug|TMS320C6747". Stored inside the .pro under "configurations".
//  (GRIDEd's Model/BuildConfiguration.h)
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "../Core/LanguageTarget.h"
#include <memory>
#include <string>
#include <vector>

namespace GRIDEm { namespace Model {

    class BuildConfiguration;
    typedef std::shared_ptr<BuildConfiguration> BuildConfigurationPtr;

    class BuildConfiguration
    {
    public:
        BuildConfiguration(const std::string& config, Core::Target target);

        std::string Config;                   // "Debug" / "Release"
        Core::Target TargetKind;
        std::string OutDir;                   // relative to project, e.g. out/Debug/C6747
        std::string ObjDir;                   // relative to project, e.g. obj/Debug/C6747
        std::vector<std::string> Defines;
        std::vector<std::string> IncludeDirs; // relative or absolute
        std::vector<std::string> LibDirs;
        std::vector<std::string> Libs;
        std::string Optimise;                 // O0 .. O3
        std::string LinkerCmd;                // linker command file (DSP)
        std::string ExtraCompileFlags;
        std::string ExtraLinkFlags;
        std::string OutputName;               // program name without extension (default = project name)

        std::string Key() const;
        bool IsDebug() const;

        void ApplyDefaults();
        BuildConfigurationPtr Clone() const;
        Core::JsonPtr ToJson() const;
        static BuildConfigurationPtr FromJson(const std::string& key, const Core::JsonPtr& o);
    };
}}
