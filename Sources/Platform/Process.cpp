// =============================================================================
//  Process.cpp - see Process.h
// =============================================================================
#include "Process.h"
#include "Environment.h"
#include "JobGroup.h"
#include "Paths.h"
#include "Pty.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace GRIDEm { namespace Platform {

    Process::Process()
        : pid(-1), inFd(-1), outFd(-1), errFd(-1), reaped(true), exitCode(0), mode(ProcessMode::Pipes) {}

    Process::~Process()
    {
        if (pid > 0 && (Running() || JobGroup::Alive(pid))) Kill();
        Wait(3000);
        CloseInput();
        if (outReader.joinable()) outReader.join();
        if (errReader.joinable()) errReader.join();
    }

    // the program as exec finds it: a path as given, a bare name on PATH
    static std::string Resolve(const std::string& program)
    {
        if (program.find('/') != std::string::npos) return program;
        for (const std::string& dir : Paths::SearchPath())
        {
            std::string p = Paths::Combine(dir, program);
            if (Paths::IsExecutable(p)) return p;
        }
        return program;
    }

    bool Process::Start(const std::vector<std::string>& argv, const std::string& workDir,
                        const std::vector<std::string>& environment, ProcessMode how, bool closeInput,
                        std::string& why)
    {
        why.clear();
        if (pid > 0 && Running()) { why = "a process is already running"; return false; }
        if (argv.empty()) { why = "no program named"; return false; }
        std::string program = Resolve(argv[0]);
        if (!Paths::IsExecutable(program)) { why = "not found: " + argv[0]; return false; }
        if (outReader.joinable()) outReader.join();
        if (errReader.joinable()) errReader.join();

        // everything the child needs is made before fork: after it only exec-safe calls
        std::vector<std::string> envBlock = Environment::WithChanges(environment);
        std::vector<char*> envp;
        for (std::string& e : envBlock) envp.push_back(&e[0]);
        envp.push_back(nullptr);
        std::vector<std::string> args = argv;
        args[0] = program;
        std::vector<char*> cargv;
        for (std::string& a : args) cargv.push_back(&a[0]);
        cargv.push_back(nullptr);
        std::string dir = (!workDir.empty() && Paths::IsDirectory(workDir)) ? workDir : std::string();

        int inPipe[2] = { -1, -1 }, outPipe[2] = { -1, -1 }, errPipe[2] = { -1, -1 };
        int master = -1;
        std::string slave;
        mode = how;
        if (how == ProcessMode::Terminal)
        {
            if (!Pty::Open(500, 50, master, slave, why)) return false;
        }
        else
        {
            if (pipe(inPipe) != 0 || pipe(outPipe) != 0 || (how == ProcessMode::Pipes && pipe(errPipe) != 0))
            {
                why = std::string("pipe: ") + std::strerror(errno);
                return false;
            }
        }

        pid_t child = fork();
        if (child < 0)
        {
            why = std::string("fork: ") + std::strerror(errno);
            return false;
        }
        if (child == 0)
        {
            if (how == ProcessMode::Terminal)
            {
                Pty::AttachChild(slave);          // a new session: it leads its own group
                close(master);
            }
            else
            {
                JobGroup::BecomeLeader();
                dup2(inPipe[0], 0);
                dup2(outPipe[1], 1);
                dup2(how == ProcessMode::Pipes ? errPipe[1] : outPipe[1], 2);
                close(inPipe[0]); close(inPipe[1]);
                close(outPipe[0]); close(outPipe[1]);
                if (errPipe[0] >= 0) { close(errPipe[0]); close(errPipe[1]); }
            }
            signal(SIGPIPE, SIG_DFL);
            if (!dir.empty() && chdir(dir.c_str()) != 0) _exit(126);
            execve(cargv[0], cargv.data(), envp.data());
            _exit(127);
        }

        pid = child;
        reaped = false;
        exitCode = 0;
        if (how == ProcessMode::Terminal)
        {
            inFd = master;
            outFd = dup(master);
            errFd = -1;
        }
        else
        {
            close(inPipe[0]);
            close(outPipe[1]);
            inFd = inPipe[1];
            outFd = outPipe[0];
            fcntl(inFd, F_SETFD, FD_CLOEXEC);
            fcntl(outFd, F_SETFD, FD_CLOEXEC);
            if (how == ProcessMode::Pipes)
            {
                close(errPipe[1]);
                errFd = errPipe[0];
                fcntl(errFd, F_SETFD, FD_CLOEXEC);
            }
            else errFd = -1;
        }
        if (closeInput && how != ProcessMode::Terminal) CloseInput();

        int o = outFd, e = errFd;
        outReader = std::thread([this, o]() { ReadLoop(o, false); });
        if (e >= 0) errReader = std::thread([this, e]() { ReadLoop(e, true); });
        return true;
    }

    void Process::ReadLoop(int fd, bool isError)
    {
        char buf[8192];
        for (;;)
        {
            ssize_t n = read(fd, buf, sizeof buf);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;          // end of file - or EIO when a terminal's child has gone
            std::string bytes(buf, (size_t)n);
            std::lock_guard<std::mutex> hold(handlerLock);
            if (isError) { if (OnError) OnError(bytes); }
            else if (OnOutput) OnOutput(bytes);
        }
        close(fd);
    }

    bool Process::Write(const std::string& bytes)
    {
        std::lock_guard<std::mutex> hold(stateLock);
        if (inFd < 0) return false;
        size_t done = 0;
        while (done < bytes.size())
        {
            ssize_t n = write(inFd, bytes.data() + done, bytes.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            done += (size_t)n;
        }
        return true;
    }

    void Process::CloseInput()
    {
        std::lock_guard<std::mutex> hold(stateLock);
        if (inFd >= 0 && mode != ProcessMode::Terminal) { close(inFd); inFd = -1; }
        else if (inFd >= 0 && reaped) { close(inFd); inFd = -1; }
    }

    void Process::Reap(bool block)
    {
        pid_t child;
        {
            std::lock_guard<std::mutex> hold(stateLock);
            if (reaped || pid <= 0) return;
            child = pid;
        }
        // waitpid is not under the lock: a Write from another thread must not wait for the child to end
        int status = 0;
        pid_t r;
        do { r = waitpid(child, &status, block ? 0 : WNOHANG); } while (r < 0 && errno == EINTR);
        std::lock_guard<std::mutex> hold(stateLock);
        if (reaped) return;
        if (r == child)
        {
            reaped = true;
            if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) exitCode = 128 + WTERMSIG(status);
        }
        else if (r < 0) reaped = true;
    }

    bool Process::Running()
    {
        Reap(false);
        std::lock_guard<std::mutex> hold(stateLock);
        return pid > 0 && !reaped;
    }

    bool Process::Wait(int timeoutMs)
    {
        if (pid <= 0) return true;
        if (timeoutMs < 0) Reap(true);
        else
        {
            for (int waited = 0; ; waited += 10)
            {
                if (!Running()) break;
                if (waited >= timeoutMs) return false;
                usleep(10 * 1000);
            }
        }
        // a terminal's master end gives EIO only once the child is gone; close our side
        if (mode == ProcessMode::Terminal)
        {
            std::lock_guard<std::mutex> hold(stateLock);
            if (inFd >= 0) { close(inFd); inFd = -1; }
        }
        // its output is all read once the readers end (a grandchild holding the pipe
        // open is given 30 s, as GRIDEd's ToolRunner gives it)
        for (int waited = 0; waited < 30000; waited += 10)
        {
            bool done = true;
            if (outReader.joinable() && JobGroup::Alive(pid)) done = false;
            if (done) break;
            usleep(10 * 1000);
        }
        if (outReader.joinable() && !JobGroup::Alive(pid)) outReader.join();
        if (errReader.joinable() && !JobGroup::Alive(pid)) errReader.join();
        return true;
    }

    int Process::ExitCode()
    {
        std::lock_guard<std::mutex> hold(stateLock);
        return exitCode;
    }

    void Process::Kill()
    {
        if (pid > 0) JobGroup::Kill(pid);
    }

    void Process::End(int graceMs)
    {
        if (pid > 0) JobGroup::End(pid, graceMs);
    }

    void Process::Resize(int columns, int rows)
    {
        std::lock_guard<std::mutex> hold(stateLock);
        if (mode == ProcessMode::Terminal && inFd >= 0) Pty::Resize(inFd, columns, rows);
    }
}}
