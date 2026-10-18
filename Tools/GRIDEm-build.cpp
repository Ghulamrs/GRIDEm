// =============================================================================
//  GRIDEm-build.cpp - GRIDEm's build without its window: the headless runner
//  for the parity checks (reference, section 7.2). It opens what the window's
//  File > Open opens (a .grws, a .pro, a CCS project, a .sln, a .vcxproj),
//  builds it with BuildEngine on this thread and prints every output line, as
//  the Output pane (Build) shows them.
//
//    GRIDEm-build <path> [--config Debug|Release] [--target <name>]
//                        [--clean | --rebuild] [--project <name>]
//
//  The settings are the window's (gride.settings.json); toolchain.json is the
//  shipped one, or the one $GRIDE_TOOLCHAIN names. The exit code is 0 when the
//  build succeeded, 1 when it failed, 2 when nothing could be opened.
// =============================================================================
#include "../Sources/Core/LanguageTarget.h"
#include "../Sources/Core/StringUtil.h"
#include "../Sources/Model/CcsProjectReader.h"
#include "../Sources/Model/GrideProject.h"
#include "../Sources/Model/VsProjectReader.h"
#include "../Sources/Model/VsSolution.h"
#include "../Sources/Model/Workspace.h"
#include "../Sources/Platform/Paths.h"
#include "../Sources/Services/BuildEngine.h"
#include "../Sources/Settings/GrideSettings.h"
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using namespace GRIDEm;

static void Usage()
{
    std::printf("usage: GRIDEm-build <workspace|project> [--config Debug|Release] [--target <name>]\n"
                "                    [--clean | --rebuild] [--project <name>]\n"
                "  <name> of a target: Windows, Linux, macOS, TMS320C6747\n");
}

// what the window's OpenPath opens, as a workspace
static Model::WorkspacePtr Open(const std::string& path, const Settings::GrideSettingsPtr& settings)
{
    std::string ext = Core::StringUtil::ToLower(Platform::Paths::Extension(path));
    std::string cg = settings->Env->CcsCompilerRoot();
    if (ext == ".grws") return Model::Workspace::Load(path, cg);
    if (ext == ".pro")
    {
        // GRIDE 2.0's .pro for a Visual Studio project: its solution, with the compiler and arguments the .pro gives
        std::string sln = Model::GrideProject::GreySolution(path);
        if (!sln.empty())
        {
            Model::GrideProjectPtr g = Model::GrideProject::Load(path);
            Model::WorkspacePtr w = Model::Workspace::ForSolution(Model::VsSolution::Read(sln));
            for (const Model::GrideProjectPtr& p : w->Projects)
                if (Core::StringUtil::EqualsIgnoreCase(p->Name, g->Name))
                {
                    p->Toolchain = g->Toolchain;
                    p->ProgramArgs = g->ProgramArgs;
                    w->StartupName = p->Name;
                }
            w->ActiveConfig = Core::JsonParser::ParseFile(path)->GetString("activeConfiguration", w->ActiveConfig);
            return w;
        }
        return Model::Workspace::ForSingleProject(Model::GrideProject::Load(path));
    }
    if (Model::CcsProjectReader::IsCcsProject(path) &&
        (Platform::Paths::IsDirectory(path) || ext == ".project" || ext == ".ccsproject" || ext == ".cproject"))
        return Model::Workspace::ForSingleProject(Model::CcsProjectReader::Read(path, cg));
    if (Model::VsProjectReader::IsSolution(path))
        return Model::Workspace::ForSolution(Model::VsSolution::Read(path));
    if (Model::VsProjectReader::IsVsProject(path))
        return Model::Workspace::ForSingleProject(Model::VsProjectReader::Read(path, std::string()));
    return nullptr;
}

int main(int argc, char** argv)
{
    std::string path, config, target, projectName;
    Services::BuildAction action = Services::BuildAction::Build;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if ((a == "--config" || a == "--target" || a == "--project") && i + 1 < argc)
        {
            std::string v = argv[++i];
            if (a == "--config") config = v;
            else if (a == "--target") target = v;
            else projectName = v;
        }
        else if (a == "--clean") action = Services::BuildAction::Clean;
        else if (a == "--rebuild") action = Services::BuildAction::Rebuild;
        else if (a == "--help" || a == "-h") { Usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); Usage(); return 2; }
        else path = a;
    }
    if (path.empty()) { Usage(); return 2; }

    std::string warning;
    Settings::GrideSettingsPtr settings = Settings::SettingsManager::Load(warning);
    if (!warning.empty()) std::printf("%s\n", warning.c_str());
    for (const std::string& problem : Settings::ToolchainDefinitions::Current()->Problems)
        std::printf("%s\n", problem.c_str());
    settings->PublishPaths();

    Model::WorkspacePtr w;
    try { w = Open(Platform::Paths::FullPath(path), settings); }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "Could not open %s\n\n%s\n", path.c_str(), ex.what());
        return 2;
    }
    if (!w) { std::fprintf(stderr, "Could not open %s: not a workspace or a project\n", path.c_str()); return 2; }
    for (const std::string& e : w->LoadErrors) std::printf("%s\n", e.c_str());
    for (const Model::GrideProjectPtr& p : w->Projects)
        for (const std::string& line : p->ImportReportLines()) std::printf("%s\n", line.c_str());

    Services::BuildRequestPtr r = std::make_shared<Services::BuildRequest>();
    if (projectName.empty()) r->Projects = w->BuildOrder();
    else
    {
        Model::GrideProjectPtr p = w->FindProject(projectName);
        if (!p) { std::fprintf(stderr, "no project %s in %s\n", projectName.c_str(), path.c_str()); return 2; }
        r->Projects = w->BuildOrderFor(p);
    }
    r->Config = config.empty() ? w->ActiveConfig : config;
    r->TargetKind = target.empty() ? w->ActiveTarget : Core::TargetInfo::Parse(target);
    r->Action = action;
    r->WorkspaceDir = w->Directory();

    Services::BuildEngine engine(settings);
    engine.OnOutput = [](const std::string& line) { std::printf("%s\n", line.c_str()); std::fflush(stdout); };
    std::vector<Services::BuildRequestPtr> one(1, r);
    Services::BuildSummaryPtr s = engine.RunBatch(one);
    if (!s) return 1;
    std::printf("%d error(s), %d warning(s)\n", s->Errors(), s->Warnings());
    return s->Ok() ? 0 : 1;
}
