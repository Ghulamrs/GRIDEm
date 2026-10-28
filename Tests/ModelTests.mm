// =============================================================================
//  ModelTests.mm - workspaces and projects as GRIDEd writes them: GRIDEd's own
//  samples (Tests/Fixtures/samples, copied from GRIDEd/samples) load, and a
//  save, reload and save again gives the same bytes.
// =============================================================================
#import "TestSupport.h"
#include "Model/GrideProject.h"
#include "Model/VsProjectReader.h"
#include "Model/VsSolution.h"
#include "Model/Workspace.h"
#include <sys/stat.h>
#include <unistd.h>

using namespace GRIDEm;
using namespace GRIDEm::Model;

@interface ModelTests : XCTestCase
@end

@implementation ModelTests

- (void)setUp
{
    GRLoadMacToolchain();
}

- (void)testShowcaseLoads
{
    std::string dir = GRCopyFixture("samples/showcase");
    WorkspacePtr w = Workspace::Load(dir + "/showcase.grws", std::string());
    GRAssertEq(w->Projects.size(), (size_t)3);
    XCTAssertTrue(w->LoadErrors.empty());
    GRAssertEq(w->StartupName, "solve3-shalimar");
    GRAssertEq(w->ActiveConfig, "Debug");
    XCTAssertTrue(w->FindProject("average-c90") != nullptr);
    GRAssertEq(w->BuildOrder().size(), (size_t)3);
    for (const GrideProjectPtr& p : w->Projects)
    {
        XCTAssertFalse(p->IsImported());
        XCTAssertFalse(p->AllFiles().empty(), @"%s has no files", p->Name.c_str());
    }
}

- (void)testShowcaseSaveReloadSaveIsByteStable
{
    std::string dir = GRCopyFixture("samples/showcase");
    std::string grws = dir + "/showcase.grws";
    WorkspacePtr w = Workspace::Load(grws, std::string());
    w->Save();
    for (const GrideProjectPtr& p : w->Projects) p->Save();
    std::string first = GRRead(grws);
    std::vector<std::string> pros;
    for (const GrideProjectPtr& p : w->Projects) pros.push_back(GRRead(p->FilePath));
    XCTAssertTrue(first.find("\r\n") != std::string::npos);                    // CRLF, as GRIDEd writes
    // the older form ("gride", "folders") is saved in the one format
    for (const std::string& text : pros)
    {
        XCTAssertTrue(text.find("\"gride\"") == std::string::npos);
        XCTAssertTrue(text.find("\"folders\"") == std::string::npos);
        XCTAssertTrue(text.find("\"groups\"") != std::string::npos);
    }

    WorkspacePtr again = Workspace::Load(grws, std::string());
    XCTAssertFalse(again->HasChanges());
    for (size_t i = 0; i < again->Projects.size() && i < w->Projects.size(); i++)
    {
        XCTAssertTrue(again->Projects[i]->IsRide());
        GRAssertEq(again->Projects[i]->AllFiles().size(), w->Projects[i]->AllFiles().size());
    }
    again->Save();
    for (const GrideProjectPtr& p : again->Projects) p->Save();
    GRAssertEq(GRRead(grws), first);
    for (size_t i = 0; i < again->Projects.size() && i < pros.size(); i++)
        GRAssertEq(GRRead(again->Projects[i]->FilePath), pros[i]);
}

- (void)testGRIDEdWrittenWorkspaceIsWrittenBackUnchanged
{
    // cppparse.grws was written by GRIDEd itself
    std::string dir = GRCopyFixture("samples/cppparse");
    std::string grws = dir + "/cppparse.grws";
    std::string before = GRRead(grws);
    WorkspacePtr w = Workspace::Load(grws, std::string());
    w->Save();
    GRAssertEq(GRRead(grws), before);
}

- (void)testVisualStudioProjectReadOnly
{
    std::string dir = GRCopyFixture("samples/cppparse");
    WorkspacePtr w = Workspace::Load(dir + "/cppparse.grws", std::string());
    GRAssertEq(w->Projects.size(), (size_t)1);
    if (w->Projects.empty()) return;
    GrideProjectPtr p = w->Projects[0];
    XCTAssertTrue(p->IsImported());
    XCTAssertTrue(p->IsVs());
    XCTAssertTrue(p->AllFiles().size() >= 70, @"%d files", (int)p->AllFiles().size());
}

