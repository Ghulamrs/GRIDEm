// =============================================================================
//  Workspace.h - a group of projects (.grws JSON), like a Visual Studio .sln
//
//  {
//    "gride": "1.0", "type": "workspace", "name": "cppparse",
//    "projects": [ { "path": "cppparse/cppparse.vcxproj", "kind": "vs", "readOnly": true, "solution": "cppparse.sln",
//                    "toolchain": "cl", "programArgs": ["--health", "."] } ],
//    "solution": "cppparse.sln",
//    "startup": "cppparse", "activeConfig": "Release", "activeTarget": "Windows"
//  }
//  A project read from Visual Studio's or CCS's files ("vs", "ccs7") is never written; what
//  GRIDE keeps for it - the compiler word, the folders' own, the program's arguments,
//  emulateOnly - is kept in its entry here. A .grws written by GRIDEd opens here and is
//  written back as GRIDEd writes it. (GRIDEd's Model/Workspace.h)
// =============================================================================
#pragma once
#include "CcsProjectReader.h"
#include "GrideProject.h"
#include "VsProjectReader.h"
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace GRIDEm { namespace Model {

    // full path -> 1-based lines; shared with the editors and the debugger. Keeps the order the
    // files were added (as GRIDEd's Dictionary does) and ignores case in the paths.
    class BreakpointTable
    {
    public:
        typedef std::vector<std::pair<std::string, std::set<int> > > Entries;
        std::set<int>* Find(const std::string& path);                 // nullptr when the file has none
        const std::set<int>* Find(const std::string& path) const;
        std::set<int>& GetOrAdd(const std::string& path);
        void Set(const std::string& path, const std::set<int>& lines);
        void Remove(const std::string& path);
        void Clear() { entries.clear(); }
        int Count() const { return (int)entries.size(); }
        const Entries& Items() const { return entries; }
    private:
        Entries entries;
    };

    // Files read-only on disk that GRIDEm was allowed to write. The first save of such a file asks
    // ("File on the disk is read only, are you sure you need to update?") once per key - a project's
    // file path, so once per project; the file's own path for a file of no project. On yes the file is
    // made writable for its owner and its mode remembered; RelockAll gives every such file the mode it
    // had, so once the project is closed other programs find its files read-only again.
    class DiskLocks
    {
    public:
        typedef std::vector<std::pair<std::string, unsigned> > Entries;   // path, mode before
        // true for a file that may be written as it is - or that does not exist yet
        static bool IsWritable(const std::string& path);
        // true when path may be written now: writable already, or agreed (ask() is called only when
        // the key has not been agreed to yet) and made writable. A no leaves the file as it is.
        bool Prepare(const std::string& path, const std::string& key, const std::function<bool()>& ask);
        void RelockAll();              // and forgets what was agreed
        const Entries& Items() const { return entries; }
    private:
        Entries entries;
        std::vector<std::string> agreed;
        bool Unlock(const std::string& path);
    };

    class Workspace;
    typedef std::shared_ptr<Workspace> WorkspacePtr;

    class Workspace
    {
    public:
        Workspace();

        // a Visual Studio solution opened as a workspace: the .sln, its solution folders ("Libs/Inner",
        // outer ones first) and the files they hold (absolute), which the explorer shows under them
        std::string SolutionPath;
        std::vector<std::string> SolutionFolders;
        std::vector<std::pair<std::string, std::vector<std::string> > > SolutionItems;

        // the solution's C++ projects read-only, their dependencies by GUID, the solution folders and
        // items; a project that cannot be read, and every non-C++ project, is a line in LoadErrors
        static WorkspacePtr ForSolution(const VsSolutionPtr& s);

        std::string FilePath;
        std::string Name;
        std::vector<GrideProjectPtr> Projects;
        std::vector<std::string> LoadErrors;
        std::string StartupName;
        std::string ActiveConfig;
        Core::Target ActiveTarget;
        bool IsTemporary;              // opened from a single .pro - not yet saved as .grws
        bool IsDirty;
        BreakpointTable Breakpoints;

        std::string Directory() const;
        GrideProjectPtr StartupProject() const;
        GrideProjectPtr FindProject(const std::string& name) const;
        GrideProjectPtr FindProjectForFile(const std::string& abs) const;
        // whether the editor holds a file read-only: never, while its project is open. An imported
        // project's lock (IsReadOnly) is on its structure - files, folders, settings - not on the text
        // of the files it lists, so a CCS or Visual Studio project's sources edit and save like any other.
        bool IsDocumentReadOnly(const std::string& abs) const;
        // the project a file belongs to: one it lists, or the one whose own file (.pro) it is
        GrideProjectPtr FindProjectOwning(const std::string& abs) const;
        // read-only files made writable while the workspace is open (DiskLocks); closing it relocks them
        DiskLocks Locks;
        // true when abs may be written now - asked once per project when the file is read-only on disk
        bool PrepareWrite(const std::string& abs, const std::function<bool()>& ask);
        bool AddProject(const GrideProjectPtr& p);
        void RemoveProject(const GrideProjectPtr& p);
        // projects ordered so that dependencies are built first
        std::vector<GrideProjectPtr> BuildOrder() const;
        // dependencies of one project (recursively) followed by the project itself
        std::vector<GrideProjectPtr> BuildOrderFor(const GrideProjectPtr& root) const;

        // ---- load / save --------------------------------------------------------
        // cgToolRoot is needed to expand ${CG_TOOL_ROOT} in CCS projects. Throws std::runtime_error
        // (Core::JsonException for a malformed file); a project that cannot be read is a line in LoadErrors.
        static WorkspacePtr Load(const std::string& path, const std::string& cgToolRoot);
        // wraps a single project (opened from .pro or CCS folder) in a workspace
        static WorkspacePtr ForSingleProject(const GrideProjectPtr& p);
        // true when saving would write something different from the file as loaded / last saved
        bool HasChanges() const;
        void Save();                   // throws std::runtime_error when the file cannot be written
        Core::JsonPtr ToJson() const;
        // saves every writable project that changed
        void SaveProjects();

        // what GRIDE keeps for an imported project, in its entry: each field only when it says something
        static void WriteImportedState(const Core::JsonPtr& e, const GrideProjectPtr& p);
        static void ReadImportedState(const Core::JsonPtr& e, const GrideProjectPtr& p);
        // the Property Pages' changes to an imported project: the entries of ImportedSettings that differ
        // from what its files say (ImportedBaseline); nullptr when there are none
        static Core::JsonPtr ImportedSettingsChanged(const GrideProjectPtr& p);

    private:
        bool hasSavedText;
        std::string savedText;         // JSON of the file as loaded / last saved
        void Visit(const GrideProjectPtr& p, std::vector<GrideProjectPtr>& result,
                   std::vector<GrideProject*>& visiting) const;
    };
}}
