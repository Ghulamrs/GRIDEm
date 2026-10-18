// =============================================================================
//  CodeConverter.h - C <-> Shalimar source conversion (Tools > Convert)
//  Runs toolchain.json's converter (the tool of role "converter", c2s) with its
//  convertToShalimar / convertToC operation, found as its lookup says. A tool or a
//  command template named in Settings > Converters takes its place.
//  (GRIDEd's Services/CodeConverter.h)
// =============================================================================
#pragma once
#include "../Settings/GrideSettings.h"
#include "../Settings/ToolchainDefinitions.h"
#include <string>

namespace GRIDEm { namespace Services {

    enum class ConvertDirection { CToShalimar, ShalimarToC };

    class CodeConverter
    {
    public:
        explicit CodeConverter(const Settings::ConverterSettingsPtr& s);

        const std::string& Log() const { return log; }

        // toolchain.json's converter (c2s), or nullptr when the file names none
        static Settings::ToolDefinitionPtr Definition();
        // the operation of toolchain.json's converter for a direction
        static std::string OperationId(ConvertDirection d);

        // the converter for a direction: the one named in Settings > Converters, else toolchain.json's
        // converter where its lookup finds it, else its bare name, so a "not found" message names it
        std::string ToolPath(ConvertDirection d) const;
        // where toolchain.json's converter was looked for (empty when Settings names the tool)
        std::string LookedIn(ConvertDirection d) const;
        // the command line: the template from Settings when one is set, else the converter's operation
        // from toolchain.json ("--to-shalimar" / "--to-c" for c2s), else GRIDE 1.0's "tool input -o output"
        std::string CommandLine(const std::string& tool, const std::string& input, const std::string& output,
                                ConvertDirection d) const;

        static ConvertDirection DirectionFor(const std::string& input);
        // kalman.c -> kalman.shl ; if it exists: kalman_conv.shl
        static std::string SuggestOutput(const std::string& input, ConvertDirection d);

        // true when the converter wrote the output file in this run. c2s exits 1 both when it wrote the
        // file with BEYOND marks and when it refused (then it writes nothing), so a fresh file decides.
        bool Convert(const std::string& input, const std::string& output, ConvertDirection d);

    private:
        std::string Named(ConvertDirection d) const;
        void Append(const std::string& line) { log += line; log += "\n"; }

        Settings::ConverterSettingsPtr settings;
        std::string log;
    };
}}
