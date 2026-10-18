// Symbols.cpp - symbol table export (JSON and tab-separated text).
#include "Symbols.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace cppparse {

namespace {

struct Symbol {
    std::string kind;        // namespace, class, struct, union, method, field, function, variable,
                             // parameter, local, typedef, enum, enumerator, macro
    std::string name;
    std::string qualified;
    std::string scope;       // enclosing namespace / class / function
    std::string type;        // type for variables, return type for functions
    std::string signature;   // functions: full signature
    std::string storage;     // static, extern, mutable, inline, virtual, pure virtual, ...
    std::string access;
    std::string file;
    int         line;
    int         endLine;
    std::string definedAt;   // declaration -> definition
    std::string declaredAt;  // definition -> declaration
    std::string extra;       // bases, enumerators, macro body, template header
    Symbol() : line(0), endLine(0) {}
};

std::string jsonEscape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) { out += "\\u00"; out += "0123456789abcdef"[c >> 4]; out += "0123456789abcdef"[c & 15]; }
            else out += static_cast<char>(c);
        }
    }
    return out;
}

std::string functionStorage(const FunctionInfo& fn)
{
    std::vector<std::string> k;
    if (fn.isStatic) k.push_back("static");
    if (fn.isPureVirtual) k.push_back("pure virtual"); else if (fn.isVirtual) k.push_back("virtual");
    if (fn.isInline) k.push_back("inline");
    if (fn.isExplicit) k.push_back("explicit");
    if (fn.isExtern) k.push_back("extern");
    if (fn.isFriend) k.push_back("friend");
    if (fn.isConstructor) k.push_back("constructor");
    if (fn.isDestructor) k.push_back("destructor");
    if (fn.isConversion) k.push_back("conversion");
    else if (fn.isOperator) k.push_back("operator");
    if (fn.isConst) k.push_back("const");
    if (fn.hasBody) k.push_back("definition"); else k.push_back("declaration");
    std::string out;
    for (size_t i = 0; i < k.size(); ++i) { if (i) out += " "; out += k[i]; }
    return out;
}

void addFunction(const CodeBase& cb, const FunctionInfo& fn, const std::string& kind, std::vector<Symbol>& out)
{
    Symbol s;
    s.kind = kind;
    s.name = fn.name;
    s.qualified = fn.ownerClass ? fn.ownerClass->qualifiedName + "::" + fn.name : fn.fullName();
    s.scope = fn.ownerClass ? fn.ownerClass->qualifiedName : (fn.scope.empty() ? "" : fn.scope);
    s.type = fn.returnType;
    s.signature = fn.signature();
    s.storage = functionStorage(fn);
    s.access = accessName(fn.access);
    s.file = cb.displayPath(fn.file);
    s.line = fn.line;
    s.endLine = fn.hasBody ? fn.endLine : fn.line;
    s.definedAt = fn.definedAt;
    s.declaredAt = fn.declaredAt;
    s.extra = fn.tmpl.text();
    out.push_back(s);

    std::string fnScope = s.qualified + "(" + fn.paramText + ")";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (fn.params[i].name.empty()) continue;
        Symbol p;
        p.kind = "parameter";
        p.name = fn.params[i].name;
        p.qualified = fnScope + "::" + p.name;
        p.scope = fnScope;
        p.type = fn.params[i].type;
        p.file = s.file;
        p.line = fn.line;
        p.extra = fn.params[i].defaultValue.empty() ? "" : "= " + fn.params[i].defaultValue;
        out.push_back(p);
    }
    for (size_t i = 0; i < fn.locals.size(); ++i) {
        Symbol l;
        l.kind = "local";
        l.name = fn.locals[i].name;
        l.qualified = fnScope + "::" + l.name;
        l.scope = fnScope;
        l.type = fn.locals[i].type;
        l.file = s.file;
        l.line = fn.locals[i].line;
        l.extra = fn.locals[i].initializer.empty() ? "" : "= " + fn.locals[i].initializer;
        out.push_back(l);
    }
}

