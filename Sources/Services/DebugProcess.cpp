// =============================================================================
//  DebugProcess.cpp - see DebugProcess.h
// =============================================================================
#include "DebugProcess.h"
#include "../Platform/CommandLine.h"
#include "../Platform/Process.h"
#include <chrono>
#include <unistd.h>

namespace GRIDEm { namespace Services {

    DebugProcess::DebugProcess()
        : outAfterCr(false), errAfterCr(false), running(false), exitCode(0), onConsole(false), apart(false),
          vtState(0), row(1), column(1) {}

    DebugProcess::~DebugProcess()
    {
        if (proc && running) proc->Kill();
        JoinWaiter();
    }

    std::string DebugProcess::Transcript()
    {
        std::lock_guard<std::mutex> hold(lock);
        return transcript;
    }

    bool DebugProcess::Start(const std::string& commandLine, const std::string& workDir,
                             const std::vector<std::string>& environment, std::string& why)
    {
        return StartWith(commandLine, workDir, environment, Way::Joined, why);
    }

    bool DebugProcess::StartApart(const std::string& commandLine, const std::string& workDir,
                                  const std::vector<std::string>& environment, std::string& why)
    {
        return StartWith(commandLine, workDir, environment, Way::Apart, why);
    }

    bool DebugProcess::StartOnConsole(const std::string& commandLine, const std::string& workDir,
                                      const std::vector<std::string>& environment, std::string& why)
    {
        return StartWith(commandLine, workDir, environment, Way::Console, why);
    }

    bool DebugProcess::StartWith(const std::string& commandLine, const std::string& workDir,
                                 const std::vector<std::string>& environment, Way way, std::string& why)
    {
        why.clear();
        if (running) { why = "a session is already running"; return false; }
        JoinWaiter();
        std::string file, args;
        Platform::CommandLine::SplitCommand(commandLine, file, args);
        std::vector<std::string> argv;
        argv.push_back(file);
        std::vector<std::string> rest = Platform::CommandLine::Split(args);
        argv.insert(argv.end(), rest.begin(), rest.end());
        {
            std::lock_guard<std::mutex> hold(lock);
            pending.clear();
            heard.clear();
            outLine.clear(); errLine.clear();
            outAfterCr = errAfterCr = false;
            vtState = 0; vtParams.clear(); row = 1; column = 1;
        }
        apart = way == Way::Apart;
        onConsole = way == Way::Console;
        exitCode = 0;
        proc.reset(new Platform::Process());
        proc->OnOutput = [this](const std::string& b) { OnOutputBytes(b); };
        proc->OnError = [this](const std::string& b) { OnErrorBytes(b); };
        Platform::ProcessMode mode = way == Way::Console ? Platform::ProcessMode::Terminal
                                   : way == Way::Apart ? Platform::ProcessMode::Pipes
                                   : Platform::ProcessMode::Joined;
        running = true;
        std::string reason;
        if (!proc->Start(argv, workDir, environment, mode, false, reason))
        {
            running = false;
            if (way == Way::Console) why = "could not start " + file + " on a terminal: " + reason;
            else if (reason.compare(0, 11, "not found: ") == 0) why = reason;
            else why = "could not start " + file + ": " + reason;
            proc.reset();
            return false;
        }
        waiter = std::thread([this]() { WaitChild(); });
        return true;
    }

    // ---- what the child writes ----------------------------------------------
    void DebugProcess::OutputLine(const std::string& line)
    {
        pending += line;
        pending += "\n";
        transcript += line + "\n";
    }

    void DebugProcess::ErrorLine(const std::string& line)
    {
        if (!apart) { OutputLine(line); return; }
        heard.push_back(line);
        transcript += "! " + line + "\n";
    }

    // lines as .NET's reader hands them: \n, \r\n or a lone \r ends one
    static void SplitInto(const std::string& bytes, std::string& line, bool& afterCr, std::vector<std::string>& done)
    {
        for (char c : bytes)
        {
            if (afterCr)
            {
                afterCr = false;
                if (c == '\n') continue;
            }
            if (c == '\r' || c == '\n')
            {
                done.push_back(line);
                line.clear();
                afterCr = c == '\r';
                continue;
            }
            line += c;
        }
    }

    void DebugProcess::OnOutputBytes(const std::string& bytes)
    {
        {
            std::lock_guard<std::mutex> hold(lock);
            if (onConsole)
            {
                std::string text = PlainText(bytes);
                if (text.empty()) return;
                pending += text;
                transcript += text;
            }
            else
            {
                std::vector<std::string> lines;
                SplitInto(bytes, outLine, outAfterCr, lines);
                for (const std::string& l : lines) OutputLine(l);
            }
        }
        gotData.notify_all();
    }

    void DebugProcess::OnErrorBytes(const std::string& bytes)
    {
        {
            std::lock_guard<std::mutex> hold(lock);
            std::vector<std::string> lines;
            SplitInto(bytes, errLine, errAfterCr, lines);
            for (const std::string& l : lines) ErrorLine(l);
        }
        gotData.notify_all();
    }

