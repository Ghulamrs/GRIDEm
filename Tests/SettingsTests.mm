// =============================================================================
//  SettingsTests.mm - toolchain.json as GRIDEm ships it, settings.json
//  round-trips, RIDE's Compiler Options store.
// =============================================================================
#import "TestSupport.h"
#include "Core/LanguageTarget.h"
#include "Settings/CompilerOptions.h"
#include "Settings/GrideSettings.h"
#include "Settings/ToolchainDefinitions.h"

using namespace GRIDEm;
using namespace GRIDEm::Core;
using namespace GRIDEm::Settings;

@interface SettingsTests : XCTestCase
@end

@implementation SettingsTests

- (void)setUp
{
    GRLoadMacToolchain();
}

- (void)testMacToolchainLoadsWithoutProblems
{
    ToolchainDefinitionsPtr d = GRLoadMacToolchain();
    XCTAssertTrue(d->ShippedFound);
    XCTAssertTrue(d->Problems.empty(), @"%s", d->Problems.empty() ? "" : d->Problems.front().c_str());
    GRAssertEq(d->Targets.size(), (size_t)4);
    XCTAssertTrue(d->Tools.size() >= 20);
    XCTAssertTrue(d->CompilerOptions.size() >= 17);
}

- (void)testTheMacIsTheHost
{
    ToolchainDefinitionsPtr d = GRLoadMacToolchain();
    TargetDefinitionPtr host = d->HostTarget();
    XCTAssertTrue(host != nullptr);
    if (host) GRAssertEq(host->Name, "arm64-darwin");
    TargetDefinitionPtr win = d->TargetByArch("x86_64-windows");
    XCTAssertTrue(win != nullptr && !win->Host);
}

- (void)testToolLookupsByRoleWordAndName
{
    ToolchainDefinitionsPtr d = GRLoadMacToolchain();
    XCTAssertTrue(d->Tool("cpp11") != nullptr);
    XCTAssertTrue(d->ToolByWord("cc1") != nullptr);                    // c90's other word
    XCTAssertTrue(d->ToolByName("c90.exe") != nullptr);
    ToolDefinitionPtr dbg = d->ToolFor(Target::MacOS, "debugger");
    XCTAssertTrue(dbg != nullptr);
    if (dbg) GRAssertEq(dbg->Id, "lldb");
    ToolDefinitionPtr sim = d->ToolFor(Target::C6747, "simulator");
    XCTAssertTrue(sim != nullptr);
    if (sim) GRAssertEq(sim->Id, "sim6747");
    XCTAssertTrue(d->RuntimeOf(Target::C6747) != nullptr);
}

- (void)testUserOverlayWinsFieldByField
{
    std::string dir = GRTempDir();
    std::string user = dir + "/toolchain.json";
    GRWrite(user, "{ \"tools\": { \"c90\": { \"exe\": \"my-c90\" } } }");
    ToolchainDefinitionsPtr d = ToolchainDefinitions::Load(GRSourceRoot() + "/Resources/toolchain.json", user);
    XCTAssertTrue(d->Problems.empty());
    ToolDefinitionPtr c90 = d->Tool("c90");
    XCTAssertTrue(c90 != nullptr);
    if (c90)
    {
        GRAssertEq(c90->Exe, "my-c90");
        XCTAssertFalse(c90->Aliases.empty());                            // the shipped field stands
    }
    XCTAssertTrue(d->IsOverridden("tools", "c90"));
}

- (void)testMalformedUserFieldIsAProblemNotAFailure
{
    std::string user = GRTempDir() + "/toolchain.json";
    GRWrite(user, "{ \"tools\": { \"c90\": { \"exe\": 12 } } }");
    ToolchainDefinitionsPtr d = ToolchainDefinitions::Load(GRSourceRoot() + "/Resources/toolchain.json", user);
    XCTAssertFalse(d->Problems.empty());
    ToolDefinitionPtr c90 = d->Tool("c90");
    XCTAssertTrue(c90 != nullptr && !c90->Exe.empty());
}

- (void)testSettingsRoundTripIsByteStable
{
    GrideSettingsPtr s = GrideSettings::FromJson(Core::JsonValue::MakeObject());
    std::string first = JsonWriter::Write(s->ToJson());
    GrideSettingsPtr again = GrideSettings::FromJson(JsonParser::Parse(first));
    GRAssertEq(JsonWriter::Write(again->ToJson()), first);
    GRAssertEq(JsonWriter::Write(s->Clone()->ToJson()), first);
}

- (void)testSettingsKeySetSurvivesARoundTrip
{
    GrideSettingsPtr s = GrideSettings::FromJson(Core::JsonValue::MakeObject());
    s->KeySet = "macos";
    std::string text = JsonWriter::Write(s->ToJson());
    GRAssertEq(GrideSettings::FromJson(JsonParser::Parse(text))->KeySet, "macos");
}

