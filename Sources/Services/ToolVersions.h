// =============================================================================
//  ToolVersions.h - Help > About: the IDE's own version and each toolchain
//  program's, as the program reports it itself (--version, else its banner
//  with no arguments). Nothing is written down here but the IDE's own 1.0,
//  which a program that reports no version of its own goes with.
//  (GRIDEd's Services/ToolVersions.h, namespace GrideAbout there)
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    struct ToolVersion
    {
        std::string Name;
        std::string Version;        // "1.3", or empty when the program does not say
        std::string Description;    // what the program calls itself, the author and version taken out
    };

    class ToolVersions
    {
    public:
        static const char* IdeVersion;    // "1.5.0", as Info.plist
        static const char* Author;        // "(c)2026 G. R. Akhtar" with the copyright sign

        static std::vector<std::string> Names();
        // the program in dir asked for its version: --version first, then no arguments
        static ToolVersion Ask(const std::string& dir, const std::string& name);
        // the whole About text below the title: version, author, the toolchain table, the framework
        static std::string Text(const std::string& dir, const std::string& settingsPath);

    private:
        static std::string Run(const std::string& exe, const std::string& args);
        static std::string Own(const std::string& line);
        static bool IsComplaint(const std::string& line);
        static std::string Tidy(const std::string& name, const std::string& s);
    };
}}
