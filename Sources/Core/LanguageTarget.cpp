// =============================================================================
//  LanguageTarget.cpp - see LanguageTarget.h
// =============================================================================
#include "LanguageTarget.h"
#include "StringUtil.h"

namespace GRIDEm { namespace Core {

    std::string LanguageInfo::Name(Language l)
    {
        switch (l)
        {
        case Language::C90:       return "C90";
        case Language::Cpp11:     return "C++11";
        case Language::Shalimar:  return "Shalimar";
        case Language::Assembly:  return "Assembly";
        case Language::Header:    return "Header";
        case Language::LinkerCmd: return "Linker command";
        default:                  return "Other";
        }
    }

    Language LanguageInfo::Parse(const std::string& s)
    {
        if (s.empty()) return Language::C90;
        std::string t = StringUtil::ToUpper(StringUtil::Trim(s));
        if (t == "C90" || t == "C" || t == "C89") return Language::C90;
        if (t == "C++11" || t == "CPP11" || t == "C++" || t == "CPP") return Language::Cpp11;
        if (t == "SHALIMAR") return Language::Shalimar;
        if (t == "ASSEMBLY" || t == "ASM") return Language::Assembly;
        if (t == "HEADER") return Language::Header;
        // must round-trip with Name(): "Linker command"
        if (t == "LINKER COMMAND" || t == "LINKERCMD" || t == "LINKER") return Language::LinkerCmd;
        return Language::Other;
    }

    std::vector<Language> LanguageInfo::Compiled()
    {
        std::vector<Language> r;
        r.push_back(Language::C90);
        r.push_back(Language::Cpp11);
        r.push_back(Language::Shalimar);
        return r;
    }

    bool LanguageInfo::IsCompiled(Language l)
    {
        return l == Language::C90 || l == Language::Cpp11 || l == Language::Shalimar;
    }

    std::string TargetInfo::Name(Target t)
    {
        switch (t)
        {
        case Target::Windows: return "Windows";
        case Target::Linux:   return "Linux";
        case Target::MacOS:   return "macOS";
        default:              return "TMS320C6747";
        }
    }

    std::string TargetInfo::DisplayName(Target t)
    {
        switch (t)
        {
        case Target::Windows: return "Windows (MASM / LINK)";
        case Target::Linux:   return "Linux";
        case Target::MacOS:   return "macOS";
        default:              return "TMS320C6747 (DSP)";
        }
    }

    std::string TargetInfo::FolderName(Target t)
    {
        switch (t)
        {
        case Target::Windows: return "Win";
        case Target::Linux:   return "Linux";
        case Target::MacOS:   return "macOS";
        default:              return "C6747";
        }
    }

    Target TargetInfo::Parse(const std::string& s)
    {
        if (s.empty()) return Target::Windows;
        std::string t = StringUtil::ToUpper(StringUtil::Trim(s));
        if (StringUtil::StartsWith(t, "WIN")) return Target::Windows;
        if (StringUtil::StartsWith(t, "LINUX")) return Target::Linux;
        if (StringUtil::StartsWith(t, "MAC")) return Target::MacOS;
        if (StringUtil::Contains(t, "6747") || StringUtil::Contains(t, "C6") || StringUtil::Contains(t, "DSP")
            || StringUtil::Contains(t, "TMS"))
            return Target::C6747;
        return Target::Windows;
    }

    std::vector<Target> TargetInfo::All()
    {
        std::vector<Target> r;
        r.push_back(Target::Windows);
        r.push_back(Target::Linux);
        r.push_back(Target::MacOS);
        r.push_back(Target::C6747);
        return r;
    }

    std::string TargetInfo::ProgramExtension(Target t)
    {
        switch (t)
        {
        case Target::Windows: return ".exe";
        case Target::C6747:   return ".out";
        default:              return "";
        }
    }

    std::string ConfigName::Make(const std::string& config, Target t)
    {
        return config + "|" + TargetInfo::Name(t);
    }

    std::string ConfigName::ConfigPart(const std::string& key)
    {
        size_t i = key.find('|');
        return i == std::string::npos ? key : key.substr(0, i);
    }

    std::string ConfigName::TargetPart(const std::string& key)
    {
        size_t i = key.find('|');
        if (i == std::string::npos) return std::string();
        return key.substr(i + 1);
    }
}}
