// =============================================================================
//  PlatformTests.mm - Platform: children started without a shell, their output,
//  input and exit codes, a group ended together; the environment.
// =============================================================================
#import "TestSupport.h"
#include "Platform/Environment.h"
#include "Platform/JobGroup.h"
#include "Platform/Paths.h"
#include "Platform/Process.h"
#include <mutex>
#include <unistd.h>

using namespace GRIDEm;
using namespace GRIDEm::Platform;

// collects what a child writes, from its reader threads
struct Collected
{
    std::mutex lock;
    std::string out;
    std::string err;
};

static void Attach(Process& p, Collected& c)
{
    p.OnOutput = [&c](const std::string& bytes) { std::lock_guard<std::mutex> hold(c.lock); c.out += bytes; };
    p.OnError = [&c](const std::string& bytes) { std::lock_guard<std::mutex> hold(c.lock); c.err += bytes; };
}

@interface PlatformTests : XCTestCase
@end

@implementation PlatformTests

- (void)testEchoOutputAndExitCode
{
    Process p;
    Collected c;
    Attach(p, c);
    std::string why;
    XCTAssertTrue(p.Start(std::vector<std::string>{ "/bin/echo", "hello", "world" }, std::string(),
                          std::vector<std::string>(), ProcessMode::Pipes, true, why), @"%s", why.c_str());
    XCTAssertTrue(p.Wait(10000));
    GRAssertEq(p.ExitCode(), 0);
    std::lock_guard<std::mutex> hold(c.lock);
    GRAssertEq(c.out, "hello world\n");
}

- (void)testStandardErrorApartAndJoined
{
    std::vector<std::string> argv{ "/bin/sh", "-c", "echo out; echo err 1>&2; exit 3" };
    std::string why;
    {
        Process p;
        Collected c;
        Attach(p, c);
        XCTAssertTrue(p.Start(argv, std::string(), std::vector<std::string>(), ProcessMode::Pipes, true, why));
        XCTAssertTrue(p.Wait(10000));
        GRAssertEq(p.ExitCode(), 3);
        std::lock_guard<std::mutex> hold(c.lock);
        GRAssertEq(c.out, "out\n");
        GRAssertEq(c.err, "err\n");
    }
    {
        Process p;
        Collected c;
        Attach(p, c);
        XCTAssertTrue(p.Start(argv, std::string(), std::vector<std::string>(), ProcessMode::Joined, true, why));
        XCTAssertTrue(p.Wait(10000));
        std::lock_guard<std::mutex> hold(c.lock);
        XCTAssertTrue(c.out.find("out\n") != std::string::npos && c.out.find("err\n") != std::string::npos);
        GRAssertEq(c.err, "");
    }
}

- (void)testInputToCat
{
    Process p;
    Collected c;
    Attach(p, c);
    std::string why;
    XCTAssertTrue(p.Start(std::vector<std::string>{ "/bin/cat" }, std::string(), std::vector<std::string>(),
                          ProcessMode::Pipes, false, why));
    XCTAssertTrue(p.Write("line one\nline two\n"));
    p.CloseInput();
    XCTAssertTrue(p.Wait(10000));
    GRAssertEq(p.ExitCode(), 0);
    std::lock_guard<std::mutex> hold(c.lock);
    GRAssertEq(c.out, "line one\nline two\n");
}

- (void)testClosedInputGivesEndOfFile
{
    Process p;
    Collected c;
    Attach(p, c);
    std::string why;
    XCTAssertTrue(p.Start(std::vector<std::string>{ "/bin/cat" }, std::string(), std::vector<std::string>(),
                          ProcessMode::Pipes, true, why));
    XCTAssertTrue(p.Wait(10000), @"cat waited for input that was closed");
}

- (void)testWorkingDirectoryAndEnvironment
{
    std::string dir = GRTempDir();
    Process p;
    Collected c;
    Attach(p, c);
    std::string why;
    XCTAssertTrue(p.Start(std::vector<std::string>{ "/bin/sh", "-c", "pwd -P; echo \"[$GRIDEM_T]\"; echo \"[${HOME+set}]\"" },
                          dir, std::vector<std::string>{ "GRIDEM_T=yes", "HOME=" }, ProcessMode::Pipes, true, why));
    XCTAssertTrue(p.Wait(10000));
    std::lock_guard<std::mutex> hold(c.lock);
    char real[4096];
    std::string expectedDir = realpath(dir.c_str(), real) != nullptr ? std::string(real) : dir;
    GRAssertEq(c.out, expectedDir + "\n[yes]\n[]\n");          // NAME= removes the variable
}