// a RIDE .pro ("groups", "build") - GRIDE 1.0's and RIDE 4.7's compilerpp - shows its files
// (regression: it opened with no folders and no targets)
- (void)testRideProjectShowsItsGroups
{
    std::string dir = GRCopyFixture("samples/ride/compilerpp");
    std::string pro = dir + "/compilerpp.pro";
    std::string before = GRRead(pro);
    GrideProjectPtr p = GrideProject::Load(pro);
    XCTAssertTrue(p->IsRide());
    XCTAssertFalse(p->IsReadOnly);
    XCTAssertFalse(p->IsImported());
    GRAssertEq(p->Name, "compilerpp");
    GRAssertEq(p->Folders.size(), (size_t)3);
    if (p->Folders.size() == 3)
    {
        GRAssertEq(p->Folders[0]->Name, "Sources");
        GRAssertEq(p->Folders[0]->Files.size(), (size_t)16);
        GRAssertEq(p->Folders[1]->Name, "Headers");
        GRAssertEq(p->Folders[2]->Name, "Input");
    }
    XCTAssertTrue(p->Lang == Core::Language::Cpp11);
    XCTAssertTrue(!p->Targets.empty());
    XCTAssertTrue(p->SupportsTarget(Core::Target::C6747));
    // the Input group is not in build.groups: shown, never compiled, and not excluded either
    XCTAssertFalse(p->IsBuilt(p->ToAbsolute("sample.cpp")));
    XCTAssertTrue(p->IsBuilt(p->ToAbsolute("main.cpp")));
    XCTAssertFalse(p->IsExcluded(p->ToAbsolute("sample.cpp")));
    GRAssertEq(p->ProgramName(), "compilerpp");
    GRAssertEq(p->ProgramArgs.size(), (size_t)2);
    // saved with nothing changed: the same bytes RIDE wrote
    p->IsDirty = true;
    p->Save();
    GRAssertEq(GRRead(pro), before);
}

// a RIDE .pro changed in GRIDEm is written back in RIDE's own form: what changed is changed, what
// GRIDEm does not know (build.target, arch, open ...) stays where RIDE put it, and RIDE reads it
// (regression: the project opened locked, and Save would have written GRIDE's form over it)
- (void)testRideProjectSavesInRideForm
{
    std::string dir = GRTempDir();
    std::string pro = dir + "/SampleExt.pro";
    GRWrite(pro, "{\n  \"name\": \"SampleExt\",\n  \"toolchain\": \"auto\",\n  \"arch\": \"arm64-darwin\",\n"
                 "  \"groups\": {\n    \"Sources\": [\n      \"Math.cpp\"\n    ],\n    \"Headers\": [\n      \"Matrix.h\"\n    ]\n  },\n"
                 "  \"open\": \"Vector.h\",\n  \"indent\": 4,\n"
                 "  \"build\": {\n    \"target\": \"SampleExt\",\n    \"groups\": [\n      \"Sources\"\n    ]\n  }\n}\n");
    GrideProjectPtr p = GrideProject::Load(pro);
    XCTAssertTrue(p->IsRide());
    XCTAssertFalse(p->IsReadOnly);

    XCTAssertTrue(p->AddFile(p->ToAbsolute("Vector.h"), "Headers"));
    XCTAssertTrue(p->AddFile(p->ToAbsolute("Quat.cpp"), "Sources"));
    p->ProgramArgs.push_back("-v");
    p->EmulateOnly = true;
    p->SetFolderToolchain("Sources", "cpp11");
    p->IsDirty = true;
    p->Save();

    std::string text = GRRead(pro);
    XCTAssertTrue(text.find("\r") == std::string::npos);                // RIDE's "\n", never CRLF
    XCTAssertTrue(text.find("\"gride\"") == std::string::npos);         // never GRIDE's form
    XCTAssertTrue(text.find("\"folders\"") == std::string::npos);
    XCTAssertTrue(text.find("  \"indent\": 4,\n") != std::string::npos);   // kept, whole number as RIDE writes it
    XCTAssertTrue(text.find("\"open\": \"Vector.h\"") != std::string::npos);
    XCTAssertTrue(text.find("\"arch\": \"arm64-darwin\"") != std::string::npos);
    XCTAssertTrue(text.find("\"target\": \"SampleExt\"") != std::string::npos);
    XCTAssertTrue(text.find("    \"Headers\": [\n      \"Matrix.h\",\n      \"Vector.h\"\n    ]") != std::string::npos);
    XCTAssertTrue(text.size() > 0 && text[text.size() - 1] == '\n');
    XCTAssertFalse(p->IsDirty);

    GrideProjectPtr back = GrideProject::Load(pro);
    XCTAssertTrue(back->IsRide());
    GRAssertEq(back->Folders.size(), (size_t)2);
    XCTAssertTrue(back->ContainsFile(back->ToAbsolute("Vector.h")));
    XCTAssertTrue(back->ContainsFile(back->ToAbsolute("Quat.cpp")));
    XCTAssertFalse(back->IsExcluded(back->ToAbsolute("Quat.cpp")));
    XCTAssertTrue(back->IsBuilt(back->ToAbsolute("Quat.cpp")));
    XCTAssertFalse(back->IsBuilt(back->ToAbsolute("Vector.h")));
    GRAssertEq(back->ProgramArgs.size(), (size_t)1);
    XCTAssertTrue(back->EmulateOnly);
    GRAssertEq(back->ToolchainFor("Sources"), "cpp11");

    // saved again with nothing changed: the same bytes
    back->IsDirty = true;
    back->Save();
    GRAssertEq(GRRead(pro), text);
}

