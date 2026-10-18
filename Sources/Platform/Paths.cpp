// =============================================================================
//  Paths.cpp - see Paths.h
// =============================================================================
#include "Paths.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace GRIDEm { namespace Platform {

    static std::string ErrnoText(const std::string& what, const std::string& path)
    {
        return what + " '" + path + "': " + std::strerror(errno);
    }

    // ---- names ---------------------------------------------------------------
    bool Paths::IsRooted(const std::string& path)
    {
        return !path.empty() && (path[0] == '/' || path[0] == '~');
    }

    std::string Paths::Combine(const std::string& a, const std::string& b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        if (IsRooted(b)) return b;
        if (a[a.size() - 1] == '/') return a + b;
        return a + "/" + b;
    }

    std::string Paths::ExpandHome(const std::string& path)
    {
        if (path.empty() || path[0] != '~') return path;
        if (path.size() == 1) return HomeDirectory();
        if (path[1] == '/') return HomeDirectory() + path.substr(1);
        return path;
    }

    std::string Paths::FullPath(const std::string& path)
    {
        return FullPath(CurrentDirectory(), path);
    }

    std::string Paths::FullPath(const std::string& baseDir, const std::string& path)
    {
        std::string p = ExpandHome(path);
        if (!IsRooted(p)) p = Combine(ExpandHome(baseDir), p);
        if (!IsRooted(p)) p = Combine(CurrentDirectory(), p);
        std::vector<std::string> parts;
        size_t i = 0;
        while (i <= p.size())
        {
            size_t j = p.find('/', i);
            if (j == std::string::npos) j = p.size();
            std::string part = p.substr(i, j - i);
            if (part == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!part.empty() && part != ".") parts.push_back(part);
            i = j + 1;
        }
        std::string r;
        for (const std::string& part : parts) r += "/" + part;
        if (r.empty()) r = "/";
        return r;
    }

    std::string Paths::DirectoryName(const std::string& path)
    {
        size_t i = path.find_last_of('/');
        if (i == std::string::npos) return std::string();
        if (i == 0) return "/";
        return path.substr(0, i);
    }

    std::string Paths::FileName(const std::string& path)
    {
        size_t i = path.find_last_of('/');
        if (i == std::string::npos) return path;
        return path.substr(i + 1);
    }

    std::string Paths::Extension(const std::string& path)
    {
        std::string name = FileName(path);
        size_t i = name.find_last_of('.');
        if (i == std::string::npos || i == name.size() - 1) return std::string();
        return name.substr(i);
    }

    std::string Paths::FileNameWithoutExtension(const std::string& path)
    {
        std::string name = FileName(path);
        std::string ext = Extension(name);
        return name.substr(0, name.size() - ext.size());
    }

    std::string Paths::ChangeExtension(const std::string& path, const std::string& ext)
    {
        std::string old = Extension(path);
        std::string stem = path.substr(0, path.size() - old.size());
        if (ext.empty()) return stem;
        if (ext[0] == '.') return stem + ext;
        return stem + "." + ext;
    }

    // ---- what is there -------------------------------------------------------
    bool Paths::Exists(const std::string& path)
    {
        struct stat st;
        return !path.empty() && stat(ExpandHome(path).c_str(), &st) == 0;
    }

    bool Paths::IsFile(const std::string& path)
    {
        struct stat st;
        return !path.empty() && stat(ExpandHome(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    bool Paths::IsDirectory(const std::string& path)
    {
        struct stat st;
        return !path.empty() && stat(ExpandHome(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool Paths::IsExecutable(const std::string& path)
    {
        return IsFile(path) && access(ExpandHome(path).c_str(), X_OK) == 0;
    }

    bool Paths::ModifiedTime(const std::string& path, std::time_t& out)
    {
        struct stat st;
        if (path.empty() || stat(ExpandHome(path).c_str(), &st) != 0) return false;
        out = st.st_mtime;
        return true;
    }

    long long Paths::FileSize(const std::string& path)
    {
        struct stat st;
        if (path.empty() || stat(ExpandHome(path).c_str(), &st) != 0) return -1;
        return (long long)st.st_size;
    }

    static std::vector<std::string> ListWhere(const std::string& dir, int which)   // 0 all, 1 files, 2 dirs
    {
        std::vector<std::string> r;
        DIR* d = opendir(Paths::ExpandHome(dir).c_str());
        if (d == nullptr) return r;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr)
        {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            if (which == 1 && !Paths::IsFile(Paths::Combine(dir, name))) continue;
            if (which == 2 && !Paths::IsDirectory(Paths::Combine(dir, name))) continue;
            r.push_back(name);
        }
        closedir(d);
        std::sort(r.begin(), r.end());
        return r;
    }

    std::vector<std::string> Paths::List(const std::string& dir)            { return ListWhere(dir, 0); }
    std::vector<std::string> Paths::ListFiles(const std::string& dir)       { return ListWhere(dir, 1); }
    std::vector<std::string> Paths::ListDirectories(const std::string& dir) { return ListWhere(dir, 2); }

    // ---- changes -------------------------------------------------------------
    bool Paths::CreateDirectories(const std::string& dir)
    {
        if (dir.empty()) return false;
        std::string full = FullPath(dir);
        if (IsDirectory(full)) return true;
        std::string parent = DirectoryName(full);
        if (!parent.empty() && parent != full && !IsDirectory(parent))
            if (!CreateDirectories(parent)) return false;
        return mkdir(full.c_str(), 0755) == 0 || IsDirectory(full);
    }

    bool Paths::ReadFile(const std::string& path, std::string& out, std::string& error)
    {
        FILE* f = std::fopen(ExpandHome(path).c_str(), "rb");
        if (f == nullptr) { error = ErrnoText("cannot open", path); return false; }
        std::string data;
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        bool bad = std::ferror(f) != 0;
        std::fclose(f);
        if (bad) { error = ErrnoText("cannot read", path); return false; }
        out.swap(data);
        return true;
    }

    bool Paths::WriteFile(const std::string& path, const std::string& data, std::string& error)
    {
        std::string full = ExpandHome(path);
        std::string dir = DirectoryName(full);
        if (!dir.empty() && !IsDirectory(dir)) CreateDirectories(dir);
        FILE* f = std::fopen(full.c_str(), "wb");
        if (f == nullptr) { error = ErrnoText("cannot write", path); return false; }
        bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
        if (std::fclose(f) != 0) ok = false;
        if (!ok) { error = ErrnoText("cannot write", path); return false; }
        return true;
    }

    bool Paths::WriteFileAtomically(const std::string& path, const std::string& data, std::string& error)
    {
        std::string full = FullPath(path);
        std::string tmp = full + ".tmp";
        if (!WriteFile(tmp, data, error)) { DeleteFile(tmp); return false; }
        if (std::rename(tmp.c_str(), full.c_str()) != 0)
        {
            error = ErrnoText("cannot replace", path);
            DeleteFile(tmp);
            return false;
        }
        return true;
    }

    bool Paths::DeleteFile(const std::string& path)
    {
        return unlink(ExpandHome(path).c_str()) == 0;
    }

    bool Paths::CopyFile(const std::string& from, const std::string& to, std::string& error)
    {
        std::string data;
        if (!ReadFile(from, data, error)) return false;
        if (!WriteFile(to, data, error)) return false;
        struct stat st;
        if (stat(ExpandHome(from).c_str(), &st) == 0) chmod(ExpandHome(to).c_str(), st.st_mode & 07777);
        return true;
    }

    bool Paths::Rename(const std::string& from, const std::string& to, std::string& error)
    {
        if (std::rename(ExpandHome(from).c_str(), ExpandHome(to).c_str()) != 0)
        {
            error = ErrnoText("cannot rename", from);
            return false;
        }
        return true;
    }

    bool Paths::DeleteTree(const std::string& dir)
    {
        if (!IsDirectory(dir)) return DeleteFile(dir);
        bool ok = true;
        for (const std::string& name : List(dir))
        {
            std::string p = Combine(dir, name);
            struct stat st;
            if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { if (!DeleteTree(p)) ok = false; }
            else if (!DeleteFile(p)) ok = false;
        }
        if (rmdir(ExpandHome(dir).c_str()) != 0) ok = false;
        return ok;
    }

    // ---- places --------------------------------------------------------------
    std::string Paths::CurrentDirectory()
    {
        char buf[4096];
        if (getcwd(buf, sizeof buf) == nullptr) return "/";
        return buf;
    }

    std::string Paths::HomeDirectory()
    {
        std::string home;
        if (GetEnvironment("HOME", home) && !home.empty()) return home;
        struct passwd* pw = getpwuid(getuid());
        if (pw != nullptr && pw->pw_dir != nullptr) return pw->pw_dir;
        return "/";
    }

    std::string Paths::TempDirectory()
    {
        std::string t;
        if (GetEnvironment("TMPDIR", t) && !t.empty())
        {
            if (t.size() > 1 && t[t.size() - 1] == '/') t.erase(t.size() - 1);
            return t;
        }
        return "/tmp";
    }

    std::string Paths::ExecutablePath()
    {
#if defined(__APPLE__)
        char buf[4096];
        uint32_t size = sizeof buf;
        if (_NSGetExecutablePath(buf, &size) == 0)
        {
            char real[4096];
            if (realpath(buf, real) != nullptr) return real;
            return buf;
        }
        return std::string();
#else
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n <= 0) return std::string();
        buf[n] = '\0';
        return buf;
#endif
    }

    std::string Paths::AppDirectory()
    {
        return DirectoryName(ExecutablePath());
    }

    std::string Paths::ResourcesDirectory()
    {
        std::string app = AppDirectory();
        // GRIDEm.app/Contents/MacOS -> GRIDEm.app/Contents/Resources
        if (FileName(app) == "MacOS")
        {
            std::string res = Combine(DirectoryName(app), "Resources");
            if (IsDirectory(res)) return res;
        }
        return app;
    }

    std::string Paths::UserSettingsDirectory()
    {
#if defined(__APPLE__)
        return Combine(HomeDirectory(), "Library/Application Support/GRIDE");
#else
        std::string xdg;
        if (GetEnvironment("XDG_CONFIG_HOME", xdg) && !xdg.empty()) return Combine(xdg, "GRIDE");
        return Combine(HomeDirectory(), ".config/GRIDE");
#endif
    }

    // ---- environment ---------------------------------------------------------
    bool Paths::GetEnvironment(const std::string& name, std::string& out)
    {
        const char* v = std::getenv(name.c_str());
        if (v == nullptr) return false;
        out = v;
        return true;
    }

    std::vector<std::string> Paths::SearchPath()
    {
        std::vector<std::string> r;
        std::string path;
        if (!GetEnvironment("PATH", path)) return r;
        size_t i = 0;
        while (i <= path.size())
        {
            size_t j = path.find(':', i);
            if (j == std::string::npos) j = path.size();
            if (j > i) r.push_back(path.substr(i, j - i));
            i = j + 1;
        }
        return r;
    }
}}