void addVariable(const CodeBase& cb, const VariableInfo& v, const std::string& kind, const std::string& owner, std::vector<Symbol>& out)
{
    Symbol s;
    s.kind = kind;
    s.name = v.name;
    s.qualified = owner.empty() ? v.fullName() : owner + "::" + v.name;
    s.scope = owner.empty() ? v.scope : owner;
    s.type = v.type;
    std::string st;
    if (v.isStatic) st += "static ";
    if (v.isExtern) st += "extern ";
    if (v.isMutable) st += "mutable ";
    if (v.isConst) st += "const ";
    if (!v.qualifier.empty()) st += "static-member-definition ";
    s.storage = trim(st);
    s.access = accessName(v.access);
    s.file = cb.displayPath(v.file);
    s.line = v.line;
    s.definedAt = v.definedAt;
    s.extra = v.initializer.empty() ? "" : "= " + v.initializer;
    if (!v.bitField.empty()) s.extra += " : " + v.bitField;
    out.push_back(s);
}

void addClass(const CodeBase& cb, const ClassInfo& c, std::vector<Symbol>& out)
{
    Symbol s;
    s.kind = ClassInfo::kindName(c.kind);
    s.name = c.name;
    s.qualified = c.qualifiedName + c.tmpl.specializationArgs;
    s.scope = c.owner ? c.owner->qualifiedName : c.scope;
    s.storage = c.isDefinition ? (c.isAbstract ? "definition abstract" : "definition") : "forward-declaration";
    s.access = accessName(c.access);
    s.file = cb.displayPath(c.file);
    s.line = c.line;
    s.endLine = c.endLine;
    std::string bases;
    for (size_t b = 0; b < c.bases.size(); ++b) {
        if (b) bases += ", ";
        bases += std::string(accessName(c.bases[b].access)) + (c.bases[b].isVirtual ? " virtual " : " ") + c.bases[b].name;
    }
    s.extra = c.tmpl.text();
    if (!bases.empty()) s.extra += (s.extra.empty() ? "" : "; ") + std::string("bases: ") + bases;
    out.push_back(s);

    for (size_t m = 0; m < c.methods.size(); ++m) addFunction(cb, c.methods[m], "method", out);
    for (size_t f = 0; f < c.fields.size(); ++f) addVariable(cb, c.fields[f], "field", c.qualifiedName, out);
    for (size_t t = 0; t < c.typedefs.size(); ++t) {
        Symbol td;
        td.kind = "typedef"; td.name = c.typedefs[t].name; td.qualified = c.qualifiedName + "::" + td.name; td.scope = c.qualifiedName;
        td.type = c.typedefs[t].text; td.access = accessName(c.typedefs[t].access); td.file = s.file; td.line = c.typedefs[t].line;
        out.push_back(td);
    }
    for (size_t e = 0; e < c.enums.size(); ++e) {
        Symbol en;
        en.kind = "enum"; en.name = c.enums[e].name; en.qualified = c.qualifiedName + "::" + en.name; en.scope = c.qualifiedName;
        en.access = accessName(c.enums[e].access); en.file = s.file; en.line = c.enums[e].line;
        for (size_t k = 0; k < c.enums[e].enumerators.size(); ++k) { if (k) en.extra += ", "; en.extra += c.enums[e].enumerators[k]; }
        out.push_back(en);
        for (size_t k = 0; k < c.enums[e].enumerators.size(); ++k) {
            Symbol v;
            std::string n = c.enums[e].enumerators[k];
            size_t eq = n.find('=');
            v.kind = "enumerator"; v.name = trim(n.substr(0, eq)); v.qualified = c.qualifiedName + "::" + v.name; v.scope = c.qualifiedName;
            v.type = c.enums[e].name; v.file = s.file; v.line = c.enums[e].line; v.extra = eq == std::string::npos ? "" : trim(n.substr(eq));
            out.push_back(v);
        }
    }
}

