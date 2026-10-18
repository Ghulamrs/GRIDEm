// Report.cpp - totals, shared helpers and the plain-text report.
#include "Report.hpp"
#include "Health.hpp"
#include "PathUtil.hpp"

#include <sstream>

namespace cppparse {

Totals::Totals()
    : files(0), headers(0), sources(0), discovered(0), lines(0), tokens(0),
      directives(0), includes(0), includesResolved(0), macros(0), conditionals(0), pragmas(0), inactiveDirectives(0),
      classes(0), structs(0), unions(0), classTemplates(0), abstractClasses(0), forwardOnly(0), nestedClasses(0),
      methods(0), methodsDefined(0), methodsUndefined(0), pureVirtual(0), virtualMethods(0), constructors(0),
      destructors(0), operators(0), staticMethods(0), fields(0), staticFields(0),
      freeFunctions(0), freeDefined(0), freeDeclaredOnly(0), memberDefinitionsOutOfLine(0),
      globals(0), globalExterns(0), globalStatics(0), staticMemberDefinitions(0), locals(0), parameters(0),
      typedefs(0), enums(0), enumerators(0), usings(0), namespaces(0), friends(0),
      tryBlocks(0), catchHandlers(0), throwStatements(0), functionTryBlocks(0), functionsWithTry(0),
      diagnostics(0), messages(0), healthErrors(0), healthWarnings(0), healthInfos(0)
{
}

// ---- helpers ------------------------------------------------------------------

const FunctionInfo* bodyOf(const FunctionInfo& fn)
{
    return fn.definition ? fn.definition : (fn.hasBody ? &fn : 0);
}

std::string declText(const std::string& type, const std::string& name)
{
    if (name.empty()) return type;
    size_t p = type.find("(*)");
    if (p != std::string::npos) return type.substr(0, p + 2) + name + type.substr(p + 2);
    p = type.find("(&)");
    if (p != std::string::npos) return type.substr(0, p + 2) + name + type.substr(p + 2);
    if (type.empty()) return name;
    return type + " " + name;
}

std::string functionKind(const FunctionInfo& fn)
{
    std::vector<std::string> k;
    if (fn.isConstructor) k.push_back("constructor");
    if (fn.isDestructor) k.push_back("destructor");
    if (fn.isConversion) k.push_back("conversion operator");
    else if (fn.isOperator) k.push_back("operator");
    if (fn.isPureVirtual) k.push_back("pure virtual");
    else if (fn.isVirtual) k.push_back("virtual");
    if (fn.isStatic) k.push_back("static");
    if (fn.isExplicit) k.push_back("explicit");
    if (fn.isInline) k.push_back("inline");
    if (fn.isFriend) k.push_back("friend");
    if (fn.isExtern) k.push_back("extern");
    if (fn.tmpl.isTemplate) k.push_back("template");
    if (fn.isConst) k.push_back("const");
    std::string out;
    for (size_t i = 0; i < k.size(); ++i) { if (i) out += ", "; out += k[i]; }
    return out;
}

std::string locationText(const CodeBase& cb, const std::string& file, int line, int endLine)
{
    std::string s = cb.displayPath(file) + ":" + intToString(line);
    if (endLine > line) s += "-" + intToString(endLine);
    return s;
}

std::string directiveNote(const CodeBase& cb, const Directive& d)
{
    std::string note;
    if (d.inactive) note = "[inactive: inside #if 0] ";
    if (d.isIncludeGuard) note += "[include guard]";
    if (d.kind == Directive::Include) {
        if (!d.resolvedPath.empty()) note += "-> " + cb.displayPath(d.resolvedPath);
        else note += d.systemInclude ? "(system header, not followed)" : "(NOT FOUND)";
    }
    if (d.kind == Directive::Define) note += d.macroParams.empty() ? "object-like macro" : "function-like macro";
    return note;
}

std::string exceptionSummary(const ExceptionInfo& e)
{
    if (e.tryBlocks == 0 && e.catchHandlers == 0 && e.throwStatements == 0) return "";
    std::string s = intToString(e.tryBlocks) + " try" + (e.functionTryBlock ? " (function-try-block)" : "") +
                    ", " + intToString(e.catchHandlers) + " catch";
    if (!e.caughtTypes.empty()) {
        s += " [";
        for (size_t i = 0; i < e.caughtTypes.size(); ++i) { if (i) s += "; "; s += e.caughtTypes[i]; }
        s += "]";
    }
    s += ", " + intToString(e.throwStatements) + " throw";
    return s;
}

namespace {

void treeRec(const ClassInfo* c, const std::string& indent, std::vector<std::string>& lines,
             std::vector<const ClassInfo*>& stack)
{
    for (size_t i = 0; i < stack.size(); ++i) if (stack[i] == c) { lines.push_back(indent + "(cycle: " + c->qualifiedName + ")"); return; }
    stack.push_back(c);
    for (size_t d = 0; d < c->derived.size(); ++d) {
        const ClassInfo* k = c->derived[d];
        std::string extra;
        for (size_t b = 0; b < k->bases.size(); ++b) {
            if (k->bases[b].resolved == c) {
                extra = std::string(accessName(k->bases[b].access)) + (k->bases[b].isVirtual ? " virtual" : "");
            }
        }
        std::string others;
        for (size_t b = 0; b < k->bases.size(); ++b)
            if (k->bases[b].resolved != c) { if (!others.empty()) others += ", "; others += k->bases[b].name; }
        std::string line = indent + "+-- " + std::string(ClassInfo::kindName(k->kind)) + " " + k->qualifiedName + k->tmpl.specializationArgs + "  (" + extra + ")";
        if (!others.empty()) line += "  also derives from: " + others;
        if (k->isAbstract) line += "  [abstract]";
        lines.push_back(line);
        treeRec(k, indent + "    ", lines, stack);
    }
    stack.pop_back();
}

} // namespace

void inheritanceTreeLines(const CodeBase& cb, std::vector<std::string>& lines)
{
    const std::vector<ClassInfo*>& classes = cb.classes();
    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo* c = classes[i];
        if (!c->isDefinition) continue;
        bool hasResolvedBase = false;
        for (size_t b = 0; b < c->bases.size(); ++b) if (c->bases[b].resolved) hasResolvedBase = true;
        if (hasResolvedBase) continue;
        std::string line = std::string(ClassInfo::kindName(c->kind)) + " " + c->qualifiedName + c->tmpl.specializationArgs;
        if (!c->bases.empty()) {
            line += "  : ";
            for (size_t b = 0; b < c->bases.size(); ++b) { if (b) line += ", "; line += std::string(accessName(c->bases[b].access)) + " " + c->bases[b].name + " (external)"; }
        }
        if (c->isAbstract) line += "  [abstract]";
        if (c->owner) line += "  [nested in " + c->owner->qualifiedName + "]";
        lines.push_back(line);
        std::vector<const ClassInfo*> stack;
        treeRec(c, "    ", lines, stack);
    }
}

