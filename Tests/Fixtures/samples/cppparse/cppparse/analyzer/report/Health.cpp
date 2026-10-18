// Health.cpp - version 2 health checker.
#include "Health.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace cppparse {

const char* severityName(Finding::Severity s)
{
    switch (s) {
    case Finding::Error:   return "error";
    case Finding::Warning: return "warning";
    default:               return "info";
    }
}

void countFindings(const std::vector<Finding>& f, int& errors, int& warnings, int& infos)
{
    errors = warnings = infos = 0;
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i].severity == Finding::Error) ++errors;
        else if (f[i].severity == Finding::Warning) ++warnings;
        else ++infos;
    }
}

namespace {

// ---- small helpers ------------------------------------------------------------

std::string noSpaces(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) if (!isspace(static_cast<unsigned char>(s[i]))) out += s[i];
    return out;
}

std::string normParams(const std::vector<Parameter>& params)
{
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) out += noSpaces(params[i].type) + ";";
    return out;
}

std::string paramList(const FunctionInfo& fn)
{
    return fn.name + "(" + fn.paramText + ")" + (fn.isConst ? " const" : "");
}

bool isTypeWord(const std::string& s)
{
    return s == "int" || s == "char" || s == "short" || s == "long" || s == "unsigned" || s == "signed" ||
           s == "float" || s == "double" || s == "void" || s == "bool" || s == "wchar_t" || s == "size_t" ||
           s == "__int64" || s == "ptrdiff_t";
}

bool isBuiltinType(const std::string& fullType)
{
    std::string type = stripTemplateArgs(fullType);   // "std::vector<Foo*>" is not a pointer
    if (type.find('*') != std::string::npos) return true;
    std::string word;
    for (size_t i = 0; i <= type.size(); ++i) {
        char c = i < type.size() ? type[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_') { word += c; continue; }
        if (!word.empty() && isTypeWord(word)) return true;
        word.clear();
    }
    return false;
}

// "const geom::Shape*&" -> "geom::Shape"; "std::vector<int>" -> "std::vector"
std::string baseTypeName(const std::string& type)
{
    std::string t = stripTemplateArgs(type);
    std::string out, word;
    std::vector<std::string> words;
    for (size_t i = 0; i <= t.size(); ++i) {
        char c = i < t.size() ? t[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') { word += c; continue; }
        if (!word.empty()) words.push_back(word);
        word.clear();
        if (c == '[') break;
    }
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w == "const" || w == "volatile" || w == "struct" || w == "class" || w == "union" || w == "typename" ||
            w == "static" || w == "mutable" || w == "register" || w == "enum")
            continue;
        if (isTypeWord(w)) return "";
        out = w;   // last remaining word is the type name
        break;
    }
    if (!out.empty() && out.compare(0, 2, "::") == 0) out = out.substr(2);
    return out;
}

bool isMacroLikeWord(const std::string& w)
{
    bool letter = false;
    for (size_t i = 0; i < w.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(w[i]);
        if (islower(c)) return false;
        if (isupper(c)) letter = true;
    }
    return letter && w.size() > 1;
}

// simplified spelling of a type for declaration/definition comparison:
// no spaces, no specifiers, no "Scope::" prefixes, no attribute macros
std::string simplifyType(const std::string& type)
{
    // tokens: words and punctuation; a word directly followed by "::" is a qualifier and is dropped
    std::vector<std::string> toks;
    std::string word;
    for (size_t i = 0; i <= type.size(); ++i) {
        char c = i < type.size() ? type[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_') { word += c; continue; }
        if (!word.empty()) { toks.push_back(word); word.clear(); }
        if (c == ':' && i + 1 < type.size() && type[i + 1] == ':') { toks.push_back("::"); ++i; continue; }
        if (!isspace(static_cast<unsigned char>(c)) && i < type.size()) toks.push_back(std::string(1, c));
    }
    std::string out;
    for (size_t i = 0; i < toks.size(); ++i) {
        const std::string& t = toks[i];
        if (t == "::") continue;
        if (i + 1 < toks.size() && toks[i + 1] == "::") continue;   // qualifier
        if (t == "virtual" || t == "static" || t == "inline" || t == "explicit" || t == "typename" || t == "extern" || isMacroLikeWord(t)) continue;
        out += t;
    }
    return out;
}

bool acceptsArgs(const FunctionInfo& fn, int n)
{
    int required = 0;
    bool variadic = false;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].type == "...") { variadic = true; continue; }
        if (fn.params[i].defaultValue.empty()) ++required;
    }
    if (variadic) return n >= required;
    return n >= required && n <= static_cast<int>(fn.params.size());
}

std::string argWord(int n) { return intToString(n) + (n == 1 ? " argument" : " arguments"); }

bool isStdName(const std::string& n)
{
    return isKnownLibraryName(n);
}

bool inAnonymousNamespace(const std::string& scope)
{
    return scope.find("(anonymous)") != std::string::npos;
}

// ---- context ---------------------------------------------------------------------

struct Ctx {
    const CodeBase&       cb;
    std::vector<Finding>& out;
    bool                  hasSources;
    std::set<std::string> macroNames;
    std::set<std::string> typedefNames;
    std::set<std::string> enumTypeNames;

    Ctx(const CodeBase& c, std::vector<Finding>& o) : cb(c), out(o), hasSources(false) {}

    void add(Finding::Severity s, const char* code, const std::string& file, int line,
             const std::string& msg, const std::string& detail = "")
    {
        Finding f;
        f.severity = s;
        f.code = code;
        f.file = file;
        f.line = line;
        f.message = msg;
        f.detail = detail;
        out.push_back(f);
    }

    std::string loc(const std::string& file, int line) const { return cb.displayPath(file) + ":" + intToString(line); }

    const ClassInfo* classOf(const std::string& scope, const std::string& type) const
    {
        std::string n = baseTypeName(type);
        if (n.empty()) return 0;
        return cb.findClass(scope, n);
    }
};

// ---- member lookup ---------------------------------------------------------------

enum Lookup { Found, NotFound, Unknown };

