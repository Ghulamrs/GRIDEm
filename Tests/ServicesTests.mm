// =============================================================================
//  ServicesTests.mm - reading what the tools say: diagnostics in the shapes of
//  GRIDEd's BuildMessage.h, command templates, RIDE's compiler rules, and what
//  gdb, lldb and cdb answer (DebugTranscript), live or recorded.
// =============================================================================
#import "TestSupport.h"
#include <mutex>
#include <stdlib.h>
#include <sys/stat.h>
#include "Settings/GrideSettings.h"
#include "Services/BuildEngine.h"
#include "Model/Workspace.h"
#include "Core/LanguageTarget.h"
#include "Services/BuildMessage.h"
#include "Services/Debugger.h"
#include "Services/GdbDebugger.h"
#include "Services/MacroExpander.h"
#include "Services/RideToolchain.h"

using namespace GRIDEm;
using namespace GRIDEm::Core;
using namespace GRIDEm::Services;

static BuildMessagePtr Parse(const std::string& line)
{
    return BuildMessageParser::TryParse(line, "/w");
}

@interface ServicesTests : XCTestCase
@end

@implementation ServicesTests

- (void)setUp
{
    GRLoadMacToolchain();
}

// ---- diagnostics (the shapes listed in GRIDEd's BuildMessage.h) -----------------

- (void)testMsvcStyle
{
    BuildMessagePtr m = Parse("kalman.c(4): warning W102: 'Q' could be const");
    XCTAssertTrue(m != nullptr);
    if (!m) return;
    XCTAssertTrue(m->Sev == Severity::Warning);
    GRAssertEq(m->File, "/w/kalman.c");
    GRAssertEq(m->Line, 4);
    GRAssertEq(m->Code, "W102");
    GRAssertEq(m->Text, "'Q' could be const");
    m = Parse("kalman.c(4,9): error E10: bad");
    XCTAssertTrue(m != nullptr && m->Sev == Severity::Error && m->Column == 9);
}

- (void)testTiStyle
{
    BuildMessagePtr m = Parse("\"kalman.c\", line 4: error #20: identifier \"x\" is undefined");
    XCTAssertTrue(m != nullptr);
    if (!m) return;
    GRAssertEq(m->Line, 4);
    GRAssertEq(m->Code, "#20");
    GRAssertEq(m->Text, "identifier \"x\" is undefined");
}

- (void)testGnuStyle
{
    BuildMessagePtr m = Parse("kalman.c:4:9: error: expected ';'");
    XCTAssertTrue(m != nullptr);
    if (!m) return;
    XCTAssertTrue(m->Sev == Severity::Error);
    GRAssertEq(m->Line, 4);
    GRAssertEq(m->Column, 9);
    GRAssertEq(m->Text, "expected ';'");
}

- (void)testAsm6xAndLinkStyles
{
    BuildMessagePtr m = Parse("\"main.asm\", ERROR!   at line 5: [E0002] Illegal mnemonic");
    XCTAssertTrue(m != nullptr);
    if (m)
    {
        GRAssertEq(m->File, "/w/main.asm");
        GRAssertEq(m->Line, 5);
        GRAssertEq(m->Code, "E0002");
    }
    m = Parse("LINK : fatal error LNK1104: cannot open file 'x.lib'");
    XCTAssertTrue(m != nullptr);
    if (m)
    {
        XCTAssertTrue(m->Sev == Severity::Error);
        GRAssertEq(m->Code, "LNK1104");
        GRAssertEq(m->File, "");
    }
}

- (void)testPlainOutputIsNoMessage
{
    XCTAssertTrue(Parse("hello there") == nullptr);
    XCTAssertTrue(Parse("") == nullptr);
}

- (void)testShalimarDiagnosticNamesTheSource
{
    BuildMessagePtr m = BuildMessageParser::TryParseShalimar("Error: line 7: unknown name 'q'", "/w/a.shl");
    XCTAssertTrue(m != nullptr);
    if (!m) return;
    GRAssertEq(m->File, "/w/a.shl");
    GRAssertEq(m->Line, 7);
    GRAssertEq(m->Text, "unknown name 'q'");
}

