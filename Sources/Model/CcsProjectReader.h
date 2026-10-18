// =============================================================================
//  CcsProjectReader.h - read-only import of Code Composer Studio 7.4 / 5.5 projects
//
//  CCS is Eclipse based. A project folder holds three XML files:
//     .project      Eclipse: project name, linked resources
//     .cproject     CDT: build configurations, include paths, defines, libs
//     .ccsproject   TI: device variant, code-generation tools version
//  CcsReading reads them as RIDE does; this turns the reading into an in-memory
//  GrideProject (Kind = "ccs7", IsReadOnly = true). Nothing is ever written back
//  to the CCS files. (GRIDEd's Model/CcsProjectReader.h)
// =============================================================================
#pragma once
#include "CcsReading.h"
#include "GrideProject.h"
#include <string>

namespace GRIDEm { namespace Model {

    class CcsProjectReader
    {
    public:
        // pathOrFolder may be the project folder, its .project or its .ccsproject; "" when neither exists
        static std::string ResolveFolder(const std::string& pathOrFolder);
        static bool IsCcsProject(const std::string& pathOrFolder);
        // a CCS workspace folder (.metadata), which is not a project: its members open one at a time
        static bool IsCcsWorkspace(const std::string& folder);
        // cgToolRoot = the CCS root (or the compiler directory) from the settings (may be empty).
        // Throws std::runtime_error when it is not a CCS project or a file will not parse.
        static GrideProjectPtr Read(const std::string& pathOrFolder, const std::string& cgToolRoot);

    private:
        // a path inside the project folder is kept relative; one outside stays absolute
        static std::string Relative(const std::string& dir, const std::string& absSlashed);
    };
}}
