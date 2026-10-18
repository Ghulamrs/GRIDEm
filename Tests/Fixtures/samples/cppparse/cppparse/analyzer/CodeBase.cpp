// CodeBase.cpp - loading, include following and cross-file linking.
#include "CodeBase.hpp"
#include "Health.hpp"
#include "Lexer.hpp"
#include "Parallel.hpp"
#include "Parser.hpp"
#include "PathUtil.hpp"
#include "ProjectReader.hpp"

#include <algorithm>
#include <cctype>

namespace cppparse {

CodeBase::CodeBase() : m_followIncludes(true), m_threads(hardwareThreads()), m_parseMs(0) {}

CodeBase::~CodeBase()
{
    for (size_t i = 0; i < m_files.size(); ++i) delete m_files[i];
}

std::string CodeBase::title() const
{
    if (!m_solutions.empty()) return baseName(m_solutions[0].path);
    if (!m_projects.empty()) return m_projects[0].name;
    if (!m_inputs.empty()) return baseName(m_inputs[0]);
    return "cppparse";
}

std::string CodeBase::baseDirectory() const
{
    if (!m_solutions.empty()) return dirName(m_solutions[0].path);
    if (!m_projects.empty()) return dirName(m_projects[0].path);
    if (!m_inputs.empty()) return dirName(absolutePath(m_inputs[0]));
    return currentDirectory();
}

std::string CodeBase::displayPath(const std::string& abs) const
{
    return relativeTo(baseDirectory(), abs);
}

// ---- loading ------------------------------------------------------------------

bool CodeBase::addInput(const std::string& path)
{
    m_inputs.push_back(path);
    if (isDirectory(path)) return loadFolder(path);
    std::string ext = extension(path);
    if (ext == ".sln") return loadSolution(path);
    if (ext == ".vcxproj" || ext == ".vcproj") return loadProject(path, "");

    std::vector<std::string> one;
    one.push_back(path);
    return addFiles(one);
}

bool CodeBase::addFiles(const std::vector<std::string>& paths)
{
    std::vector<std::string> abs;
    std::vector<std::string> inc;
    bool ok = true;
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string a = absolutePath(paths[i]);
        if (!fileExists(a)) { m_messages.push_back("error: cannot open " + paths[i]); ok = false; continue; }
        if (m_projects.empty() || m_projects.back().kind != "files") {
            ProjectInfo p;
            p.kind = "files";
            p.name = "(files given on the command line)";
            p.path = a;
            p.version = "n/a";
            m_projects.push_back(p);
        }
        ProjectInfo& p = m_projects.back();
        if (isHeaderExtension(extension(a))) addUnique(p.headerFiles, a); else addUnique(p.sourceFiles, a);
        addUnique(inc, dirName(a));
        abs.push_back(a);
    }
    if (abs.empty()) return ok;
    loadFiles(abs, "command line", m_projects.back().name, inc);
    return ok;
}

bool CodeBase::loadSolution(const std::string& path)
{
    SolutionInfo sln;
    if (!ProjectReader::readSolution(path, sln)) {
        m_messages.push_back("error: cannot read solution " + path);
        return false;
    }
    m_solutions.push_back(sln);
    for (size_t i = 0; i < sln.warnings.size(); ++i) m_messages.push_back("solution: " + sln.warnings[i]);
    if (sln.projectPaths.empty()) m_messages.push_back("solution lists no C++ projects: " + path);
    bool ok = true;
    for (size_t i = 0; i < sln.projectPaths.size(); ++i)
        if (!loadProject(sln.projectPaths[i], dirName(sln.path))) ok = false;
    return ok;
}

bool CodeBase::loadFolder(const std::string& path)
{
    std::string dir = normalizePath(absolutePath(path));
    if (dir.size() > 1 && dir[dir.size() - 1] == '/') dir.erase(dir.size() - 1);
    ProjectInfo p;
    p.kind = "folder";
    p.name = baseName(dir);
    if (p.name.empty()) p.name = dir;
    p.path = dir + "/(folder)";     // dirName(path) yields the folder itself
    p.version = "folder scan (recursive)";
    std::vector<std::string> dirsWithHeaders;
    findSourceFiles(dir, p.headerFiles, p.sourceFiles, dirsWithHeaders, 0);
    p.includeDirs.push_back(dir);
    for (size_t i = 0; i < dirsWithHeaders.size(); ++i) addUnique(p.includeDirs, dirsWithHeaders[i]);
    if (p.headerFiles.empty() && p.sourceFiles.empty()) {
        m_messages.push_back("folder contains no header or source files: " + dir);
        m_projects.push_back(p);
        return false;
    }
    std::vector<std::string> all(p.headerFiles);
    all.insert(all.end(), p.sourceFiles.begin(), p.sourceFiles.end());
    loadFiles(all, "folder", p.name, p.includeDirs);
    m_projects.push_back(p);
    return true;
}