void collectMacros(const CodeBase& cb, std::vector<std::string>& lines)
{
    const std::vector<SourceFile*>& files = cb.files();
    for (size_t f = 0; f < files.size(); ++f) {
        for (size_t d = 0; d < files[f]->directives.size(); ++d) {
            const Directive& dir = files[f]->directives[d];
            if (dir.kind != Directive::Define || dir.inactive) continue;
            std::string line = padRight(dir.argument + dir.macroParams, 32) + " " + padRight(dir.macroBody.empty() ? "(empty)" : dir.macroBody, 40) +
                               " " + cb.displayPath(files[f]->path) + ":" + intToString(dir.line);
            if (dir.isIncludeGuard) line += "  [include guard]";
            lines.push_back(line);
        }
    }
}

void collectDiagnostics(const CodeBase& cb, std::vector<std::string>& lines)
{
    const std::vector<SourceFile*>& files = cb.files();
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t d = 0; d < files[f]->diagnostics.size(); ++d)
            lines.push_back(cb.displayPath(files[f]->path) + " " + files[f]->diagnostics[d]);
}

// ---- totals -------------------------------------------------------------------

namespace {

void countFunction(const FunctionInfo& fn, Totals& t, bool member)
{
    if (member) {
        t.methods++;
        if (fn.definition) t.methodsDefined++; else if (!fn.isPureVirtual) t.methodsUndefined++;
        if (fn.isPureVirtual) t.pureVirtual++;
        if (fn.isVirtual || fn.isPureVirtual) t.virtualMethods++;
        if (fn.isConstructor) t.constructors++;
        if (fn.isDestructor) t.destructors++;
        if (fn.isOperator) t.operators++;
        if (fn.isStatic) t.staticMethods++;
    }
    t.parameters += static_cast<int>(fn.params.size());
    if (fn.hasBody) {
        t.locals += static_cast<int>(fn.locals.size());
        t.tryBlocks += fn.exceptions.tryBlocks;
        t.catchHandlers += fn.exceptions.catchHandlers;
        t.throwStatements += fn.exceptions.throwStatements;
        if (fn.exceptions.functionTryBlock) t.functionTryBlocks++;
        if (fn.exceptions.tryBlocks) t.functionsWithTry++;
    }
}

} // namespace

