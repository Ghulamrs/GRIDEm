// Types.cpp - interned types and declaration-text parsing.
#include "Types.hpp"
#include "Instantiate.hpp"
#include "PathUtil.hpp"

#include <cctype>
#include <cstdlib>

namespace cppparse {

Type::Type() : kind(Unknown), isConst(false), isUnsigned(false), inner(0), count(0), cls(0) {}

int Type::rank() const
{
    switch (kind) {
    case Bool: return 0;
    case Char: return 1;
    case Short: return 2;
    case Enum: return 3;
    case Int: return 3;
    case Long: return 4;
    case LongLong: return 5;
    case Float: return 6;
    case Double: return 7;
    default: return -1;
    }
}

int Type::builtinSize() const
{
    switch (kind) {
    case Void: return 0;
    case Bool: case Char: return 1;
    case Short: return 2;
    case Int: case Enum: case Float: return 4;
    case Long: return 4;
    case LongLong: case Double: return 8;
    case Pointer: case Reference: case Function: case Null: return 8;
    case Array: return inner ? inner->builtinSize() * count : 0;
    default: return 0;
    }
}

std::string Type::str() const
{
    std::string s;
    switch (kind) {
    case Void: s = "void"; break;
    case Bool: s = "bool"; break;
    case Char: s = isUnsigned ? "unsigned char" : "char"; break;
    case Short: s = isUnsigned ? "unsigned short" : "short"; break;
    case Int: s = isUnsigned ? "unsigned int" : "int"; break;
    case Long: s = isUnsigned ? "unsigned long" : "long"; break;
    case LongLong: s = isUnsigned ? "unsigned long long" : "long long"; break;
    case Float: s = "float"; break;
    case Double: s = "double"; break;
    case Pointer: s = (inner ? inner->str() : "?") + "*"; break;
    case Reference: s = (inner ? inner->str() : "?") + "&"; break;
    case Array: s = (inner ? inner->str() : "?") + "[" + (count ? intToString(count) : "") + "]"; break;
    case Class: s = cls ? cls->qualifiedName : name; break;
    case Enum: s = "enum " + name; break;
    case Function: s = (inner ? inner->str() : "?") + " (*)()"; break;
    case Null: s = "null"; break;
    default: s = name.empty() ? "<unknown>" : name; break;
    }
    if (isConst) s = (kind == Pointer || kind == Function) ? s + " const" : "const " + s;
    return s;
}

std::string Type::key() const
{
    return str() + "|" + intToString(static_cast<int>(kind)) + (cls ? "@" + intToString(static_cast<int>(reinterpret_cast<size_t>(cls) & 0xFFFFFF)) : "");
}

// ---- table --------------------------------------------------------------------

TypeTable::TypeTable(const CodeBase& cb) : m_cb(cb), m_depth(0), m_instantiator(0)
{
    indexTypedefsAndEnums();
}

TypeTable::~TypeTable()
{
    for (std::map<std::string, Type*>::iterator it = m_types.begin(); it != m_types.end(); ++it) delete it->second;
}

const Type* TypeTable::intern(const Type& t)
{
    Lock l(semanticMutex());
    std::string k = t.key();
    std::map<std::string, Type*>::iterator it = m_types.find(k);
    if (it != m_types.end()) return it->second;
    Type* n = new Type(t);
    m_types[k] = n;
    return n;
}

const Type* TypeTable::builtin(Type::Kind k, bool isUnsigned, bool isConst)
{
    Type t;
    t.kind = k;
    t.isUnsigned = isUnsigned && (k == Type::Char || k == Type::Short || k == Type::Int || k == Type::Long || k == Type::LongLong);
    t.isConst = isConst;
    return intern(t);
}

const Type* TypeTable::nullType() { Type t; t.kind = Type::Null; return intern(t); }

const Type* TypeTable::pointer(const Type* to, bool isConst)
{
    Type t; t.kind = Type::Pointer; t.inner = to; t.isConst = isConst; return intern(t);
}

const Type* TypeTable::reference(const Type* to)
{
    if (to && to->kind == Type::Reference) return to;
    Type t; t.kind = Type::Reference; t.inner = to; return intern(t);
}

const Type* TypeTable::array(const Type* elem, int count)
{
    Type t; t.kind = Type::Array; t.inner = elem; t.count = count; return intern(t);
}

const Type* TypeTable::classType(const ClassInfo* cls, bool isConst)
{
    Type t; t.kind = Type::Class; t.cls = cls; t.name = cls ? cls->qualifiedName : "?"; t.isConst = isConst; return intern(t);
}

const Type* TypeTable::enumType(const std::string& name, bool isConst)
{
    Type t; t.kind = Type::Enum; t.name = name; t.isConst = isConst; return intern(t);
}

const Type* TypeTable::unknown(const std::string& name, bool isConst)
{
    Type t; t.kind = Type::Unknown; t.name = name; t.isConst = isConst; return intern(t);
}

const Type* TypeTable::function(const Type* ret)
{
    Type t; t.kind = Type::Function; t.inner = ret; return intern(t);
}

const Type* TypeTable::withConst(const Type* t, bool c)
{
    if (!t || t->isConst == c) return t;
    if (t->kind == Type::Reference) return reference(withConst(t->inner, c));
    Type n(*t); n.isConst = c; return intern(n);
}

const Type* TypeTable::decay(const Type* t)
{
    if (!t) return t;
    if (t->kind == Type::Reference) t = t->inner;
    if (t->kind == Type::Array) return pointer(t->inner);
    return t;
}

void TypeTable::indexTypedefsAndEnums()
{
    const std::vector<SourceFile*>& files = m_cb.files();
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        for (size_t i = 0; i < sf.typedefs.size(); ++i) {
            const TypedefInfo& td = sf.typedefs[i];
            // "typedef <target> <name>" -> target
            std::string text = td.text;
            if (startsWith(text, "typedef ")) text = text.substr(8);
            size_t pos = text.rfind(td.name);
            if (pos != std::string::npos) text = trim(text.substr(0, pos)) + trim(text.substr(pos + td.name.size()));
            if (!td.scope.empty()) m_typedefs[td.scope + "::" + td.name] = text;
            if (!m_typedefs.count(td.name)) m_typedefs[td.name] = text;
        }
        for (size_t i = 0; i < sf.enums.size(); ++i) {
            const EnumInfo& e = sf.enums[i];
            std::string qn = e.scope.empty() ? e.name : e.scope + "::" + e.name;
            m_enums[e.name] = e.scope;
            m_enums[qn] = e.scope;
            long value = 0;
            for (size_t k = 0; k < e.enumerators.size(); ++k) {
                std::string n = e.enumerators[k];
                size_t eq = n.find('=');
                if (eq != std::string::npos) { value = std::strtol(trim(n.substr(eq + 1)).c_str(), 0, 0); n = trim(n.substr(0, eq)); }
                m_enumerators[trim(n)] = std::make_pair(e.name, value);
                ++value;
            }
        }
        for (size_t c = 0; c < sf.allClasses.size(); ++c) {
            const ClassInfo& cls = *sf.allClasses[c];
            for (size_t i = 0; i < cls.typedefs.size(); ++i) {
                const TypedefInfo& td = cls.typedefs[i];
                std::string text = td.text;
                if (startsWith(text, "typedef ")) text = text.substr(8);
                size_t pos = text.rfind(td.name);
                if (pos != std::string::npos) text = trim(text.substr(0, pos)) + trim(text.substr(pos + td.name.size()));
                m_typedefs[cls.qualifiedName + "::" + td.name] = text;
                m_typedefs[cls.name + "::" + td.name] = text;
            }
            for (size_t i = 0; i < cls.enums.size(); ++i) {
                const EnumInfo& e = cls.enums[i];
                m_enums[cls.qualifiedName + "::" + e.name] = cls.qualifiedName;
                m_enums[cls.name + "::" + e.name] = cls.qualifiedName;
                long value = 0;
                for (size_t k = 0; k < e.enumerators.size(); ++k) {
                    std::string n = e.enumerators[k];
                    size_t eq = n.find('=');
                    if (eq != std::string::npos) { value = std::strtol(trim(n.substr(eq + 1)).c_str(), 0, 0); n = trim(n.substr(0, eq)); }
                    m_enumerators[cls.qualifiedName + "::" + trim(n)] = std::make_pair(e.name, value);
                    ++value;
                }
            }
        }
    }
}

