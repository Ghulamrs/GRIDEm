// =============================================================================
//  Debugger.h - debugger abstraction + placeholder implementation
//  (GRIDEd's Services/Debugger.h)
//
//  DebugEngine         abstract base the IDE talks to (Debug menu, panes)
//  PlaceholderDebugger simulates a session (break, step, registers, memory)
//                      where nothing can debug a build.
//
//  Evaluate has two forms; an engine that overrides one writes
//  `using DebugEngine::Evaluate;` so the other stays callable (C++ hides it otherwise).
// =============================================================================
#pragma once
#include "BuildMessage.h"
#include "../Core/LanguageTarget.h"
#include "../Core/StringUtil.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    enum class DebugState { Idle, Running, Break, Terminated };

    class StopInfo
    {
    public:
        StopInfo(const std::string& file, int line, const std::string& reason, const std::string& function = std::string())
            : File(file), Line(line), Reason(reason), Function(function) {}
        std::string File;
        int Line;                  // 1-based
        std::string Reason;        // "breakpoint", "step", ...
        std::string Function;      // the function it stopped in, when the engine says ("main"); "" for a Shalimar stop
    };
    typedef std::shared_ptr<StopInfo> StopInfoPtr;

    struct RegisterValue
    {
        RegisterValue(const std::string& n, const std::string& v) : Name(n), Value(v) {}
        std::string Name;
        std::string Value;
    };

    struct DebugFrame
    {
        DebugFrame(const std::string& fn, const std::string& file, int line) : Function(fn), File(file), Line(line) {}
        std::string Function;
        std::string File;
        int Line;
    };

    struct DebugVariable
    {
        DebugVariable(const std::string& n, const std::string& v, const std::string& t, const std::string& a = std::string())
            : Name(n), Value(v), Type(t), Address(a) {}
        std::string Name;
        std::string Value;
        std::string Type;
        std::string Address;       // "0x..." where the variable lives, "" in a register or unknown
    };

    // RIDE's Watch: an expression read again at every stop and frame change
    struct DebugWatch
    {
        explicit DebugWatch(const std::string& expression) : Expression(expression), Ok(false) {}
        std::string Expression;
        std::string Value;         // the value, or what the debugger said when it could not answer
        std::string Type;
        std::string Address;
        bool Ok;
    };

    // full path -> 1-based lines, shared with the editors and the workspace
    typedef std::map<std::string, std::set<int>, Core::CaseInsensitiveLess> BreakpointMap;
    typedef std::shared_ptr<BreakpointMap> BreakpointMapPtr;

    typedef std::function<void(DebugState s)> DebugStateHandler;
    typedef std::function<void(const StopInfoPtr& info)> DebugStopHandler;

    // -------------------------------------------------------------------------
    //  DebugEngine - the contract between the IDE and any debugger
    // -------------------------------------------------------------------------
    class DebugEngine
    {
    public:
        virtual ~DebugEngine() {}

        DebugStateHandler OnStateChanged;
        DebugStopHandler OnStopped;
        TextLineHandler OnOutput;             // the engine's own lines (the Output pane's Debug source)
        TextLineHandler OnProgramOutput;      // the program's own lines, as heard through the debugger

        DebugState State() const { return state; }
        bool IsActive() const { return state == DebugState::Running || state == DebugState::Break; }
        StopInfoPtr Location() const { return location; }

        virtual std::string Name() const = 0;
        // the program behind the engine (vm6747 / sim6747 for the C6x), and why that one
        virtual std::string Backend() const { return Name(); }
        virtual std::string BackendWhy() const { return std::string(); }
        int CurrentFrame() const { return currentFrame; }
        int ExitStatus() const { return exitStatus; }
        bool Exited() const { return exited; }

        // program = linked output, entryFile/entryLine = where main() starts
        virtual bool Start(const std::string& program, Core::Target target, const std::string& entryFile, int entryLine) = 0;
        virtual void Stop() = 0;
        virtual void Continue() = 0;
        virtual void BreakAll() = 0;
        virtual void StepInto() = 0;
        virtual void StepOver() = 0;
        virtual void StepOut() = 0;
        virtual void RunToCursor(const std::string& file, int line) = 0;

        virtual std::vector<RegisterValue> Registers() = 0;
        virtual std::vector<DebugFrame> CallStack() = 0;
        virtual std::vector<DebugVariable> Locals() = 0;
        virtual std::string Evaluate(const std::string& expression) = 0;
        virtual std::vector<unsigned char> ReadMemory(unsigned long long address, int length) = 0;

        // ---- what a real engine adds (RIDE's Debugger); an engine without them answers as below ----
        virtual bool SelectFrame(int index) { (void)index; return false; }
        virtual std::string Evaluate(const std::string& expression, bool& ok) { ok = true; return Evaluate(expression); }
        virtual std::string AddressOf(const std::string& expression) { (void)expression; return std::string(); }
        virtual std::string TypeOf(const std::string& expression) { (void)expression; return std::string(); }
        virtual bool SetVariable(const std::string& name, const std::string& value, std::string& said);
        virtual std::vector<DebugWatch>& Watches() { return watches; }
        virtual void AddWatch(const std::string& expression);
        virtual void SetWatch(int index, const std::string& expression);
        virtual void RemoveWatch(int index);
        virtual void ReadWatches();
        // the breakpoints changed while the session runs: an engine that must be told resends them
        virtual void BreakpointsChanged() {}
        virtual std::string WhyNoLocals() { return std::string(); }
        virtual std::string WhyNoStackWalk() { return std::string(); }

        void SetBreakpoints(const BreakpointMapPtr& bps) { if (bps) breakpoints = bps; }
        void SetSourceFiles(const std::vector<std::string>& files) { sources = files; }
        // a file as the debugger named it (a leaf, or a path) to the project's own
        std::string ResolveFile(const std::string& named) const;

    protected:
        DebugEngine();

        DebugState state;
        StopInfoPtr location;
        BreakpointMapPtr breakpoints;
        std::vector<std::string> sources;
        std::vector<DebugWatch> watches;
        int currentFrame;
        int exitStatus;
        bool exited;

        void SetState(DebugState s);
        void Print(const std::string& s);
        void PrintProgram(const std::string& s);
        void StopAt(const std::string& file, int line, const std::string& reason, const std::string& function = std::string());
    };

    // -------------------------------------------------------------------------
    //  PlaceholderDebugger - a simulation where nothing can debug a build
    // -------------------------------------------------------------------------
    class PlaceholderDebugger : public DebugEngine
    {
    public:
        using DebugEngine::Evaluate;

        PlaceholderDebugger();

        std::string Name() const override { return "Placeholder debugger (simulation)"; }
        bool Start(const std::string& program, Core::Target t, const std::string& entryFile, int entryLine) override;
        void Stop() override;
        void Continue() override;
        void BreakAll() override;
        void StepInto() override { Step(); }
        void StepOver() override { Step(); }
        void StepOut() override { Continue(); }
        void RunToCursor(const std::string& f, int line) override;
        std::vector<RegisterValue> Registers() override;
        std::vector<DebugFrame> CallStack() override;
        std::vector<DebugVariable> Locals() override;
        std::string Evaluate(const std::string& expression) override;
        std::vector<unsigned char> ReadMemory(unsigned long long address, int length) override;

    private:
        std::vector<std::string> lines;
        std::string file;
        Core::Target target;
        int steps;

        static std::string Hex32(int v);
        void Finish();
        void Step();
        int NextBreakpoint(int afterLine) const;
        int NextCodeLine(int from) const;
        int FunctionStartLine(int line) const;
        std::string FunctionAt(int line) const;
    };
}}