void computeTotals(const CodeBase& cb, Totals& t)
{
    const std::vector<SourceFile*>& files = cb.files();
    std::vector<std::string> ns;
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        t.files++;
        if (sf.isHeader) t.headers++; else t.sources++;
        if (sf.origin != "project" && sf.origin != "command line" && sf.origin != "folder") t.discovered++;
        t.lines += sf.lineCount;
        t.tokens += sf.tokenCount;
        t.diagnostics += static_cast<int>(sf.diagnostics.size());
        for (size_t d = 0; d < sf.directives.size(); ++d) {
            const Directive& dir = sf.directives[d];
            t.directives++;
            if (dir.inactive) { t.inactiveDirectives++; continue; }
            switch (dir.kind) {
            case Directive::Include: t.includes++; if (!dir.resolvedPath.empty()) t.includesResolved++; break;
            case Directive::Define:  if (!dir.isIncludeGuard) t.macros++; break;
            case Directive::If: case Directive::Ifdef: case Directive::Ifndef: if (!dir.isIncludeGuard) t.conditionals++; break;
            case Directive::Pragma:  t.pragmas++; break;
            default: break;
            }
        }
        for (size_t c = 0; c < sf.allClasses.size(); ++c) {
            const ClassInfo& cls = *sf.allClasses[c];
            if (!cls.isDefinition) { t.forwardOnly++; continue; }
            if (cls.kind == ClassInfo::Struct) t.structs++; else if (cls.kind == ClassInfo::Union) t.unions++; else t.classes++;
            if (cls.tmpl.isTemplate) t.classTemplates++;
            if (cls.isAbstract) t.abstractClasses++;
            if (cls.owner) t.nestedClasses++;
            t.fields += static_cast<int>(cls.fields.size());
            for (size_t i = 0; i < cls.fields.size(); ++i) if (cls.fields[i].isStatic) t.staticFields++;
            t.typedefs += static_cast<int>(cls.typedefs.size());
            t.enums += static_cast<int>(cls.enums.size());
            for (size_t i = 0; i < cls.enums.size(); ++i) t.enumerators += static_cast<int>(cls.enums[i].enumerators.size());
            t.friends += static_cast<int>(cls.friends.size());
            t.usings += static_cast<int>(cls.usings.size());
            for (size_t m = 0; m < cls.methods.size(); ++m) countFunction(cls.methods[m], t, true);
        }
        for (size_t k = 0; k < sf.functions.size(); ++k) {
            const FunctionInfo& fn = sf.functions[k];
            if (!fn.qualifier.empty()) { t.memberDefinitionsOutOfLine++; countFunction(fn, t, false); continue; }
            t.freeFunctions++;
            if (fn.hasBody) t.freeDefined++; else if (!fn.definition) t.freeDeclaredOnly++;
            countFunction(fn, t, false);
        }
        for (size_t v = 0; v < sf.variables.size(); ++v) {
            const VariableInfo& var = sf.variables[v];
            if (!var.qualifier.empty()) { t.staticMemberDefinitions++; continue; }
            t.globals++;
            if (var.isExtern) t.globalExterns++;
            if (var.isStatic) t.globalStatics++;
        }
        t.typedefs += static_cast<int>(sf.typedefs.size());
        t.enums += static_cast<int>(sf.enums.size());
        for (size_t i = 0; i < sf.enums.size(); ++i) t.enumerators += static_cast<int>(sf.enums[i].enumerators.size());
        t.usings += static_cast<int>(sf.usings.size());
        for (size_t i = 0; i < sf.namespaces.size(); ++i) addUnique(ns, sf.namespaces[i]);
    }
    t.namespaces = static_cast<int>(ns.size());
    t.messages = static_cast<int>(cb.messages().size());
    countFindings(cb.findings(), t.healthErrors, t.healthWarnings, t.healthInfos);
}

void totalsLines(const Totals& t, std::vector<std::string>& L)
{
    L.push_back("Files parsed: " + intToString(t.files) + "  (headers " + intToString(t.headers) + ", sources " + intToString(t.sources) +
                ", of which found only through #include: " + intToString(t.discovered) + ")");
    L.push_back("Lines of code: " + intToString(t.lines) + "  (tokens " + intToString(t.tokens) + ")");
    L.push_back("Preprocessor directives: " + intToString(t.directives) + "  (#include " + intToString(t.includes) + ", resolved " +
                intToString(t.includesResolved) + "; #define " + intToString(t.macros) + "; #if/#ifdef/#ifndef " + intToString(t.conditionals) +
                "; #pragma " + intToString(t.pragmas) + "; inside #if 0: " + intToString(t.inactiveDirectives) + ")");
    L.push_back("Namespaces: " + intToString(t.namespaces));
    L.push_back("Classes: " + intToString(t.classes + t.structs + t.unions) + "  (class " + intToString(t.classes) + ", struct " + intToString(t.structs) +
                ", union " + intToString(t.unions) + "; templates " + intToString(t.classTemplates) + "; abstract " + intToString(t.abstractClasses) +
                "; nested " + intToString(t.nestedClasses) + "; forward declarations only " + intToString(t.forwardOnly) + ")");
    L.push_back("Methods: " + intToString(t.methods) + "  (with a body " + intToString(t.methodsDefined) + ", declared but no body found " +
                intToString(t.methodsUndefined) + "; virtual " + intToString(t.virtualMethods) + ", pure virtual " + intToString(t.pureVirtual) +
                "; constructors " + intToString(t.constructors) + ", destructors " + intToString(t.destructors) + "; operators " +
                intToString(t.operators) + "; static " + intToString(t.staticMethods) + ")");
    L.push_back("Out-of-line member definitions: " + intToString(t.memberDefinitionsOutOfLine));
    L.push_back("Fields: " + intToString(t.fields) + "  (static " + intToString(t.staticFields) + ", static member definitions " + intToString(t.staticMemberDefinitions) + ")");
    L.push_back("Free functions: " + intToString(t.freeFunctions) + "  (definitions " + intToString(t.freeDefined) + ", declarations without definition " + intToString(t.freeDeclaredOnly) + ")");
    L.push_back("Global variables: " + intToString(t.globals) + "  (extern declarations " + intToString(t.globalExterns) + ", file-static " + intToString(t.globalStatics) + ")");
    L.push_back("Function parameters: " + intToString(t.parameters) + ";  local variables: " + intToString(t.locals));
    L.push_back("Typedefs: " + intToString(t.typedefs) + ";  enums: " + intToString(t.enums) + " (" + intToString(t.enumerators) + " enumerators);  using: " +
                intToString(t.usings) + ";  friends: " + intToString(t.friends));
    L.push_back("Exception handling: try " + intToString(t.tryBlocks) + " (function-try-blocks " + intToString(t.functionTryBlocks) + "), catch " +
                intToString(t.catchHandlers) + ", throw " + intToString(t.throwStatements) + ", functions with try: " + intToString(t.functionsWithTry));
    L.push_back("Parser diagnostics: " + intToString(t.diagnostics) + ";  loader messages: " + intToString(t.messages));
    L.push_back("Health findings: errors " + intToString(t.healthErrors) + ", warnings " + intToString(t.healthWarnings) + ", info " + intToString(t.healthInfos));
}

