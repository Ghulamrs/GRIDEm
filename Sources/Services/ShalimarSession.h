// =============================================================================
//  ShalimarSession.h - RIDE's shalimar::Session for GRIDEm (GRIDEd's Services/ShalimarSession.h).
//  A Shalimar program stops itself: the compiler calls shm_line before every statement, and a
//  Debug build's runtime, started with the session's environment (SHM_DEBUG=1), talks on its
//  standard error while the program prints on its standard output - a line for every source
//  (#file <unit> <leaf>), one when it is ready, one at each stop (#stop <unit> <line> <depth>) and
//  one at the end (#exit <status>). What it is told is the session's commands in toolchain.json
//  (b / d unit line, c, n, s, o, w, q). On tms6747 the .out runs under the simulator. No
//  variables, and one frame that says how deep it is: RIDE's sentences say so.
// =============================================================================
#pragma once
#include "GdbDebugger.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GRIDEm { namespace Services {

    class ShalimarSession : public DebugEngine
    {
    public:
        using DebugEngine::Evaluate;

        static const int ReadyTimeoutMs = 5000;    // RIDE's wait for each line before the ready line
        static const int SettleMs = 150;           // the shipped runtime can say ready before a file line: a moment for the rest
        static const int MoveTimeoutMs = 600000;   // a move waits for the program; Stop ends the wait

        explicit ShalimarSession(const DebugLaunchPtr& launch);
        ~ShalimarSession() override;

        std::string Name() const override { return "the program itself (the Shalimar session)"; }
        std::string Backend() const override { return launch->Backend; }
        std::string BackendWhy() const override { return launch->BackendWhy; }
        bool Running();
        DebugStopPtr LastStop() const { return last; }
        // unit -> the source's leaf name, as the program named them
        const std::map<int, std::string>& Units() const { return units; }
        int Depth() const { return depth; }
        // every protocol line heard, for a report
        std::string Said();
        // what DebugProcess recorded: each line said ("> "), every answer with its marker -
        // the .transcript of Tests/Fixtures; the last session's once it has ended
        std::string Transcript();

        bool Start(const std::string& program, Core::Target target, const std::string& entryFile, int entryLine) override;
        // RIDE's Session::start: the program with the session's environment, every file line until the
        // ready line; then the first command and the breakpoints
        bool StartSession();
        void Stop() override;
        void Continue() override;
        void StepOver() override;
        void StepInto() override;
        void StepOut() override;
        void BreakAll() override;
        // a breakpoint on the line for this run alone, taken away at the next stop
        void RunToCursor(const std::string& file, int atLine) override;

        // ---- the moves, synchronous (the tests' way in; the overrides above run these on a worker) ----
        DebugStopPtr RunNow()      { return MoveNow("run"); }
        DebugStopPtr ContinueNow() { return MoveNow("continue"); }
        DebugStopPtr StepOverNow() { return MoveNow("stepOver"); }
        DebugStopPtr StepIntoNow() { return MoveNow("stepInto"); }
        DebugStopPtr StepOutNow()  { return MoveNow("stepOut"); }

        // ---- what the program answers while stopped: where it is, how deep, and nothing else ----
        std::vector<DebugVariable> Locals() override { return std::vector<DebugVariable>(); }
        std::string WhyNoLocals() override { return ShalimarWords::SaysWhereOnly; }
        std::vector<DebugFrame> CallStack() override;
        bool SelectFrame(int index) override { (void)index; return false; }
        std::string WhyNoStackWalk() override { return ShalimarWords::SaysHowDeepOnly; }
        std::string Evaluate(const std::string& expression) override;
        std::string Evaluate(const std::string& expression, bool& ok) override;
        bool SetVariable(const std::string& name, const std::string& value, std::string& said) override;
        std::vector<RegisterValue> Registers() override { return std::vector<RegisterValue>(); }
        std::vector<unsigned char> ReadMemory(unsigned long long address, int length) override;
        // the breakpoints changed while stopped: the ones set taken away, the ones wanted set
        void BreakpointsChanged() override;

    private:
        typedef std::shared_ptr<DebugProcess> DebugProcessPtr;

        DebugProcessPtr Proc();
        DebugProcessPtr TakeProc();
        void AppendSaid(const std::string& text);
        static std::string Fill(const std::string& templ, int u, int l);
        bool Register(const std::string& said);     // "#file 0 steps.shl" remembered; false for any other line
        int UnitFor(const std::string& file) const;
        std::string FileFor(int u) const;
        int SendWanted(const DebugProcessPtr& p);
        static bool Three(const std::string& rest, int& a, int& b, int& c);
        void BeginMove(const std::string& command);
        DebugStopPtr MoveNow(const std::string& commandId);
        DebugStopPtr Listen(const DebugProcessPtr& p, int timeoutMs);
        void WaitForEnd(const DebugProcessPtr& p, int ms);
        std::string PrintProgramLines(const DebugProcessPtr& p);
        void Announce(const DebugStopPtr& stop, bool ran);

        DebugLaunchPtr launch;
        Settings::SessionDefinitionPtr session;
        DebugProcessPtr proc;
        DebugProcessPtr lastProc;      // the session TakeProc took last, kept for Transcript
        std::mutex procLock;
        DebugStopPtr last;
        std::mutex gate;
        std::mutex saidLock;
        std::string said;
        std::atomic<bool> moving;
        std::thread mover;
        std::map<int, std::string> units;
        std::vector<std::pair<int, int> > sent;      // unit, line of every breakpoint told to the program
        int unit, line, depth;
        int temporaryUnit, temporaryLine;
    };
}}
