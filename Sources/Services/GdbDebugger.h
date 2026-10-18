// =============================================================================
//  GdbDebugger.h - RIDE's debugger core (src/debugger.cpp) for GRIDEm
//  (GRIDEd's Services/GdbDebugger.h)
//
//    DebuggerKind     lldb, gdb, cdb, and the C6x kind: vm6747 --debug and sim6747
//                     --run --debug, which speak gdb's words and answer in gdb's shapes
//    DebugTranscript  what a debugger said, read: where it stopped, the variables, the
//                     frames, a value, a type, an address, the program's own output;
//                     and a recorded session read back for the replay tests
//    ElfImage         whether a linked C6747 image carries a .debug_line with rows in
//                     the program's code (what sim6747 --run --debug stops on)
//    DebuggerChoice   which engine debugs a build, which backend, and the command
//                     line - from toolchain.json's debuggers and sessions
//    ProcessDebugger  the engine: a debugger on a pipe, asked one command at a time
//                     with a marker after it; the moves on a worker thread
//
//  The spellings of the commands come from the tool's session in toolchain.json;
//  the shapes read are the dialect's (gdb / lldb / cdb). The sentences are RIDE's.
//  On the Mac lldb debugs macOS programs and sim6747 / vm6747 the C6747's; cdb is
//  Windows only, but its reader stays so recorded cdb sessions still read.
// =============================================================================
#pragma once
#include "Debugger.h"
#include "DebugProcess.h"
#include "RideToolchain.h"
#include "../Settings/GrideSettings.h"
#include "../Settings/ToolchainDefinitions.h"
#include "../Settings/ToolchainSettings.h"
#include "../Model/BuildConfiguration.h"
#include "../Model/GrideProject.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GRIDEm { namespace Services {

    // the engines; Shalimar is no debugger but the program itself, which stops itself (ShalimarSession)
    enum class DebuggerKind { None, Lldb, Gdb, Cdb, C6x, Shalimar };

    // RIDE's Stop: what a move ended in
    class DebugStop
    {
    public:
        DebugStop() : Stopped(false), Exited(false), Status(0), Line(0) {}
        bool Stopped;
        bool Exited;
        int Status;
        std::string File;
        int Line;
        std::string Function;
        std::string Said;
    };
    typedef std::shared_ptr<DebugStop> DebugStopPtr;

    // one command and the answer it got, as a recorded session holds them
    struct DebugExchange
    {
        std::string Command;       // what was said ("" for what came before the first command)
        std::string Answer;        // everything read until the next command
    };

    // ---- reading what a debugger said -----------------------------------------------------------
    class DebugTranscript
    {
    public:
        static bool GdbLike(DebuggerKind k) { return k == DebuggerKind::Gdb || k == DebuggerKind::C6x; }
        static DebuggerKind DialectOf(const std::string& dialect);
        static std::vector<std::string> Lines(const std::string& text);
        // the prompts off the front of a line: "(gdb) ", "(lldb) ", cdb's "0:000> " (and several in a row)
        static std::string WithoutPrompt(const std::string& line);
        static DebuggerKind PromptOn(const std::string& line);

        // ---- where it stopped (dbg_readStop) ----
        static DebugStopPtr ReadStop(DebuggerKind kind, const std::string& said);
        static DebugStopPtr ReadCdbStop(const std::string& said);
        // "... in main () at file.c:11[:col]" -> file, line (never the column), function (RIDE's placeIn)
        static bool PlaceIn(const std::string& line, std::string& file, int& ln, std::string& function);
        // a step that did not go anywhere (lldb and cdb): the same place at a higher address, not a breakpoint
        static bool WentNowhere(DebuggerKind kind, const DebugStopPtr& before, const DebugStopPtr& after);

        // ---- the variables, the frames (reading stops at main), a value, a type, an address ----
        static std::vector<DebugVariable> ReadVariables(DebuggerKind kind, const std::string& said, const std::string& marker);
        static std::vector<DebugFrame> ReadFrames(DebuggerKind kind, const std::string& said, const std::string& marker);
        static std::string ReadValue(DebuggerKind kind, const std::string& said, const std::string& marker);
        static std::string ReadType(DebuggerKind kind, const std::string& said, const std::string& marker);
        // the first "0x..." in an answer, cdb's backtick taken out, or ""
        static std::string AddressIn(const std::string& said);
        // the complaint in an answer, or "" (RIDE's complaintIn)
        static std::string ComplaintIn(const std::string& answer);
        static bool StoppedWithNoSource(const std::string& said);

        // ---- the program's own output (dbg_programOutput): the debugger's lines dropped, unknown lines kept ----
        static std::vector<std::string> ProgramOutput(DebuggerKind kind, const std::string& said, const std::string& markerStart);

        // ---- the console's escapes and echoes (a debugger on a pseudo-terminal) ----
        static std::string WithoutEscapes(const std::string& text);
        static std::string WithoutEcho(const std::string& said, const std::string& asked, const std::string& markerCommand);

        // ---- the registers and memory ----
        static std::vector<RegisterValue> ReadRegisters(DebuggerKind kind, const std::string& said, const std::string& marker);
        static std::vector<unsigned char> ReadMemoryBytes(DebuggerKind kind, const std::string& said, const std::string& marker, int wanted);

        // ---- RIDE's lines ----
        static std::string StopLine(const std::string& file, int line, const std::string& function);
        static std::string LookingAt(const DebugFrame& frame);
        static std::string FrameLine(const DebugFrame& frame, bool looking);
        static std::string VariableLine(const DebugVariable& v);
        static std::string WatchLine(const DebugWatch& w);

        // ---- a recorded session (Tests/Fixtures): DebugProcess's transcript - "> command" lines
        //      for what was said, everything else what came back ("! " lines: standard error) ----
        static std::vector<DebugExchange> ReadRecorded(const std::string& text);
        static bool LoadRecorded(const std::string& path, std::vector<DebugExchange>& exchanges, std::string& error);

        static bool Digits(const std::string& s);
        static int ParseInt(const std::string& s, int base);
        // the last part of a path written with '/' or '\'
        static std::string LeafName(const std::string& path);

        static bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
        static unsigned long long ParseHex64(const std::string& s);

    private:
        static bool HasDigit(const std::string& s);
        static int HexDigit(char c);
        static bool SourceEcho(const std::string& line);
        static bool LldbOwn(const std::string& line);
        static bool GdbOwn(const std::string& line);
        static bool OurCdbCommand(const std::string& bare);
        static bool CdbOwn(const std::string& line);
        static bool CdbRefused(const std::string& line);
        static unsigned long long LldbFrameAddress(const std::string& said);
        static unsigned long long CdbStopAddress(const std::string& said);
        static std::string AfterFrameNumber(DebuggerKind kind, const std::string& line);
        static bool CdbFrameIn(const std::string& line, std::string& file, int& ln, std::string& function);
    };

    // ---- a linked C6747 image's line table (dbg_imageHasLines) ------------------------------------------
    class ElfImage
    {
    public:
        // a 32-bit little-endian ELF whose .debug_line has a unit whose first opcode sets an address inside an executable section
        static bool HasLines(const std::string& out, std::string& why);
    };

    // RIDE's sentences about a Shalimar program (src/shalimar/session.cpp)
    class ShalimarWords
    {
    public:
        static const char* const SaysWhereOnly;
        static const char* const SaysHowDeepOnly;
        static const char* const ReleaseHasNoSession;
        static const char* const DidNotArm;
        static const char* const StopsItself;
    };

    // ---- which engine debugs a build (dbg_for, dbg_c6xBackend, dbg_whyNot, dbg_planFor) ------------------------
    class DebugLaunch
    {
    public:
        DebugLaunch() : Kind(DebuggerKind::None) {}
        DebuggerKind Kind;
        Settings::ToolDefinitionPtr Tool;          // the debugger's definition (its session)
        Settings::SessionDefinitionPtr Session;
        std::string Command;                       // the command line that starts it
        std::string WorkDir;
        std::vector<std::string> Environment;      // NAME=value the child starts with (the session's)
        std::string Backend;                       // vm6747 / sim6747 / cdb / gdb / lldb
        std::string BackendWhy;                    // why vm6747 when sim6747 was not chosen
        std::string WhyNot;                        // RIDE's sentence when nothing can debug the build
        std::string Program;                       // what the debugger is given
        std::vector<std::string> Blind;            // the parts (folders) carrying no debug information
        bool Possible() const { return Kind != DebuggerKind::None && Session && !Command.empty(); }
    };
    typedef std::shared_ptr<DebugLaunch> DebugLaunchPtr;

    class DebuggerChoice
    {
    public:
        // program: the build's program (the .out on tms6747); assembly: what the emulator runs (a .s or a .vm
        // directory); shalimar: a Shalimar program, which stops itself where no debugger reads it (RIDE's
        // dbg_planFor: the session only where the engine would be none)
        static DebugLaunchPtr For(const Model::GrideProjectPtr& p, const Model::BuildConfigurationPtr& cfg,
                                  const Settings::ToolchainSettingsPtr& tc, const Settings::GrideSettingsPtr& s,
                                  const std::string& program, const std::string& assembly, bool shalimar,
                                  const std::string& shalimarRuntimeDir);
        // RIDE's session for a Shalimar program where no engine reads it: the program itself, started with
        // the session's environment - or, on tms6747, its .out under the simulator's session operation
        static DebugLaunchPtr OwnSession(const DebugLaunchPtr& l, const Settings::ToolDefinitionPtr& shc,
                                         const Settings::SessionDefinitionPtr& own, const std::string& arch,
                                         const std::string& why, bool underSimulator,
                                         const Settings::ToolDefinitionPtr& simDef, const std::string& simulator,
                                         const std::string& program);
        // RIDE's dbg_c6xBackend with GRIDE's setting over it: auto, sim6747, vm6747; why says what decided it
        static std::string C6xBackend(const std::string& assembly, const std::string& out, const std::string& simulator,
                                      const std::string& simName, const std::string& vmName, const std::string& setting,
                                      std::string& why);
    };

    // ---- the engine --------------------------------------------------------------------------------------
    class ProcessDebugger : public DebugEngine
    {
    public:
        using DebugEngine::Evaluate;

        static const int AskTimeoutMs = 30000;        // RIDE's readUntil
        static const int MoveTimeoutMs = 600000;      // a move waits for the program; Stop ends the wait

        explicit ProcessDebugger(const DebugLaunchPtr& launch);
        ~ProcessDebugger() override;

        std::string Name() const override;
        std::string Backend() const override { return launch->Backend; }
        std::string BackendWhy() const override { return launch->BackendWhy; }
        DebuggerKind Kind() const { return kind; }
        bool Running();
        DebugStopPtr LastStop() const { return last; }
        // everything the debugger said in this session, for a report
        std::string Said();
        // what DebugProcess recorded: each line said ("> "), every answer with its marker -
        // the .transcript of Tests/Fixtures; the last session's once it has ended
        std::string Transcript();

        // RIDE's start: the command, the preamble, the marker and the wait for it; then every breakpoint, then run
        bool Start(const std::string& program, Core::Target target, const std::string& entryFile, int entryLine) override;
        // the session without the run (the tests drive the moves themselves)
        bool StartSession();
        void Stop() override;
        void Continue() override;
        void StepOver() override;
        void StepInto() override;
        void StepOut() override;
        void BreakAll() override;
        void RunToCursor(const std::string& file, int line) override;

        // ---- the moves, synchronous (the tests' way in; the overrides above run these on a worker) ----
        DebugStopPtr RunNow()      { return MoveNow("run", false); }
        DebugStopPtr ContinueNow() { return MoveNow("continue", false); }
        DebugStopPtr StepOverNow() { return MoveNow("stepOver", true); }
        DebugStopPtr StepIntoNow() { return MoveNow("stepInto", true); }
        DebugStopPtr StepOutNow()  { return MoveNow("stepOut", false); }

        // ---- what the engine answers while stopped ----
        std::vector<DebugVariable> Locals() override;
        std::string WhyNoLocals() override;
        std::vector<DebugFrame> CallStack() override;
        bool SelectFrame(int index) override;
        std::string Evaluate(const std::string& expression) override;
        std::string Evaluate(const std::string& expression, bool& ok) override;
        std::string AddressOf(const std::string& expression) override;
        std::string TypeOf(const std::string& expression) override;
        bool SetVariable(const std::string& name, const std::string& value, std::string& said) override;
        std::vector<RegisterValue> Registers() override;
        std::vector<unsigned char> ReadMemory(unsigned long long address, int length) override;
        void BreakpointsChanged() override;

        // one command, then the marker, then what came before it (RIDE's ask)
        std::string Ask(const std::string& command);

        // ---- Shalimar's arrays on Windows (cdb): a pointer to the runtime's {int32 count, int32 element, data} ----
        static bool ShalimarArrayType(const std::string& type);
        static int ShalimarRank(const std::string& type);
        static std::string ShalimarScalar(const std::string& type);
        static bool HexValue(const std::string& s, unsigned long long& value);
        std::string ShalimarArray(unsigned long long record, int rank, const std::string& scalar, int depth);

    private:
        typedef std::shared_ptr<DebugProcess> DebugProcessPtr;

        DebugProcessPtr Proc();
        DebugProcessPtr TakeProc();
        void AppendSaid(const std::string& text);
        std::string Fill(const std::string& templ, const std::string& file, int line) const;
        void SetEveryBreakpoint();
        void BeginMove(const std::string& command, bool stepping);
        DebugStopPtr MoveNow(const std::string& commandId, bool stepping);
        DebugStopPtr AfterMoving(const std::string& commandId);
        DebugStopPtr AfterStepping(const std::string& commandId);
        bool HasSource(const DebugStopPtr& stop) const;
        static bool SamePlace(const DebugStopPtr& a, const DebugStopPtr& b);
        static std::string EchoedLine(const std::string& said);
        DebugStopPtr CdbStepInto(const DebugStopPtr& origin);
        static bool AtFunctionEntry(const std::string& said);
        std::string MarkerStart() const;
        void Announce(const DebugStopPtr& stop);

        DebugLaunchPtr launch;
        Settings::SessionDefinitionPtr session;
        DebuggerKind kind;
        DebuggerKind dialect;
        DebugProcessPtr proc;
        DebugProcessPtr lastProc;      // the session TakeProc took last, kept for Transcript
        std::mutex procLock;           // guards proc (the pointer)
        DebugStopPtr last;
        std::mutex gate;               // one exchange with the debugger at a time
        std::mutex saidLock;
        std::string said;
        std::atomic<bool> moving;
        std::thread mover;
        bool onConsole;
    };
}}
