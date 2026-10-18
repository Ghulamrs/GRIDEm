// =============================================================================
//  ParityTests.mm - GRIDEm builds as GRIDEd builds (reference section 7.2):
//  for each Tests/Fixtures/golden/<name>.txt (GRIDEd's captured Build output,
//  see golden/README.md) the workspace is rebuilt in-process with BuildEngine,
//  as GRIDEm-build does, and the progress lines are compared, normalised.
// =============================================================================
#import "TestSupport.h"
#include "Core/StringUtil.h"
#include "Model/VsProjectReader.h"
#include "Model/VsSolution.h"
#include "Model/Workspace.h"
#include "Services/BuildEngine.h"
#include "Settings/GrideSettings.h"
#include <mutex>

using namespace GRIDEm;
using namespace GRIDEm::Core;

// the header fields of a golden file ("# key: value")
struct Golden
{
    std::string workspace, config, target, root;
    std::vector<std::string> lines;
};

static Golden ReadGolden(const std::string& path)
{
    Golden g;
    g.config = "Debug";
    g.target = "TMS320C6747";
    for (const std::string& raw : StringUtil::SplitLines(GRRead(path)))
    {
        if (StringUtil::StartsWith(raw, "# "))
        {
            std::string body = raw.substr(2);
            size_t colon = body.find(':');
            if (colon == std::string::npos) continue;
            std::string key = StringUtil::Trim(body.substr(0, colon));
            std::string value = StringUtil::Trim(body.substr(colon + 1));
            if (key == "workspace") g.workspace = value;
            else if (key == "config") g.config = value;
            else if (key == "target") g.target = value;
            else if (key == "root") g.root = value;
            continue;
        }
        g.lines.push_back(raw);
    }
    return g;
}

// a progress line: "[tool]  input  ->  output"
static bool IsProgress(const std::string& line)
{
    if (line.empty() || line[0] != '[') return false;
    size_t close = line.find(']');
    return close != std::string::npos && close > 1 && close + 1 < line.size() && line[close + 1] == ' ';
}

static std::string Normalise(const std::string& line, const std::string& root)
{
    std::string s = StringUtil::ReplaceChar(StringUtil::TrimEnd(line, '\r'), '\\', '/');
    if (!root.empty())
    {
        std::string r = StringUtil::TrimEnd(StringUtil::ReplaceChar(root, '\\', '/'), '/');
        s = StringUtil::ReplaceIgnoreCase(s, r, "$(Root)");
    }
    s = StringUtil::ReplaceIgnoreCase(s, ".exe", "");
    std::string out;
    bool blank = false;
    for (char c : s)
    {
        if (c == ' ' || c == '\t')
        {
            if (!blank) out += ' ';
            blank = true;
        }
        else
        {
            out += c;
            blank = false;
        }
    }
    return StringUtil::Trim(out);
}

static std::vector<std::string> ProgressLines(const std::vector<std::string>& lines, const std::string& root)
{
    std::vector<std::string> r;
    for (const std::string& l : lines)
        if (IsProgress(StringUtil::Trim(l))) r.push_back(Normalise(StringUtil::Trim(l), root));
    return r;
}

static Model::WorkspacePtr Open(const std::string& path)
{
    std::string ext = StringUtil::ToLower(Platform::Paths::Extension(path));
    if (ext == ".grws") return Model::Workspace::Load(path, std::string());
    if (ext == ".pro") return Model::Workspace::ForSingleProject(Model::GrideProject::Load(path));
    if (Model::VsProjectReader::IsSolution(path)) return Model::Workspace::ForSolution(Model::VsSolution::Read(path));
    return nullptr;
}

@interface ParityTests : XCTestCase
@end

@implementation ParityTests

- (void)testProgressLinesMatchGRIDEd
{
    std::string dir = GRFixture("golden");
    std::vector<std::string> goldens;
    for (const std::string& f : Platform::Paths::ListFiles(dir))
        if (Platform::Paths::Extension(f) == ".txt") goldens.push_back(dir + "/" + f);
    if (goldens.empty()) XCTSkip(@"no golden build output yet in Tests/Fixtures/golden (see its README.md)");

    GRLoadMacToolchain();
    int compared = 0;
    for (const std::string& file : goldens)
    {
        Golden g = ReadGolden(file);
        XCTAssertFalse(g.workspace.empty(), @"%s: no '# workspace:' line", file.c_str());
        if (g.workspace.empty()) continue;

        // the workspace's folder, copied so the build writes into a temporary place
        std::string folder = Platform::Paths::DirectoryName(g.workspace);
        std::string copy = GRCopyFixture(folder);
        std::string path = copy + "/" + Platform::Paths::FileName(g.workspace);
        Model::WorkspacePtr w = Open(path);
        XCTAssertTrue(w != nullptr, @"%s: cannot open %s", file.c_str(), path.c_str());
        if (!w) continue;

        Settings::GrideSettingsPtr settings = Settings::GrideSettings::FromJson(Core::JsonValue::MakeObject());
        settings->PublishPaths();
        Services::BuildRequestPtr r = std::make_shared<Services::BuildRequest>();
        r->Projects = w->BuildOrder();
        r->Config = g.config;
        r->TargetKind = TargetInfo::Parse(g.target);
        r->Action = Services::BuildAction::Rebuild;
        r->WorkspaceDir = w->Directory();

        std::mutex lock;
        std::vector<std::string> output;
        Services::BuildEngine engine(settings);
        engine.OnOutput = [&lock, &output](const std::string& line)
        {
            std::lock_guard<std::mutex> hold(lock);
            output.push_back(line);
        };
        engine.RunBatch(std::vector<Services::BuildRequestPtr>(1, r));

        bool toolMissing = false;
        for (const std::string& l : output)
            if (l.find(" not found") != std::string::npos) toolMissing = true;
        if (toolMissing) continue;            // the toolchain is not on this Mac: nothing to compare

        std::vector<std::string> expected = ProgressLines(g.lines, g.root);
        std::vector<std::string> actual = ProgressLines(output, copy);
        XCTAssertEqual(actual.size(), expected.size(), @"%s: %d progress lines, GRIDEd wrote %d",
                       file.c_str(), (int)actual.size(), (int)expected.size());
        for (size_t i = 0; i < actual.size() && i < expected.size(); i++)
            XCTAssertTrue(actual[i] == expected[i], @"%s line %d:\n  GRIDEm: %s\n  GRIDEd: %s",
                          file.c_str(), (int)i + 1, actual[i].c_str(), expected[i].c_str());
        compared++;
    }
    if (compared == 0) XCTSkip(@"the toolchain is not installed here - no golden file could be compared");
}

- (void)testNormalisation
{
    GRAssertEq(Normalise("[c90.exe]  C:\\w\\src\\main.c  ->  obj\\main.s\r", "C:\\w"), "[c90] $(Root)/src/main.c -> obj/main.s");
    XCTAssertTrue(IsProgress("[asm6x]  a.s  ->  a.obj"));
    XCTAssertFalse(IsProgress("========== Build: 1 succeeded =========="));
    XCTAssertFalse(IsProgress("[]"));
}

@end
