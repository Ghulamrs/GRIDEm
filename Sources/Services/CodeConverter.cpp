// =============================================================================
//  CodeConverter.cpp - see CodeConverter.h
// =============================================================================
#include "CodeConverter.h"
#include "MacroExpander.h"
#include "RideToolchain.h"
#include "ToolRunner.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include "../Settings/ToolchainSettings.h"
#include <mutex>
#include <sys/stat.h>

namespace GRIDEm { namespace Services {

    using Core::PathUtil;
    using Core::StringUtil;
    using Platform::Paths;
    using namespace Settings;

    CodeConverter::CodeConverter(const ConverterSettingsPtr& s) : settings(s) {}

    ToolDefinitionPtr CodeConverter::Definition()
    {
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        if (!defs) return nullptr;
        return defs->ToolByRole("converter");
    }

    std::string CodeConverter::OperationId(ConvertDirection d)
    {
        return d == ConvertDirection::CToShalimar ? "convertToShalimar" : "convertToC";
    }

    std::string CodeConverter::Named(ConvertDirection d) const
    {
        if (!settings) return std::string();
        return StringUtil::Trim(d == ConvertDirection::CToShalimar ? settings->CToShalimar : settings->ShalimarToC);
    }

    std::string CodeConverter::ToolPath(ConvertDirection d) const
    {
        std::string named = Named(d);
        if (!named.empty()) return Platform::Environment::Expand(named);
        ToolDefinitionPtr t = Definition();
        if (!t) return std::string();
        std::string found = RideToolchain::FindTool(t, std::string(), ToolchainSettings::RideRoot);
        return !found.empty() ? found : t->Exe;
    }

    std::string CodeConverter::LookedIn(ConvertDirection d) const
    {
        if (!Named(d).empty()) return std::string();
        ToolDefinitionPtr t = Definition();
        if (!t) return "toolchain.json names no converter";
        return RideToolchain::SearchPlaces(t, std::string(), ToolchainSettings::RideRoot);
    }

    std::string CodeConverter::CommandLine(const std::string& tool, const std::string& input,
                                           const std::string& output, ConvertDirection d) const
    {
        MacroExpander mx;
        mx.Set("Tool", tool);
        mx.Set("Input", input);
        mx.Set("Source", input);
        mx.Set("Output", output);
        std::string templ = settings ? StringUtil::Trim(settings->CommandTemplate) : std::string();
        if (templ.empty())
        {
            ToolDefinitionPtr t = Definition();
            std::string args = t ? t->OperationArgs(OperationId(d)) : std::string();
            templ = !args.empty() ? "\"$(Tool)\" " + args : std::string(ConverterSettings::Gride1Template);
        }
        return mx.Expand(templ);
    }

    ConvertDirection CodeConverter::DirectionFor(const std::string& input)
    {
        return PathUtil::HasExtension(input, ".shl") || PathUtil::HasExtension(input, ".shm")
            ? ConvertDirection::ShalimarToC : ConvertDirection::CToShalimar;
    }

    std::string CodeConverter::SuggestOutput(const std::string& input, ConvertDirection d)
    {
        std::string ext = d == ConvertDirection::CToShalimar ? ".shl" : ".c";
        std::string outPath = Paths::ChangeExtension(input, ext);
        if (Paths::Exists(outPath))
            outPath = Paths::Combine(Paths::DirectoryName(input), Paths::FileNameWithoutExtension(input) + "_conv" + ext);
        return outPath;
    }

    // a file's write time to the nanosecond (0 when it is not there), so a file rewritten
    // within the same second still counts as written
    static long long WriteTime(const std::string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return 0;
#if defined(__APPLE__)
        return (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
        return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    }

    bool CodeConverter::Convert(const std::string& input, const std::string& output, ConvertDirection d)
    {
        log.clear();
        std::string tool = ToolPath(d);
        if (tool.empty() || !Paths::IsFile(tool))
        {
            Append("Converter not found: " + (tool.empty() ? std::string("(none)") : tool));
            std::string looked = LookedIn(d);
            if (!looked.empty()) Append("Looked in: " + looked);
            Append("Put c2s inside GRIDEm (Contents/MacOS), or set the converter in Settings > Converters.");
            return false;
        }
        std::string cmd = CommandLine(tool, input, output, d);
        Append("> " + cmd);

        long long before = WriteTime(output);
        ToolRunner r;
        std::mutex logLock;
        r.OnLine = [this, &logLock](const std::string& line)
        {
            std::lock_guard<std::mutex> hold(logLock);
            Append(line);
        };
        int code = r.Run(cmd, Paths::DirectoryName(input));
        bool written = Paths::IsFile(output) && WriteTime(output) > before;
        Append("Converter exited with code " + std::to_string(code));
        if (written && code != 0) Append("Written with marks to review (BEYOND) - see the lines above.");
        if (!written) Append("No output was written.");
        return written && (code == 0 || code == 1);
    }
}}