- (void)testCustomPatternWithNamedGroups
{
    MessagePatternPtr p = BuildMessageParser::Compile("^(?<file>[^@]+)@(?<line>\\d+) (?<msg>.*)$");
    XCTAssertTrue(p != nullptr);
    BuildMessagePtr m = BuildMessageParser::TryParse("thing.c@12 something odd", "/w", p);
    XCTAssertTrue(m != nullptr);
    if (m)
    {
        GRAssertEq(m->Line, 12);
        GRAssertEq(m->File, "/w/thing.c");
        GRAssertEq(m->Text, "something odd");
    }
}

- (void)testDiagnosticReaderKeepsTheFirstError
{
    DiagnosticReader r("/w", nullptr, "/w/a.c", "c90");
    r.Feed("a.c:3:1: warning: unused");
    r.Feed("a.c:5:2: error: first");
    r.Feed("a.c:9:2: error: second");
    XCTAssertTrue(r.First != nullptr);
    if (r.First) GRAssertEq(r.First->Line, 5);
}

// a line a tool writes about itself is held until it exits: an error only when it failed
// (regression: "c90: ..." from a tool that succeeded was counted as an error)
- (void)testHeldToolLinesAreErrorsOnlyWhenTheToolFailed
{
    DiagnosticReader r("/w", nullptr, "/w/a.c", "c90");
    XCTAssertTrue(r.Feed("c90: a note about itself") == nullptr);
    std::vector<BuildMessagePtr> held = r.TakeItself(false);
    GRAssertEq(held.size(), (size_t)1);
    if (held.size() == 1) XCTAssertTrue(held[0]->Sev == Severity::Info);
    XCTAssertTrue(r.First == nullptr);
    XCTAssertTrue(r.Feed("c90.exe: gave up") == nullptr);
    held = r.TakeItself(true);
    GRAssertEq(held.size(), (size_t)1);
    if (held.size() == 1) XCTAssertTrue(held[0]->Sev == Severity::Error);
    XCTAssertTrue(r.First != nullptr);
    XCTAssertTrue(r.TakeItself(true).empty());
}

- (void)testShownLinesAreRelativeToTheProject
{
    GRAssertEq(BuildEngine::Shown("/w/p/src/a.c:3:1: error: x", "/w/p"), "src/a.c:3:1: error: x");
    GRAssertEq(BuildEngine::Shown("  /w/p/a.c", "/w/p/"), "  a.c");
    GRAssertEq(BuildEngine::Shown("C:\\w\\p\\a.c(3): error", "C:/w/p"), "a.c(3): error");
    GRAssertEq(BuildEngine::Shown("/w/other/a.c", "/w/p"), "/w/other/a.c");
    GRAssertEq(BuildEngine::Shown("/w/p", "/w/p"), "/w/p");
}

