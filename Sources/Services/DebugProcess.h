// =============================================================================
//  DebugProcess.h - a debugger, or a program that debugs itself, as a child:
//  lines are said to its standard input and what it prints is read until a
//  marker turns up or a time runs out, as RIDE's Process does it.
//  (GRIDEd's Services/DebugProcess.h)
//  Three ways to start it:
//    Start           both streams joined, on pipes (gdb, lldb, the emulator and simulator)
//    StartOnConsole  on a pseudo-terminal (where GRIDEd uses a Windows pseudo-console):
//                    the terminal's escape sequences are taken out as it is read, Enter is "\r"
//    StartApart      the streams kept apart (RIDE's shalimar::Channel): the lines on standard
//                    error are heard one at a time, standard output is the program's own and
//                    Printed answers it
//  The child leads a process group, so Stop ends it and whatever it started.
// =============================================================================
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GRIDEm { namespace Platform { class Process; } }

namespace GRIDEm { namespace Services {

    class DebugProcess
    {
    public:
        DebugProcess();
        ~DebugProcess();
        DebugProcess(const DebugProcess&) = delete;
        DebugProcess& operator=(const DebugProcess&) = delete;

        bool Running() const { return running; }
        int ExitCode() const { return exitCode; }
        bool OnConsole() const { return onConsole; }
        // everything said and read, for a report
        std::string Transcript();

        // environment: "NAME=value" laid over this process's
        bool Start(const std::string& commandLine, const std::string& workDir,
                   const std::vector<std::string>& environment, std::string& why);
        bool StartApart(const std::string& commandLine, const std::string& workDir,
                        const std::vector<std::string>& environment, std::string& why);
        bool StartOnConsole(const std::string& commandLine, const std::string& workDir,
                            const std::vector<std::string>& environment, std::string& why);

        // a line to the child, with its line end: a newline on a pipe, Enter ("\r") on a terminal
        bool Say(const std::string& line);
        // what the child printed up to the marker (the marker and its line end taken out); found
        // says whether the marker came. When the time runs out, or the child ends, what is
        // pending is answered and found is false.
        std::string ReadUntil(const std::string& marker, int timeoutMs, bool& found);
        // the next line said on standard error (StartApart): false when none came in the time;
        // alive is false once the child has ended with nothing left to hear
        bool Hear(int timeoutMs, bool& alive, std::string& line);
        // what the program printed since the last call (standard output under StartApart)
        std::string Printed();
        // the child told to quit, then ended with everything it started
        void Stop(const std::string& quitCommand);

    private:
        enum class Way { Joined, Apart, Console };
        bool StartWith(const std::string& commandLine, const std::string& workDir,
                       const std::vector<std::string>& environment, Way way, std::string& why);
        void OnOutputBytes(const std::string& bytes);
        void OnErrorBytes(const std::string& bytes);
        void OutputLine(const std::string& line);       // the caller holds lock
        void ErrorLine(const std::string& line);        // the caller holds lock
        void WaitChild();
        void JoinWaiter();
        std::string PlainText(const std::string& bytes); // the caller holds lock
        static int LeadingNumber(const std::string& s, size_t from);

        std::unique_ptr<Platform::Process> proc;
        std::thread waiter;
        std::mutex lock;
        std::condition_variable gotData;
        std::string pending;
        std::deque<std::string> heard;
        std::string transcript;
        std::string outLine, errLine;      // a line still being read, on each pipe
        bool outAfterCr, errAfterCr;
        std::atomic<bool> running;
        std::atomic<int> exitCode;
        bool onConsole;
        bool apart;
        // where the reading of the terminal's escape sequences has got to (RIDE's plainText state)
        int vtState;
        std::string vtParams;
        int row, column;
    };
}}