bool CodeBase::loadProject(const std::string& path, const std::string& solutionDir)
{
    ProjectInfo p;
    if (!ProjectReader::readProject(path, p, solutionDir)) {
        m_messages.push_back("error: cannot read project " + path);
        return false;
    }
    for (size_t i = 0; i < p.warnings.size(); ++i) m_messages.push_back("project " + p.name + ": " + p.warnings[i]);

    std::vector<std::string> inc = p.includeDirs;
    inc.insert(inc.begin(), dirName(p.path));

    std::vector<std::string> present;
    for (size_t i = 0; i < p.headerFiles.size(); ++i) {
        if (!fileExists(p.headerFiles[i])) { m_messages.push_back("project " + p.name + ": missing header " + p.headerFiles[i]); continue; }
        present.push_back(p.headerFiles[i]);
    }
    for (size_t i = 0; i < p.sourceFiles.size(); ++i) {
        if (!fileExists(p.sourceFiles[i])) { m_messages.push_back("project " + p.name + ": missing source " + p.sourceFiles[i]); continue; }
        present.push_back(p.sourceFiles[i]);
    }
    loadFiles(present, "project", p.name, inc);
    m_projects.push_back(p);
    return true;
}

namespace {

// reads, lexes and parses one file; touches nothing shared (runs on the worker pool)
SourceFile* parseOne(const std::string& absPath, std::string& error)
{
    std::string text;
    if (!readFile(absPath, text)) { error = "error: cannot read " + absPath; return 0; }
    SourceFile* f = new SourceFile;
    f->path = normalizePath(absPath);
    f->isHeader = isHeaderExtension(extension(absPath));
    std::vector<Token> tokens;
    Lexer lexer(text);
    lexer.run(tokens);
    f->lineCount = lexer.lineCount();
    f->tokenCount = static_cast<int>(tokens.size()) - 1;
    Parser parser(*f, tokens);
    parser.parse();
    f->tokens.swap(tokens);   // kept for the version 3 body parser
    return f;
}

struct ParseJob : public ParallelJob {
    std::vector<std::string> paths;
    std::vector<SourceFile*> results;
    std::vector<std::string> errors;
    virtual void run(size_t i) { results[i] = parseOne(paths[i], errors[i]); }
};

} // namespace

SourceFile* CodeBase::registerFile(SourceFile* f, const std::string& absPath, const std::string& origin, const std::string& project)
{
    std::string key = toLower(normalizePath(absPath));
    std::map<std::string, SourceFile*>::iterator it = m_byPath.find(key);
    if (it != m_byPath.end()) {
        // a file first seen through an #include may later turn out to be listed in the project
        if (origin == "project" && it->second->origin != "project") { it->second->origin = origin; it->second->project = project; }
        delete f;
        return it->second;
    }
    f->origin = origin;
    f->project = project;
    m_files.push_back(f);
    m_byPath[key] = f;
    return f;
}

