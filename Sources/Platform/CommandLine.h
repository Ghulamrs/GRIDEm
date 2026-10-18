// =============================================================================
//  CommandLine.h - a command line as GRIDEd writes it ("tool" -a "b c") into the
//  argument vector a POSIX exec wants. The words are read by Microsoft's rules
//  (CommandLineToArgvW), since toolchain.json's argument templates are written
//  for them: quotes group, \" is a quote, backslashes elsewhere are plain.
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace GRIDEm { namespace Platform {

    class CommandLine
    {
    public:
        // "C:\x\tool.exe" -a -b  ->  file + the rest (GRIDEd's ToolRunner::SplitCommand)
        static void SplitCommand(const std::string& cmd, std::string& file, std::string& args);
        // the whole line as words
        static std::vector<std::string> Split(const std::string& line);
        // a word quoted when it needs to be, so Split reads it back
        static std::string QuoteWord(const std::string& word);
        static std::string Join(const std::vector<std::string>& words);
    };
}}