// compile steps run on workers, the slow first one last to end, and are still read back in project
// order; a tool's lines past MaxToolLines are counted, not shown, but every error is counted
- (void)testParallelStepsKeepProjectOrderAndCountHiddenLines
{
    std::string dir = GRTempDir();
    std::string tools = dir + "/tools";
    mkdir(tools.c_str(), 0755);
    mkdir((dir + "/p").c_str(), 0755);
    mkdir((dir + "/p/src").c_str(), 0755);
    GRWrite(tools + "/c90",
            "#!/bin/sh\n"
            "src=\"\"; out=\"\"\n"
            "while [ $# -gt 0 ]; do case \"$1\" in -S) src=\"$2\"; shift;; -o) out=\"$2\"; shift;; esac; shift; done\n"
            "case \"$src\" in\n"
            "  *a.c) sleep 0.5; echo \"c90: a note about itself\"; : > \"$out\"; exit 0;;\n"
            "  *b.c) : > \"$out\"; exit 0;;\n"
            "  *c.c) i=1; while [ $i -le 60 ]; do echo \"$src:$i:1: error: bad $i\"; i=$((i+1)); done; echo \"c90: gave up\"; exit 1;;\n"
            "esac\n");
    GRWrite(tools + "/asm6x",
            "#!/bin/sh\n"
            "while [ $# -gt 0 ]; do case \"$1\" in -o) : > \"$2\"; shift;; esac; shift; done\n"
            "exit 0\n");
    chmod((tools + "/c90").c_str(), 0755);
    chmod((tools + "/asm6x").c_str(), 0755);
    GRWrite(dir + "/p/src/a.c", "int a;\n");
    GRWrite(dir + "/p/src/b.c", "int b;\n" + std::string(4000, ' ') + "\n");
    GRWrite(dir + "/p/src/c.c", "int c;\n");
    GRWrite(dir + "/p/p.pro",
            "{ \"gride\": \"1.0\", \"type\": \"project\", \"name\": \"p\", \"language\": \"C90\", \"template\": \"Empty\","
            " \"targets\": [\"TMS320C6747\"], \"folders\": { \"src\": [\"src/a.c\", \"src/b.c\", \"src/c.c\"] },"
            " \"configurations\": { \"Debug|TMS320C6747\": {} } }\n");
    setenv("C90", (tools + "/c90").c_str(), 1);
    setenv("ASM6X", (tools + "/asm6x").c_str(), 1);

    Model::GrideProjectPtr p = Model::GrideProject::Load(dir + "/p/p.pro");
    Settings::GrideSettingsPtr settings = Settings::GrideSettings::FromJson(Core::JsonValue::MakeObject());
    settings->Env->BuildJobs = 3;
    settings->PublishPaths();
    Services::BuildRequestPtr r = std::make_shared<Services::BuildRequest>();
    r->Projects.push_back(p);
    r->Config = "Debug";
    r->TargetKind = Core::Target::C6747;
    r->Action = Services::BuildAction::Rebuild;
    r->WorkspaceDir = dir + "/p";
    std::mutex lock;
    std::vector<std::string> output;
    std::vector<BuildMessagePtr> messages;
    Services::BuildEngine engine(settings);
    engine.OnOutput = [&lock, &output](const std::string& line) { std::lock_guard<std::mutex> hold(lock); output.push_back(line); };
    engine.OnMessage = [&lock, &messages](const BuildMessagePtr& m) { std::lock_guard<std::mutex> hold(lock); messages.push_back(m); };
    Services::BuildSummaryPtr s = engine.RunBatch(std::vector<Services::BuildRequestPtr>(1, r));
    unsetenv("C90");
    unsetenv("ASM6X");
    std::string all;
    for (const std::string& l : output) all += l + "\n";
    size_t a = all.find("a.c  ->"), b = all.find("b.c  ->"), c = all.find("c.c  ->");
    XCTAssertTrue(a != std::string::npos && b != std::string::npos && c != std::string::npos && a < b && b < c, @"%s", all.c_str());
    XCTAssertTrue(all.find("\nsrc/c.c:40:1: error: bad 40\n") != std::string::npos, @"%s", all.c_str());
    XCTAssertTrue(all.find("bad 41") == std::string::npos, @"%s", all.c_str());
    XCTAssertTrue(all.find("[c90: 21 more line(s) not shown]") != std::string::npos, @"%s", all.c_str());
    XCTAssertTrue(s && !s->Ok());
    if (s) GRAssertEq(s->Errors(), 61);
    bool noteIsInfo = false;
    for (const BuildMessagePtr& m : messages)
        if (m->Text == "a note about itself") noteIsInfo = m->Sev == Severity::Info;
    XCTAssertTrue(noteIsInfo);
}

// ---- command templates ---------------------------------------------------------

- (void)testMacroExpander
{
    MacroExpander mx;
    mx.Set("Source", "a.c");
    mx.Set("Out", "o");
    GRAssertEq(mx.Expand("cc $(Source) -o $(Out) $(Nope)"), "cc a.c -o o $(Nope)");
    GRAssertEq(mx.Get("Source"), "a.c");
    GRAssertEq(MacroExpander::JoinFlags("-I", std::vector<std::string>{ "a", "b c" }), "-I\"a\" -I\"b c\"");
    GRAssertEq(MacroExpander::JoinDefines("-D", std::vector<std::string>{ "X=1" }), "-DX=1");
}

