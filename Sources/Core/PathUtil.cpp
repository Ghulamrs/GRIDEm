// =============================================================================
//  PathUtil.cpp - see PathUtil.h
// =============================================================================
#include "PathUtil.h"
#include "StringUtil.h"
#include "../Platform/Paths.h"
#include <vector>

namespace GRIDEm { namespace Core {

    using Platform::Paths;

    std::string PathUtil::WithTrailingSlash(const std::string& dir)
    {
        if (dir.empty()) return dir;
        char last = dir[dir.size() - 1];
        if (last == '/' || last == '\\') return dir;
        return dir + "/";
    }

    std::string PathUtil::Normalize(const std::string& path)
    {
        return StringUtil::ReplaceChar(path, '\\', '/');
    }

    // done by hand, as GRIDEd does it: a URI would read '#' and "%xx" in folder names
    std::string PathUtil::MakeRelative(const std::string& baseDir, const std::string& fullPath)
    {
        if (baseDir.empty() || fullPath.empty()) return fullPath;
        std::string b = Paths::FullPath(Normalize(baseDir));
        std::string f = Paths::FullPath(Normalize(fullPath));
        std::vector<std::string> bp = StringUtil::SplitNonEmpty(b, "/");
        std::vector<std::string> fp = StringUtil::SplitNonEmpty(f, "/");
        size_t common = 0;
        while (common < bp.size() && common < fp.size() && StringUtil::EqualsIgnoreCase(bp[common], fp[common]))
            common++;
        std::string sb;
        for (size_t i = common; i < bp.size(); i++) sb += "../";
        for (size_t i = common; i < fp.size(); i++)
        {
            sb += fp[i];
            if (i < fp.size() - 1) sb += '/';
        }
        if (sb.empty()) return ".";
        return sb;
    }

    bool PathUtil::IsStrictlyUnder(const std::string& parent, const std::string& child)
    {
        if (parent.empty() || child.empty()) return false;
        std::string p = Paths::FullPath(Normalize(parent));
        std::string c = Paths::FullPath(Normalize(child));
        if (p == "/") return c.size() > 1;
        if (c.size() <= p.size() + 1) return false;
        return StringUtil::StartsWithIgnoreCase(c, p + "/");
    }

    std::string PathUtil::MakeAbsolute(const std::string& baseDir, const std::string& relPath)
    {
        if (relPath.empty()) return relPath;
        std::string p = Normalize(relPath);
        if (Paths::IsRooted(p)) return Paths::FullPath(p);
        return Paths::FullPath(Normalize(baseDir), p);
    }

    bool PathUtil::SamePath(const std::string& a, const std::string& b)
    {
        if (a.empty() || b.empty()) return false;
        return StringUtil::EqualsIgnoreCase(Paths::FullPath(Normalize(a)), Paths::FullPath(Normalize(b)));
    }

    bool PathUtil::HasExtension(const std::string& path, const std::string& ext)
    {
        return StringUtil::EqualsIgnoreCase(Paths::Extension(Normalize(path)), ext);
    }

    std::string PathUtil::Quote(const std::string& s)
    {
        if (s.empty()) return "\"\"";
        if (s[0] == '"') return s;
        return "\"" + s + "\"";
    }

    void PathUtil::EnsureDirectory(const std::string& dir)
    {
        if (!dir.empty() && !Paths::IsDirectory(dir)) Paths::CreateDirectories(dir);
    }

    std::string PathUtil::AppDirectory()
    {
        return Paths::AppDirectory();
    }
}}
