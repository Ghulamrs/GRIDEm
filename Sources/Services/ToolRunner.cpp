// =============================================================================
//  ToolRunner.cpp - see ToolRunner.h
// =============================================================================
#include "ToolRunner.h"
#include "../Core/StringUtil.h"
#include "../Platform/CommandLine.h"
#include "../Platform/JobGroup.h"
#include "../Platform/Paths.h"
#include "../Platform/Process.h"
#include <unistd.h>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;
    using Platform::Paths;

    ToolRunner::ToolRunner() : cancelled(false) {}

    void ToolRunner::SplitCommand(const std::string& cmd, std::string& file, std::string& args)
    {
        Platform::CommandLine::SplitCommand(cmd, file, args);
    }

    int ToolRunner::Run(const std::string& commandLine, const std::string& workDir)
    {
        return Run(commandLine, workDir, nullptr);
    }

    void ToolRunner::Say(const std::string& line)
    {
        std::lock_guard<std::mutex> hold(lineLock);
        if (OnLine) OnLine(line);
    }

    // the caller holds lineLock
    void ToolRunner::Feed(LineBuffer& buffer, const std::string& bytes)
    {
        for (char c : bytes)
        {
            if (buffer.afterCr)
            {
                buffer.afterCr = false;
                if (c == '\n') continue;          // \r\n is one line end
            }
            if (c == '\r' || c == '\n')
            {
                if (OnLine) OnLine(buffer.text);
                buffer.text.clear();
                buffer.afterCr = c == '\r';
                continue;
            }
            buffer.text += c;
        }
    }

    void ToolRunner::Flush(LineBuffer& buffer)
    {
        std::lock_guard<std::mutex> hold(lineLock);
        if (!buffer.text.empty() && OnLine) OnLine(buffer.text);
        buffer.text.clear();
        buffer.afterCr = false;
    }

    // a program named with a path must be there; a bare name is looked for on PATH
    static bool ToolExists(const std::string& file)
    {
        if (file.find('/') != std::string::npos) return Paths::IsFile(file);
        if (Paths::IsFile(file)) return true;
        for (const std::string& dir : Paths::SearchPath())
            if (Paths::IsExecutable(Paths::Combine(dir, file))) return true;
        return false;
    }

    int ToolRunner::Run(const std::string& commandLine, const std::string& workDir, const ToolRunOptions* options)
    {
        std::string file, args;
        SplitCommand(commandLine, file, args);
        if (options != nullptr && !StringUtil::IsNullOrWhiteSpace(options->SetupScript))
            Say("note: setup scripts are Windows only - " + StringUtil::Trim(options->SetupScript)
                + " is not run; the tool is started directly");
        if (!ToolExists(file))
        {
            Say("error: tool not found: " + file);
            return ExitStartFailed;
        }
        if (cancelled) return ExitCancelled;

        std::vector<std::string> argv;
        argv.push_back(file);
        std::vector<std::string> rest = Platform::CommandLine::Split(args);
        argv.insert(argv.end(), rest.begin(), rest.end());

        {
            std::lock_guard<std::mutex> hold(lineLock);
            outBuffer = LineBuffer();
            errBuffer = LineBuffer();
        }
        Platform::Process p;
        p.OnOutput = [this](const std::string& bytes) { std::lock_guard<std::mutex> hold(lineLock); Feed(outBuffer, bytes); };
        p.OnError = [this](const std::string& bytes) { std::lock_guard<std::mutex> hold(lineLock); Feed(errBuffer, bytes); };
        std::vector<std::string> environment;
        if (options != nullptr) environment = options->Environment;
        std::string why;
        // input closed at once: a tool that prompts gets end of file instead of waiting for ever
        if (!p.Start(argv, workDir, environment, Platform::ProcessMode::Pipes, true, why))
        {
            Say("error: cannot start " + file + ": " + why);
            return ExitStartFailed;
        }
        while (p.Running())
        {
            if (cancelled)
            {
                p.Kill();
                p.Wait(3000);
                Flush(outBuffer);
                Flush(errBuffer);
                Say("*** Cancelled by user.");
                return ExitCancelled;
            }
            usleep(100 * 1000);
        }
        // the output still on its way: a child that keeps the pipe open (a grandchild) must not
        // hang the build - up to 30 s, as GRIDEd waits
        bool drained = false;
        for (int i = 0; i < 300; i++)
        {
            if (!Platform::JobGroup::Alive(p.Pid())) { drained = true; break; }
            if (cancelled) { p.Kill(); p.Wait(3000); return ExitCancelled; }
            usleep(100 * 1000);
        }
        if (!drained) Say("warning: tool output still open after 30 s - continuing without it");
        else p.Wait(-1);
        Flush(outBuffer);
        Flush(errBuffer);
        return p.ExitCode();
    }
}}
