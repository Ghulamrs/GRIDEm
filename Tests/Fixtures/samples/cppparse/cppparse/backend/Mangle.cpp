// Mangle.cpp - version 5: MSVC x64 name decoration. See Mangle.hpp.
#include "Mangle.hpp"
#include "PathUtil.hpp"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace cppparse {

namespace {

class Mangler {
public:
    explicit Mangler(TypeTable& types) : m_types(types) {}

    std::string function(const FunctionInfo& f);
    std::string variable(const VariableInfo& v);

private:
    // a name component with the back-reference table: "Circle" -> "Circle@" or "1"
    std::string name(const std::string& component);
    // "geom::Circle" -> "Circle@geom@" (innermost first), without the terminating '@'
    std::string scopes(const std::string& qualified);
    std::string templateName(const std::string& base, const std::string& args);
    std::string templateArg(const std::string& arg);
    std::string type(const Type* t, bool topLevelCv);
    std::string paramType(const Type* t);
    std::string classType(const Type* t);
    std::string number(long v);
    std::string operatorCode(const std::string& name, bool& ok);
    const Type* parse(const std::string& text, const std::string& scope, const ClassInfo* owner) { return m_types.parse(text, scope, owner); }

    TypeTable&               m_types;
    std::vector<std::string> m_names;      // back-reference table for name components (10 entries)
    std::vector<std::string> m_argTypes;   // back-reference table for parameter types (10 entries)
};

void splitQualified(const std::string& qualified, std::vector<std::string>& parts)
{
    // splits on "::" outside template brackets
    int depth = 0;
    std::string cur;
    for (size_t i = 0; i < qualified.size(); ++i) {
        char c = qualified[i];
        if (c == '<') ++depth;
        else if (c == '>') --depth;
        if (depth == 0 && c == ':' && i + 1 < qualified.size() && qualified[i + 1] == ':') { if (!cur.empty()) parts.push_back(cur); cur.clear(); ++i; continue; }
        cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
}

std::string Mangler::number(long v)
{
    // 0 -> "A@", 1..10 -> "0".."9", larger -> hexadecimal digits A..P followed by '@'; negative gets '?'
    std::string out;
    if (v < 0) { out += '?'; v = -v; }
    if (v == 0) return out + "A@";
    if (v <= 10) { out += static_cast<char>('0' + (v - 1)); return out; }
    std::string hex;
    unsigned long u = static_cast<unsigned long>(v);
    while (u) { hex.insert(hex.begin(), static_cast<char>('A' + (u & 15))); u >>= 4; }
    return out + hex + "@";
}

std::string Mangler::name(const std::string& component)
{
    for (size_t i = 0; i < m_names.size(); ++i)
        if (m_names[i] == component) return std::string(1, static_cast<char>('0' + i));
    if (m_names.size() < 10) m_names.push_back(component);
    return component + "@";
}

std::string Mangler::templateArg(const std::string& argText)
{
    std::string a = trim(argText);
    if (a.empty()) return "";
    bool numeric = (isdigit(static_cast<unsigned char>(a[0])) || (a[0] == '-' && a.size() > 1));
    if (numeric) return "$0" + number(atol(a.c_str()));
    if (a == "true") return "$00";
    if (a == "false") return "$0A@";
    const Type* t = parse(a, "", 0);
    if (t && !t->isUnknown()) return type(t, false);
    return "V" + a + "@@";   // unknown name: assume a class
}

std::string Mangler::templateName(const std::string& base, const std::string& args)
{
    // the whole "?$Buffer@H$03@" is one name for the outer table; the arguments use a fresh table
    std::vector<std::string> savedNames = m_names, savedArgs = m_argTypes;
    m_names.clear(); m_argTypes.clear();
    std::string enc = "?$" + base + "@";
    std::vector<std::string> list;
    int depth = 0;
    std::string cur;
    for (size_t i = 0; i < args.size(); ++i) {
        char c = args[i];
        if (c == '<' || c == '(') ++depth;
        else if (c == '>' || c == ')') --depth;
        if (c == ',' && depth == 0) { list.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!trim(cur).empty()) list.push_back(cur);
    for (size_t i = 0; i < list.size(); ++i) enc += templateArg(list[i]);
    enc += "@";                                   // ends the argument list and terminates the name
    m_names = savedNames; m_argTypes = savedArgs;
    for (size_t i = 0; i < m_names.size(); ++i)
        if (m_names[i] == enc) return std::string(1, static_cast<char>('0' + i));
    if (m_names.size() < 10) m_names.push_back(enc);
    return enc;
}

std::string Mangler::scopes(const std::string& qualified)
{
    std::vector<std::string> parts;
    splitQualified(qualified, parts);
    std::string out;
    for (size_t i = parts.size(); i > 0; --i) {
        const std::string& p = parts[i - 1];
        size_t lt = p.find('<');
        if (lt != std::string::npos && p[p.size() - 1] == '>') out += templateName(p.substr(0, lt), p.substr(lt + 1, p.size() - lt - 2));
        else if (p == "(anonymous)") out += "?A0x00000000@";
        else out += name(p);
    }
    return out;
}

std::string Mangler::classType(const Type* t)
{
    const ClassInfo* c = t->cls;
    std::string qualified = c ? c->qualifiedName : t->name;
    char letter = 'V';
    if (c && c->kind == ClassInfo::Struct) letter = 'U';
    else if (c && c->kind == ClassInfo::Union) letter = 'T';
    return letter + scopes(qualified) + "@";
}

std::string Mangler::type(const Type* t, bool topLevelCv)
{
    if (!t) return "X";
    std::string cv = topLevelCv && t->isConst ? "B" : "A";
    switch (t->kind) {
    case Type::Void:     return "X";
    case Type::Bool:     return "_N";
    case Type::Char:     return t->isUnsigned ? "E" : "D";
    case Type::Short:    return t->isUnsigned ? "G" : "F";
    case Type::Int:      return t->isUnsigned ? "I" : "H";
    case Type::Long:     return t->isUnsigned ? "K" : "J";
    case Type::LongLong: return t->isUnsigned ? "_K" : "_J";
    case Type::Float:    return "M";
    case Type::Double:   return "N";
    case Type::Pointer: case Type::Null: {
        const Type* in = t->inner;
        if (in && in->kind == Type::Function) {
            // the type table keeps only the return type of a function type: the parameter list is
            // mangled as "unknown" (void), which is enough for the symbol to be unique within a module
            return "P6A" + type(in->inner, false) + "XZ";
        }
        std::string pcv = in && in->isConst ? "B" : "A";
        return std::string(t->isConst ? "Q" : "P") + "E" + pcv + type(in, false);
    }
    case Type::Reference: {
        const Type* in = t->inner;
        std::string pcv = in && in->isConst ? "B" : "A";
        return "AE" + pcv + type(in, false);
    }
    case Type::Array: {   // decays to a pointer in parameters
        std::string pcv = t->inner && t->inner->isConst ? "B" : "A";
        return "PE" + pcv + type(t->inner, false);
    }
    case Type::Class:    return classType(t);
    case Type::Enum:     return "W4" + scopes(t->name) + "@";
    case Type::Function: return "$$A6A" + type(t->inner, false) + "XZ";
    default: break;
    }
    (void)cv;
    return "V" + scopes(t->name.empty() ? std::string("unknown") : t->name) + "@";   // unknown: treat as a class
}

std::string Mangler::paramType(const Type* t)
{
    std::string enc = type(t, false);
    bool primitive = enc.size() == 1 || (enc.size() == 2 && enc[0] == '_');
    if (primitive) return enc;
    for (size_t i = 0; i < m_argTypes.size(); ++i)
        if (m_argTypes[i] == enc) return std::string(1, static_cast<char>('0' + i));
    if (m_argTypes.size() < 10) m_argTypes.push_back(enc);
    return enc;
}

std::string Mangler::operatorCode(const std::string& name, bool& ok)
{
    static const char* const table[][2] = {
        {"=", "4"}, {"+", "H"}, {"-", "G"}, {"*", "D"}, {"/", "K"}, {"%", "L"}, {"==", "8"}, {"!=", "9"},
        {"<", "M"}, {">", "O"}, {"<=", "N"}, {">=", "P"}, {"[]", "A"}, {"()", "R"}, {"->", "C"}, {"++", "E"},
        {"--", "F"}, {"!", "7"}, {"|", "U"}, {"&", "I"}, {"^", "T"}, {"~", "S"}, {">>", "5"}, {"<<", "6"},
        {"+=", "Y"}, {"-=", "Z"}, {"*=", "X"}, {"/=", "_0"}, {"%=", "_1"}, {">>=", "_2"}, {"<<=", "_3"},
        {"&=", "_4"}, {"|=", "_5"}, {"^=", "_6"}, {"&&", "V"}, {"||", "W"}, {",", "Q"}, {"->*", "J"},
        {"new", "2"}, {"delete", "3"}, {"new[]", "_U"}, {"delete[]", "_V"}
    };
    std::string op = trim(name.substr(8));   // after "operator"
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (op == table[i][0]) { ok = true; return table[i][1]; }
    ok = false;
    return "";
}

std::string Mangler::function(const FunctionInfo& f)
{
    const ClassInfo* owner = f.ownerClass;
    std::string scopeText = owner ? owner->qualifiedName : f.scope;
    if (f.isExtern && !owner && f.qualifier.empty() && f.scope.empty()) return f.name;   // extern "C"
    if (!owner && f.scope.empty() && f.name == "main") return "main";

    std::string out = "?";
    // the function's own name
    std::string base = f.name;
    std::string targs;
    size_t lt = base.find('<');
    if (lt != std::string::npos && base[base.size() - 1] == '>') { targs = base.substr(lt + 1, base.size() - lt - 2); base = base.substr(0, lt); }
    if (f.isConstructor) out += "?0";          // the class name follows as the first scope
    else if (f.isDestructor) out += "?1";
    else if (f.isOperator) {
        bool ok;
        std::string code = operatorCode(f.name, ok);
        if (ok) out += "?" + code;
        else if (f.isConversion) out += "?B";
        else out += name(f.name);
    }
    else if (!targs.empty()) out += templateName(base, targs);
    else out += name(base);
    // enclosing scopes, innermost first
    out += scopes(scopeText) + "@";

    // access, kind, this-qualifier, calling convention
    if (owner && !f.isStatic) {
        char acc = f.access == AccessPrivate ? 'A' : f.access == AccessProtected ? 'I' : 'Q';
        if (f.isVirtual || f.isPureVirtual) acc = static_cast<char>(acc + 4);     // E / M / U
        out += acc;
        out += "E";                                 // __ptr64 this
        out += f.isConst ? "B" : "A";
        out += "A";                                 // __cdecl
    } else if (owner) {
        out += f.access == AccessPrivate ? "C" : f.access == AccessProtected ? "K" : "S";
        out += "A";
    } else {
        out += "YA";
    }

    // return type
    if (f.isConstructor || f.isDestructor || f.isConversion) out += "@";
    else {
        const Type* rt = parse(f.returnType.empty() ? std::string("void") : f.returnType, scopeText, owner);
        out += type(rt, false);
    }
    // parameters
    if (f.params.empty() || (f.params.size() == 1 && (f.params[0].type == "void" || f.params[0].type.empty()))) out += "X";
    else {
        for (size_t i = 0; i < f.params.size(); ++i) {
            if (f.params[i].type == "...") { out += "Z"; break; }
            const Type* pt = parse(f.params[i].type, scopeText, owner);
            out += paramType(pt);
        }
        if (f.params.back().type != "...") out += "@";
    }
    out += "Z";
    return out;
}

std::string Mangler::variable(const VariableInfo& v)
{
    std::string qualified = v.qualifier.empty() ? v.scope : (v.scope.empty() ? v.qualifier : v.scope + "::" + v.qualifier);
    if (!v.qualifier.empty() && v.qualifier.find("::") != std::string::npos) qualified = v.qualifier;
    std::string out = "?" + name(v.name) + scopes(qualified) + "@";
    // 0/1/2: private/protected/public static data member, 3: global
    if (!v.qualifier.empty()) out += v.access == AccessPrivate ? "0" : v.access == AccessProtected ? "1" : "2";
    else out += "3";
    const Type* t = parse(v.type, qualified, 0);
    std::string enc = type(t, false);
    if (t && t->kind == Type::Array) enc = "PE" + std::string(t->inner && t->inner->isConst ? "B" : "A") + type(t->inner, false);
    out += enc;
    out += t && t->isConst ? "B" : "A";
    return out;
}

} // namespace

std::string msvcMangleFunction(const FunctionInfo& f, TypeTable& types)
{
    Mangler m(types);
    return m.function(f);
}

std::string msvcMangleVariable(const VariableInfo& v, TypeTable& types)
{
    Mangler m(types);
    return m.variable(v);
}

std::string msvcMangleVtable(const std::string& qualifiedName)
{
    std::vector<std::string> parts;
    splitQualified(qualifiedName, parts);
    std::string out = "??_7";
    for (size_t i = parts.size(); i > 0; --i) out += parts[i - 1] + "@";
    return out + "@6B@";
}

} // namespace cppparse