Lookup findMember(const ClassInfo* cls, const std::string& name, std::vector<const FunctionInfo*>& methods,
                  bool& isField, int depth)
{
    if (!cls || depth > 32) return Unknown;
    for (size_t i = 0; i < cls->methods.size(); ++i) if (cls->methods[i].name == name) methods.push_back(&cls->methods[i]);
    for (size_t i = 0; i < cls->fields.size(); ++i) if (cls->fields[i].name == name) isField = true;
    for (size_t i = 0; i < cls->typedefs.size(); ++i) if (cls->typedefs[i].name == name) isField = true;
    for (size_t i = 0; i < cls->enums.size(); ++i)
        for (size_t k = 0; k < cls->enums[i].enumerators.size(); ++k)
            if (cls->enums[i].enumerators[k].substr(0, name.size()) == name) isField = true;
    for (size_t i = 0; i < cls->nested.size(); ++i) if (cls->nested[i]->name == name) isField = true;
    if (!methods.empty() || isField) return Found;
    bool unknown = false;
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        const ClassInfo* base = cls->bases[b].resolved;
        if (!base || !base->isDefinition) { unknown = true; continue; }
        Lookup r = findMember(base, name, methods, isField, depth + 1);
        if (r == Found) return Found;
        if (r == Unknown) unknown = true;
    }
    return unknown ? Unknown : NotFound;
}

void collectFields(const ClassInfo* cls, std::vector<const VariableInfo*>& out, int depth)
{
    if (!cls || depth > 32) return;
    for (size_t i = 0; i < cls->fields.size(); ++i) out.push_back(&cls->fields[i]);
    for (size_t b = 0; b < cls->bases.size(); ++b) collectFields(cls->bases[b].resolved, out, depth + 1);
}

void collectVirtuals(const ClassInfo* cls, std::vector<const FunctionInfo*>& out, int depth)
{
    if (!cls || depth > 32) return;
    for (size_t i = 0; i < cls->methods.size(); ++i)
        if (cls->methods[i].isVirtual || cls->methods[i].isPureVirtual) out.push_back(&cls->methods[i]);
    for (size_t b = 0; b < cls->bases.size(); ++b) collectVirtuals(cls->bases[b].resolved, out, depth + 1);
}

bool hasUnresolvedBase(const ClassInfo* cls, int depth)
{
    if (!cls || depth > 32) return false;
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        if (!cls->bases[b].resolved || !cls->bases[b].resolved->isDefinition) return true;
        if (hasUnresolvedBase(cls->bases[b].resolved, depth + 1)) return true;
    }
    return false;
}

// ---- checks: preprocessor and files --------------------------------------------

void checkFiles(Ctx& c)
{
    const std::vector<SourceFile*>& files = c.cb.files();
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        if (!sf.isHeader) c.hasSources = true;
        for (size_t d = 0; d < sf.directives.size(); ++d) {
            const Directive& dir = sf.directives[d];
            if (dir.inactive) continue;
            if (dir.kind == Directive::Define) c.macroNames.insert(dir.argument);
            if (dir.kind == Directive::Include && !dir.systemInclude && dir.resolvedPath.empty() && dir.argument.find("$(") == std::string::npos)
                c.add(Finding::Error, "E100", sf.path, dir.line, "include file not found: #include \"" + dir.argument + "\"",
                      "searched next to the including file and in the project include directories");
        }
        if (sf.isHeader && sf.includeGuard.empty() && !sf.pragmaOnce)
            c.add(Finding::Warning, "W102", sf.path, 1, "header has no include guard and no #pragma once",
                  "including it twice in one translation unit will redefine its contents");
        for (size_t i = 0; i < sf.diagnostics.size(); ++i) {
            const std::string& d = sf.diagnostics[i];
            int line = 0;
            if (d.size() > 1 && d[0] == 'L') line = atoi(d.c_str() + 1);
            std::string text = d.substr(d.find(':') + 2);
            if (text.find("#if") != std::string::npos || text.find("#else") != std::string::npos || text.find("#elif") != std::string::npos || text.find("#endif") != std::string::npos)
                c.add(Finding::Error, "E101", sf.path, line, "preprocessor conditionals are unbalanced: " + text);
            else if (startsWith(text, "missing ';'"))
                c.add(Finding::Error, "E110", sf.path, line, text);
            else if (startsWith(text, "macro invocation"))
                c.add(Finding::Info, "I112", sf.path, line, text, "not expanded; if this is not a macro the line is a syntax error");
            else if (startsWith(text, "unterminated"))
                c.add(Finding::Error, "E113", sf.path, line, text + " (missing '}')");
            else if (startsWith(text, "explicit instantiation"))
                continue;
            else
                c.add(Finding::Warning, "W111", sf.path, line, "possible syntax error: " + text);
        }
        for (size_t i = 0; i < sf.typedefs.size(); ++i) c.typedefNames.insert(sf.typedefs[i].name);
        for (size_t i = 0; i < sf.enums.size(); ++i) c.enumTypeNames.insert(sf.enums[i].name);
        for (size_t k = 0; k < sf.allClasses.size(); ++k)
            for (size_t i = 0; i < sf.allClasses[k]->typedefs.size(); ++i) c.typedefNames.insert(sf.allClasses[k]->typedefs[i].name);
    }

    // circular includes
    std::map<std::string, std::vector<std::string> > graph;
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        std::string key = toLower(sf.path);
        for (size_t d = 0; d < sf.directives.size(); ++d)
            if (sf.directives[d].kind == Directive::Include && !sf.directives[d].resolvedPath.empty() && !sf.directives[d].inactive)
                graph[key].push_back(toLower(sf.directives[d].resolvedPath));
    }
    std::set<std::string> reported;
    for (size_t f = 0; f < files.size(); ++f) {
        std::string start = toLower(files[f]->path);
        std::vector<std::string> stack;
        std::vector<size_t> idx;
        std::set<std::string> visited;
        stack.push_back(start); idx.push_back(0); visited.insert(start);
        while (!stack.empty()) {
            std::vector<std::string>& edges = graph[stack.back()];
            if (idx.back() >= edges.size()) { stack.pop_back(); idx.pop_back(); continue; }
            std::string next = edges[idx.back()++];
            if (next == start) {
                std::vector<std::string> cyc(stack);
                std::vector<std::string> sorted(cyc);
                std::sort(sorted.begin(), sorted.end());
                std::string key;
                for (size_t i = 0; i < sorted.size(); ++i) key += sorted[i] + "|";
                if (reported.insert(key).second) {
                    std::string chain;
                    for (size_t i = 0; i < cyc.size(); ++i) chain += c.cb.displayPath(cyc[i]) + " -> ";
                    chain += c.cb.displayPath(start);
                    c.add(Finding::Info, "I103", files[f]->path, 1, "circular include: " + chain,
                          "harmless with include guards, but usually a sign of tangled dependencies");
                }
                continue;
            }
            if (visited.insert(next).second) { stack.push_back(next); idx.push_back(0); }
        }
    }
}

// ---- checks: classes -----------------------------------------------------------------