    // the child ended: its last lines are read first (as GRIDEd's Exited waits for them)
    void DebugProcess::WaitChild()
    {
        Platform::Process* p = proc.get();
        // polled, so a Say on another thread is never held up by a blocking wait
        while (p->Running()) usleep(20 * 1000);
        p->Wait(-1);
        {
            std::lock_guard<std::mutex> hold(lock);
            if (!outLine.empty()) { OutputLine(outLine); outLine.clear(); }
            if (!errLine.empty()) { ErrorLine(errLine); errLine.clear(); }
        }
        exitCode = p->ExitCode();
        running = false;
        gotData.notify_all();
    }

    void DebugProcess::JoinWaiter()
    {
        if (waiter.joinable()) waiter.join();
    }

    // ---- talking to it ------------------------------------------------------
    bool DebugProcess::Say(const std::string& line)
    {
        if (!running || !proc) return false;
        if (!proc->Write(line + (onConsole ? "\r" : "\n"))) return false;
        std::lock_guard<std::mutex> hold(lock);
        transcript += "> " + line + "\n";
        return true;
    }

    std::string DebugProcess::ReadUntil(const std::string& marker, int timeoutMs, bool& found)
    {
        found = false;
        if (marker.empty()) return std::string();
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> hold(lock);
        for (;;)
        {
            size_t at = pending.find(marker);
            if (at != std::string::npos)
            {
                std::string answer = pending.substr(0, at);
                size_t after = at + marker.size();
                if (after < pending.size() && pending[after] == '\r') after++;
                if (after < pending.size() && pending[after] == '\n') after++;
                pending = pending.substr(after);
                found = true;
                return answer;
            }
            if (!running)
            {
                std::string text = pending;
                pending.clear();
                return text;
            }
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            int left = timeoutMs - elapsed;
            if (left <= 0) break;
            gotData.wait_for(hold, std::chrono::milliseconds(left < 250 ? left : 250));
        }
        std::string rest = pending;
        pending.clear();
        return rest;
    }

    bool DebugProcess::Hear(int timeoutMs, bool& alive, std::string& line)
    {
        alive = true;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> hold(lock);
        for (;;)
        {
            if (!heard.empty())
            {
                line = heard.front();
                heard.pop_front();
                return true;
            }
            if (!running) { alive = false; return false; }
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            int left = timeoutMs - elapsed;
            if (left <= 0) return false;
            gotData.wait_for(hold, std::chrono::milliseconds(left < 250 ? left : 250));
        }
    }

    std::string DebugProcess::Printed()
    {
        std::lock_guard<std::mutex> hold(lock);
        std::string text = pending;
        pending.clear();
        return text;
    }

    void DebugProcess::Stop(const std::string& quitCommand)
    {
        if (!proc) return;
        if (running && !quitCommand.empty()) Say(quitCommand);
        if (!onConsole) proc->CloseInput();
        for (int waited = 0; waited < 2000 && running; waited += 20) usleep(20 * 1000);
        if (running) proc->Kill();
        JoinWaiter();
        running = false;
    }

    // ---- the terminal -------------------------------------------------------
    // what a terminal writes, as text (RIDE's plainText): its escape sequences out - CSI, OSC such
    // as the title, the short ones - "\r" dropped, the cursor sent to a lower row as that many
    // newlines and sent right as that many spaces. One state across reads; the bytes stay UTF-8.
    std::string DebugProcess::PlainText(const std::string& bytes)
    {
        std::string out;
        for (char ch : bytes)
        {
            unsigned char c = (unsigned char)ch;
            switch (vtState)
            {
            case 0:
                if (c == 0x1b) { vtState = 1; continue; }
                if (c == '\r' || c == 0x07) continue;
                if (c == '\n') { row++; column = 1; }
                else if ((c & 0xc0) != 0x80) column++;
                out += ch;
                continue;
            case 1:
                if (c == '[') { vtState = 2; vtParams.clear(); }
                else if (c == ']') vtState = 3;
                else if (c == '(' || c == ')' || c == '#' || c == '%') vtState = 5;
                else vtState = 0;
                continue;
            case 2:
                if (c >= 0x40 && c <= 0x7e)
                {
                    if (c == 'H' || c == 'f')
                    {
                        int wanted = LeadingNumber(vtParams, 0);
                        if (wanted < 1) wanted = 1;
                        size_t semi = vtParams.find(';');
                        int col = semi == std::string::npos ? 1 : LeadingNumber(vtParams, semi + 1);
                        if (col < 1) col = 1;
                        if (row < wanted) column = 1;
                        for (; row < wanted; ++row) out += '\n';
                        for (; row == wanted && column < col; ++column) out += ' ';
                    }
                    else if (c == 'C')
                    {
                        int n = LeadingNumber(vtParams, 0);
                        if (n < 1) n = 1;
                        for (; n > 0; --n, ++column) out += ' ';
                    }
                    else if (c == 'G')
                    {
                        int col = LeadingNumber(vtParams, 0);
                        for (; column < col; ++column) out += ' ';
                    }
                    vtState = 0;
                }
                else vtParams += ch;
                continue;
            case 3:
                if (c == 0x07) vtState = 0;
                else if (c == 0x1b) vtState = 4;
                continue;
            case 4:
                vtState = c == '\\' ? 0 : 3;
                continue;
            default:
                vtState = 0;
                continue;
            }
        }
        return out;
    }

    int DebugProcess::LeadingNumber(const std::string& s, size_t from)
    {
        int value = 0;
        for (size_t i = from; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) value = value * 10 + (s[i] - '0');
        return value;
    }
}}