// ---- text report ---------------------------------------------------------------

namespace {

class Text {
public:
    void line(const std::string& s = "") { m_out << s << "\n"; }
    void line(int indent, const std::string& s) { m_out << std::string(static_cast<size_t>(indent), ' ') << s << "\n"; }
    void rule(char c = '-') { m_out << std::string(78, c) << "\n"; }
    void section(int n, const std::string& title) { m_out << "\n" << n << ". " << title << "\n"; rule(); }
    std::string str() const { return m_out.str(); }
private:
    std::ostringstream m_out;
};

std::string classHeadLine(const ClassInfo& c)
{
    std::string s = std::string(ClassInfo::kindName(c.kind)) + " " + c.qualifiedName + c.tmpl.specializationArgs;
    if (!c.bases.empty()) {
        s += " : ";
        for (size_t b = 0; b < c.bases.size(); ++b) {
            if (b) s += ", ";
            if (c.bases[b].isVirtual) s += "virtual ";
            s += std::string(accessName(c.bases[b].access)) + " " + c.bases[b].name;
        }
    }
    return s;
}

void writeFunctionDetail(Text& t, const CodeBase& cb, const FunctionInfo& fn, int indent, const ReportOptions& opt)
{
    std::string head = fn.signature();
    std::string kind = functionKind(fn);
    std::string where;
    if (fn.hasBody) where = "defined " + locationText(cb, fn.file, fn.line, fn.endLine) + (fn.declaredAt.empty() ? "" : "; declared " + fn.declaredAt);
    else where = "declared " + locationText(cb, fn.file, fn.line, 0) + (fn.definedAt.empty() ? (fn.isPureVirtual ? "" : "; NO DEFINITION FOUND") : "; defined " + fn.definedAt);
    t.line(indent, head);
    t.line(indent + 4, std::string(kind.empty() ? "" : "[" + kind + "]  ") + where);
    if (fn.tmpl.isTemplate) t.line(indent + 4, fn.tmpl.text());
    if (!fn.attributes.empty()) t.line(indent + 4, "attributes: " + fn.attributes);
    if (!fn.condition.empty()) t.line(indent + 4, "#if: " + fn.condition);
    if (!fn.linkNote.empty()) t.line(indent + 4, "note: " + fn.linkNote);
    if (!fn.params.empty()) {
        std::string p;
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i) p += ", ";
            p += fn.params[i].type + (fn.params[i].name.empty() ? "" : " " + fn.params[i].name);
            if (!fn.params[i].defaultValue.empty()) p += " = " + fn.params[i].defaultValue;
        }
        t.line(indent + 4, "parameters (" + intToString(static_cast<int>(fn.params.size())) + "): " + p);
    }
    const FunctionInfo* body = bodyOf(fn);
    if (body) {
        if (body->hasInitializerList) t.line(indent + 4, "initializer list: " + body->initializerList);
        if (opt.locals) {
            if (body->locals.empty()) t.line(indent + 4, "local variables: (none)");
            else {
                t.line(indent + 4, "local variables (" + intToString(static_cast<int>(body->locals.size())) + "):");
                for (size_t i = 0; i < body->locals.size(); ++i) {
                    const LocalVariable& v = body->locals[i];
                    t.line(indent + 8, padRight(declText(v.type, v.name) + (v.initializer.empty() ? "" : " = " + v.initializer), 60) + " L" + intToString(v.line));
                }
            }
        }
        std::string ex = exceptionSummary(body->exceptions);
        if (!ex.empty()) t.line(indent + 4, "exceptions: " + ex);
    }
}

