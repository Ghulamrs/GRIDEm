// =============================================================================
//  PathUtil.h - relative/absolute path helpers (GRIDEd's Core/PathUtil.h)
//  Project files (.pro / .grws) always store paths relative to themselves with
//  forward slashes, e.g. "src/kalman.c", so a project can be moved or zipped -
//  and opened on the Mac after it was made on Windows. A path written on
//  Windows with '\' is read with '/'.
//  Comparisons ignore case, as GRIDEd's do (and as APFS does by default).
// =============================================================================
#pragma once
#include <string>

namespace GRIDEm { namespace Core {

    class PathUtil
    {
    public:
        static std::string WithTrailingSlash(const std::string& dir);
        // '\' -> '/' (a path written on Windows)
        static std::string Normalize(const std::string& path);
        // absolute -> "src/file.c" (relative to baseDir)
        static std::string MakeRelative(const std::string& baseDir, const std::string& fullPath);
        // true when child lies inside parent (never parent itself)
        static bool IsStrictlyUnder(const std::string& parent, const std::string& child);
        // "src/file.c" -> /Users/.../src/file.c ; absolute paths pass through
        static std::string MakeAbsolute(const std::string& baseDir, const std::string& relPath);
        static bool SamePath(const std::string& a, const std::string& b);
        // case-insensitive extension check, ext given with the dot (".c")
        static bool HasExtension(const std::string& path, const std::string& ext);
        static std::string Quote(const std::string& s);
        static void EnsureDirectory(const std::string& dir);
        // the folder that holds the GRIDEm program (Contents/MacOS in the app)
        static std::string AppDirectory();
    };
}}