void collect(const CodeBase& cb, std::vector<Symbol>& out)
{
    const std::vector<SourceFile*>& files = cb.files();
    std::vector<std::string> namespaces;
    for (size_t f = 0; f < files.size(); ++f) for (size_t i = 0; i < files[f]->namespaces.size(); ++i) addUnique(namespaces, files[f]->namespaces[i]);
    for (size_t i = 0; i < namespaces.size(); ++i) {
        Symbol s;
        s.kind = "namespace";
        s.qualified = namespaces[i];
        s.name = lastComponent(namespaces[i]);
        size_t pos = namespaces[i].rfind("::");
        s.scope = pos == std::string::npos ? "" : namespaces[i].substr(0, pos);
        out.push_back(s);
    }
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        for (size_t c = 0; c < sf.allClasses.size(); ++c) addClass(cb, *sf.allClasses[c], out);
        for (size_t k = 0; k < sf.functions.size(); ++k) addFunction(cb, sf.functions[k], sf.functions[k].ownerClass ? "method" : "function", out);
        for (size_t v = 0; v < sf.variables.size(); ++v) addVariable(cb, sf.variables[v], sf.variables[v].qualifier.empty() ? "variable" : "field", "", out);
        for (size_t t = 0; t < sf.typedefs.size(); ++t) {
            Symbol td;
            td.kind = "typedef"; td.name = sf.typedefs[t].name; td.scope = sf.typedefs[t].scope;
            td.qualified = td.scope.empty() ? td.name : td.scope + "::" + td.name;
            td.type = sf.typedefs[t].text; td.file = cb.displayPath(sf.path); td.line = sf.typedefs[t].line;
            out.push_back(td);
        }
        for (size_t e = 0; e < sf.enums.size(); ++e) {
            const EnumInfo& en = sf.enums[e];
            Symbol s;
            s.kind = "enum"; s.name = en.name; s.scope = en.scope; s.qualified = en.scope.empty() ? en.name : en.scope + "::" + en.name;
            s.file = cb.displayPath(sf.path); s.line = en.line;
            for (size_t k = 0; k < en.enumerators.size(); ++k) { if (k) s.extra += ", "; s.extra += en.enumerators[k]; }
            out.push_back(s);
            for (size_t k = 0; k < en.enumerators.size(); ++k) {
                Symbol v;
                std::string n = en.enumerators[k];
                size_t eq = n.find('=');
                v.kind = "enumerator"; v.name = trim(n.substr(0, eq)); v.scope = en.scope; v.qualified = en.scope.empty() ? v.name : en.scope + "::" + v.name;
                v.type = en.name; v.file = s.file; v.line = en.line; v.extra = eq == std::string::npos ? "" : trim(n.substr(eq));
                out.push_back(v);
            }
        }
        for (size_t d = 0; d < sf.directives.size(); ++d) {
            const Directive& dir = sf.directives[d];
            if (dir.kind != Directive::Define || dir.inactive) continue;
            Symbol s;
            s.kind = "macro"; s.name = dir.argument; s.qualified = dir.argument; s.signature = dir.argument + dir.macroParams;
            s.storage = dir.isIncludeGuard ? "include-guard" : (dir.macroParams.empty() ? "object-like" : "function-like");
            s.file = cb.displayPath(sf.path); s.line = dir.line; s.extra = dir.macroBody;
            out.push_back(s);
        }
    }
}

void jsonField(std::ostringstream& s, const char* key, const std::string& value, bool& first)
{
    if (value.empty()) return;
    if (!first) s << ", ";
    first = false;
    s << "\"" << key << "\": \"" << jsonEscape(value) << "\"";
}

} // namespace

int countSymbols(const CodeBase& cb)
{
    std::vector<Symbol> syms;
    collect(cb, syms);
    return static_cast<int>(syms.size());
}