void checkClasses(Ctx& c)
{
    const std::vector<ClassInfo*>& classes = c.cb.classes();

    // E120 duplicate class definitions
    std::map<std::string, const ClassInfo*> seen;
    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo& cls = *classes[i];
        if (!cls.isDefinition) continue;
        std::string key = cls.qualifiedName + cls.tmpl.specializationArgs;
        std::map<std::string, const ClassInfo*>::iterator it = seen.find(key);
        if (it == seen.end()) { seen[key] = &cls; continue; }
        bool conditional = !cls.condition.empty() || !it->second->condition.empty();
        c.add(conditional ? Finding::Warning : Finding::Error, "E120", cls.file, cls.line,
              std::string(ClassInfo::kindName(cls.kind)) + " " + cls.qualifiedName + " is defined more than once",
              "first definition at " + c.loc(it->second->file, it->second->line) + (conditional ? "; the definitions are under different #if conditions" : ""));
    }

    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo& cls = *classes[i];
        if (!cls.isDefinition) continue;
        std::string lookupScope = cls.owner ? cls.owner->qualifiedName : cls.scope;

        // E121 duplicate / self base, W122 unresolved base
        for (size_t b = 0; b < cls.bases.size(); ++b) {
            const BaseClass& base = cls.bases[b];
            std::string bn = stripTemplateArgs(base.name);
            for (size_t k = 0; k < b; ++k)
                if (stripTemplateArgs(cls.bases[k].name) == bn)
                    c.add(Finding::Error, "E121", cls.file, cls.line, cls.qualifiedName + " lists base class " + bn + " twice");
            if (base.resolved == &cls || lastComponent(bn) == cls.name)
                c.add(Finding::Error, "E121", cls.file, cls.line, cls.qualifiedName + " derives from itself");
            else if (!base.resolved)
                c.add(Finding::Warning, "W122", cls.file, cls.line, "base class " + base.name + " of " + cls.qualifiedName + " is not defined in the parsed files",
                      "members inherited from it cannot be checked; add its header to the input if it belongs to the project");
            else if (!base.resolved->isDefinition)
                c.add(Finding::Error, "E124", cls.file, cls.line, "base class " + base.name + " of " + cls.qualifiedName + " is only forward-declared",
                      "a base class must be a complete type; the definition was not found in the parsed files");
        }

        // E123 duplicate members
        std::set<std::string> methodKeys, fieldKeys;
        for (size_t m = 0; m < cls.methods.size(); ++m) {
            const FunctionInfo& fn = cls.methods[m];
            std::string key = fn.name + "(" + normParams(fn.params) + ")" + (fn.isConst ? "c" : "");
            if (!methodKeys.insert(key).second)
                c.add(Finding::Error, "E123", fn.file, fn.line, cls.qualifiedName + "::" + paramList(fn) + " is declared twice in the class");
        }
        for (size_t f = 0; f < cls.fields.size(); ++f) {
            const VariableInfo& v = cls.fields[f];
            if (!fieldKeys.insert(v.name).second)
                c.add(Finding::Error, "E123", v.file, v.line, "member " + cls.qualifiedName + "::" + v.name + " is declared twice in the class");
            // E124 incomplete field type
            if (v.type.find('*') == std::string::npos && v.type.find('&') == std::string::npos && !v.isStatic) {
                const ClassInfo* ft = c.classOf(lookupScope, v.type);
                if (ft && !ft->isDefinition)
                    c.add(Finding::Error, "E124", v.file, v.line, "member " + cls.qualifiedName + "::" + v.name + " has incomplete type " + ft->qualifiedName,
                          "the class is only forward-declared in the parsed files; a member by value needs the full definition (or use a pointer/reference)");
            }
            // W126 static member never defined
            if (v.isStatic && v.definedAt.empty() && !(v.isConst && !v.initializer.empty()) && c.hasSources)
                c.add(Finding::Warning, "W126", v.file, v.line, "static member " + cls.qualifiedName + "::" + v.name + " is declared but never defined",
                      "add \"" + v.type + " " + cls.qualifiedName + "::" + v.name + ";\" to one .cpp file, or the linker reports an unresolved external");
        }

        // W125 polymorphic class without virtual destructor
        bool hasVirtual = false;
        const FunctionInfo* dtor = 0;
        for (size_t m = 0; m < cls.methods.size(); ++m) {
            if (cls.methods[m].isVirtual || cls.methods[m].isPureVirtual) hasVirtual = true;
            if (cls.methods[m].isDestructor) dtor = &cls.methods[m];
        }
        if (hasVirtual && (!dtor || !dtor->isVirtual)) {
            std::vector<const FunctionInfo*> inherited;
            bool inheritedVirtualDtor = false;
            collectVirtuals(&cls, inherited, 0);
            for (size_t k = 0; k < inherited.size(); ++k) if (inherited[k]->isDestructor) inheritedVirtualDtor = true;
            if (!inheritedVirtualDtor)
                c.add(cls.derived.empty() ? Finding::Info : Finding::Warning, "W125", cls.file, dtor ? dtor->line : cls.line,
                      cls.qualifiedName + " has virtual methods but " + (dtor ? "a non-virtual" : "no virtual") + " destructor",
                      cls.derived.empty() ? "deleting a derived object through a pointer to this class would be undefined behaviour"
                                          : "derived classes exist: deleting through a base pointer skips their destructors");
        }

        // W128 hides base virtual; I127 abstract through inheritance
        if (!cls.bases.empty()) {
            std::vector<const FunctionInfo*> baseVirtuals;
            for (size_t b = 0; b < cls.bases.size(); ++b) collectVirtuals(cls.bases[b].resolved, baseVirtuals, 0);
            for (size_t m = 0; m < cls.methods.size(); ++m) {
                const FunctionInfo& fn = cls.methods[m];
                if (fn.isConstructor || fn.isDestructor || fn.isOperator || fn.isStatic) continue;
                const FunctionInfo* sameName = 0;
                bool overrides = false;
                for (size_t k = 0; k < baseVirtuals.size(); ++k) {
                    if (baseVirtuals[k]->name != fn.name || baseVirtuals[k]->isDestructor) continue;
                    sameName = baseVirtuals[k];
                    if (normParams(baseVirtuals[k]->params) == normParams(fn.params) && baseVirtuals[k]->isConst == fn.isConst) { overrides = true; break; }
                }
                if (sameName && !overrides)
                    c.add(Finding::Warning, "W128", fn.file, fn.line, cls.qualifiedName + "::" + paramList(fn) + " hides the virtual " +
                          sameName->ownerClass->qualifiedName + "::" + paramList(*sameName) + " instead of overriding it",
                          "the parameter list or const-ness differs; calls through a base pointer will not reach this method");
            }
            if (!cls.isAbstract && !hasUnresolvedBase(&cls, 0)) {
                std::vector<std::string> missing;
                for (size_t k = 0; k < baseVirtuals.size(); ++k) {
                    const FunctionInfo* pv = baseVirtuals[k];
                    if (!pv->isPureVirtual) continue;
                    std::string key = pv->name + "(" + normParams(pv->params) + ")" + (pv->isConst ? "c" : "");
                    bool implemented = false;
                    for (size_t m = 0; m < cls.methods.size() && !implemented; ++m) {
                        const FunctionInfo& fn = cls.methods[m];
                        if (!fn.isPureVirtual && fn.name + "(" + normParams(fn.params) + ")" + (fn.isConst ? "c" : "") == key) implemented = true;
                    }
                    for (size_t o = 0; o < baseVirtuals.size() && !implemented; ++o) {
                        const FunctionInfo* other = baseVirtuals[o];
                        if (other == pv || other->isPureVirtual) continue;
                        if (other->name + "(" + normParams(other->params) + ")" + (other->isConst ? "c" : "") == key &&
                            other->ownerClass != pv->ownerClass) implemented = true;
                    }
                    if (!implemented) addUnique(missing, pv->ownerClass->qualifiedName + "::" + paramList(*pv));
                }
                if (!missing.empty()) {
                    std::string list;
                    for (size_t k = 0; k < missing.size(); ++k) { if (k) list += ", "; list += missing[k]; }
                    c.add(Finding::Info, "I127", cls.file, cls.line, cls.qualifiedName + " is abstract: it does not implement " + list,
                          "objects of this class cannot be created until every pure virtual method is implemented");
                }
            }
        }

        // W132 declared but never defined
        for (size_t m = 0; m < cls.methods.size(); ++m) {
            const FunctionInfo& fn = cls.methods[m];
            if (fn.definition || fn.isPureVirtual || fn.isFriend || fn.linkNote == "= default" || fn.linkNote == "= delete") continue;
            if (fn.access == AccessPrivate && (fn.isConstructor || fn.name == "operator=")) continue; // non-copyable idiom
            c.add(c.hasSources ? Finding::Warning : Finding::Info, "W132", fn.file, fn.line,
                  cls.qualifiedName + "::" + paramList(fn) + " is declared but no definition was found",
                  c.hasSources ? "calling it gives an unresolved external at link time" : "no .cpp files were given; the definition may live elsewhere");
        }

        // E162 duplicate enumerators inside the class
        std::set<std::string> enumerators;
        for (size_t e = 0; e < cls.enums.size(); ++e)
            for (size_t k = 0; k < cls.enums[e].enumerators.size(); ++k) {
                std::string n = cls.enums[e].enumerators[k];
                n = trim(n.substr(0, n.find('=')));
                if (!enumerators.insert(n).second)
                    c.add(Finding::Error, "E162", cls.file, cls.enums[e].line, "enumerator " + n + " is defined twice in " + cls.qualifiedName);
            }
    }
}

