// =============================================================================
//  LanguageTarget.h - the 3 languages and 4 targets GRIDE knows about
//  (GRIDEd's Core/LanguageTarget.h: LanguageInfo, TargetInfo, ConfigName)
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace GRIDEm { namespace Core {

    // Source languages. Assembly/Header/Other are file kinds that can live in
    // any project; C90, Cpp11 and Shalimar are the three compiled languages.
    enum class Language { C90, Cpp11, Shalimar, Assembly, Header, LinkerCmd, Other };

    enum class Target { Windows, Linux, MacOS, C6747 };

    enum class BuildConfigKind { Debug, Release };

    class LanguageInfo
    {
    public:
        // name stored in .pro / settings ("C90", "C++11", "Shalimar")
        static std::string Name(Language l);
        static Language Parse(const std::string& s);
        // the three compiled languages, in menu order
        static std::vector<Language> Compiled();
        static bool IsCompiled(Language l);
    };

    class TargetInfo
    {
    public:
        // key used in JSON and in configuration names "Debug|TMS320C6747"
        static std::string Name(Target t);
        static std::string DisplayName(Target t);
        // short folder name used in out/<Config>/<dir>
        static std::string FolderName(Target t);
        static Target Parse(const std::string& s);
        static std::vector<Target> All();
        // default output extension of a linked program
        static std::string ProgramExtension(Target t);
    };

    class ConfigName
    {
    public:
        // "Debug|TMS320C6747"
        static std::string Make(const std::string& config, Target t);
        static std::string ConfigPart(const std::string& key);
        static std::string TargetPart(const std::string& key);
    };
}}