void CodeBase::loadFiles(const std::vector<std::string>& absPaths, const std::string& origin, const std::string& project,
                         const std::vector<std::string>& includeDirs)
{
    long start = millisecondsNow();
    std::vector<std::string> round;
    std::vector<std::string> origins;
    for (size_t i = 0; i < absPaths.size(); ++i) {
        std::string key = toLower(normalizePath(absPaths[i]));
        if (m_byPath.count(key)) { registerFile(new SourceFile, absPaths[i], origin, project); continue; }   // already loaded: refresh its origin only
        bool dup = false;
        for (size_t k = 0; k < round.size(); ++k) if (toLower(normalizePath(round[k])) == key) { dup = true; break; }
        if (!dup) { round.push_back(absPaths[i]); origins.push_back(origin); }
    }
    // rounds: parse the current list in parallel, register in order, collect the includes
    // discovered by these files for the next round
    for (int depth = 0; depth < 64 && !round.empty(); ++depth) {
        ParseJob job;
        job.paths = round;
        job.results.assign(round.size(), static_cast<SourceFile*>(0));
        job.errors.assign(round.size(), std::string());
        runParallel(job, round.size(), m_threads);
        std::vector<std::string> next;
        std::vector<std::string> nextOrigins;
        for (size_t i = 0; i < round.size(); ++i) {
            if (!job.results[i]) { if (!job.errors[i].empty()) m_messages.push_back(job.errors[i]); continue; }
            SourceFile* f = registerFile(job.results[i], round[i], origins[i], project);
            std::vector<std::string> discovered;
            resolveIncludes(f, includeDirs, project, m_followIncludes ? &discovered : 0);
            for (size_t d = 0; d < discovered.size(); ++d) {
                std::string key = toLower(normalizePath(discovered[d]));
                if (m_byPath.count(key)) continue;
                bool dup = false;
                for (size_t k = 0; k < next.size(); ++k) if (toLower(normalizePath(next[k])) == key) { dup = true; break; }
                if (!dup) { next.push_back(discovered[d]); nextOrigins.push_back("included by " + baseName(f->path)); }
            }
        }
        round.swap(next);
        origins.swap(nextOrigins);
    }
    m_parseMs += millisecondsNow() - start;
}

SourceFile* CodeBase::loadFile(const std::string& absPath, const std::string& origin, const std::string& project,
                               const std::vector<std::string>& includeDirs)
{
    std::vector<std::string> one;
    one.push_back(absPath);
    loadFiles(one, origin, project, includeDirs);
    return fileByPath(absPath) ? const_cast<SourceFile*>(fileByPath(absPath)) : 0;
}

void CodeBase::resolveIncludes(SourceFile* f, const std::vector<std::string>& includeDirs, const std::string& project,
                               std::vector<std::string>* discovered)
{
    (void)project;
    std::string here = dirName(f->path);
    for (size_t i = 0; i < f->directives.size(); ++i) {
        Directive& d = f->directives[i];
        if (d.kind != Directive::Include || d.inactive) continue;
        if (d.argument.empty() || d.argument.find("$(") != std::string::npos) continue;

        std::string found;
        // the standard headers (<string>, <vector>, ...) are cppparse's own extensionless files in include/: this
        // front end does not read them, it assumes the std:: names as it always did
        if (d.systemInclude && d.argument.find('.') == std::string::npos) continue;
        if (!d.systemInclude) {
            std::string cand = joinPath(here, d.argument);
            if (fileExists(cand)) found = cand;
        }
        for (size_t k = 0; found.empty() && k < includeDirs.size(); ++k) {
            std::string cand = joinPath(includeDirs[k], d.argument);
            if (fileExists(cand)) found = cand;
        }
        if (found.empty()) continue;
        d.resolvedPath = normalizePath(found);
        if (discovered) discovered->push_back(d.resolvedPath);
    }
}

// ---- linking ------------------------------------------------------------------

void CodeBase::indexClasses()
{
    m_classes.clear();
    m_byQualified.clear();
    m_bySimple.clear();
    for (size_t i = 0; i < m_files.size(); ++i) {
        SourceFile* f = m_files[i];
        for (size_t c = 0; c < f->allClasses.size(); ++c) {
            ClassInfo* cls = f->allClasses[c];
            m_classes.push_back(cls);
            // prefer definitions over forward declarations in the index
            std::map<std::string, ClassInfo*>::iterator it = m_byQualified.find(cls->qualifiedName);
            if (it == m_byQualified.end() || (!it->second->isDefinition && cls->isDefinition))
                m_byQualified[cls->qualifiedName] = cls;
            m_bySimple[lastComponent(cls->name)].push_back(cls);
        }
    }
}

ClassInfo* CodeBase::findClass(const std::string& scope, const std::string& name) const
{
    std::string n = stripTemplateArgs(name);
    if (startsWith(n, "::")) n = n.substr(2);
    if (n.empty()) return 0;

    // try scope::name, then progressively shorter scopes
    std::string s = scope;
    for (;;) {
        std::string q = s.empty() ? n : s + "::" + n;
        std::map<std::string, ClassInfo*>::const_iterator it = m_byQualified.find(q);
        if (it != m_byQualified.end()) return it->second;
        if (s.empty()) break;
        size_t pos = s.rfind("::");
        s = (pos == std::string::npos) ? "" : s.substr(0, pos);
    }
    // fall back to the simple name
    std::map<std::string, std::vector<ClassInfo*> >::const_iterator it = m_bySimple.find(lastComponent(n));
    if (it == m_bySimple.end()) return 0;
    const std::vector<ClassInfo*>& v = it->second;
    ClassInfo* best = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!endsWith(v[i]->qualifiedName, n)) continue;
        if (!best || (!best->isDefinition && v[i]->isDefinition)) best = v[i];
    }
    return best;
}