// ---- checks: functions (declarations vs definitions) --------------------------------

void checkDeclDef(Ctx& c, const FunctionInfo& decl)
{
    const FunctionInfo* def = decl.definition;
    if (!def || def == &decl) return;
    if (!decl.tmpl.isTemplate && !def->tmpl.isTemplate && !decl.returnType.empty() && !def->returnType.empty() &&
        simplifyType(decl.returnType) != simplifyType(def->returnType))
        c.add(Finding::Error, "E134", def->file, def->line, "definition of " + def->fullName() + "(" + def->paramText + ") returns " + def->returnType +
              " but the declaration returns " + decl.returnType, "declared at " + c.loc(decl.file, decl.line));
    for (size_t i = 0; i < decl.params.size() && i < def->params.size(); ++i)
        if (!decl.params[i].defaultValue.empty() && !def->params[i].defaultValue.empty())
            c.add(Finding::Error, "E135", def->file, def->line, "default argument for parameter " + (def->params[i].name.empty() ? intToString(static_cast<int>(i + 1)) : def->params[i].name) +
                  " of " + def->fullName() + " is repeated in the definition", "a default argument may be given once, normally in the declaration at " + c.loc(decl.file, decl.line));
}

void checkConstructor(Ctx& c, const FunctionInfo& fn)
{
    const ClassInfo* cls = fn.ownerClass;
    if (!cls || !fn.isConstructor || !fn.hasBody) return;

    // split the initializer list
    std::vector<std::string> names;
    if (fn.hasInitializerList) {
        std::string cur;
        int depth = 0;
        for (size_t i = 0; i <= fn.initializerList.size(); ++i) {
            char ch = i < fn.initializerList.size() ? fn.initializerList[i] : ',';
            if (ch == '(' || ch == '<' || ch == '[') ++depth;
            else if (ch == ')' || ch == '>' || ch == ']') --depth;
            if (ch == ',' && depth == 0) {
                std::string entry = trim(cur);
                size_t p = entry.find('(');
                names.push_back(stripTemplateArgs(trim(entry.substr(0, p))));
                cur.clear();
                continue;
            }
            cur += ch;
        }
    }

    // declaration order: bases, then fields
    std::vector<std::string> order;
    for (size_t b = 0; b < cls->bases.size(); ++b) order.push_back(stripTemplateArgs(cls->bases[b].name));
    for (size_t f = 0; f < cls->fields.size(); ++f) if (!cls->fields[f].isStatic) order.push_back(cls->fields[f].name);

    int lastIndex = -1;
    bool misordered = false;
    for (size_t n = 0; n < names.size(); ++n) {
        if (names[n].empty()) continue;
        int index = -1;
        for (size_t o = 0; o < order.size(); ++o)
            if (order[o] == names[n] || lastComponent(order[o]) == lastComponent(names[n])) { index = static_cast<int>(o); break; }
        if (index < 0) {
            bool unresolvedBase = hasUnresolvedBase(cls, 0) || cls->tmpl.isTemplate;
            c.add(unresolvedBase ? Finding::Warning : Finding::Error, "E136", fn.file, fn.line,
                  "constructor initializer \"" + names[n] + "\" names neither a member nor a base of " + cls->qualifiedName);
            continue;
        }
        if (index < lastIndex) misordered = true;
        if (index > lastIndex) lastIndex = index;
    }
    if (misordered) {
        std::string declared;
        for (size_t o = 0; o < order.size(); ++o) { if (o) declared += ", "; declared += lastComponent(order[o]); }
        c.add(Finding::Warning, "W137", fn.file, fn.line, "initializer list of " + cls->qualifiedName + "::" + fn.name + " is not in declaration order",
              "members are always initialised in declaration order (" + declared + "); reorder the list to avoid reading an uninitialised member");
    }

    // W138 builtin / pointer members not initialised anywhere in this constructor
    std::vector<std::string> missing;
    for (size_t f = 0; f < cls->fields.size(); ++f) {
        const VariableInfo& v = cls->fields[f];
        if (v.isStatic || !isBuiltinType(v.type) || v.type.find('[') != std::string::npos) continue;
        if (contains(names, v.name)) continue;
        if (contains(fn.bodyIdents, v.name)) continue;
        missing.push_back(v.name);
    }
    if (!missing.empty() && !(cls->kind == ClassInfo::Union)) {
        std::string list;
        for (size_t k = 0; k < missing.size(); ++k) { if (k) list += ", "; list += missing[k]; }
        c.add(Finding::Warning, "W138", fn.file, fn.line, "constructor " + cls->qualifiedName + "::" + fn.name + "(" + fn.paramText + ") leaves " + list + " uninitialised",
              "built-in and pointer members have indeterminate values unless the constructor sets them");
    }
}

