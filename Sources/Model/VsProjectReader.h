// =============================================================================
//  VsProjectReader.h - a Visual Studio C++ project as a read-only GrideProject
//
//  The reading (VsSolution.h) becomes a project of Kind "vs": the files in the
//  folders of its .filters (or their directories), the Debug and Release
//  configurations for the Windows target from the project's own (x64 first, then
//  Win32), with its defines, include directories, libraries and optimisation;
//  what the file has and GRIDE does not take is said in the report lines. The
//  build is GRIDEm's own toolchain into .gride/ under the project; Microsoft's cl
//  is Windows only. The Visual Studio files are never written.
//  (GRIDEd's Model/VsProjectReader.h)
// =============================================================================
#pragma once
#include "GrideProject.h"
#include <string>

namespace GRIDEm { namespace Model {

    class VsProjectReader
    {
    public:
        static bool IsVsProject(const std::string& path);
        static bool IsSolution(const std::string& path);
        // solutionPath may be empty (a project opened alone: $(SolutionDir) is then unresolved and said so).
        // Throws std::runtime_error when the file cannot be read.
        static GrideProjectPtr Read(const std::string& path, const std::string& solutionPath);

    private:
        static VsProjectConfigPtr Pick(const VsProjectPtr& r, const std::string& name);
        static std::string OptimiseOf(const VsProjectConfigPtr& c);
        static std::string Relative(const std::string& dir, const std::string& abs);
    };
}}