bool TypeTable::findEnumerator(const std::string& name, const ClassInfo* owner, std::string& enumName, long& value) const
{
    Lock l(semanticMutex());
    for (const ClassInfo* c = owner; c; c = c->owner) {
        std::map<std::string, std::pair<std::string, long> >::const_iterator it = m_enumerators.find(c->qualifiedName + "::" + name);
        if (it != m_enumerators.end()) { enumName = it->second.first; value = it->second.second; return true; }
        for (size_t b = 0; b < c->bases.size(); ++b) {
            if (!c->bases[b].resolved) continue;
            if (findEnumerator(name, c->bases[b].resolved, enumName, value)) return true;
        }
    }
    std::map<std::string, std::pair<std::string, long> >::const_iterator it = m_enumerators.find(name);
    if (it != m_enumerators.end()) { enumName = it->second.first; value = it->second.second; return true; }
    return false;
}

bool TypeTable::isTypeName(const std::string& name, const std::string& scope, const ClassInfo* owner)
{
    Lock l(semanticMutex());
    static const char* const builtins[] = { "void", "bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed",
                                            "wchar_t", "size_t", "ptrdiff_t", "__int64", "__int32", "intptr_t", "uintptr_t", "ssize_t",
                                            "time_t", "clock_t", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
                                            "int64_t", "uint64_t", 0 };
    for (int i = 0; builtins[i]; ++i) if (name == builtins[i]) return true;
    std::string n = stripTemplateArgs(name);
    if (m_typedefs.count(n) || m_enums.count(n)) return true;
    if (owner) {
        for (const ClassInfo* c = owner; c; c = c->owner) {
            if (m_typedefs.count(c->qualifiedName + "::" + n) || m_enums.count(c->qualifiedName + "::" + n)) return true;
            for (size_t k = 0; k < c->nested.size(); ++k) if (c->nested[k]->name == n) return true;
        }
    }
    return m_cb.findClass(owner ? owner->qualifiedName : scope, n) != 0;
}