std::string varTypeFor(const Ctx& c, const FunctionInfo& fn, const std::string& name, bool& found)
{
    found = true;
    // locals are recorded without block scope: the same name declared twice with
    // different types (pointer in one block, object in another) is ambiguous
    std::string localType;
    bool haveLocal = false, ambiguous = false;
    for (size_t i = 0; i < fn.locals.size(); ++i) {
        if (fn.locals[i].name != name) continue;
        if (haveLocal && noSpaces(fn.locals[i].type) != noSpaces(localType)) ambiguous = true;
        localType = fn.locals[i].type;
        haveLocal = true;
    }
    if (haveLocal) { if (ambiguous) { found = false; return ""; } return localType; }
    for (size_t i = 0; i < fn.params.size(); ++i) if (fn.params[i].name == name) return fn.params[i].type;
    if (fn.ownerClass) {
        std::vector<const VariableInfo*> fields;
        collectFields(fn.ownerClass, fields, 0);
        for (size_t i = 0; i < fields.size(); ++i) if (fields[i]->name == name) return fields[i]->type;
    }
    const std::vector<SourceFile*>& files = c.cb.files();
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v)
            if (files[f]->variables[v].name == name && files[f]->variables[v].qualifier.empty()) return files[f]->variables[v].type;
    found = false;
    return "";
}

void checkArgCount(Ctx& c, const FunctionInfo& fn, const CallInfo& call, const std::vector<const FunctionInfo*>& candidates, const std::string& what)
{
    if (candidates.empty()) return;
    bool templateInvolved = false;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (acceptsArgs(*candidates[i], call.argCount)) return;
        if (candidates[i]->tmpl.isTemplate || (candidates[i]->ownerClass && candidates[i]->ownerClass->tmpl.isTemplate)) templateInvolved = true;
    }
    std::string list;
    for (size_t i = 0; i < candidates.size() && i < 4; ++i) { if (i) list += "; "; list += paramList(*candidates[i]) + " at " + c.loc(candidates[i]->file, candidates[i]->line); }
    c.add(templateInvolved ? Finding::Warning : Finding::Error, "E142", fn.file, call.line,
          "call to " + what + " with " + argWord(call.argCount) + " matches no declaration", "candidates: " + list);
}

void checkCtorCall(Ctx& c, const FunctionInfo& fn, const CallInfo& call, const ClassInfo* cls)
{
    if (!cls || !cls->isDefinition || cls->tmpl.isTemplate || hasUnresolvedBase(cls, 0)) return;
    std::vector<const FunctionInfo*> ctors;
    for (size_t m = 0; m < cls->methods.size(); ++m) if (cls->methods[m].isConstructor) ctors.push_back(&cls->methods[m]);
    if (ctors.empty()) {
        if (call.argCount <= 1) return;   // implicit default / copy constructor
        c.add(Finding::Error, "E145", fn.file, call.line, cls->qualifiedName + " has no constructor taking " + argWord(call.argCount));
        return;
    }
    for (size_t i = 0; i < ctors.size(); ++i) if (acceptsArgs(*ctors[i], call.argCount)) return;
    if (cls->isAbstract) {
        c.add(Finding::Error, "E145", fn.file, call.line, "cannot create an object of abstract class " + cls->qualifiedName);
        return;
    }
    std::string list;
    for (size_t i = 0; i < ctors.size(); ++i) { if (i) list += "; "; list += paramList(*ctors[i]); }
    c.add(Finding::Error, "E145", fn.file, call.line,
          call.argCount == 0 ? cls->qualifiedName + " has no default constructor" : "no constructor of " + cls->qualifiedName + " takes " + argWord(call.argCount),
          "constructors: " + list);
}