void CodeBase::linkBases()
{
    for (size_t i = 0; i < m_classes.size(); ++i) {
        ClassInfo* cls = m_classes[i];
        std::string lookupScope = cls->owner ? cls->owner->qualifiedName : cls->scope;
        for (size_t b = 0; b < cls->bases.size(); ++b) {
            ClassInfo* base = findClass(lookupScope, cls->bases[b].name);
            if (base && base != cls) {
                cls->bases[b].resolved = base;
                base->derived.push_back(cls);
            }
        }
    }
}

namespace {

std::string normalizedTypes(const std::vector<Parameter>& params)
{
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) {
        std::string t;
        for (size_t k = 0; k < params[i].type.size(); ++k)
            if (!isspace(static_cast<unsigned char>(params[i].type[k]))) t += params[i].type[k];
        out += t + ";";
    }
    return out;
}

std::string locationOf(const CodeBase& cb, const FunctionInfo& fn)
{
    return cb.displayPath(fn.file) + ":" + intToString(fn.line);
}

} // namespace

void CodeBase::linkFunctions()
{
    // inline definitions are their own definition
    for (size_t i = 0; i < m_classes.size(); ++i)
        for (size_t m = 0; m < m_classes[i]->methods.size(); ++m) {
            FunctionInfo& fn = m_classes[i]->methods[m];
            fn.ownerClass = m_classes[i];
            if (fn.hasBody) fn.definition = &fn;
        }

    for (size_t i = 0; i < m_files.size(); ++i) {
        SourceFile* f = m_files[i];
        for (size_t k = 0; k < f->functions.size(); ++k) {
            FunctionInfo& fn = f->functions[k];
            if (fn.hasBody) fn.definition = &fn;
            if (fn.qualifier.empty()) continue;

            ClassInfo* cls = findClass(fn.scope, fn.qualifier);
            if (!cls) continue;   // may be a namespace-qualified free function, handled below
            fn.ownerClass = cls;

            std::string want = normalizedTypes(fn.params);
            FunctionInfo* exact = 0;
            FunctionInfo* byCount = 0;
            int countMatches = 0;
            for (size_t m = 0; m < cls->methods.size(); ++m) {
                FunctionInfo& cand = cls->methods[m];
                if (cand.name != fn.name) continue;
                if (cand.isConst != fn.isConst) continue;
                if (cand.params.size() == fn.params.size()) {
                    ++countMatches;
                    byCount = &cand;
                    if (normalizedTypes(cand.params) == want) { exact = &cand; break; }
                }
            }
            FunctionInfo* decl = exact ? exact : (countMatches == 1 ? byCount : 0);
            if (!decl) {
                fn.linkNote = "no matching declaration of " + fn.name + "(" + fn.paramText + ") in " + cls->qualifiedName;
                continue;
            }
            if (fn.hasBody) {
                if (decl->definition && decl->definition != decl && decl->definition != &fn)
                    fn.linkNote = "duplicate definition; first one at " + locationOf(*this, *decl->definition);
                decl->definition = &fn;
                decl->definedAt = locationOf(*this, fn);
            }
            fn.declaredAt = locationOf(*this, *decl);
            fn.access = decl->access;
            if (decl->isVirtual) fn.isVirtual = true;
            if (decl->isStatic) fn.isStatic = true;
            if (decl->isConstructor) fn.isConstructor = true;
            if (decl->isDestructor) fn.isDestructor = true;
        }
    }

    // free functions: declaration <-> definition by full name and parameter count.
    // "void ns::f() {...}" (qualifier = namespace) matches "void f();" inside namespace ns.
    for (size_t i = 0; i < m_files.size(); ++i) {
        std::vector<FunctionInfo>& fns = m_files[i]->functions;
        for (size_t k = 0; k < fns.size(); ++k) {
            FunctionInfo& decl = fns[k];
            if (decl.hasBody || !decl.qualifier.empty()) continue;
            for (size_t j = 0; j < m_files.size() && !decl.definition; ++j) {
                std::vector<FunctionInfo>& others = m_files[j]->functions;
                for (size_t o = 0; o < others.size(); ++o) {
                    FunctionInfo& def = others[o];
                    if (!def.hasBody || def.ownerClass) continue;
                    if (def.fullName() != decl.fullName() || def.params.size() != decl.params.size()) continue;
                    decl.definition = &def;
                    decl.definedAt = locationOf(*this, def);
                    if (def.declaredAt.empty()) def.declaredAt = locationOf(*this, decl);
                    break;
                }
            }
        }
    }
    // qualified definitions that matched neither a class nor a namespace declaration
    for (size_t i = 0; i < m_files.size(); ++i) {
        std::vector<FunctionInfo>& fns = m_files[i]->functions;
        for (size_t k = 0; k < fns.size(); ++k) {
            FunctionInfo& fn = fns[k];
            if (fn.qualifier.empty() || fn.ownerClass || !fn.declaredAt.empty()) continue;
            fn.linkNote = "no class or namespace declaration of " + fn.qualifier + "::" + fn.name + " found in the parsed files";
        }
    }
}

