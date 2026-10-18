// =============================================================================
//  ToolRunner.h - starts one external tool (compiler, assembler, linker,
//  converter ...) with redirected output, line by line.
//  Run() blocks; call it from a worker thread (BuildEngine does).
//  (GRIDEd's Services/ToolRunner.h)
//
//  Tools are started directly, never through a shell. A setup script (GRIDEd's
//  vcvars64.bat through cmd.exe) is Windows only: GRIDEm says so and runs the
//  tool directly. The tool leads a process group, so Cancel ends it and
//  everything it started.
// =============================================================================
#pragma once
#include "BuildMessage.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    // how one tool is started (filled from the toolchain of the build target)
    class ToolRunOptions
    {
    public:
        ToolRunOptions() : OutputCodePage(0) {}
        std::string SetupScript;               // Windows only; kept so settings round-trip
        std::vector<std::string> Environment;  // "NAME=value", %VAR% expanded; NAME= removes it
        int OutputCodePage;                    // kept from GRIDEd; the Mac's tools write UTF-8
    };

    class ToolRunner
    {
    public:
        static const int ExitStartFailed = -1;
        static const int ExitCancelled = -2;

        ToolRunner();

        TextLineHandler OnLine;                // called on the reader threads, one line at a time

        bool Cancelled() const { return cancelled; }

        // splits  "/x/tool" -a -b  into file name + arguments
        static void SplitCommand(const std::string& cmd, std::string& file, std::string& args);

        int Run(const std::string& commandLine, const std::string& workDir);
        // returns the exit code, ExitStartFailed when the tool could not be started,
        // ExitCancelled after Cancel()
        int Run(const std::string& commandLine, const std::string& workDir, const ToolRunOptions* options);

        void Cancel() { cancelled = true; }
        void Reset() { cancelled = false; }

    private:
        // the bytes of one stream into lines: \n, \r\n or a lone \r ends one
        struct LineBuffer
        {
            LineBuffer() : afterCr(false) {}
            std::string text;
            bool afterCr;
        };
        void Feed(LineBuffer& buffer, const std::string& bytes);
        void Flush(LineBuffer& buffer);
        void Say(const std::string& line);

        std::atomic<bool> cancelled;
        std::mutex lineLock;
        LineBuffer outBuffer, errBuffer;
    };
}}