void checkCalls(Ctx& c, const FunctionInfo& fn, std::map<std::string, std::pair<int, std::string> >& undeclared)
{
    if (fn.typeChecked) return;   // the version 3 type checker produced typed findings for this body
    std::string scope = fn.ownerClass ? fn.ownerClass->qualifiedName : fn.scope;
    const std::vector<SourceFile*>& files = c.cb.files();

    for (size_t k = 0; k < fn.calls.size(); ++k) {
        const CallInfo& call = fn.calls[k];
        const std::string& name = call.name;

        if (call.kind == CallInfo::New || call.kind == CallInfo::Construct) {
            const ClassInfo* cls = c.cb.findClass(scope, name);
            if (cls) checkCtorCall(c, fn, call, cls);
            continue;
        }

        if (call.kind == CallInfo::Member || call.kind == CallInfo::Pointer) {
            if (call.receiver.empty()) continue;
            const ClassInfo* cls = 0;
            bool pointer = false;
            if (call.receiver == "this") { cls = fn.ownerClass; pointer = true; }
            else {
                bool found = false;
                std::string type = varTypeFor(c, fn, call.receiver, found);
                if (!found) continue;
                pointer = type.find('*') != std::string::npos;
                cls = c.classOf(scope, type);
                if (!cls) continue;
                if (call.kind == CallInfo::Member && pointer)
                    c.add(Finding::Error, "E144", fn.file, call.line, call.receiver + " is a pointer: use " + call.receiver + "->" + name + "() instead of '.'");
                else if (call.kind == CallInfo::Pointer && !pointer && cls->isDefinition) {
                    bool smart = false;
                    for (size_t m = 0; m < cls->methods.size(); ++m) if (cls->methods[m].name == "operator->") smart = true;
                    if (!smart) c.add(Finding::Error, "E144", fn.file, call.line, call.receiver + " is not a pointer: use " + call.receiver + "." + name + "() instead of '->'");
                }
            }
            if (!cls || !cls->isDefinition) continue;
            std::vector<const FunctionInfo*> methods;
            bool isField = false;
            Lookup r = findMember(cls, name, methods, isField, 0);
            if (r == NotFound) c.add(Finding::Error, "E143", fn.file, call.line, cls->qualifiedName + " has no member named " + name,
                                     "receiver " + call.receiver + " has type " + cls->qualifiedName);
            else if (r == Found && !isField) checkArgCount(c, fn, call, methods, cls->qualifiedName + "::" + name);
            continue;
        }

        if (call.kind == CallInfo::Scoped) {
            if (call.qualifier.empty() || call.qualifier == "std") continue;
            const ClassInfo* cls = c.cb.findClass(scope, call.qualifier);
            if (cls && cls->isDefinition) {
                std::vector<const FunctionInfo*> methods;
                bool isField = false;
                Lookup r = findMember(cls, name, methods, isField, 0);
                if (r == NotFound) c.add(Finding::Error, "E143", fn.file, call.line, cls->qualifiedName + " has no member named " + name);
                else if (r == Found && !isField) checkArgCount(c, fn, call, methods, cls->qualifiedName + "::" + name);
                continue;
            }
            // namespace-qualified free function
            std::vector<const FunctionInfo*> cands;
            for (size_t f = 0; f < files.size(); ++f)
                for (size_t i = 0; i < files[f]->functions.size(); ++i) {
                    const FunctionInfo& g = files[f]->functions[i];
                    if (g.name == name && endsWith(g.fullName(), call.qualifier + "::" + name)) cands.push_back(&g);
                }
            if (!cands.empty()) checkArgCount(c, fn, call, cands, call.qualifier + "::" + name);
            continue;
        }

        // plain call: variable of function pointer type?
        bool isVar = false;
        varTypeFor(c, fn, name, isVar);
        if (isVar) continue;
        // member of the owner class (or its bases)
        if (fn.ownerClass) {
            std::vector<const FunctionInfo*> methods;
            bool isField = false;
            Lookup r = findMember(fn.ownerClass, name, methods, isField, 0);
            if (r == Found) { if (!isField) checkArgCount(c, fn, call, methods, fn.ownerClass->qualifiedName + "::" + name); continue; }
            if (r == Unknown) continue;
        }
        // free functions anywhere
        std::vector<const FunctionInfo*> cands;
        for (size_t f = 0; f < files.size(); ++f)
            for (size_t i = 0; i < files[f]->functions.size(); ++i) {
                const FunctionInfo& g = files[f]->functions[i];
                if (g.name == name && (g.ownerClass == 0)) cands.push_back(&g);
            }
        if (!cands.empty()) { checkArgCount(c, fn, call, cands, name); continue; }
        // constructor / functional cast
        const ClassInfo* cls = c.cb.findClass(scope, name);
        if (cls) { checkCtorCall(c, fn, call, cls); continue; }
        if (c.macroNames.count(name) || c.typedefNames.count(name) || c.enumTypeNames.count(name)) continue;
        if (contains(fn.localTypedefs, name)) continue;
        bool allCaps = true;
        for (size_t i = 0; i < name.size(); ++i) if (islower(static_cast<unsigned char>(name[i]))) { allCaps = false; break; }
        if (allCaps || isStdName(name) || name == "main") continue;
        if (name.size() > 1 && name[0] == '_' ) continue;   // _T(), __builtin, _countof ...
        std::map<std::string, std::pair<int, std::string> >::iterator it = undeclared.find(name);
        if (it == undeclared.end()) undeclared[name] = std::make_pair(1, c.loc(fn.file, call.line) + " in " + fn.fullName());
        else it->second.first++;
    }
}

void checkBody(Ctx& c, const FunctionInfo& fn)
{
    if (!fn.hasBody) return;
    const ClassInfo* owner = fn.ownerClass;
    bool emptyBody = fn.bodyIdents.empty() && fn.locals.empty() && fn.calls.empty() && fn.returnsValue == 0 && fn.returnsVoid == 0;
    std::string rt = noSpaces(fn.returnType);

    // W140 / W141 returns
    if (!fn.isConstructor && !fn.isDestructor && !fn.isConversion && !rt.empty() && rt != "void" && fn.name != "main" &&
        fn.returnsValue == 0 && fn.exceptions.throwStatements == 0)
        c.add(Finding::Warning, "W140", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ") returns " + fn.returnType + " but has no \"return <value>;\"",
              "falling off the end of a non-void function is undefined behaviour");
    if (rt == "void" && fn.returnsValue > 0)
        c.add(Finding::Warning, "W141", fn.file, fn.line, fn.fullName() + " is void but returns a value");
    if (fn.isDestructor && fn.exceptions.throwStatements > 0)
        c.add(Finding::Warning, "W147", fn.file, fn.line, "destructor " + fn.fullName() + " throws; an exception leaving a destructor during unwinding terminates the program");

    // W150 unused parameters
    if (!emptyBody && fn.name != "main") {
        for (size_t i = 0; i < fn.params.size(); ++i) {
            const Parameter& p = fn.params[i];
            if (p.name.empty() || contains(fn.bodyIdents, p.name)) continue;
            if (fn.hasInitializerList && fn.initializerList.find(p.name) != std::string::npos) continue;
            c.add(fn.isVirtual ? Finding::Info : Finding::Warning, "W150", fn.file, fn.line, "parameter " + p.name + " of " + fn.fullName() + " is never used",
                  fn.isVirtual ? "common for virtual overrides; remove the name or cast it to void to document the intent" : "");
        }
    }
    // W151 unused locals, W152 shadowing
    std::vector<const VariableInfo*> fields;
    if (owner) collectFields(owner, fields, 0);
    for (size_t i = 0; i < fn.locals.size(); ++i) {
        const LocalVariable& v = fn.locals[i];
        int uses = 0;
        for (size_t k = 0; k < fn.bodyIdents.size(); ++k) if (fn.bodyIdents[k] == v.name) ++uses;
        if (uses == 0)
            c.add(isBuiltinType(v.type) ? Finding::Warning : Finding::Info, "W151", fn.file, v.line, "local variable " + v.name + " in " + fn.fullName() + " is never used",
                  isBuiltinType(v.type) ? "" : "an object of class type may be intentional (RAII guard); otherwise remove it");
        for (size_t p = 0; p < fn.params.size(); ++p)
            if (fn.params[p].name == v.name)
                c.add(Finding::Warning, "W152", fn.file, v.line, "local variable " + v.name + " shadows a parameter of " + fn.fullName());
        for (size_t f = 0; f < fields.size(); ++f)
            if (fields[f]->name == v.name)
                c.add(Finding::Warning, "W152", fn.file, v.line, "local variable " + v.name + " in " + fn.fullName() + " shadows the member " + owner->qualifiedName + "::" + v.name,
                      "the member is hidden for the rest of the block; assignments go to the local");
    }
    for (size_t p = 0; p < fn.params.size(); ++p)
        for (size_t f = 0; f < fields.size(); ++f)
            if (!fn.params[p].name.empty() && fields[f]->name == fn.params[p].name && !fn.isConstructor)
                c.add(Finding::Info, "W152", fn.file, fn.line, "parameter " + fn.params[p].name + " of " + fn.fullName() + " shadows the member " + owner->qualifiedName + "::" + fn.params[p].name);

    // body notes from the parser
    for (size_t i = 0; i < fn.notes.size(); ++i) {
        const BodyNote& n = fn.notes[i];
        if (n.code == "assign-in-condition") c.add(Finding::Warning, "W153", fn.file, n.line, n.text);
        else if (n.code == "empty-statement") c.add(Finding::Warning, "W154", fn.file, n.line, n.text);
        else if (n.code == "catch-by-value") c.add(Finding::Warning, "W155", fn.file, n.line, n.text, "catching by value slices derived exceptions and copies the object");
        else if (n.code == "handler-after-catch-all") c.add(Finding::Error, "E156", fn.file, n.line, n.text, "catch (...) must be the last handler");
        else if (n.code == "unreachable") c.add(Finding::Warning, "W157", fn.file, n.line, n.text);
    }
}