// a .pro naming another machine's host arch builds for this one: x86_64-windows reads as macOS, and
// the arch is written back as the file said it (regression: a Windows .pro reached only -S on the Mac)
- (void)testAnotherHostsArchReadsAsMacOSAndIsWrittenBack
{
    std::string dir = GRTempDir();
    std::string pro = dir + "/Win.pro";
    GRWrite(pro, "{\n  \"name\": \"Win\",\n  \"arch\": \"x86_64-windows\",\n"
                 "  \"groups\": {\n    \"Sources\": [\n      \"main.c\"\n    ]\n  }\n}\n");
    GrideProjectPtr p = GrideProject::Load(pro);
    XCTAssertTrue(p->IsRide());
    XCTAssertFalse(p->Targets.empty());
    if (!p->Targets.empty()) XCTAssertTrue(p->Targets[0] == Core::Target::MacOS);
    XCTAssertTrue(GrideProject::TargetOfProjectArch("x86_64-linux") == Core::Target::MacOS);
    XCTAssertTrue(GrideProject::TargetOfProjectArch("tms6747") == Core::Target::C6747);
    // no "build": the program is named after the project, and every group builds
    GRAssertEq(p->ProgramName(), "Win");
    XCTAssertTrue(p->IsBuilt(p->ToAbsolute("main.c")));

    XCTAssertTrue(p->AddFile(p->ToAbsolute("util.c"), "Sources"));
    p->IsDirty = true;
    p->Save();
    std::string text = GRRead(pro);
    XCTAssertTrue(text.find("\"arch\": \"x86_64-windows\"") != std::string::npos);
    XCTAssertTrue(text.find("\"util.c\"") != std::string::npos);
}

// the file a project opens with: its "open", else the source that defines main
- (void)testFileToOpenIsTheMainSource
{
    std::string dir = GRTempDir();
    std::string pro = dir + "/two.pro";
    GRWrite(pro, "{\n  \"name\": \"two\",\n  \"groups\": {\n    \"Sources\": [\n      \"util.c\",\n      \"app.c\"\n    ]\n  }\n}\n");
    GRWrite(dir + "/util.c", "/* int main(void) { } */\nint twice(int x) { return 2 * x; }\n");
    GRWrite(dir + "/app.c", "int twice(int x);\nint main(void) { return twice(0); }\n");
    GrideProjectPtr p = GrideProject::Load(pro);
    std::string open;
    XCTAssertTrue(p->FileToOpen(open));
    GRAssertEq(Platform::Paths::FileName(open), "app.c");
}

