// =============================================================================
//  GRIDEm-toolcheck.cpp - Tools > Toolchain Check as a console tool (phase 1 of
//  the reference): reads toolchain.json as GRIDEm does (the shipped file with
//  the user's laid over it), prints every problem in it, then each tool: where
//  it was found, or that it is missing and everywhere it was looked for.
//    GRIDEm-toolcheck [--toolchain path/to/toolchain.json] [--tooldir dir] [--root dir]
//  Exit code: 0 when the file has no problems, 1 otherwise (missing tools are
//  reported, not counted - a Mac has no cdb).
// =============================================================================
#include "Settings/ToolchainDefinitions.h"
#include "Services/RideToolchain.h"
#include <cstdio>
#include <string>

using namespace GRIDEm;

int main(int argc, char** argv)
{
    std::string shipped, toolDir, root;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--toolchain" && i + 1 < argc) shipped = argv[++i];
        else if (a == "--tooldir" && i + 1 < argc) toolDir = argv[++i];
        else if (a == "--root" && i + 1 < argc) root = argv[++i];
        else
        {
            std::printf("usage: GRIDEm-toolcheck [--toolchain file] [--tooldir dir] [--root dir]\n");
            return 2;
        }
    }
    if (shipped.empty()) shipped = Settings::ToolchainDefinitions::ShippedPath();
    std::string user = Settings::ToolchainDefinitions::UserPath(!Settings::ToolchainDefinitions::PortableDirectory().empty());
    Settings::ToolchainDefinitionsPtr defs = Settings::ToolchainDefinitions::Load(shipped, user);
    Settings::ToolchainDefinitions::SetCurrent(defs);

    std::printf("toolchain.json (shipped): %s%s\n", defs->ShippedFile.c_str(), defs->ShippedFound ? "" : "  (not found)");
    std::printf("toolchain.json (user):    %s\n", defs->UserFile.empty() ? "(none)" : defs->UserFile.c_str());
    std::printf("%d targets, %d tools, %d libraries, %d compiler options\n",
                (int)defs->Targets.size(), (int)defs->Tools.size(), (int)defs->Libraries.size(),
                (int)defs->CompilerOptions.size());
    Settings::TargetDefinitionPtr host = defs->HostTarget();
    std::printf("host target: %s\n\n", host ? host->Name.c_str() : "(none)");

    std::printf("Problems (%d)\n", (int)defs->Problems.size());
    for (const std::string& p : defs->Problems) std::printf("  %s\n", p.c_str());

    int found = 0, missing = 0;
    std::printf("\nTools\n");
    for (const Settings::ToolDefinitionPtr& t : defs->Tools)
    {
        std::string path = Services::RideToolchain::FindTool(t, toolDir, root);
        if (!path.empty())
        {
            found++;
            std::printf("  found    %-10s %s\n", t->Id.c_str(), path.c_str());
        }
        else
        {
            missing++;
            std::printf("  missing  %-10s looked in %s\n", t->Id.c_str(),
                        Services::RideToolchain::SearchPlaces(t, toolDir, root).c_str());
        }
    }
    std::printf("\n%d found, %d missing\n", found, missing);
    return defs->Problems.empty() ? 0 : 1;
}