// ---- RIDE's compiler rules ------------------------------------------------------

- (void)testRideToolchainRules
{
    XCTAssertTrue(RideToolchain::Resolve(RideTool::Auto, Language::Shalimar) == RideTool::Shalimar);
    XCTAssertTrue(RideToolchain::Resolve(RideTool::Auto, Language::Cpp11) == RideTool::Cpp11);
    XCTAssertTrue(RideToolchain::CanCompile(RideTool::C90, Language::C90));
    XCTAssertFalse(RideToolchain::CanCompile(RideTool::C90, Language::Cpp11));
    XCTAssertTrue(RideToolchain::Refusal(RideTool::C90, Language::Cpp11).find("cpp11") != std::string::npos);
    GRAssertEq(RideToolchain::Word(RideTool::Shalimar), "shalimar");
    GRAssertEq(RideToolchain::Arch(Target::C6747), "tms6747");
    XCTAssertTrue(RideToolchain::IsEmulated(Target::C6747));
}

// ---- what debuggers say ---------------------------------------------------------

- (void)testGdbStopAndExit
{
    DebugStopPtr s = DebugTranscript::ReadStop(DebuggerKind::Gdb, "Breakpoint 1, main () at hello.c:5\n5\t  int x = 1;\n");
    XCTAssertTrue(s->Stopped);
    GRAssertEq(s->File, "hello.c");
    GRAssertEq(s->Line, 5);
    GRAssertEq(s->Function, "main");
    s = DebugTranscript::ReadStop(DebuggerKind::Gdb, "[Inferior 1 (process 42) exited with code 07]\n");
    XCTAssertTrue(s->Exited);
    GRAssertEq(s->Status, 7);
}

- (void)testLldbStopAndExit
{
    DebugStopPtr s = DebugTranscript::ReadStop(DebuggerKind::Lldb,
        "Process 123 stopped\n* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
        "    frame #0: 0x0000000100003f80 hello`main at hello.c:5:7\n");
    XCTAssertTrue(s->Stopped);
    GRAssertEq(s->File, "hello.c");
    GRAssertEq(s->Line, 5);
    GRAssertEq(s->Function, "main");
    s = DebugTranscript::ReadStop(DebuggerKind::Lldb, "Process 123 exited with status = 3 (0x00000003)\n");
    XCTAssertTrue(s->Exited);
    GRAssertEq(s->Status, 3);
}

- (void)testCdbStopAndExit
{
    DebugStopPtr s = DebugTranscript::ReadStop(DebuggerKind::Cdb, "Breakpoint 0 hit\nC:\\x\\hello.c(5)+0x1e\nhello!main+0x1e:\n");
    XCTAssertTrue(s->Stopped);
    GRAssertEq(s->File, "C:\\x\\hello.c");
    GRAssertEq(s->Line, 5);
    GRAssertEq(s->Function, "main");
    GRAssertEq(DebugTranscript::LeafName(s->File), "hello.c");
    s = DebugTranscript::ReadStop(DebuggerKind::Cdb, "ntdll!NtTerminateProcess+0x14:\nExit process 0:1a2c, code 3\n");
    XCTAssertTrue(s->Exited);
    GRAssertEq(s->Status, 3);
}