// an imported project's lock is on its structure, never on the text of its files: a Visual Studio
// or RIDE project's sources edit and save in the editor (regression: they opened read-only, so not
// even one line could be commented out)
- (void)testImportedProjectsFilesAreEditable
{
    std::string dir = GRCopyFixture("samples/cppparse");
    WorkspacePtr w = Workspace::Load(dir + "/cppparse.grws", std::string());
    XCTAssertFalse(w->Projects.empty());
    if (w->Projects.empty()) return;
    GrideProjectPtr vs = w->Projects[0];
    XCTAssertTrue(vs->IsVs());
    XCTAssertFalse(vs->AllFiles().empty());
    for (const std::string& f : vs->AllFiles()) XCTAssertFalse(w->IsDocumentReadOnly(f), @"%s", f.c_str());

    std::string rideDir = GRCopyFixture("samples/ride/compilerpp");
    WorkspacePtr r = Workspace::ForSingleProject(GrideProject::Load(rideDir + "/compilerpp.pro"));
    GrideProjectPtr ride = r->Projects[0];
    XCTAssertTrue(ride->IsRide());
    for (const std::string& f : ride->AllFiles()) XCTAssertFalse(r->IsDocumentReadOnly(f), @"%s", f.c_str());
}

// a file read-only on disk: the first save asks once for its project, every file of the project is
// then made writable as it is saved, and closing (RelockAll) gives each the mode it had
- (void)testReadOnlyOnDiskAskedOncePerProjectAndRelockedOnClose
{
    std::string dir = GRCopyFixture("samples/ride/compilerpp");
    std::string pro = dir + "/compilerpp.pro";
    WorkspacePtr w = Workspace::ForSingleProject(GrideProject::Load(pro));
    std::vector<std::string> files = w->Projects[0]->AllFiles();
    XCTAssertTrue(files.size() >= 3);
    if (files.size() < 3) return;
    std::string a = files[0], b = files[1], plain = files[2];
    for (const std::string& f : { a, b, plain }) GRWrite(f, "int x;\n");      // the fixture holds only the .pro
    chmod(a.c_str(), 0444);
    chmod(b.c_str(), 0444);
    chmod(pro.c_str(), 0444);
    XCTAssertFalse(DiskLocks::IsWritable(a));
    XCTAssertTrue(DiskLocks::IsWritable(plain));

    int asked = 0;
    bool answer = false;
    std::function<bool()> ask = [&asked, &answer]() { asked++; return answer; };
    // no: nothing changes, and the next save asks again
    XCTAssertFalse(w->PrepareWrite(a, ask));
    GRAssertEq(asked, 1);
    XCTAssertFalse(DiskLocks::IsWritable(a));
    // yes: asked once for the project - its other files and its .pro follow without asking
    answer = true;
    XCTAssertTrue(w->PrepareWrite(a, ask));
    XCTAssertTrue(w->PrepareWrite(b, ask));
    XCTAssertTrue(w->PrepareWrite(pro, ask));
    XCTAssertTrue(w->PrepareWrite(plain, ask));
    GRAssertEq(asked, 2);
    XCTAssertTrue(DiskLocks::IsWritable(a) && DiskLocks::IsWritable(b) && DiskLocks::IsWritable(pro));
    GRAssertEq(w->Locks.Items().size(), (size_t)3);             // the writable one is not listed

    // closed: read-only again for every other program, and the next open asks again
    w->Locks.RelockAll();
    XCTAssertFalse(DiskLocks::IsWritable(a));
    XCTAssertFalse(DiskLocks::IsWritable(b));
    XCTAssertFalse(DiskLocks::IsWritable(pro));
    XCTAssertTrue(DiskLocks::IsWritable(plain));
    XCTAssertTrue(w->PrepareWrite(a, ask));
    GRAssertEq(asked, 3);
    w->Locks.RelockAll();
}

// GrideProject::BeforeWrite is asked before a project's file is written; a no leaves it unwritten
- (void)testProjectSaveAsksBeforeWriting
{
    std::string dir = GRTempDir();
    std::vector<Core::Target> targets{ Core::Target::MacOS };
    GrideProjectPtr p = GrideProject::CreateNew(dir, "Guarded", Core::Language::C90, "Console application", targets);
    p->Save();
    std::string before = GRRead(p->FilePath);
    std::string asked;
    GrideProject::BeforeWrite = [&asked](const std::string& path) { asked = path; return false; };
    p->Name = "Renamed";
    p->IsDirty = true;
    p->Save();
    GrideProject::BeforeWrite = nullptr;
    GRAssertEq(asked, p->FilePath);
    GRAssertEq(GRRead(p->FilePath), before);
    XCTAssertTrue(p->IsDirty);
}

