// CodeBase.hpp - loads solutions, projects and source files, follows
// #include lines, and links declarations to definitions across files.
//
// Strict C++98.
#ifndef CPPPARSE_CODEBASE_HPP
#define CPPPARSE_CODEBASE_HPP

#include "Model.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

class CodeBase {
public:
    CodeBase();
    ~CodeBase();

    void setFollowIncludes(bool follow) { m_followIncludes = follow; }
    void setThreads(int threads) { m_threads = threads < 1 ? 1 : threads; }
    int  threads() const { return m_threads; }
    long parseMilliseconds() const { return m_parseMs; }   // time spent reading, lexing and parsing

    // Accepts a .sln, .vcxproj, .vcproj, a folder or a single header/source file.
    bool addInput(const std::string& path);
    // Several plain files at once (wildcard expansions, response files): parsed in parallel.
    bool addFiles(const std::vector<std::string>& paths);

    // Resolves base classes, matches out-of-line definitions to declarations,
    // computes derived-class lists and field associations. Call once.
    void link();

    // Runs the version 2 health checks (after link()).
    void checkHealth();
    void addFindings(const std::vector<Finding>& more);   // version 3 type checker results
    const std::vector<Finding>&       findings()  const { return m_findings; }
    const SourceFile*                 fileByPath(const std::string& path) const;

    const std::vector<SourceFile*>&   files()     const { return m_files; }
    const std::vector<ProjectInfo>&   projects()  const { return m_projects; }
    const std::vector<SolutionInfo>&  solutions() const { return m_solutions; }
    const std::vector<ClassInfo*>&    classes()   const { return m_classes; }
    const std::vector<std::string>&   messages()  const { return m_messages; }
    const std::vector<std::string>&   inputs()    const { return m_inputs; }

    std::string title() const;        // name of the first solution/project/file
    std::string baseDirectory() const; // directory used for relative display paths
    std::string displayPath(const std::string& abs) const;

    ClassInfo* findClass(const std::string& scope, const std::string& name) const;

private:
    CodeBase(const CodeBase&);
    CodeBase& operator=(const CodeBase&);

    bool        loadSolution(const std::string& path);
    bool        loadFolder(const std::string& path);
    bool        loadProject(const std::string& path, const std::string& solutionDir);
    SourceFile* loadFile(const std::string& absPath, const std::string& origin, const std::string& project,
                         const std::vector<std::string>& includeDirs);
    // parses many files on the worker pool, then registers them and follows their includes in rounds
    void        loadFiles(const std::vector<std::string>& absPaths, const std::string& origin, const std::string& project,
                          const std::vector<std::string>& includeDirs);
    SourceFile* registerFile(SourceFile* f, const std::string& absPath, const std::string& origin, const std::string& project);
    void        resolveIncludes(SourceFile* f, const std::vector<std::string>& includeDirs, const std::string& project,
                                std::vector<std::string>* discovered);
    void        indexClasses();
    void        linkBases();
    void        linkFunctions();
    void        linkVariables();
    void        linkAssociations();
    void        collectTypeNames(const std::string& type, std::vector<std::string>& out) const;

    bool                                m_followIncludes;
    int                                 m_threads;
    long                                m_parseMs;
    std::vector<std::string>            m_inputs;
    std::vector<SourceFile*>            m_files;
    std::vector<ProjectInfo>            m_projects;
    std::vector<SolutionInfo>           m_solutions;
    std::vector<ClassInfo*>             m_classes;
    std::vector<std::string>            m_messages;
    std::vector<Finding>                m_findings;
    std::map<std::string, SourceFile*>  m_byPath;
    std::map<std::string, ClassInfo*>   m_byQualified;
    std::map<std::string, std::vector<ClassInfo*> > m_bySimple;
};

} // namespace cppparse

#endif