std::string renderSymbolsJson(const CodeBase& cb)
{
    std::vector<Symbol> syms;
    collect(cb, syms);
    std::ostringstream s;
    s << "{\n  \"generator\": \"cppparse\",\n  \"project\": \"" << jsonEscape(cb.title()) << "\",\n  \"baseDirectory\": \""
      << jsonEscape(cb.baseDirectory()) << "\",\n  \"symbolCount\": " << syms.size() << ",\n  \"symbols\": [\n";
    for (size_t i = 0; i < syms.size(); ++i) {
        const Symbol& y = syms[i];
        bool first = true;
        s << "    {";
        jsonField(s, "kind", y.kind, first);
        jsonField(s, "name", y.name, first);
        jsonField(s, "qualified", y.qualified, first);
        jsonField(s, "scope", y.scope, first);
        jsonField(s, "type", y.type, first);
        jsonField(s, "signature", y.signature, first);
        jsonField(s, "storage", y.storage, first);
        jsonField(s, "access", y.access, first);
        jsonField(s, "file", y.file, first);
        if (y.line) { s << (first ? "" : ", ") << "\"line\": " << y.line; first = false; }
        if (y.endLine > y.line) s << ", \"endLine\": " << y.endLine;
        jsonField(s, "definedAt", y.definedAt, first);
        jsonField(s, "declaredAt", y.declaredAt, first);
        jsonField(s, "extra", y.extra, first);
        s << "}" << (i + 1 < syms.size() ? "," : "") << "\n";
    }
    s << "  ]\n}\n";
    return s.str();
}

// ---- map file ---------------------------------------------------------------------