void writeClass(Text& t, const CodeBase& cb, const ClassInfo& c, const ReportOptions& opt)
{
    t.line();
    t.line(classHeadLine(c));
    t.line(4, "location: " + locationText(cb, c.file, c.line, c.endLine) + (c.isDefinition ? "" : "  (forward declaration only)"));
    if (c.tmpl.isTemplate) t.line(4, "template: " + c.tmpl.text() + (c.tmpl.specializationArgs.empty() ? "" : "  specialization " + c.tmpl.specializationArgs));
    if (!c.attributes.empty()) t.line(4, "attributes: " + c.attributes);
    if (!c.condition.empty()) t.line(4, "#if: " + c.condition);
    if (c.owner) t.line(4, "nested in: " + c.owner->qualifiedName + " (" + accessName(c.access) + ")");
    if (c.isTypedefName) t.line(4, "anonymous " + std::string(ClassInfo::kindName(c.kind)) + " named through typedef");
    t.line(4, std::string("abstract: ") + (c.isAbstract ? "yes" : "no"));
    for (size_t b = 0; b < c.bases.size(); ++b)
        t.line(4, "base: " + std::string(accessName(c.bases[b].access)) + (c.bases[b].isVirtual ? " virtual " : " ") + c.bases[b].name +
                  (c.bases[b].resolved ? "  -> " + c.bases[b].resolved->qualifiedName + " (" + locationText(cb, c.bases[b].resolved->file, c.bases[b].resolved->line, 0) + ")" : "  (not defined in the parsed files)"));
    if (!c.derived.empty()) {
        std::string d;
        for (size_t i = 0; i < c.derived.size(); ++i) { if (i) d += ", "; d += c.derived[i]->qualifiedName; }
        t.line(4, "derived classes (" + intToString(static_cast<int>(c.derived.size())) + "): " + d);
    }
    if (!c.associations.empty()) {
        std::string a;
        for (size_t i = 0; i < c.associations.size(); ++i) { if (i) a += ", "; a += c.associations[i]->qualifiedName; }
        t.line(4, "uses (member types): " + a);
    }
    if (!c.isDefinition) return;

    t.line(4, "methods (" + intToString(static_cast<int>(c.methods.size())) + "):");
    Access last = AccessNone;
    for (size_t m = 0; m < c.methods.size(); ++m) {
        if (c.methods[m].access != last) { last = c.methods[m].access; t.line(6, std::string(accessName(last)) + ":"); }
        writeFunctionDetail(t, cb, c.methods[m], 8, opt);
    }
    t.line(4, "fields (" + intToString(static_cast<int>(c.fields.size())) + "):");
    last = AccessNone;
    for (size_t f = 0; f < c.fields.size(); ++f) {
        const VariableInfo& v = c.fields[f];
        if (v.access != last) { last = v.access; t.line(6, std::string(accessName(last)) + ":"); }
        std::string s = (v.isStatic ? "static " : "") + std::string(v.isMutable ? "mutable " : "") + declText(v.type, v.name);
        if (!v.bitField.empty()) s += " : " + v.bitField;
        if (!v.initializer.empty()) s += " = " + v.initializer;
        s = padRight(s, 50) + " L" + intToString(v.line);
        if (!v.definedAt.empty()) s += "  defined " + v.definedAt;
        if (!v.condition.empty()) s += "  #if: " + v.condition;
        t.line(8, s);
    }
    if (!c.typedefs.empty()) {
        t.line(4, "typedefs (" + intToString(static_cast<int>(c.typedefs.size())) + "):");
        for (size_t i = 0; i < c.typedefs.size(); ++i) t.line(8, std::string(accessName(c.typedefs[i].access)) + ": " + c.typedefs[i].text + "   L" + intToString(c.typedefs[i].line));
    }
    if (!c.enums.empty()) {
        t.line(4, "enums (" + intToString(static_cast<int>(c.enums.size())) + "):");
        for (size_t i = 0; i < c.enums.size(); ++i) {
            std::string e;
            for (size_t k = 0; k < c.enums[i].enumerators.size(); ++k) { if (k) e += ", "; e += c.enums[i].enumerators[k]; }
            t.line(8, std::string(accessName(c.enums[i].access)) + ": enum " + c.enums[i].name + " { " + e + " }   L" + intToString(c.enums[i].line));
        }
    }
    if (!c.friends.empty()) {
        t.line(4, "friends (" + intToString(static_cast<int>(c.friends.size())) + "):");
        for (size_t i = 0; i < c.friends.size(); ++i) t.line(8, "friend " + c.friends[i].text + "   L" + intToString(c.friends[i].line));
    }
    if (!c.usings.empty()) for (size_t i = 0; i < c.usings.size(); ++i) t.line(4, c.usings[i].text + "   L" + intToString(c.usings[i].line));
    if (!c.nested.empty()) {
        std::string n;
        for (size_t i = 0; i < c.nested.size(); ++i) { if (i) n += ", "; n += c.nested[i]->qualifiedName; }
        t.line(4, "nested classes (" + intToString(static_cast<int>(c.nested.size())) + "): " + n);
    }
}

} // namespace

