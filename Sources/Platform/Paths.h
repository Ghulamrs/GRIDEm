// =============================================================================
//  Paths.h - files and folders through POSIX calls
//  Stands in for what GRIDEd takes from System::IO (File, Directory, Path) and
//  Application::ExecutablePath. No AppKit here, so the library builds and runs
//  its tests anywhere POSIX is.
//  Paths are UTF-8 with '/' separators.
// =============================================================================
#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace GRIDEm { namespace Platform {

    class Paths
    {
    public:
        // ---- names --------------------------------------------------------
        static bool IsRooted(const std::string& path);                  // starts with '/' or '~'
        static std::string Combine(const std::string& a, const std::string& b);
        // absolute, with "." and ".." resolved and no trailing '/'; the file need not exist
        // (System::IO::Path::GetFullPath)
        static std::string FullPath(const std::string& path);
        static std::string FullPath(const std::string& baseDir, const std::string& path);
        static std::string DirectoryName(const std::string& path);      // "" for a bare name
        static std::string FileName(const std::string& path);
        static std::string FileNameWithoutExtension(const std::string& path);
        static std::string Extension(const std::string& path);          // ".c", or ""
        static std::string ChangeExtension(const std::string& path, const std::string& ext);
        static std::string ExpandHome(const std::string& path);         // "~/x" -> "/Users/me/x"

        // ---- what is there ------------------------------------------------
        static bool Exists(const std::string& path);
        static bool IsFile(const std::string& path);
        static bool IsDirectory(const std::string& path);
        static bool IsExecutable(const std::string& path);
        static bool ModifiedTime(const std::string& path, std::time_t& out);
        static long long FileSize(const std::string& path);              // -1 when absent
        // names (not paths) in a folder, sorted, without "." and ".."
        static std::vector<std::string> List(const std::string& dir);
        static std::vector<std::string> ListFiles(const std::string& dir);
        static std::vector<std::string> ListDirectories(const std::string& dir);

        // ---- changes ------------------------------------------------------
        static bool CreateDirectories(const std::string& dir);
        static bool ReadFile(const std::string& path, std::string& out, std::string& error);
        static bool WriteFile(const std::string& path, const std::string& data, std::string& error);
        // a temp file beside it, then rename - never a half-written file
        static bool WriteFileAtomically(const std::string& path, const std::string& data, std::string& error);
        static bool DeleteFile(const std::string& path);
        static bool CopyFile(const std::string& from, const std::string& to, std::string& error);
        static bool Rename(const std::string& from, const std::string& to, std::string& error);
        // deletes a folder and everything in it
        static bool DeleteTree(const std::string& dir);

        // ---- places -------------------------------------------------------
        static std::string CurrentDirectory();
        static std::string HomeDirectory();
        static std::string TempDirectory();
        static std::string ExecutablePath();
        // the folder of the program: GRIDEm.app/Contents/MacOS in the app
        static std::string AppDirectory();
        // GRIDEm.app/Contents/Resources in the app; beside the program otherwise
        static std::string ResourcesDirectory();
        // ~/Library/Application Support/GRIDE on the Mac, $XDG_CONFIG_HOME/GRIDE elsewhere
        static std::string UserSettingsDirectory();

        // ---- environment --------------------------------------------------
        static bool GetEnvironment(const std::string& name, std::string& out);   // false when unset
        static std::vector<std::string> SearchPath();                            // $PATH split
    };
}}