- (void)testMissingProgramIsReported
{
    Process p;
    std::string why;
    XCTAssertFalse(p.Start(std::vector<std::string>{ "/no/such/tool" }, std::string(), std::vector<std::string>(),
                           ProcessMode::Pipes, true, why));
    XCTAssertTrue(why.find("not found") != std::string::npos, @"%s", why.c_str());
}

- (void)testKillEndsTheWholeGroup
{
    std::string pidFile = GRTempDir() + "/grandchild.pid";
    Process p;
    std::string why;
    XCTAssertTrue(p.Start(std::vector<std::string>{ "/bin/sh", "-c", "sleep 30 & echo $! > '" + pidFile + "'; sleep 30" },
                          std::string(), std::vector<std::string>(), ProcessMode::Pipes, true, why));
    for (int i = 0; i < 200 && GRRead(pidFile).empty(); i++) usleep(10 * 1000);
    int grandchild = atoi(GRRead(pidFile).c_str());
    XCTAssertTrue(grandchild > 0);
    p.Kill();
    XCTAssertTrue(p.Wait(5000));
    XCTAssertTrue(p.ExitCode() == 128 + 9, @"exit %d", p.ExitCode());
    bool gone = false;
    for (int i = 0; i < 200 && !gone; i++)
    {
        gone = kill(grandchild, 0) != 0;
        if (!gone) usleep(10 * 1000);
    }
    XCTAssertTrue(gone, @"the grandchild %d outlived Kill", grandchild);
    XCTAssertFalse(JobGroup::Alive(p.Pid()));
}

- (void)testTerminalMode
{
    Process p;
    Collected c;
    Attach(p, c);
    std::string why;
    XCTAssertTrue(p.Start(std::vector<std::string>{ "/bin/sh", "-c", "test -t 1 && echo tty" }, std::string(),
                          std::vector<std::string>(), ProcessMode::Terminal, false, why), @"%s", why.c_str());
    XCTAssertTrue(p.Wait(10000));
    std::lock_guard<std::mutex> hold(c.lock);
    XCTAssertTrue(c.out.find("tty") != std::string::npos, @"%s", c.out.c_str());
}

- (void)testEnvironmentExpand
{
    setenv("GRIDEM_X", "value", 1);
    unsetenv("GRIDEM_UNSET");
    GRAssertEq(Environment::Expand("a %GRIDEM_X% b"), "a value b");
    GRAssertEq(Environment::Expand("$GRIDEM_X/${GRIDEM_X}"), "value/value");
    GRAssertEq(Environment::Expand("%GRIDEM_UNSET%/$GRIDEM_UNSET"), "%GRIDEM_UNSET%/$GRIDEM_UNSET");
    GRAssertEq(Environment::Expand("100% sure"), "100% sure");
}

- (void)testEnvironmentWithChanges
{
    setenv("GRIDEM_GONE", "x", 1);
    std::vector<std::string> block = Environment::WithChanges(std::vector<std::string>{ "GRIDEM_NEW = 1", "GRIDEM_GONE=" });
    bool sawNew = false, sawGone = false;
    for (const std::string& e : block)
    {
        if (e == "GRIDEM_NEW=1") sawNew = true;
        if (e.compare(0, 12, "GRIDEM_GONE=") == 0) sawGone = true;
    }
    XCTAssertTrue(sawNew);
    XCTAssertFalse(sawGone);
}

- (void)testPathsFiles
{
    std::string dir = GRTempDir();
    std::string error;
    XCTAssertTrue(Paths::CreateDirectories(dir + "/a/b"));
    XCTAssertTrue(Paths::WriteFile(dir + "/a/b/f.txt", "x", error));
    XCTAssertTrue(Paths::IsFile(dir + "/a/b/f.txt"));
    XCTAssertTrue(Paths::IsDirectory(dir + "/a"));
    GRAssertEq(Paths::FileSize(dir + "/a/b/f.txt"), 1LL);
    GRAssertEq(Paths::ListDirectories(dir + "/a").size(), (size_t)1);
    XCTAssertTrue(Paths::CopyFile(dir + "/a/b/f.txt", dir + "/g.txt", error));
    GRAssertEq(GRRead(dir + "/g.txt"), "x");
    XCTAssertTrue(Paths::DeleteTree(dir + "/a"));
    XCTAssertFalse(Paths::Exists(dir + "/a"));
}

@end