std::string renderTextReport(const CodeBase& cb, const ReportOptions& opt)
{
    Text t;
    Totals tot;
    computeTotals(cb, tot);
    const std::vector<SourceFile*>& files = cb.files();

    t.rule('=');
    t.line("cppparse report: " + cb.title());
    std::string in;
    for (size_t i = 0; i < cb.inputs().size(); ++i) { if (i) in += ", "; in += cb.inputs()[i]; }
    t.line("Input: " + in);
    t.line("Paths are shown relative to: " + cb.baseDirectory());
    t.rule('=');

    // 1. project
    t.section(1, "PROJECT");
    for (size_t s = 0; s < cb.solutions().size(); ++s) {
        const SolutionInfo& sln = cb.solutions()[s];
        t.line("Solution: " + cb.displayPath(sln.path) + (sln.version.empty() ? "" : "  (" + sln.version + ")"));
        for (size_t i = 0; i < sln.projectPaths.size(); ++i) t.line(4, "project: " + cb.displayPath(sln.projectPaths[i]));
    }
    for (size_t p = 0; p < cb.projects().size(); ++p) {
        const ProjectInfo& pr = cb.projects()[p];
        t.line();
        t.line("Project: " + pr.name + "  [" + (pr.kind == "files" ? pr.kind : cb.displayPath(pr.path)) + "]");
        t.line(4, pr.version);
        if (!pr.configurations.empty()) { std::string c; for (size_t i = 0; i < pr.configurations.size(); ++i) { if (i) c += ", "; c += pr.configurations[i]; } t.line(4, "configurations: " + c); }
        if (!pr.preprocessorDefinitions.empty()) { std::string c; for (size_t i = 0; i < pr.preprocessorDefinitions.size(); ++i) { if (i) c += "; "; c += pr.preprocessorDefinitions[i]; } t.line(4, "preprocessor definitions: " + c); }
        if (!pr.includeDirs.empty()) { t.line(4, "include directories:"); for (size_t i = 0; i < pr.includeDirs.size(); ++i) t.line(8, cb.displayPath(pr.includeDirs[i]) + (fileExists(pr.includeDirs[i]) ? "" : "")); }
        t.line(4, "header files (" + intToString(static_cast<int>(pr.headerFiles.size())) + "):");
        for (size_t i = 0; i < pr.headerFiles.size(); ++i) t.line(8, cb.displayPath(pr.headerFiles[i]) + (fileExists(pr.headerFiles[i]) ? "" : "   [MISSING]"));
        t.line(4, "source files (" + intToString(static_cast<int>(pr.sourceFiles.size())) + "):");
        for (size_t i = 0; i < pr.sourceFiles.size(); ++i) t.line(8, cb.displayPath(pr.sourceFiles[i]) + (fileExists(pr.sourceFiles[i]) ? "" : "   [MISSING]"));
        if (!pr.otherFiles.empty()) {
            t.line(4, "other files (" + intToString(static_cast<int>(pr.otherFiles.size())) + "):");
            for (size_t i = 0; i < pr.otherFiles.size(); ++i) t.line(8, cb.displayPath(pr.otherFiles[i]));
        }
    }
    {
        std::vector<std::string> disc;
        for (size_t f = 0; f < files.size(); ++f)
            if (files[f]->origin != "project" && files[f]->origin != "command line" && files[f]->origin != "folder")
                disc.push_back(cb.displayPath(files[f]->path) + "   (" + files[f]->origin + ")");
        t.line();
        t.line("Files found only through #include, not listed in the project (" + intToString(static_cast<int>(disc.size())) + "):");
        for (size_t i = 0; i < disc.size(); ++i) t.line(4, disc[i]);
    }
    if (!cb.messages().empty()) {
        t.line();
        t.line("Loader messages:");
        for (size_t i = 0; i < cb.messages().size(); ++i) t.line(4, cb.messages()[i]);
    }

    // 2. files
    t.section(2, "FILES  (" + intToString(tot.files) + ": " + intToString(tot.headers) + " headers, " + intToString(tot.sources) + " sources)");
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        int defs = 0;
        for (size_t c = 0; c < sf.allClasses.size(); ++c) if (sf.allClasses[c]->isDefinition) ++defs;
        t.line();
        t.line(std::string(sf.isHeader ? "[H] " : "[C] ") + cb.displayPath(sf.path) + "   (" + intToString(sf.lineCount) + " lines, " +
               intToString(static_cast<int>(sf.directives.size())) + " directives, " + intToString(defs) + " class definitions, " +
               intToString(static_cast<int>(sf.functions.size())) + " function declarations/definitions, " +
               intToString(static_cast<int>(sf.variables.size())) + " file-scope variables)");
        t.line(4, "origin: " + sf.origin + (sf.project.empty() ? "" : ", project " + sf.project));
        if (!sf.includeGuard.empty()) t.line(4, "include guard: " + sf.includeGuard);
        if (sf.pragmaOnce) t.line(4, "#pragma once");
        if (opt.directives && !sf.directives.empty()) {
            t.line(4, "preprocessor directives:");
            for (size_t d = 0; d < sf.directives.size(); ++d) {
                const Directive& dir = sf.directives[d];
                t.line(8, padRight("L" + intToString(dir.line), 6) + padRight("#" + dir.text, 56) + " " + directiveNote(cb, dir));
            }
        }
        if (!sf.namespaces.empty()) { std::string n; for (size_t i = 0; i < sf.namespaces.size(); ++i) { if (i) n += ", "; n += sf.namespaces[i]; } t.line(4, "namespaces: " + n); }
        t.line(4, "declarations:");
        for (size_t c = 0; c < sf.allClasses.size(); ++c) {
            const ClassInfo& cls = *sf.allClasses[c];
            t.line(8, padRight(classHeadLine(cls), 60) + " L" + intToString(cls.line) + (cls.isDefinition ? "-L" + intToString(cls.endLine) : " (forward)") +
                      (cls.isAbstract ? "  abstract" : "") + (cls.condition.empty() ? "" : "  #if: " + cls.condition));
        }
        for (size_t k = 0; k < sf.functions.size(); ++k) {
            const FunctionInfo& fn = sf.functions[k];
            std::string s = (fn.tmpl.isTemplate ? fn.tmpl.text() + " " : "") + (fn.returnType.empty() ? "" : fn.returnType + " ") + fn.fullName() + "(" + fn.paramText + ")" + (fn.isConst ? " const" : "");
            t.line(8, padRight(s, 60) + " L" + intToString(fn.line) + (fn.hasBody ? "-L" + intToString(fn.endLine) + " definition" : " declaration") +
                      (fn.qualifier.empty() ? "" : "  [member of " + fn.qualifier + "]"));
        }
        for (size_t v = 0; v < sf.variables.size(); ++v) {
            const VariableInfo& var = sf.variables[v];
            t.line(8, padRight(std::string(var.isExtern ? "extern " : "") + (var.isStatic ? "static " : "") + declText(var.type, var.fullName()) +
                      (var.initializer.empty() ? "" : " = " + var.initializer), 60) + " L" + intToString(var.line) + (var.qualifier.empty() ? "  global" : "  static member definition"));
        }
        for (size_t i = 0; i < sf.typedefs.size(); ++i) t.line(8, padRight(sf.typedefs[i].text, 60) + " L" + intToString(sf.typedefs[i].line));
        for (size_t i = 0; i < sf.enums.size(); ++i) t.line(8, padRight("enum " + sf.enums[i].name + " (" + intToString(static_cast<int>(sf.enums[i].enumerators.size())) + " enumerators)", 60) + " L" + intToString(sf.enums[i].line));
        for (size_t i = 0; i < sf.usings.size(); ++i) t.line(8, padRight(sf.usings[i].text, 60) + " L" + intToString(sf.usings[i].line));
        if (!sf.diagnostics.empty()) {
            t.line(4, "parser diagnostics (" + intToString(static_cast<int>(sf.diagnostics.size())) + "):");
            for (size_t i = 0; i < sf.diagnostics.size(); ++i) t.line(8, sf.diagnostics[i]);
        }
    }

    // 3. classes
    t.section(3, "CLASSES  (" + intToString(tot.classes + tot.structs + tot.unions) + " definitions)");
    t.line("List:");
    const std::vector<ClassInfo*>& classes = cb.classes();
    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo& c = *classes[i];
        if (!c.isDefinition) continue;
        t.line(4, padRight(std::string(ClassInfo::kindName(c.kind)) + " " + c.qualifiedName + c.tmpl.specializationArgs, 44) +
                  padRight(intToString(static_cast<int>(c.methods.size())) + " methods, " + intToString(static_cast<int>(c.fields.size())) + " fields", 24) +
                  locationText(cb, c.file, c.line, 0));
    }
    t.line();
    t.line("Details:");
    for (size_t i = 0; i < classes.size(); ++i) writeClass(t, cb, *classes[i], opt);

    // 4. diagram
    t.section(4, "CLASS DIAGRAM (inheritance tree; see the .svg / .dot / .html files for the picture)");
    {
        std::vector<std::string> lines;
        inheritanceTreeLines(cb, lines);
        if (lines.empty()) t.line("(no classes)");
        for (size_t i = 0; i < lines.size(); ++i) t.line(lines[i]);
    }

    // 5. globals
    t.section(5, "GLOBAL DEFINITIONS");
    t.line("5.1 Free functions (" + intToString(tot.freeFunctions) + "):");
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t k = 0; k < files[f]->functions.size(); ++k)
            if (files[f]->functions[k].qualifier.empty()) writeFunctionDetail(t, cb, files[f]->functions[k], 4, opt);
    t.line();
    t.line("5.2 Out-of-line member definitions (" + intToString(tot.memberDefinitionsOutOfLine) + "):");
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t k = 0; k < files[f]->functions.size(); ++k) {
            const FunctionInfo& fn = files[f]->functions[k];
            if (fn.qualifier.empty()) continue;
            t.line(4, padRight(fn.fullName() + "(" + fn.paramText + ")" + (fn.isConst ? " const" : ""), 60) + " " + locationText(cb, fn.file, fn.line, fn.endLine) +
                      (fn.declaredAt.empty() ? "   [" + (fn.linkNote.empty() ? std::string("no declaration found") : fn.linkNote) + "]" : "   declared " + fn.declaredAt));
        }
    t.line();
    t.line("5.3 Global variables (" + intToString(tot.globals) + "):");
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v) {
            const VariableInfo& var = files[f]->variables[v];
            if (!var.qualifier.empty()) continue;
            t.line(4, padRight(std::string(var.isExtern ? "extern " : "") + (var.isStatic ? "static " : "") + declText(var.type, var.fullName()) +
                      (var.initializer.empty() ? "" : " = " + var.initializer), 60) + " " + locationText(cb, var.file, var.line, 0) + (var.condition.empty() ? "" : "  #if: " + var.condition));
        }
    t.line();
    t.line("5.4 Static member definitions (" + intToString(tot.staticMemberDefinitions) + "):");
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v) {
            const VariableInfo& var = files[f]->variables[v];
            if (var.qualifier.empty()) continue;
            t.line(4, padRight(declText(var.type, var.fullName()) + (var.initializer.empty() ? "" : " = " + var.initializer), 60) + " " + locationText(cb, var.file, var.line, 0) +
                      (var.definedAt.empty() ? "   [no matching static field found]" : "   declared " + var.definedAt));
        }
    t.line();
    t.line("5.5 File-scope typedefs:");
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t i = 0; i < files[f]->typedefs.size(); ++i)
            t.line(4, padRight(files[f]->typedefs[i].text, 60) + " " + locationText(cb, files[f]->path, files[f]->typedefs[i].line, 0));
    t.line();
    t.line("5.6 File-scope enums:");
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t i = 0; i < files[f]->enums.size(); ++i) {
            const EnumInfo& e = files[f]->enums[i];
            std::string en;
            for (size_t k = 0; k < e.enumerators.size(); ++k) { if (k) en += ", "; en += e.enumerators[k]; }
            t.line(4, padRight("enum " + (e.scope.empty() ? e.name : e.scope + "::" + e.name) + " { " + en + " }", 60) + " " + locationText(cb, files[f]->path, e.line, 0));
        }
    t.line();
    t.line("5.7 Macros (#define, " + intToString(tot.macros) + " outside include guards):");
    {
        std::vector<std::string> lines;
        collectMacros(cb, lines);
        for (size_t i = 0; i < lines.size(); ++i) t.line(4, lines[i]);
    }
    t.line();
    t.line("5.8 Namespaces and using:");
    {
        std::vector<std::string> ns;
        for (size_t f = 0; f < files.size(); ++f) for (size_t i = 0; i < files[f]->namespaces.size(); ++i) addUnique(ns, files[f]->namespaces[i]);
        for (size_t i = 0; i < ns.size(); ++i) t.line(4, "namespace " + ns[i]);
        for (size_t f = 0; f < files.size(); ++f)
            for (size_t i = 0; i < files[f]->usings.size(); ++i) t.line(4, padRight(files[f]->usings[i].text, 60) + " " + locationText(cb, files[f]->path, files[f]->usings[i].line, 0));
    }

    // 6. exceptions
    t.section(6, "EXCEPTION HANDLING  (try " + intToString(tot.tryBlocks) + ", catch " + intToString(tot.catchHandlers) + ", throw " + intToString(tot.throwStatements) + ")");
    {
        bool any = false;
        for (size_t f = 0; f < files.size(); ++f) {
            const SourceFile& sf = *files[f];
            for (size_t k = 0; k < sf.functions.size(); ++k) {
                std::string ex = exceptionSummary(sf.functions[k].exceptions);
                if (ex.empty()) continue;
                any = true;
                t.line(4, padRight(sf.functions[k].fullName() + "(" + sf.functions[k].paramText + ")", 56) + " " + locationText(cb, sf.path, sf.functions[k].line, 0) + "   " + ex);
            }
            for (size_t c = 0; c < sf.allClasses.size(); ++c)
                for (size_t m = 0; m < sf.allClasses[c]->methods.size(); ++m) {
                    const FunctionInfo& fn = sf.allClasses[c]->methods[m];
                    if (!fn.hasBody) continue;
                    std::string ex = exceptionSummary(fn.exceptions);
                    if (ex.empty()) continue;
                    any = true;
                    t.line(4, padRight(sf.allClasses[c]->qualifiedName + "::" + fn.name + "(" + fn.paramText + ")", 56) + " " + locationText(cb, sf.path, fn.line, 0) + "   " + ex);
                }
        }
        if (!any) t.line(4, "(no try/catch/throw found)");
    }

    // 7. health
    t.section(7, "HEALTH  (version 2 checker; also written to <name>-health.txt)");
    {
        std::vector<std::string> lines;
        healthLines(cb, lines);
        for (size_t i = 0; i < lines.size(); ++i) t.line(lines[i]);
    }

    // 8. totals
    t.section(8, "TOTALS");
    {
        std::vector<std::string> lines;
        totalsLines(tot, lines);
        for (size_t i = 0; i < lines.size(); ++i) t.line(lines[i]);
    }
    t.line();
    t.rule('=');
    t.line("End of report");
    return t.str();
}

} // namespace cppparse