// cdb's "?? x": the value after the type, a quoted text or a braced list whole, true and false values too
- (void)testCdbValues
{
    const std::string m = "<<gride>>";
    GRAssertEq(DebugTranscript::ReadValue(DebuggerKind::Cdb, "int 0n42\n", m), "42");
    GRAssertEq(DebugTranscript::ReadValue(DebuggerKind::Cdb, "char [12] \"Alice Smith\"\n", m), "\"Alice Smith\"");
    GRAssertEq(DebugTranscript::ReadType(DebuggerKind::Cdb, "char [12] \"Alice Smith\"\n", m), "char [12]");
    GRAssertEq(DebugTranscript::ReadValue(DebuggerKind::Cdb, "int [3] {5, 7, 13}\n", m), "{5, 7, 13}");
    GRAssertEq(DebugTranscript::ReadType(DebuggerKind::Cdb, "int [3] {5, 7, 13}\n", m), "int [3]");
    GRAssertEq(DebugTranscript::ReadValue(DebuggerKind::Cdb, "bool true\n", m), "true");
    GRAssertEq(DebugTranscript::ReadType(DebuggerKind::Cdb, "bool false\n", m), "bool");
    GRAssertEq(DebugTranscript::ReadValue(DebuggerKind::Cdb, "Couldn't resolve error at 'count'\n", m), "");
}

// a standard header named by its full path is a source cdb stopped in
- (void)testCdbStopInAStandardHeader
{
    DebugStopPtr s = DebugTranscript::ReadStop(DebuggerKind::Cdb, "C:\\VS\\include\\string(120)+0x5\nprog!std::basic_string+0x5:\n");
    XCTAssertTrue(s->Stopped);
    GRAssertEq(s->File, "C:\\VS\\include\\string");
    GRAssertEq(s->Line, 120);
}

// a watch on a name the stopped function does not have says so plainly; a watch reads its value at the stop
- (void)testWatchNotInScope
{
    GRAssertEq(DebugTranscript::Friendly("Couldn't resolve error at 'count'", "count + 1"), "not in scope here");
    GRAssertEq(DebugTranscript::Friendly("Couldn't resolve error at 'count'", "*count"), "not in scope here");
    GRAssertEq(DebugTranscript::Friendly("Couldn't resolve error at 'n'", "count + n"), "Couldn't resolve error at 'n'");
    GRAssertEq(DebugTranscript::Friendly("Memory access error", "p->x"), "Memory access error");

    std::string dir = GRTempDir();
    GRWrite(dir + "/m.c", "int main(void)\n{\n    int x = 1;\n    return x;\n}\n");
    PlaceholderDebugger d;
    d.AddWatch("x");
    GRAssertEq(d.Watches()[0].Value, "not running");
    d.Start(dir + "/m", Core::Target::MacOS, dir + "/m.c", 1);
    XCTAssertTrue(d.State() == DebugState::Break);
    XCTAssertTrue(d.Watches()[0].Value != "not running");
    d.Stop();
}

- (void)testLocals
{
    std::vector<DebugVariable> v = DebugTranscript::ReadVariables(DebuggerKind::Gdb, "x = 1\nname = 0x4005 \"hi\"\n<<M>>\n", "<<M>>");
    GRAssertEq(v.size(), (size_t)2);
    if (v.size() == 2)
    {
        GRAssertEq(v[0].Name, "x");
        GRAssertEq(v[0].Value, "1");
        GRAssertEq(v[1].Value, "0x4005 \"hi\"");
    }
    v = DebugTranscript::ReadVariables(DebuggerKind::Lldb, "(int) x = 1\n(double) y = 2.5\n", "<<M>>");
    GRAssertEq(v.size(), (size_t)2);
    if (v.size() == 2)
    {
        GRAssertEq(v[1].Name, "y");
        GRAssertEq(v[1].Value, "2.5");
        GRAssertEq(v[1].Type, "double");
    }
}

- (void)testFrames
{
    std::vector<DebugFrame> f = DebugTranscript::ReadFrames(DebuggerKind::Gdb,
        "#0  add (a=1, b=2) at m.c:3\n#1  0x0000555 in main () at m.c:9\n", "<<M>>");
    GRAssertEq(f.size(), (size_t)2);
    if (f.size() == 2)
    {
        GRAssertEq(f[0].Function, "add");
        GRAssertEq(f[1].Function, "main");
        GRAssertEq(f[1].Line, 9);
    }
    f = DebugTranscript::ReadFrames(DebuggerKind::Lldb,
        "* thread #1, stop reason = breakpoint 1.1\n"
        "  * frame #0: 0x0000000100003f60 m`add(a=1, b=2) at m.c:3:12\n"
        "    frame #1: 0x0000000100003f90 m`main at m.c:9:5\n", "<<M>>");
    GRAssertEq(f.size(), (size_t)2);
    if (f.size() == 2)
    {
        GRAssertEq(f[0].Function, "add");
        GRAssertEq(f[0].File, "m.c");
        GRAssertEq(f[0].Line, 3);
    }
}

