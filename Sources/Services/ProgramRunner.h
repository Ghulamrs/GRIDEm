// =============================================================================
//  ProgramRunner.h - runs a built program inside the IDE, as RIDE's window runs
//  one: its output comes line by line into the Output pane, an input line feeds
//  its standard input, Stop kills it and everything it started (its process
//  group), and the end is "[program returned N]" or "[stopped]". Verify runs
//  the emulator and the simulator captured and compares them line for line.
//  (GRIDEd's Services/ProgramRunner.h)
//
//  A program for this Mac runs on a pseudo-terminal, so its output is line
//  buffered and arrives as it is written; the terminal's "\r" is taken out and
//  the echo of what is typed is not shown again. The C6747 runners (sim6747,
//  vm6747) run on pipes, as GRIDEd runs everything.
// =============================================================================
#pragma once
#include "BuildMessage.h"
#include "ToolRunner.h"
#include "../Platform/Process.h"
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GRIDEm { namespace Services {

    typedef std::function<void(int status, bool stopped)> ProgramExitedHandler;

    class ProgramRunner
    {
    public:
        ProgramRunner();
        ~ProgramRunner();                       // stops a program still running
        ProgramRunner(const ProgramRunner&) = delete;
        ProgramRunner& operator=(const ProgramRunner&) = delete;

        TextLineHandler OnOutput;               // the program's lines, both streams, as they come (worker threads)
        ProgramExitedHandler OnExited;          // once, on a worker thread

        bool IsRunning() const { return running; }
        int LastStatus;

        // starts the command line (the program, then its arguments, each quoted) in workDir; false when
        // it could not start, with the reason in the output. onTerminal: a program for this Mac
        // (a pseudo-terminal); false for the C6747 runners (pipes). codePage is kept from GRIDEd and unused.
        bool Start(const std::string& commandLine, const std::string& workDir,
                   const std::vector<std::string>& environment, int codePage, bool onTerminal);

        // a line of input for the program, with its newline
        bool Write(const std::string& line);
        // end of input (a program reading to the end goes on)
        void CloseInput();
        // kills the program and everything it started
        void Stop();
        bool WaitForExit(int milliseconds);

        // RIDE's verifyBuilt: the emulator on the assembly and the simulator on the .out, both captured,
        // compared line for line with the simulator's "CYCLES " line aside; the lines go to `say`;
        // 0 when they agree, 1 when they differ, 2 when the simulation is missing
        static int Verify(const std::string& emulateCommand, const std::string& simulateCommand,
                          const std::string& workDir, int codePage, const std::string& outName,
                          const std::string& missing, bool hasArguments,
                          const std::function<void(const std::string&)>& say);

        // a program run to the end with its output kept (its input closed, as RIDE's runCaptured)
        static int Capture(const std::string& command, const std::string& workDir, int codePage,
                           std::vector<std::string>& lines);

    private:
        struct LineBuffer
        {
            LineBuffer() : afterCr(false) {}
            std::string text;
            bool afterCr;
        };
        void Feed(LineBuffer& buffer, const std::string& bytes);
        void Flush(LineBuffer& buffer);
        void Say(const std::string& line);
        void WaitAndFinish();
        void JoinWaiter();

        std::unique_ptr<Platform::Process> proc;
        std::thread waiter;
        std::atomic<bool> running;
        std::atomic<bool> stopped;
        bool terminal;
        std::mutex lineLock;
        LineBuffer outBuffer, errBuffer;
        std::deque<std::string> echoes;         // lines typed on the terminal, whose echo is not shown
    };
}}