// a Visual Studio project's settings changed in the Property Pages are kept in the workspace's entry
// - only what differs from its files - and come back on the next open; its own files are never written
- (void)testImportedProjectSettingsKeptInWorkspace
{
    std::string dir = GRCopyFixture("samples/cppparse");
    std::string grws = dir + "/cppparse.grws";
    WorkspacePtr w = Workspace::Load(grws, std::string());
    GrideProjectPtr p = w->Projects[0];
    XCTAssertTrue(p->IsVs() && p->IsReadOnly && p->SettingsEditable());
    XCTAssertFalse(Workspace::ImportedSettingsChanged(p));       // nothing changed yet
    std::string vcxproj = GRRead(p->FilePath);
    XCTAssertFalse(p->Configurations.empty());
    if (p->Configurations.empty()) return;
    std::string key = p->Configurations[0]->Key();
    p->Configurations[0]->Defines.push_back("GRIDEM_TEST=1");
    p->Save();                                                    // the .vcxproj is never written
    w->IsDirty = true;
    w->Save();
    GRAssertEq(GRRead(p->FilePath), vcxproj);
    std::string text = GRRead(grws);
    XCTAssertTrue(text.find("GRIDEM_TEST=1") != std::string::npos);
    XCTAssertTrue(text.find("\"targets\"") == std::string::npos);   // unchanged entries are not kept

    WorkspacePtr again = Workspace::Load(grws, std::string());
    GrideProjectPtr q = again->Projects[0];
    bool found = false;
    for (const BuildConfigurationPtr& c : q->Configurations)
        if (c->Key() == key)
            for (const std::string& d : c->Defines) if (d == "GRIDEM_TEST=1") found = true;
    XCTAssertTrue(found);
}

- (void)testSolutionOpensAsAWorkspace
{
    std::string dir = GRCopyFixture("samples/cppparse");
    std::string sln = dir + "/cppparse.sln";
    XCTAssertTrue(VsProjectReader::IsSolution(sln));
    WorkspacePtr w = Workspace::ForSolution(VsSolution::Read(sln));
    GRAssertEq(w->Projects.size(), (size_t)1);
    if (!w->Projects.empty()) GRAssertEq(w->Projects[0]->Name, "cppparse");
}

- (void)testNewProjectSkeleton
{
    std::string dir = GRTempDir();
    std::vector<Core::Target> targets{ Core::Target::MacOS, Core::Target::C6747 };
    GrideProjectPtr p = GrideProject::CreateNew(dir, "Hello", Core::Language::C90, "Console application", targets);
    XCTAssertTrue(p != nullptr);
    if (!p) return;
    GRAssertEq(p->Name, "Hello");
    XCTAssertTrue(p->Lang == Core::Language::C90);
    GRAssertEq(p->Targets.size(), (size_t)2);
    p->Save();
    GrideProjectPtr back = GrideProject::Load(p->FilePath);
    GRAssertEq(back->Name, "Hello");
    GRAssertEq(back->Targets.size(), (size_t)2);
}


// a workspace saved on Windows names Windows, which here only reaches the assembly: it opens on a
// target this machine runs, and the file keeps "Windows" until another target is chosen
- (void)testWorkspaceOfAnotherHostOpensOnATargetThatRunsHere
{
    std::string dir = GRCopyFixture("samples/showcase");
    std::string grws = dir + "/showcase.grws";
    WorkspacePtr w = Workspace::Load(grws, std::string());
    XCTAssertTrue(w->ActiveTarget == Core::Target::C6747);
    w->Save();
    XCTAssertTrue(GRRead(grws).find("\"activeTarget\": \"Windows\"") != std::string::npos);
    w->ActiveTarget = Core::Target::MacOS;
    w->Save();
    XCTAssertTrue(GRRead(grws).find("\"activeTarget\": \"macOS\"") != std::string::npos);
}

@end