namespace {

std::string hex4(int v)
{
    static const char* digits = "0123456789ABCDEF";
    std::string s(4, '0');
    for (int i = 3; i >= 0; --i) { s[static_cast<size_t>(i)] = digits[v & 15]; v >>= 4; }
    return s;
}

std::string hex8(int v)
{
    return hex4((v >> 16) & 0xFFFF) + hex4(v & 0xFFFF);
}

// searches a class and its bases for a method or field name
bool classHasMember(const ClassInfo* cls, const std::string& name, bool& isMethod, int depth)
{
    if (!cls || depth > 32) return false;
    for (size_t i = 0; i < cls->methods.size(); ++i) if (cls->methods[i].name == name) { isMethod = true; return true; }
    for (size_t i = 0; i < cls->fields.size(); ++i) if (cls->fields[i].name == name) { isMethod = false; return true; }
    for (size_t b = 0; b < cls->bases.size(); ++b) if (classHasMember(cls->bases[b].resolved, name, isMethod, depth + 1)) return true;
    return false;
}

const ClassInfo* memberOwner(const ClassInfo* cls, const std::string& name, int depth)
{
    if (!cls || depth > 32) return 0;
    for (size_t i = 0; i < cls->methods.size(); ++i) if (cls->methods[i].name == name) return cls;
    for (size_t i = 0; i < cls->fields.size(); ++i) if (cls->fields[i].name == name) return cls;
    for (size_t b = 0; b < cls->bases.size(); ++b) { const ClassInfo* o = memberOwner(cls->bases[b].resolved, name, depth + 1); if (o) return o; }
    return 0;
}

std::string typeName(const std::string& type)
{
    std::string t = stripTemplateArgs(type);
    std::string word, last;
    for (size_t i = 0; i <= t.size(); ++i) {
        char c = i < t.size() ? t[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') { word += c; continue; }
        if (!word.empty() && word != "const" && word != "volatile" && word != "struct" && word != "class" && word != "typename") last = word;
        word.clear();
        if (c == '[') break;
    }
    return last;
}

struct Reference { std::string from; int line; std::string caller; };
typedef std::map<std::string, std::vector<Reference> > RefMap;

std::string varType(const CodeBase& cb, const FunctionInfo& fn, const std::string& name)
{
    for (size_t i = 0; i < fn.locals.size(); ++i) if (fn.locals[i].name == name) return fn.locals[i].type;
    for (size_t i = 0; i < fn.params.size(); ++i) if (fn.params[i].name == name) return fn.params[i].type;
    for (const ClassInfo* c = fn.ownerClass; c; c = c->bases.empty() ? 0 : c->bases[0].resolved)
        for (size_t i = 0; i < c->fields.size(); ++i) if (c->fields[i].name == name) return c->fields[i].type;
    const std::vector<SourceFile*>& files = cb.files();
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v)
            if (files[f]->variables[v].name == name && files[f]->variables[v].qualifier.empty()) return files[f]->variables[v].type;
    return "";
}

void addRef(RefMap& refs, const std::string& target, const CodeBase& cb, const FunctionInfo& fn, int line)
{
    Reference r;
    r.from = cb.displayPath(fn.file);
    r.line = line;
    r.caller = fn.fullName();
    refs[target].push_back(r);
}

// resolves every call and every identifier use in a body to a symbol name
void collectReferences(const CodeBase& cb, const FunctionInfo& fn, RefMap& refs)
{
    if (!fn.hasBody) return;
    std::string scope = fn.ownerClass ? fn.ownerClass->qualifiedName : fn.scope;
    const std::vector<SourceFile*>& files = cb.files();

    for (size_t k = 0; k < fn.calls.size(); ++k) {
        const CallInfo& c = fn.calls[k];
        std::string target;
        if (c.kind == CallInfo::New || c.kind == CallInfo::Construct) {
            const ClassInfo* cls = cb.findClass(scope, c.name);
            if (cls) target = cls->qualifiedName + "::" + cls->name;   // constructor
        } else if (c.kind == CallInfo::Member || c.kind == CallInfo::Pointer) {
            const ClassInfo* cls = c.receiver == "this" ? fn.ownerClass : 0;
            if (!cls && !c.receiver.empty()) {
                std::string tn = typeName(varType(cb, fn, c.receiver));
                if (!tn.empty()) cls = cb.findClass(scope, tn);
            }
            const ClassInfo* owner = memberOwner(cls, c.name, 0);
            if (owner) target = owner->qualifiedName + "::" + c.name;
        } else if (c.kind == CallInfo::Scoped) {
            const ClassInfo* cls = c.qualifier.empty() ? 0 : cb.findClass(scope, c.qualifier);
            const ClassInfo* owner = memberOwner(cls, c.name, 0);
            if (owner) target = owner->qualifiedName + "::" + c.name;
            else if (!c.qualifier.empty() && c.qualifier != "std") {
                for (size_t f = 0; f < files.size() && target.empty(); ++f)
                    for (size_t i = 0; i < files[f]->functions.size(); ++i)
                        if (files[f]->functions[i].name == c.name && endsWith(files[f]->functions[i].fullName(), c.qualifier + "::" + c.name)) { target = files[f]->functions[i].fullName(); break; }
            }
        } else {
            const ClassInfo* owner = memberOwner(fn.ownerClass, c.name, 0);
            if (owner) target = owner->qualifiedName + "::" + c.name;
            else {
                for (size_t f = 0; f < files.size() && target.empty(); ++f)
                    for (size_t i = 0; i < files[f]->functions.size(); ++i)
                        if (files[f]->functions[i].name == c.name && !files[f]->functions[i].ownerClass) { target = files[f]->functions[i].fullName(); break; }
                if (target.empty()) { const ClassInfo* cls = cb.findClass(scope, c.name); if (cls) target = cls->qualifiedName + "::" + cls->name; }
            }
        }
        if (!target.empty()) addRef(refs, target, cb, fn, c.line);
    }

    // variable uses: members of the owner class and globals
    std::set<std::string> seen;
    for (size_t k = 0; k < fn.bodyIdents.size(); ++k) {
        const std::string& id = fn.bodyIdents[k];
        if (!seen.insert(id).second) continue;
        bool isLocal = false;
        for (size_t i = 0; i < fn.locals.size() && !isLocal; ++i) if (fn.locals[i].name == id) isLocal = true;
        for (size_t i = 0; i < fn.params.size() && !isLocal; ++i) if (fn.params[i].name == id) isLocal = true;
        if (isLocal) continue;
        const ClassInfo* owner = memberOwner(fn.ownerClass, id, 0);
        if (owner) {
            bool isMethod = false;
            classHasMember(owner, id, isMethod, 0);
            if (!isMethod) addRef(refs, owner->qualifiedName + "::" + id, cb, fn, fn.line);
            continue;
        }
        for (size_t f = 0; f < files.size(); ++f) {
            bool done = false;
            for (size_t v = 0; v < files[f]->variables.size() && !done; ++v) {
                const VariableInfo& g = files[f]->variables[v];
                if (g.name == id && g.qualifier.empty() && !g.isExtern) { addRef(refs, g.fullName(), cb, fn, fn.line); done = true; }
            }
            if (done) break;
        }
    }
}

std::string symbolText(const Symbol& y)
{
    // the parameter list follows the name itself: the first '(' can belong to the name (operator()) or to
    // the return type (a function pointer)
    size_t at = y.signature.find(y.name + "(");
    if (at != std::string::npos) return y.qualified + y.signature.substr(at + y.name.size());   // name + "(params) const"
    size_t paren = y.signature.find('(');
    if (paren != std::string::npos) return y.qualified + y.signature.substr(paren);
    return y.qualified;
}

} // namespace