void checkFunctions(Ctx& c)
{
    const std::vector<SourceFile*>& files = c.cb.files();
    const std::vector<ClassInfo*>& classes = c.cb.classes();
    std::map<std::string, std::pair<int, std::string> > undeclared;

    for (size_t i = 0; i < classes.size(); ++i)
        for (size_t m = 0; m < classes[i]->methods.size(); ++m) {
            const FunctionInfo& fn = classes[i]->methods[m];
            checkDeclDef(c, fn);
            if (fn.hasBody) { checkConstructor(c, fn); checkBody(c, fn); checkCalls(c, fn, undeclared); }
        }

    std::map<std::string, const FunctionInfo*> definitions;   // E163
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        for (size_t k = 0; k < sf.functions.size(); ++k) {
            const FunctionInfo& fn = sf.functions[k];
            checkDeclDef(c, fn);
            if (!fn.qualifier.empty() && fn.hasBody && fn.declaredAt.empty() && fn.ownerClass) {
                // E130 with a hint about the closest declaration
                std::string hint;
                for (size_t m = 0; m < fn.ownerClass->methods.size(); ++m) {
                    const FunctionInfo& d = fn.ownerClass->methods[m];
                    if (d.name != fn.name) continue;
                    if (d.isConst != fn.isConst && d.params.size() == fn.params.size()) hint = "the class declares " + paramList(d) + " at " + c.loc(d.file, d.line) + ": const-ness differs";
                    else hint = "the class declares " + paramList(d) + " at " + c.loc(d.file, d.line) + ": parameters differ";
                }
                c.add(Finding::Error, "E130", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ")" + (fn.isConst ? " const" : "") + " is defined but not declared in " + fn.ownerClass->qualifiedName,
                      hint.empty() ? "every member function must be declared inside the class definition" : hint);
            } else if (!fn.qualifier.empty() && fn.hasBody && fn.declaredAt.empty() && !fn.ownerClass) {
                c.add(Finding::Error, "E130", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ") is defined but " + fn.qualifier + " is neither a class nor a namespace with such a declaration in the parsed files");
            }
            if (!fn.linkNote.empty() && startsWith(fn.linkNote, "duplicate definition"))
                c.add(Finding::Error, "E131", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ") is defined twice", fn.linkNote);
            if (fn.qualifier.empty() && !fn.hasBody && !fn.definition && !fn.isExtern && !fn.isFriend)
                c.add(c.hasSources ? Finding::Warning : Finding::Info, "W133", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ") is declared but no definition was found");
            if (fn.hasBody) {
                checkConstructor(c, fn);
                checkBody(c, fn);
                checkCalls(c, fn, undeclared);
                // W165 non-inline function defined in a header
                bool ownerTemplate = fn.ownerClass && fn.ownerClass->tmpl.isTemplate;
                if (sf.isHeader && !fn.isInline && !fn.isStatic && !fn.tmpl.isTemplate && !ownerTemplate && !inAnonymousNamespace(fn.scope) && fn.name != "main")
                    c.add(Finding::Warning, "W165", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ") is defined in a header without inline",
                          "including the header from two .cpp files defines the function twice (linker error)");
                // E163 same free function defined in two files
                if (!fn.isInline && !fn.isStatic && !fn.tmpl.isTemplate && !ownerTemplate && !inAnonymousNamespace(fn.scope) && !sf.isHeader) {
                    std::string key = fn.fullName() + "(" + normParams(fn.params) + ")" + (fn.isConst ? "c" : "");
                    std::map<std::string, const FunctionInfo*>::iterator it = definitions.find(key);
                    if (it != definitions.end() && !samePath(it->second->file, fn.file)) {
                        bool isMain = fn.name == "main" && fn.scope.empty();
                        c.add(isMain ? Finding::Info : Finding::Error, "E163", fn.file, fn.line, fn.fullName() + "(" + fn.paramText + ") is defined in two source files",
                              "also defined at " + c.loc(it->second->file, it->second->line) + (isMain ? "; fine if the folder holds several programs" : " (one definition rule)"));
                    }
                    else definitions[key] = &fn;
                }
            }
        }
    }

    for (std::map<std::string, std::pair<int, std::string> >::iterator it = undeclared.begin(); it != undeclared.end(); ++it) {
        std::string where = it->second.second;
        size_t colon = where.find(':');
        std::string file, lineText;
        if (colon != std::string::npos) { file = where.substr(0, colon); lineText = where.substr(colon + 1); }
        c.add(Finding::Info, "I146", file, atoi(lineText.c_str()), "call to " + it->first + "() which is not declared in the parsed files" +
              (it->second.first > 1 ? " (" + intToString(it->second.first) + " calls)" : ""),
              "first call " + where + "; it may come from a system or third-party header that was not parsed");
    }
}

// ---- checks: globals ---------------------------------------------------------------------

