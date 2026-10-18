// =============================================================================
//  ServicesTests.mm - reading what the tools say: diagnostics in the shapes of
//  GRIDEd's BuildMessage.h, command templates, RIDE's compiler rules, and what
//  gdb, lldb and cdb answer (DebugTranscript), live or recorded.
// =============================================================================
#import "TestSupport.h"
#include <mutex>
#include "Settings/GrideSettings.h"
#include "Services/BuildEngine.h"
#include "Model/Workspace.h"
#include "Core/LanguageTarget.h"
#include "Services/BuildMessage.h"
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