namespace {

struct Word { std::string text; bool punct; };

void splitType(const std::string& text, std::vector<Word>& out)
{
    std::string cur;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') { cur += c; continue; }
        if (!cur.empty()) { Word w; w.text = cur; w.punct = false; out.push_back(w); cur.clear(); }
        if (c == '*' || c == '&' || c == '[' || c == ']' || c == '(' || c == ')' || c == '<' || c == '>' || c == ',') {
            Word w; w.text = std::string(1, c); w.punct = true; out.push_back(w);
        } else if (isdigit(static_cast<unsigned char>(c))) {
            cur += c;
        }
    }
}

} // namespace

const Type* TypeTable::parse(const std::string& text, const std::string& scope, const ClassInfo* owner)
{
    // parse reads the typedef/enum maps and may instantiate templates: serialised (the lock is recursive)
    Lock l(semanticMutex());
    if (m_depth > 16) return unknown(text);
    ++m_depth;
    // template arguments of the base type, taken from the raw text so that expressions
    // such as "R - 1" survive
    std::string rawArgs;
    {
        size_t lt = text.find('<');
        if (lt != std::string::npos) {
            int d = 0;
            for (size_t k = lt; k < text.size(); ++k) {
                if (text[k] == '<') ++d;
                else if (text[k] == '>') { --d; if (d == 0) { rawArgs = text.substr(lt + 1, k - lt - 1); break; } }
            }
        }
    }
    std::vector<Word> words;
    splitType(text, words);

    bool isConst = false, isUnsigned = false, sawSigned = false;
    int longs = 0;
    Type::Kind kind = Type::Unknown;
    std::string name;
    std::string templateArgs;
    size_t i = 0;
    bool haveBase = false;

    for (; i < words.size(); ++i) {
        const Word& w = words[i];
        if (w.punct) break;
        const std::string& t = w.text;
        if (t == "const") { isConst = true; continue; }
        if (t == "volatile" || t == "struct" || t == "class" || t == "union" || t == "enum" || t == "typename" || t == "static" ||
            t == "mutable" || t == "register" || t == "extern" || t == "inline" || t == "virtual" || t == "explicit") continue;
        if (t == "unsigned") { isUnsigned = true; if (!haveBase) { kind = Type::Int; haveBase = true; } continue; }
        if (t == "signed") { sawSigned = true; if (!haveBase) { kind = Type::Int; haveBase = true; } continue; }
        if (t == "long") { ++longs; kind = longs > 1 ? Type::LongLong : Type::Long; haveBase = true; continue; }
        if (t == "int") { if (!haveBase || kind == Type::Int) kind = Type::Int; haveBase = true; continue; }
        if (t == "void") { kind = Type::Void; haveBase = true; continue; }
        if (t == "bool") { kind = Type::Bool; haveBase = true; continue; }
        if (t == "char") { kind = Type::Char; haveBase = true; continue; }
        if (t == "short") { kind = Type::Short; haveBase = true; continue; }
        if (t == "float") { kind = Type::Float; haveBase = true; continue; }
        if (t == "double") { kind = longs ? Type::Double : Type::Double; haveBase = true; continue; }
        if (t == "wchar_t") { kind = Type::Short; isUnsigned = true; haveBase = true; continue; }
        if (t == "size_t" || t == "uintptr_t" || t == "uint64_t") { kind = Type::Long; isUnsigned = true; haveBase = true; continue; }
        if (t == "ptrdiff_t" || t == "intptr_t" || t == "ssize_t" || t == "time_t" || t == "clock_t" || t == "int64_t") { kind = Type::Long; haveBase = true; continue; }
        if (t == "int32_t") { kind = Type::Int; haveBase = true; continue; }
        if (t == "uint32_t") { kind = Type::Int; isUnsigned = true; haveBase = true; continue; }
        if (t == "int16_t") { kind = Type::Short; haveBase = true; continue; }
        if (t == "uint16_t") { kind = Type::Short; isUnsigned = true; haveBase = true; continue; }
        if (t == "int8_t") { kind = Type::Char; haveBase = true; continue; }
        if (t == "uint8_t") { kind = Type::Char; isUnsigned = true; haveBase = true; continue; }
        if (t == "__int64") { kind = Type::LongLong; haveBase = true; continue; }
        if (t == "__int32") { kind = Type::Int; haveBase = true; continue; }
        if (isMacroLike(t)) continue;   // SAMPLE_API and friends
        if (haveBase && kind != Type::Unknown) break;   // "int x" style trailing identifiers are not part of the type
        name = t;
        haveBase = true;
        // template arguments: kept as text for instantiation
        if (i + 1 < words.size() && words[i + 1].text == "<") {
            int d = 0;
            for (++i; i < words.size(); ++i) {
                if (words[i].text == "<") { ++d; if (d > 1) templateArgs += "<"; continue; }
                if (words[i].text == ">") { --d; if (d == 0) break; templateArgs += ">"; continue; }
                if (!templateArgs.empty() && !words[i].punct && templateArgs[templateArgs.size() - 1] != '<' && templateArgs[templateArgs.size() - 1] != ' ') templateArgs += " ";
                templateArgs += words[i].text;
                if (words[i].text == ",") templateArgs += " ";
            }
            if (!rawArgs.empty()) templateArgs = rawArgs;
        }
        ++i;
        break;
    }
    (void)sawSigned;

    const Type* base = 0;
    if (!name.empty()) {
        if (name.compare(0, 2, "::") == 0) name = name.substr(2);
        std::string lookupScope = owner ? owner->qualifiedName : scope;
        // typedef?
        std::string target;
        bool isTypedef = false;
        for (const ClassInfo* c = owner; c && !isTypedef; c = c->owner) {
            std::map<std::string, std::string>::const_iterator it = m_typedefs.find(c->qualifiedName + "::" + name);
            if (it != m_typedefs.end()) { target = it->second; isTypedef = true; }
            for (size_t b = 0; b < c->bases.size() && !isTypedef; ++b)
                if (c->bases[b].resolved) {
                    it = m_typedefs.find(c->bases[b].resolved->qualifiedName + "::" + name);
                    if (it != m_typedefs.end()) { target = it->second; isTypedef = true; }
                }
        }
        if (!isTypedef) {
            std::map<std::string, std::string>::const_iterator it = m_typedefs.find(name);
            if (it == m_typedefs.end() && !scope.empty()) it = m_typedefs.find(scope + "::" + name);
            if (it != m_typedefs.end()) { target = it->second; isTypedef = true; }
        }
        if (isTypedef && trim(target) != name && !target.empty()) {
            base = parse(target, scope, owner);
        } else if (m_enums.count(name) || (owner && m_enums.count(owner->qualifiedName + "::" + name))) {
            base = enumType(lastComponent(name));
        } else {
            const ClassInfo* cls = m_cb.findClass(lookupScope, name);
            if (cls && cls->tmpl.isTemplate && cls->isDefinition && cls->tmpl.specializationArgs.empty()) {
                // a class template: instantiate with the written arguments, or, inside the
                // template's own instantiation, refer to the current instantiation
                const ClassInfo* inst = 0;
                bool insideTemplate = false;   // the injected class name inside the template itself
                for (const ClassInfo* o = owner; o; o = o->owner) if (o == cls) insideTemplate = true;
                if (!templateArgs.empty() && m_instantiator) inst = m_instantiator->instantiateClass(cls, templateArgs, *this);
                if (!inst && templateArgs.empty())
                    for (const ClassInfo* o = owner; o; o = o->owner)
                        if (startsWith(o->name, cls->name + "<")) { inst = o; break; }
                if (!inst && templateArgs.empty() && !insideTemplate && m_instantiator) inst = m_instantiator->instantiateClass(cls, "", *this);   // all defaulted?
                base = inst ? classType(inst) : classType(cls);
            }
            else if (cls) base = classType(cls);
            else base = unknown(stripTemplateArgs(name));
        }
        base = withConst(base, isConst);
        isConst = false;
    } else if (haveBase) {
        base = builtin(kind, isUnsigned, isConst);
        isConst = false;
    } else {
        base = unknown(trim(text).empty() ? "<none>" : text);
    }

    // declarator suffixes: * & [N] (*)(...)
    const Type* t = base;
    for (; i < words.size(); ++i) {
        const Word& w = words[i];
        if (w.text == "*") { bool c = (i + 1 < words.size() && words[i + 1].text == "const"); t = pointer(t, c); if (c) ++i; continue; }
        if (w.text == "&") { t = reference(t); continue; }
        if (w.text == "[") {
            int count = 0;
            if (i + 1 < words.size() && !words[i + 1].punct) count = std::atoi(words[i + 1].text.c_str());
            t = array(t, count);
            while (i < words.size() && words[i].text != "]") ++i;
            continue;
        }
        if (w.text == "(" && i + 1 < words.size() && words[i + 1].text == "*") { t = function(t); break; }
        if (w.text == "const") { t = withConst(t, true); continue; }
        if (!w.punct) continue;   // a declarator name
    }
    --m_depth;
    return t;
}

} // namespace cppparse