void checkGlobals(Ctx& c)
{
    const std::vector<SourceFile*>& files = c.cb.files();
    std::map<std::string, const VariableInfo*> definitions;
    std::vector<const VariableInfo*> externs;
    std::map<std::string, std::set<std::string> > enumerators;   // scope -> names

    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        for (size_t v = 0; v < sf.variables.size(); ++v) {
            const VariableInfo& var = sf.variables[v];
            if (!var.qualifier.empty()) {
                if (var.definedAt.empty()) {
                    const ClassInfo* cls = c.cb.findClass(var.scope, var.qualifier);
                    c.add(Finding::Error, "E130", var.file, var.line, "static member definition " + var.fullName() + " has no matching declaration" +
                          (cls ? " in " + cls->qualifiedName : "; class " + var.qualifier + " was not found"));
                }
                continue;
            }
            if (var.isExtern) { externs.push_back(&var); continue; }
            if (var.isStatic || inAnonymousNamespace(var.scope)) continue;
            if (sf.isHeader && !var.isConst)
                c.add(Finding::Warning, "W164", var.file, var.line, "global variable " + var.fullName() + " is defined in a header",
                      "every .cpp including the header gets its own definition (linker error); declare it extern in the header and define it in one .cpp");
            if (var.isConst) continue;
            std::map<std::string, const VariableInfo*>::iterator it = definitions.find(var.fullName());
            if (it != definitions.end() && !samePath(it->second->file, var.file))
                c.add(Finding::Error, "E160", var.file, var.line, "global variable " + var.fullName() + " is defined in two files",
                      "also defined at " + c.loc(it->second->file, it->second->line) + " (one definition rule); make one of them extern");
            else definitions[var.fullName()] = &var;
        }
        for (size_t e = 0; e < sf.enums.size(); ++e)
            for (size_t k = 0; k < sf.enums[e].enumerators.size(); ++k) {
                std::string n = sf.enums[e].enumerators[k];
                n = trim(n.substr(0, n.find('=')));
                if (!enumerators[sf.enums[e].scope].insert(n).second)
                    c.add(Finding::Error, "E162", sf.path, sf.enums[e].line, "enumerator " + n + " is defined twice in the same scope",
                          "enumerators of different enums share the enclosing scope in C++98");
            }
    }
    for (size_t i = 0; i < externs.size(); ++i) {
        if (definitions.find(externs[i]->fullName()) != definitions.end()) continue;
        if (!c.hasSources) continue;
        c.add(Finding::Warning, "W161", externs[i]->file, externs[i]->line, "extern variable " + externs[i]->fullName() + " is declared but never defined in the parsed files",
              "using it gives an unresolved external at link time");
    }
}

struct FindingOrder {
    bool operator()(const Finding& a, const Finding& b) const
    {
        if (a.severity != b.severity) return a.severity < b.severity;
        if (a.file != b.file) return a.file < b.file;
        if (a.line != b.line) return a.line < b.line;
        return a.code < b.code;
    }
};

} // namespace

// ---- entry points --------------------------------------------------------------------

void runHealthChecks(const CodeBase& cb, std::vector<Finding>& out)
{
    Ctx c(cb, out);
    checkFiles(c);
    checkClasses(c);
    checkFunctions(c);
    checkGlobals(c);
    std::stable_sort(out.begin(), out.end(), FindingOrder());
}

// the number of the finding's project, counted from 1 in input (solution) order: the "N>" prefix MSBuild
// gives every line of a project's build output
int projectNumber(const CodeBase& cb, const Finding& f)
{
    const SourceFile* sf = f.file.empty() ? 0 : cb.fileByPath(f.file);
    if (sf)
        for (size_t p = 0; p < cb.projects().size(); ++p)
            if (cb.projects()[p].name == sf->project) return static_cast<int>(p) + 1;
    return 1;
}

// the Visual Studio / MSBuild form "N>file(line): error CODE: message", which the Output window, the
// Error List and VS Code turn into a link to the line
std::string findingLine(const CodeBase& cb, const Finding& f)
{
    std::string where = f.file.empty() ? cb.title() : cb.displayPath(f.file) + (f.line > 0 ? "(" + intToString(f.line) + ")" : "");
    return intToString(projectNumber(cb, f)) + ">" + where + ": " + severityName(f.severity) + " " + f.code + ": " + f.message;
}

void healthLines(const CodeBase& cb, std::vector<std::string>& L)
{
    const std::vector<Finding>& f = cb.findings();
    int e, w, i;
    countFindings(f, e, w, i);
    L.push_back("errors " + intToString(e) + ", warnings " + intToString(w) + ", info " + intToString(i) +
                "   (E = will not compile or link, W = likely bug or risky construct, I = worth knowing)");
    if (f.empty()) { L.push_back("    no findings"); return; }
    Finding::Severity last = Finding::Info;
    bool first = true;
    for (size_t k = 0; k < f.size(); ++k) {
        if (first || f[k].severity != last) {
            L.push_back("");
            L.push_back(std::string("--- ") + severityName(f[k].severity) + "s ---");
            last = f[k].severity;
            first = false;
        }
        L.push_back(findingLine(cb, f[k]));
        if (!f[k].detail.empty()) L.push_back(intToString(projectNumber(cb, f[k])) + ">    " + f[k].detail);
    }
    std::map<std::string, int> byCode;
    for (size_t k = 0; k < f.size(); ++k) byCode[f[k].code]++;
    L.push_back("");
    L.push_back("summary by code:");
    for (std::map<std::string, int>::iterator it = byCode.begin(); it != byCode.end(); ++it)
        L.push_back("    " + it->first + "  x " + intToString(it->second));
}

std::string renderHealthText(const CodeBase& cb)
{
    std::ostringstream s;
    s << std::string(78, '=') << "\n";
    s << "cppparse health report: " << cb.title() << "\n";
    std::string in;
    for (size_t i = 0; i < cb.inputs().size(); ++i) { if (i) in += ", "; in += cb.inputs()[i]; }
    s << "Input: " << in << "\n";
    s << "Paths are shown relative to: " << cb.baseDirectory() << "\n";
    s << std::string(78, '=') << "\n\n";
    std::vector<std::string> lines;
    healthLines(cb, lines);
    for (size_t i = 0; i < lines.size(); ++i) s << lines[i] << "\n";
    s << "\nThe checks are static and declaration-level: macros are not expanded and #if\n"
         "conditions are not evaluated, so a finding can be a false positive when the\n"
         "code depends on them. Codes are documented in cppparse/Health.hpp.\n";
    return s.str();
}

} // namespace cppparse