void CodeBase::linkVariables()
{
    for (size_t i = 0; i < m_files.size(); ++i) {
        std::vector<VariableInfo>& vars = m_files[i]->variables;
        for (size_t k = 0; k < vars.size(); ++k) {
            VariableInfo& v = vars[k];
            if (v.qualifier.empty()) continue;
            ClassInfo* cls = findClass(v.scope, v.qualifier);
            if (!cls) continue;
            for (size_t f = 0; f < cls->fields.size(); ++f) {
                if (cls->fields[f].name == v.name) {
                    cls->fields[f].definedAt = displayPath(v.file) + ":" + intToString(v.line);
                    v.definedAt = displayPath(cls->file) + ":" + intToString(cls->fields[f].line);
                    break;
                }
            }
        }
    }
}

void CodeBase::collectTypeNames(const std::string& type, std::vector<std::string>& out) const
{
    // identifiers and "A::B" chains inside a type string
    std::string cur;
    for (size_t i = 0; i <= type.size(); ++i) {
        char c = i < type.size() ? type[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') { cur += c; continue; }
        if (!cur.empty()) {
            while (!cur.empty() && cur[cur.size() - 1] == ':') cur.erase(cur.size() - 1);
            if (!cur.empty() && !isdigit(static_cast<unsigned char>(cur[0]))) out.push_back(cur);
            cur.clear();
        }
    }
}

void CodeBase::linkAssociations()
{
    for (size_t i = 0; i < m_classes.size(); ++i) {
        ClassInfo* cls = m_classes[i];
        std::string lookupScope = cls->owner ? cls->owner->qualifiedName : cls->scope;
        for (size_t f = 0; f < cls->fields.size(); ++f) {
            std::vector<std::string> names;
            collectTypeNames(cls->fields[f].type, names);
            for (size_t n = 0; n < names.size(); ++n) {
                ClassInfo* other = findClass(lookupScope, names[n]);
                if (!other || other == cls) continue;
                bool have = false;
                for (size_t a = 0; a < cls->associations.size(); ++a) if (cls->associations[a] == other) { have = true; break; }
                if (!have) cls->associations.push_back(other);
            }
        }
    }
}

void CodeBase::link()
{
    indexClasses();
    linkBases();
    linkFunctions();
    linkVariables();
    linkAssociations();
}

void CodeBase::checkHealth()
{
    m_findings.clear();
    runHealthChecks(*this, m_findings);
}

namespace {
struct FindingLess {
    bool operator()(const Finding& a, const Finding& b) const
    {
        if (a.severity != b.severity) return a.severity < b.severity;
        if (a.file != b.file) return a.file < b.file;
        if (a.line != b.line) return a.line < b.line;
        return a.code < b.code;
    }
};
}

void CodeBase::addFindings(const std::vector<Finding>& more)
{
    m_findings.insert(m_findings.end(), more.begin(), more.end());
    std::stable_sort(m_findings.begin(), m_findings.end(), FindingLess());
}

const SourceFile* CodeBase::fileByPath(const std::string& path) const
{
    std::map<std::string, SourceFile*>::const_iterator it = m_byPath.find(toLower(normalizePath(path)));
    return it == m_byPath.end() ? 0 : it->second;
}

} // namespace cppparse
