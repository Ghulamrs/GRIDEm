// =============================================================================
//  ProgramRunner.cpp - see ProgramRunner.h
// =============================================================================
#include "ProgramRunner.h"
#include "../Core/StringUtil.h"
#include "../Platform/CommandLine.h"
#include "../Platform/Paths.h"
#include <unistd.h>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;
    using Platform::Paths;

    ProgramRunner::ProgramRunner() : LastStatus(0), running(false), stopped(false), terminal(false) {}

    ProgramRunner::~ProgramRunner()
    {
        if (running) Stop();
        JoinWaiter();
    }

    void ProgramRunner::JoinWaiter()
    {
        if (waiter.joinable() && waiter.get_id() != std::this_thread::get_id()) waiter.join();
    }

    bool ProgramRunner::Start(const std::string& commandLine, const std::string& workDir,
                              const std::vector<std::string>& environment, int codePage, bool onTerminal)
    {
        (void)codePage;
        if (running) return false;
        JoinWaiter();
        if (waiter.joinable()) waiter.detach();     // Start called from inside OnExited
        std::string file, args;
        ToolRunner::SplitCommand(commandLine, file, args);
        if (!Paths::IsFile(file)) { Say("could not run " + file); return false; }
        std::vector<std::string> argv = Platform::CommandLine::Split(args);
        argv.insert(argv.begin(), file);

        proc.reset(new Platform::Process());
        {
            std::lock_guard<std::mutex> hold(lineLock);
            outBuffer = LineBuffer();
            errBuffer = LineBuffer();
            echoes.clear();
        }
        proc->OnOutput = [this](const std::string& bytes) { Feed(outBuffer, bytes); };
        proc->OnError = [this](const std::string& bytes) { Feed(errBuffer, bytes); };
        terminal = onTerminal;
        stopped = false;
        running = true;
        std::string why;
        Platform::ProcessMode mode = onTerminal ? Platform::ProcessMode::Terminal : Platform::ProcessMode::Pipes;
        if (!proc->Start(argv, workDir, environment, mode, false, why))
        {
            running = false;
            Say("could not run " + file + ": " + why);
            return false;
        }
        waiter = std::thread([this]() { WaitAndFinish(); });
        return true;
    }

    // the program ended: its last lines are flushed, then OnExited
    void ProgramRunner::WaitAndFinish()
    {
        Platform::Process* p = proc.get();
        p->Wait(-1);
        int code = p->ExitCode();
        {
            std::lock_guard<std::mutex> hold(lineLock);
            Flush(outBuffer);
            Flush(errBuffer);
        }
        LastStatus = code;
        running = false;
        if (OnExited) OnExited(code, stopped);
    }

    bool ProgramRunner::Write(const std::string& line)
    {
        if (!proc || !running) return false;
        if (terminal)
        {
            std::lock_guard<std::mutex> hold(lineLock);
            echoes.push_back(line);
        }
        // a pipe takes the line as it is; the terminal reads "\n" as Enter
        return proc->Write(line + "\n");
    }

    void ProgramRunner::CloseInput()
    {
        if (!proc) return;
        // a terminal's end of input is Ctrl-D at the start of a line
        if (terminal) proc->Write(std::string(1, '\x04'));
        else proc->CloseInput();
    }

    void ProgramRunner::Stop()
    {
        if (!proc || !running) return;
        stopped = true;
        proc->Kill();
    }

    bool ProgramRunner::WaitForExit(int milliseconds)
    {
        for (int waited = 0; running && waited < milliseconds; waited += 20) usleep(20 * 1000);
        return !running;
    }

    // ---- the bytes into lines ------------------------------------------------
    void ProgramRunner::Feed(LineBuffer& buffer, const std::string& bytes)
    {
        std::lock_guard<std::mutex> hold(lineLock);
        for (char c : bytes)
        {
            if (c == '\n')
            {
                if (buffer.afterCr) { buffer.afterCr = false; continue; }
                Say(buffer.text);
                buffer.text.clear();
            }
            else if (c == '\r')
            {
                Say(buffer.text);
                buffer.text.clear();
                buffer.afterCr = true;
            }
            else
            {
                buffer.afterCr = false;
                buffer.text += c;
            }
        }
    }

    void ProgramRunner::Flush(LineBuffer& buffer)
    {
        if (!buffer.text.empty()) Say(buffer.text);
        buffer.text.clear();
        buffer.afterCr = false;
    }

    // called with lineLock held (or before the reader threads start)
    void ProgramRunner::Say(const std::string& line)
    {
        if (terminal && !echoes.empty() && line == echoes.front())
        {
            echoes.pop_front();         // the terminal's echo of what was typed
            return;
        }
        if (OnOutput) OnOutput(line);
    }

    // ---- Verify -------------------------------------------------------------
    int ProgramRunner::Verify(const std::string& emulateCommand, const std::string& simulateCommand,
                              const std::string& workDir, int codePage, const std::string& outName,
                              const std::string& missing, bool hasArguments,
                              const std::function<void(const std::string&)>& say)
    {
        if (!missing.empty()) { say("[verify] " + missing); return 2; }
        if (hasArguments) say("[verify] the simulator takes no command line - both run without one");
        std::vector<std::string> emulated, simulated;
        int emuStatus = Capture(emulateCommand, workDir, codePage, emulated);
        Capture(simulateCommand, workDir, codePage, simulated);
        std::string cycles;
        std::vector<std::string> b;
        for (const std::string& line : simulated)
        {
            if (StringUtil::StartsWith(line, "CYCLES ")) { cycles = line.substr(7); continue; }
            b.push_back(line);
        }
        for (const std::string& line : emulated) say(line);
        say("[verify] emulator  (vm6747, the assembly):    " + std::to_string(emulated.size()) + " lines, exit "
            + std::to_string(emuStatus));
        say("[verify] simulator (sim6747, " + outName + "): " + std::to_string(b.size()) + " lines"
            + (cycles.empty() ? std::string() : ", " + cycles));
        size_t k = 0;
        while (k < emulated.size() && k < b.size() && emulated[k] == b[k]) k++;
        if (k == emulated.size() && k == b.size())
        {
            say("[verify] the two agree, line for line");
            return 0;
        }
        say("[verify] they differ at line " + std::to_string(k + 1) + ":");
        say("  emulator : " + (k < emulated.size() ? emulated[k] : std::string("(no more output)")));
        say("  simulator: " + (k < b.size() ? b[k] : std::string("(no more output)")));
        return 1;
    }

    int ProgramRunner::Capture(const std::string& command, const std::string& workDir, int codePage,
                               std::vector<std::string>& lines)
    {
        ToolRunner r;
        r.OnLine = [&lines](const std::string& s) { lines.push_back(s); };
        ToolRunOptions o;
        o.OutputCodePage = codePage;
        return r.Run(command, workDir, &o);
    }
}}