// GRIDEd's keys since GRIDEx3: buildJobs, windowsDebugger, windowsRuntime, and "gride" in place of
// "ride" in the paths and the key set; the former names are still read
- (void)testSettingsNewKeysAndFormerNames
{
    GrideSettingsPtr s = GrideSettings::FromJson(Core::JsonValue::MakeObject());
    s->Env->BuildJobs = 3;
    s->Env->WindowsDebugger = "cdb";
    s->Env->WindowsRuntime = "grt";
    s->Env->RideRoot = "/opt/gride";
    JsonPtr env = s->ToJson()->GetObject("environment");
    XCTAssertTrue(env->Has("grideRoot"));
    XCTAssertFalse(env->Has("rideRoot"));
    XCTAssertFalse(env->Has("helpDirectory"));          // this copy's own docs folder is no setting
    GrideSettingsPtr back = GrideSettings::FromJson(JsonParser::Parse(JsonWriter::Write(s->ToJson())));
    GRAssertEq(back->Env->BuildJobs, 3);
    GRAssertEq(back->Env->EffectiveWindowsDebugger(), "cdb");
    GRAssertEq(back->Env->RideRoot, "/opt/gride");

    JsonPtr former = JsonParser::Parse("{ \"environment\": { \"rideRoot\": \"/opt/ride\", \"buildJobs\": -2,"
                                       " \"windowsRuntime\": \"other\" }, \"keySet\": \"ride\" }");
    GrideSettingsPtr old = GrideSettings::FromJson(former);
    GRAssertEq(old->Env->RideRoot, "/opt/ride");
    GRAssertEq(old->Env->BuildJobs, 0);
    GRAssertEq(old->KeySet, "gride");
    if (getenv("GRIDE_WINDOWS_RUNTIME") == nullptr) GRAssertEq(old->Env->EffectiveWindowsRuntime(), "auto");
}

// settings.json is shared with the other outlooks: a save keeps the keys it does not own
- (void)testSettingsSaveKeepsTheOtherOutlooksKeys
{
    GRAssertEq(std::string(SettingsManager::FileName), "settings.json");
    GRAssertEq(Platform::Paths::FileName(SettingsManager::UserPath()), "settings.json");
    std::string path = GRTempDir() + "/settings.json";
    GRWrite(path, "{ \"white\": { \"theme\": \"Light\" } }\n");
    XCTAssertFalse(SettingsManager::Holds(JsonParser::ParseFile(path)));
    GrideSettingsPtr s = GrideSettings::FromJson(Core::JsonValue::MakeObject());
    s->FilePath = path;
    XCTAssertTrue(SettingsManager::Save(s));
    JsonPtr doc = JsonParser::ParseFile(path);
    XCTAssertTrue(SettingsManager::Holds(doc));
    XCTAssertTrue(doc->GetObject("white") != nullptr);
    XCTAssertTrue(doc->GetObject("environment") != nullptr);
}

- (void)testSettingsDefaults
{
    GrideSettingsPtr s = GrideSettings::FromJson(Core::JsonValue::MakeObject());
    GRAssertEq(s->Toolchains.size(), (size_t)4);
    XCTAssertTrue(s->DetectLanguage("x.cpp", Language::C90) == Language::Cpp11);
    XCTAssertTrue(s->DetectLanguage("x.shl", Language::C90) == Language::Shalimar);
    XCTAssertTrue(s->DetectLanguage("x.h", Language::Shalimar) == Language::Shalimar);   // headers follow the project
}

- (void)testCompilerOptionDefaults
{
    GRAssertEq(CompilerOptionStore::DefaultValue("Debug", "c90.opt"), "-O0");
    GRAssertEq(CompilerOptionStore::DefaultValue("Release", "c90.opt"), "-O2");
    GRAssertEq(CompilerOptionStore::DefaultValue("debug", "cpp11.defines"), "_DEBUG=1");
    GRAssertEq(CompilerOptionStore::DefaultValue("Release", "cpp11.defines"), "NDEBUG=1");
}

- (void)testCompilerOptionStoreSetUnsetAndJson
{
    CompilerOptionStore s;
    XCTAssertTrue(s.Empty());
    s.Set("debug", "c90.opt", "-O1");
    GRAssertEq(s.Value("Debug", "c90.opt"), "-O1");
    GRAssertEq(s.Value("Release", "c90.opt"), "-O2");                    // unset: the default
    GRAssertEq(JsonWriter::Write(s.ToJson()), "{\r\n  \"debug\": {\r\n    \"c90.opt\": \"-O1\"\r\n  }\r\n}\r\n");
    CompilerOptionStore t;
    t.FromJson(s.ToJson());
    GRAssertEq(JsonWriter::Write(t.ToJson()), JsonWriter::Write(s.ToJson()));
    s.Unset("Debug", "c90.opt");
    XCTAssertTrue(s.Empty());
    GRAssertEq(s.Value("Debug", "c90.opt"), "-O0");
}

- (void)testCompilerOptionEntries
{
    std::vector<std::string> e = CompilerOptionStore::Entries("a; b;;c ");
    GRAssertEq(e.size(), (size_t)3);
    if (e.size() == 3) GRAssertEq(e[2], "c");
}

@end
