// =============================================================================
//  Process.h - one child process, started directly (no shell), as GRIDEd's
//  ToolRunner and DebugProcess start theirs through System::Diagnostics::Process.
//
//  Three ways to start it:
//    Pipes      standard output and standard error on two pipes, heard apart
//    Joined     both on one pipe (a debugger whose answers come on either)
//    Terminal   on a pseudo-terminal: a program that wants one (lldb, the shell)
//  The child leads a process group of its own, so Kill ends whatever it started.
//
//  What the child writes is handed to OnOutput / OnError as it comes, on a
//  reader thread of its own - one call at a time, never two at once.
// =============================================================================
#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>

namespace GRIDEm { namespace Platform {

    enum class ProcessMode { Pipes, Joined, Terminal };

    class Process
    {
    public:
        typedef std::function<void(const std::string& bytes)> BytesHandler;

        Process();
        ~Process();                        // kills a child still running, joins the readers
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        // set before Start; called on the reader threads
        BytesHandler OnOutput;
        BytesHandler OnError;              // Pipes only

        // argv[0] is the program (a path, or a name looked up on PATH).
        // environment: "NAME=value" changes over this process's (NAME= removes).
        // closeInput: the child's input closed at once - a tool that prompts gets end of file.
        bool Start(const std::vector<std::string>& argv, const std::string& workDir,
                   const std::vector<std::string>& environment, ProcessMode mode, bool closeInput,
                   std::string& why);

        bool Write(const std::string& bytes);       // to the child's input
        void CloseInput();
        // true when the child has ended within the time (negative: wait for ever); its
        // output is all read by then
        bool Wait(int timeoutMs);
        bool Running();
        int ExitCode();                    // the exit status, or 128 + the signal
        pid_t Pid() const { return pid; }
        void Kill();                       // the child and everything it started
        void End(int graceMs);             // SIGTERM first, then SIGKILL
        void Resize(int columns, int rows);    // Terminal only

    private:
        void ReadLoop(int fd, bool isError);
        void Reap(bool block);

        pid_t pid;
        int inFd, outFd, errFd;
        bool reaped;
        int exitCode;
        ProcessMode mode;
        std::thread outReader, errReader;
        std::mutex handlerLock;
        std::mutex stateLock;
    };
}}