- (void)testStopLine
{
    GRAssertEq(DebugTranscript::StopLine("m.c", 3, "add"), "stopped at m.c:3 in add");
}

- (void)testRecordedTranscript
{
    std::vector<DebugExchange> ex = DebugTranscript::ReadRecorded(
        "(gdb) banner\n> break m.c:3\nBreakpoint 1 at 0x1: file m.c, line 3.\n> run\nBreakpoint 1, add () at m.c:3\n");
    GRAssertEq(ex.size(), (size_t)3);
    if (ex.size() != 3) return;
    GRAssertEq(ex[0].Command, "");
    GRAssertEq(ex[1].Command, "break m.c:3");
    GRAssertEq(ex[2].Command, "run");
    DebugStopPtr s = DebugTranscript::ReadStop(DebuggerKind::Gdb, ex[2].Answer);
    XCTAssertTrue(s->Stopped);
    GRAssertEq(s->Line, 3);
}

- (void)testRecordedFixturesReplay
{
    // every Tests/Fixtures/sessions/<backend>/*.transcript reads (see Tests/Fixtures/README.md)
    std::string root = GRFixture("sessions");
    int read = 0;
    for (const std::string& backend : Platform::Paths::ListDirectories(root))
    {
        std::string dir = root + "/" + backend;
        for (const std::string& f : Platform::Paths::ListFiles(dir))
        {
            if (Platform::Paths::Extension(f) != ".transcript") continue;
            std::vector<DebugExchange> ex;
            std::string error;
            XCTAssertTrue(DebugTranscript::LoadRecorded(dir + "/" + f, ex, error), @"%s: %s", f.c_str(), error.c_str());
            XCTAssertFalse(ex.empty(), @"%s holds no exchange", f.c_str());
            read++;
        }
    }
    if (read == 0) XCTSkip(@"no recorded sessions yet in Tests/Fixtures/sessions");
}


// a C++ program for macOS is linked by the host C++ driver, so the C++ runtime is there
// (regression: linked with cc, compilerpp missed operator new, the RTTI vtables, __gxx_personality_v0)
- (void)testCxxProgramForTheMacLinksTheCxxRuntime
{
    std::string dir = GRCopyFixture("samples/cxxlink");
    Model::GrideProjectPtr p = Model::GrideProject::Load(dir + "/cxxlink.pro");
    Settings::GrideSettingsPtr settings = Settings::GrideSettings::FromJson(Core::JsonValue::MakeObject());
    settings->PublishPaths();
    Services::BuildRequestPtr r = std::make_shared<Services::BuildRequest>();
    r->Projects.push_back(p);
    r->Config = "Debug";
    r->TargetKind = Core::Target::MacOS;
    r->Action = Services::BuildAction::Rebuild;
    r->WorkspaceDir = dir;
    std::mutex lock;
    std::vector<std::string> output;
    Services::BuildEngine engine(settings);
    engine.OnOutput = [&lock, &output](const std::string& line)
    {
        std::lock_guard<std::mutex> hold(lock);
        output.push_back(line);
    };
    Services::BuildSummaryPtr s = engine.RunBatch(std::vector<Services::BuildRequestPtr>(1, r));
    std::string all;
    for (const std::string& l : output) all += l + "\n";
    if (all.find("cpp11 not found") != std::string::npos) XCTSkip(@"cpp11 is not installed here");
    XCTAssertTrue(all.find("Undefined symbols") == std::string::npos, @"%s", all.c_str());
    XCTAssertTrue(s && s->Ok(), @"%s", all.c_str());
    XCTAssertTrue(all.find("linked with c++") != std::string::npos, @"%s", all.c_str());
}

@end