std::string renderSymbolsMap(const CodeBase& cb)
{
    std::vector<Symbol> syms;
    collect(cb, syms);
    const std::vector<SourceFile*>& files = cb.files();
    std::ostringstream s;

    s << " cppparse symbol map\n\n";
    s << " Project:        " << cb.title() << "\n";
    s << " Base directory: " << cb.baseDirectory() << "\n";
    s << " Symbols:        " << syms.size() << "\n";
    s << " Reference:      module:ordinal (module = file number below, ordinal = position in the module)\n\n";

    // modules
    std::map<std::string, int> moduleOf;
    s << " Modules\n\n";
    s << "  Module  Lines   Symbols  File\n";
    std::vector<int> perModule(files.size() + 1, 0);
    for (size_t i = 0; i < syms.size(); ++i) {
        for (size_t f = 0; f < files.size(); ++f)
            if (syms[i].file == cb.displayPath(files[f]->path)) { perModule[f + 1]++; break; }
    }
    for (size_t f = 0; f < files.size(); ++f) {
        moduleOf[cb.displayPath(files[f]->path)] = static_cast<int>(f + 1);
        s << "  " << hex4(static_cast<int>(f + 1)) << "    " << padRight(intToString(files[f]->lineCount), 7) << " " << padRight(intToString(perModule[f + 1]), 8)
          << " " << cb.displayPath(files[f]->path) << (files[f]->origin == "project" || files[f]->origin == "folder" || files[f]->origin == "command line" ? "" : "   (" + files[f]->origin + ")") << "\n";
    }
    s << "\n";

    // symbols by module
    s << " Symbols by module\n\n";
    s << "  Reference      Kind        Symbol" << std::string(54, ' ') << "Location          Linked to\n";
    std::vector<std::string> refOf(syms.size());
    std::vector<int> ordinal(files.size() + 2, 0);
    for (size_t f = 0; f <= files.size(); ++f) {
        std::string modPath = f < files.size() ? cb.displayPath(files[f]->path) : "";
        bool headerPrinted = false;
        for (size_t i = 0; i < syms.size(); ++i) {
            const Symbol& y = syms[i];
            if (y.file != modPath) continue;
            if (!headerPrinted) {
                s << "\n  " << (f < files.size() ? hex4(static_cast<int>(f + 1)) + "  " + modPath : "0000  (no file: namespaces)") << "\n";
                headerPrinted = true;
            }
            int m = f < files.size() ? static_cast<int>(f + 1) : 0;
            refOf[i] = hex4(m) + ":" + hex8(++ordinal[static_cast<size_t>(m)]);
            std::string where = y.line ? "L" + intToString(y.line) + (y.endLine > y.line ? "-" + intToString(y.endLine) : "") : "";
            std::string link;
            if (!y.definedAt.empty()) link = "-> defined " + y.definedAt;
            else if (!y.declaredAt.empty()) link = "<- declared " + y.declaredAt;
            std::string text = symbolText(y);
            if (!y.access.empty() && (y.kind == "method" || y.kind == "field")) text = std::string(1, static_cast<char>(toupper(y.access[0]))) + " " + text;
            s << "  " << refOf[i] << "  " << padRight(y.kind, 11) << " " << padRight(text, 59) << " " << padRight(where, 17) << " " << link << "\n";
        }
    }
    s << "\n";

    // sorted by name
    s << " Symbols by name\n\n";
    std::vector<std::pair<std::string, size_t> > byName;
    for (size_t i = 0; i < syms.size(); ++i)
        if (syms[i].kind != "parameter" && syms[i].kind != "local") byName.push_back(std::make_pair(syms[i].qualified, i));
    std::sort(byName.begin(), byName.end());
    s << "  Reference      Kind        Symbol" << std::string(54, ' ') << "File:line\n";
    for (size_t k = 0; k < byName.size(); ++k) {
        const Symbol& y = syms[byName[k].second];
        s << "  " << refOf[byName[k].second] << "  " << padRight(y.kind, 11) << " " << padRight(symbolText(y), 59) << " " << y.file << (y.line ? ":" + intToString(y.line) : "") << "\n";
    }
    s << "\n";

    // cross reference
    RefMap refs;
    for (size_t f = 0; f < files.size(); ++f) {
        for (size_t k = 0; k < files[f]->functions.size(); ++k) collectReferences(cb, files[f]->functions[k], refs);
        for (size_t c = 0; c < files[f]->allClasses.size(); ++c)
            for (size_t m = 0; m < files[f]->allClasses[c]->methods.size(); ++m) collectReferences(cb, files[f]->allClasses[c]->methods[m], refs);
    }
    s << " Cross reference  (symbol <- referenced from)\n\n";
    for (RefMap::iterator it = refs.begin(); it != refs.end(); ++it) {
        s << "  " << it->first << "  (" << it->second.size() << ")\n";
        for (size_t r = 0; r < it->second.size(); ++r)
            s << "      " << padRight(it->second[r].from + ":" + intToString(it->second[r].line), 34) << " in " << it->second[r].caller << "\n";
    }
    s << "\n";

    // unreferenced functions and globals
    s << " Unreferenced symbols  (functions and globals never called or used in the parsed bodies)\n\n";
    int unref = 0;
    for (size_t i = 0; i < syms.size(); ++i) {
        const Symbol& y = syms[i];
        if (y.kind != "function" && y.kind != "method" && y.kind != "variable") continue;
        if (y.storage.find("declaration") != std::string::npos && y.kind != "variable") continue;   // count definitions once
        if (y.name == "main") continue;
        std::string key = y.qualified;
        if (refs.find(key) != refs.end()) continue;
        if (y.kind == "method" && (y.storage.find("constructor") != std::string::npos || y.storage.find("destructor") != std::string::npos || y.storage.find("virtual") != std::string::npos || y.storage.find("operator") != std::string::npos)) continue;
        s << "  " << refOf[i] << "  " << padRight(y.kind, 11) << " " << padRight(symbolText(y), 59) << " " << y.file << ":" << y.line << "\n";
        ++unref;
    }
    if (!unref) s << "  (none)\n";
    s << "\n";

    // entry point
    s << " Entry point\n\n";
    bool haveMain = false;
    for (size_t i = 0; i < syms.size(); ++i)
        if (syms[i].kind == "function" && syms[i].name == "main" && syms[i].storage.find("definition") != std::string::npos) {
            s << "  " << refOf[i] << "  " << symbolText(syms[i]) << "  " << syms[i].file << ":" << syms[i].line << "\n";
            haveMain = true;
        }
    if (!haveMain) s << "  (no main() found: library or incomplete input)\n";
    return s.str();
}

std::string renderSymbolsText(const CodeBase& cb)
{
    std::vector<Symbol> syms;
    collect(cb, syms);
    std::ostringstream s;
    s << "# cppparse symbol table: " << cb.title() << "  (" << syms.size() << " symbols)\n";
    s << "# kind\tqualified name\ttype or signature\tstorage\taccess\tfile:line\tscope\tdefined at\tdeclared at\textra\n";
    for (size_t i = 0; i < syms.size(); ++i) {
        const Symbol& y = syms[i];
        s << y.kind << "\t" << y.qualified << "\t" << (y.signature.empty() ? y.type : y.signature) << "\t" << y.storage << "\t" << y.access
          << "\t" << y.file << (y.line ? ":" + intToString(y.line) : "") << "\t" << y.scope << "\t" << y.definedAt << "\t" << y.declaredAt << "\t" << y.extra << "\n";
    }
    return s.str();
}

} // namespace cppparse
