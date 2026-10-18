// CxxParser.cpp - version 6: C++98 recursive-descent parser. See CxxParser.hpp.
#include "CxxParser.hpp"

#include <cstdlib>
#include <sstream>

namespace cppparse {
namespace cxx {

namespace {

const char* const kKeywords[] = {
    "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const", "const_cast", "continue", "default", "delete", "do",
    "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if",
    "inline", "int", "long", "mutable", "namespace", "new", "operator", "private", "protected", "public", "register",
    "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_cast", "struct", "switch", "template", "this",
    "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
    "wchar_t", "while", 0
};

const char* const kTypeKeywords[] = { "char", "short", "int", "long", "signed", "unsigned", "float", "double", "void", "bool", "wchar_t", 0 };

const char* const kDeclSpecKeywords[] = { "typedef", "static", "extern", "mutable", "register", "auto", "friend", "virtual", "explicit", "inline", "const", "volatile", 0 };

const char* const kOperatorNames[] = {
    "new", "delete", "+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<", ">", "+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=",
    "<<", ">>", ">>=", "<<=", "==", "!=", "<=", ">=", "&&", "||", "++", "--", ",", "->*", "->", "()", "[]", 0
};

std::string numText(int v) { std::ostringstream s; s << v; return s.str(); }

} // namespace

bool Parser::isKeyword(const std::string& s)
{
    for (int i = 0; kKeywords[i]; ++i) if (s == kKeywords[i]) return true;
    return false;
}

bool Parser::isTypeKeyword(const std::string& s)
{
    for (int i = 0; kTypeKeywords[i]; ++i) if (s == kTypeKeywords[i]) return true;
    return false;
}

bool Parser::isDeclSpecKeyword(const std::string& s)
{
    for (int i = 0; kDeclSpecKeywords[i]; ++i) if (s == kDeclSpecKeywords[i]) return true;
    return false;
}

Parser::Parser(const std::vector<PpToken>& tokens, CxxArena& arena)
    : m_toks(tokens), m_arena(arena), m_pos(0), m_tentative(0), m_errors(0), m_templateDepth(0), m_noGreater(0)
{
    PpToken end;
    end.kind = PpToken::End;
    end.text = "";
    if (!m_toks.empty()) { end.file = m_toks.back().file; end.line = m_toks.back().line; }
    m_toks.push_back(end);
    pushScope("", false);
}


void Parser::error(const std::string& msg)
{
    if (m_tentative > 0) return;
    ++m_errors;
    ParseDiag d;
    d.file = cur().file; d.line = cur().line;
    d.message = msg + (cur().kind == PpToken::End ? std::string(" here end of input") : " near '" + cur().text + "'");
    m_diags.push_back(d);
}

bool Parser::expect(const char* s, const char* context)
{
    if (accept(s)) return true;
    error(std::string("expected '") + s + "' " + context);
    return false;
}

void Parser::skipTo(const char* a, const char* b)
{
    int depth = 0;
    while (!atEnd()) {
        const std::string& t = cur().text;
        if (cur().kind == PpToken::Punct) {
            if (t == "(" || t == "[" || t == "{") { ++depth; advance(); continue; }
            if (t == ")" || t == "]") { if (depth > 0) --depth; advance(); continue; }
            if (t == "}") { if (depth > 0) { --depth; advance(); continue; } if (std::string(b) == "}") return; advance(); continue; }
            if (depth == 0 && (t == a || t == b)) { if (t == ";") advance(); return; }
        }
        advance();
    }
}

void Parser::skipBalanced()
{
    int depth = 0;
    do {
        const std::string& t = cur().text;
        if (cur().kind == PpToken::Punct && (t == "(" || t == "[" || t == "{")) ++depth;
        else if (cur().kind == PpToken::Punct && (t == ")" || t == "]" || t == "}")) --depth;
        advance();
    } while (depth > 0 && !atEnd());
}

int Parser::declspecArguments()
{
    if (!at("(")) return 0;
    int align = 0;
    const size_t start = m_pos;
    skipBalanced();
    // __declspec( align(N) ) among other attributes such as dllimport or noinline
    for (size_t p = start + 1; p + 3 < m_pos; ++p)
        if (m_toks[p].kind == PpToken::Ident && m_toks[p].text == "align" && m_toks[p + 1].is("(") && m_toks[p + 2].kind == PpToken::Number && m_toks[p + 3].is(")")) {
            int n = std::atoi(m_toks[p + 2].text.c_str());
            if (n >= 1 && n <= 8192 && (n & (n - 1)) == 0) align = n;
            else error("__declspec(align(" + m_toks[p + 2].text + ")): the alignment must be a power of two from 1 to 8192");
        }
    return align;
}


void Parser::pushScope(const std::string& name, bool isClass)
{
    Scope s;
    s.name = name;
    s.isClass = isClass;
    if (!name.empty()) {
        std::string q = m_qualifierStack.empty() ? name : m_qualifierStack.back() + "::" + name;
        m_qualifierStack.push_back(q);
        NamedScope& ns = m_named[q];             // a reopened namespace knows the names of its earlier parts
        s.types = ns.types; s.templates = ns.templates; s.namespaces = ns.namespaces;
    }
    m_scopes.push_back(s);
}

void Parser::popScope()
{
    if (m_scopes.empty()) return;
    if (!m_scopes.back().name.empty() && !m_qualifierStack.empty()) m_qualifierStack.pop_back();
    m_scopes.pop_back();
}

void Parser::declareType(const std::string& name)
{
    if (name.empty() || m_scopes.empty()) return;
    m_scopes.back().types.insert(name);
    if (!m_scopes.back().name.empty() && !m_qualifierStack.empty()) m_named[m_qualifierStack.back()].types.insert(name);
}

void Parser::declareTemplate(const std::string& name)
{
    if (name.empty() || m_scopes.empty()) return;
    m_scopes.back().templates.insert(name);
    if (!m_scopes.back().name.empty() && !m_qualifierStack.empty()) m_named[m_qualifierStack.back()].templates.insert(name);
}

void Parser::declareNamespace(const std::string& name)
{
    if (name.empty() || m_scopes.empty()) return;
    m_scopes.back().namespaces.insert(name);
    if (!m_scopes.back().name.empty() && !m_qualifierStack.empty()) m_named[m_qualifierStack.back()].namespaces.insert(name);
}

void Parser::declareOther(const std::string& name)
{
    if (name.empty() || m_scopes.empty()) return;
    m_scopes.back().others.insert(name);
}

std::string Parser::currentClassName() const
{
    for (size_t i = m_scopes.size(); i > 0; --i) if (m_scopes[i - 1].isClass) return m_scopes[i - 1].name;
    return "";
}

// the scope a qualifier (the first 'parts' components of n) denotes, or 0 when unknown
const Parser::NamedScope* Parser::findNamedScope(const QualifiedName& n, size_t parts) const
{
    std::string rel;
    for (size_t i = 0; i < parts; ++i) { if (i) rel += "::"; rel += n.parts[i].ident; }
    if (n.global) {
        std::map<std::string, NamedScope>::const_iterator it = m_named.find(rel);
        return it == m_named.end() ? 0 : &it->second;
    }
    // relative to each enclosing named scope, innermost first, then global
    for (size_t q = m_qualifierStack.size(); q > 0; --q) {
        std::map<std::string, NamedScope>::const_iterator it = m_named.find(m_qualifierStack[q - 1] + "::" + rel);
        if (it != m_named.end()) return &it->second;
    }
    std::map<std::string, NamedScope>::const_iterator it = m_named.find(rel);
    return it == m_named.end() ? 0 : &it->second;
}

bool Parser::isTypeName(const QualifiedName& n) const
{
    if (n.parts.empty()) return false;
    const NameComponent& last = n.parts.back();
    if (last.convType || last.ident.empty() || last.ident[0] == '~' || last.ident.compare(0, 8, "operator") == 0) return false;
    if (n.parts.size() == 1 && !n.global) {
        for (size_t i = m_scopes.size(); i > 0; --i) {
            const Scope& s = m_scopes[i - 1];
            if (s.types.count(last.ident)) return true;      // class templates are in 'types' too; function templates only in 'templates'
        }
        return false;
    }
    // a constructor / destructor name is never a type: "Point::Point"
    if (n.parts.size() >= 2 && n.parts[n.parts.size() - 2].ident == last.ident) return false;
    const NamedScope* ns = findNamedScope(n, n.parts.size() - 1);
    if (ns) return ns->types.count(last.ident) != 0;
    // the qualifier is unknown (another translation unit, a library header, a dependent name): say yes
    // when the syntax calls for a type - the callers only ask in that situation
    return true;
}

bool Parser::isTemplateName(const QualifiedName& n) const
{
    if (n.parts.empty()) return false;
    const std::string& id = n.parts.back().ident;
    if (n.parts.size() == 1 && !n.global) {
        // the innermost scope that declares the name decides: a local variable, a parameter or a function
        // hides a template of the same name further out ("int count = 0; count < n" after <algorithm>)
        for (size_t i = m_scopes.size(); i > 0; --i) {
            if (m_scopes[i - 1].templates.count(id)) return true;
            if (m_scopes[i - 1].others.count(id)) return false;
        }
        return false;
    }
    const NamedScope* ns = findNamedScope(n, n.parts.size() - 1);
    return ns ? ns->templates.count(id) != 0 : false;
}

bool Parser::isNamespaceName(const std::string& n) const
{
    for (size_t i = m_scopes.size(); i > 0; --i) if (m_scopes[i - 1].namespaces.count(n)) return true;
    return false;
}

// scans a qualified name starting here the current token without consuming it
bool Parser::qualifiedNameAhead(size_t& endPos, bool& hasTemplate) const
{
    size_t p = m_pos;
    hasTemplate = false;
    if (m_toks[p].text == "::" && m_toks[p].kind == PpToken::Punct) ++p;
    bool any = false;
    for (;;) {
        if (m_toks[p].kind != PpToken::Ident || isKeyword(m_toks[p].text)) break;
        ++p; any = true;
        if (m_toks[p].text == "<" && m_toks[p].kind == PpToken::Punct) {
            // skip balanced template arguments
            int depth = 0; size_t q = p;
            for (; q < m_toks.size() && m_toks[q].kind != PpToken::End; ++q) {
                const std::string& t = m_toks[q].text;
                if (m_toks[q].kind != PpToken::Punct) continue;
                if (t == "<") ++depth;
                else if (t == ">") { if (--depth == 0) { ++q; break; } }
                else if (t == ">>") { depth -= 2; if (depth <= 0) { ++q; break; } }
                else if (t == ";" || t == "{" || t == "}") { depth = -1; break; }
            }
            if (depth > 0 || depth == -1) break;
            p = q; hasTemplate = true;
        }
        if (m_toks[p].text == "::" && m_toks[p].kind == PpToken::Punct && m_toks[p + 1].kind == PpToken::Ident && !isKeyword(m_toks[p + 1].text)) { ++p; continue; }
        break;
    }
    endPos = p;
    return any;
}

bool Parser::looksLikeTypeStart() const
{
    const PpToken& t = cur();
    if (t.kind != PpToken::Ident && !(t.kind == PpToken::Punct && t.text == "::")) return false;
    if (isTypeKeyword(t.text) || t.text == "const" || t.text == "volatile" || t.text == "class" || t.text == "struct" || t.text == "union" || t.text == "enum" || t.text == "typename") return true;
    if (isKeyword(t.text)) return false;
    size_t e; bool ht;
    if (!qualifiedNameAhead(e, ht)) return false;
    // decide with the parser's knowledge
    QualifiedName n;
    size_t p = m_pos;
    if (m_toks[p].text == "::") { n.global = true; ++p; }
    while (p < e) {
        if (m_toks[p].kind == PpToken::Ident) { NameComponent c; c.ident = m_toks[p].text; n.parts.push_back(c); ++p; continue; }
        if (m_toks[p].text == "<") { n.parts.back().hasTemplateArgs = true; int depth = 0; for (; p < e; ++p) { if (m_toks[p].text == "<") ++depth; else if (m_toks[p].text == ">") { if (--depth == 0) { ++p; break; } } else if (m_toks[p].text == ">>") { depth -= 2; if (depth <= 0) { ++p; break; } } } continue; }
        ++p;
    }
    if (n.parts.empty()) return false;
    if (n.parts.size() == 1 && !n.global) return isTypeName(n);
    return isTypeName(n);
}


bool Parser::templateArguments(std::vector<TemplateArg>& args)
{
    if (!expect("<", "for template arguments")) return false;
    ++m_noGreater;
    if (!at(">") && !at(">>")) {
        for (;;) {
            TemplateArg a;
            TypeId* ty = 0;
            size_t save = m_pos;
            if (tryTypeId(ty) && (at(",") || at(">") || at(">>"))) a.type = ty;
            else {
                m_pos = save;
                Expr* e = conditional();
                if (!e) { --m_noGreater; return false; }
                a.expr = e;
            }
            args.push_back(a);
            if (accept(",")) continue;
            break;
        }
    }
    --m_noGreater;
    if (at(">>")) { m_toks[m_pos].text = ">"; return true; }      // the second '>' closes the enclosing argument list
    return expect(">", "after template arguments");
}

bool Parser::qualifiedName(QualifiedName& n, bool allowTemplateArgs, bool typeContext)
{
    n.global = accept("::");
    for (;;) {
        NameComponent c;
        Loc here = loc();
        if (accept("~")) {
            if (!atIdent()) { error("expected a class name after '~'"); return false; }
            c.ident = "~" + cur().text; advance();
        } else if (at("operator")) {
            advance();
            bool found = false;
            // operator new[] / delete[]
            if ((at("new") || at("delete")) && peek().text == "[" && peek(2).text == "]") { c.ident = "operator " + cur().text + "[]"; advance(); advance(); advance(); found = true; }
            if (!found) for (int k = 0; kOperatorNames[k]; ++k) {
                std::string op = kOperatorNames[k];
                if (op == "()" && at("(") && peek().text == ")") { c.ident = "operator()"; advance(); advance(); found = true; break; }
                if (op == "[]" && at("[") && peek().text == "]") { c.ident = "operator[]"; advance(); advance(); found = true; break; }
                if (op != "()" && op != "[]" && at(op.c_str()) && cur().kind != PpToken::Ident) { c.ident = "operator" + op; advance(); found = true; break; }
                if ((op == "new" || op == "delete") && at(op.c_str())) { c.ident = "operator " + op; advance(); found = true; break; }
            }
            if (!found) {
                // conversion function: operator type-id (no parentheses in the type)
                TypeId* t = m_arena.typeId();
                if (!declSpecifiers(t->spec, false, false) || !t->spec.hasType()) { error("bad operator name"); return false; }
                Declarator* d = m_arena.declarator();
                // only ptr-operators are allowed here
                while (at("*") || at("&")) { DeclaratorPart p; p.kind = at("*") ? DeclaratorPart::Pointer : DeclaratorPart::Reference; advance(); while (at("const") || at("volatile")) { if (at("const")) p.isConst = true; else p.isVolatile = true; advance(); } d->parts.push_back(p); }
                t->decl = d;
                c.ident = "operator";
                c.convType = t;
            }
            (void)here;
        } else if (atIdent()) {
            c.ident = cur().text; advance();
        } else {
            error("expected a name");
            return false;
        }
        if (allowTemplateArgs && at("<") && !c.convType) {
            // template arguments when the name is a template, or in a type context when the brackets balance
            QualifiedName probe = n; probe.parts.push_back(c);
            bool take = isTemplateName(probe);
            if (!take && (typeContext || !probe.parts.empty())) {
                size_t save = m_pos; size_t e; bool ht;
                // the balance scan starts here the identifier: step back one token
                --m_pos; take = qualifiedNameAhead(e, ht) && ht; m_pos = save;
                if (take && !typeContext) {
                    // in an expression an unknown name followed by balanced <...> is a template-id only when a
                    // qualifier or '(' follows the '>' (f<int>(x), A<B>::c)
                    take = e < m_toks.size() && (m_toks[e].text == "(" || m_toks[e].text == "::");
                }
            }
            if (take) { c.hasTemplateArgs = true; if (!templateArguments(c.templateArgs)) return false; }
        }
        n.parts.push_back(c);
        if (at("::") && ((peek().kind == PpToken::Ident && !isKeyword(peek().text)) || peek().text == "~" || peek().text == "operator")) { advance(); continue; }
        if (at("::") && peek().text == "*") break;      // member pointer: handled by the declarator
        break;
    }
    return true;
}

bool Parser::lessIsTemplate(const QualifiedName& n) const { return isTemplateName(n); }


bool Parser::declSpecifiers(DeclSpec& spec, bool inClass, bool allowClassDef)
{
    bool any = false;
    spec.loc = loc();
    for (;;) {
        const PpToken& t = cur();
        if (t.kind != PpToken::Ident && !(t.kind == PpToken::Punct && t.text == "::")) break;
        const std::string& w = t.text;
        if (w == "typedef") { spec.isTypedef = true; advance(); any = true; continue; }
        if (w == "static") { spec.isStatic = true; advance(); any = true; continue; }
        if (w == "extern") { if (peek().kind == PpToken::String) break; spec.isExtern = true; advance(); any = true; continue; }
        if (w == "mutable") { spec.isMutable = true; advance(); any = true; continue; }
        if (w == "register") { spec.isRegister = true; advance(); any = true; continue; }
        if (w == "auto") { spec.isAuto = true; advance(); any = true; continue; }
        if (w == "friend") { spec.isFriend = true; advance(); any = true; continue; }
        if (w == "virtual") { spec.isVirtual = true; advance(); any = true; continue; }
        if (w == "explicit") { spec.isExplicit = true; advance(); any = true; continue; }
        if (w == "inline") { spec.isInline = true; advance(); any = true; continue; }
        if (w == "const") { spec.isConst = true; advance(); any = true; continue; }
        if (w == "volatile") { spec.isVolatile = true; advance(); any = true; continue; }
        if (w == "__declspec") {                                // Microsoft extension: align(N) is kept, the rest ignored
            advance();
            int a = declspecArguments();
            if (a > spec.align) spec.align = a;
            any = true; continue;
        }
        if (w == "__cdecl" || w == "__stdcall" || w == "__fastcall" || w == "__forceinline" || w == "__inline" || w == "__restrict") {
            advance(); if (at("(")) skipBalanced(); any = true; continue;       // Microsoft extensions, ignored
        }
        if (isTypeKeyword(w)) {
            if (spec.kind == DeclSpec::Named || spec.kind == DeclSpec::Class || spec.kind == DeclSpec::Enum) break;
            spec.kind = DeclSpec::Builtin;
            if (w == "char") ++spec.builtinChar; else if (w == "int") ++spec.builtinInt; else if (w == "long") ++spec.builtinLong;
            else if (w == "short") ++spec.builtinShort; else if (w == "signed") ++spec.builtinSigned; else if (w == "unsigned") ++spec.builtinUnsigned;
            else if (w == "float") ++spec.builtinFloat; else if (w == "double") ++spec.builtinDouble; else if (w == "void") ++spec.builtinVoid;
            else if (w == "bool") ++spec.builtinBool; else if (w == "wchar_t") ++spec.builtinWchar;
            advance(); any = true; continue;
        }
        if (spec.hasType()) break;
        if (w == "class" || w == "struct" || w == "union") {
            // definition when a '{' or ': bases' follows the optional name; else an elaborated type specifier
            size_t p = m_pos + 1;
            while (m_toks[p].kind == PpToken::Ident && (m_toks[p].text == "__declspec")) { p += 1; if (m_toks[p].text == "(") { int d = 0; for (; p < m_toks.size(); ++p) { if (m_toks[p].text == "(") ++d; else if (m_toks[p].text == ")" && --d == 0) { ++p; break; } } } }
            size_t q = p;
            if (m_toks[q].text == "::") ++q;
            while (m_toks[q].kind == PpToken::Ident && !isKeyword(m_toks[q].text)) {
                ++q;
                if (m_toks[q].text == "<") { int d = 0; for (; q < m_toks.size(); ++q) { if (m_toks[q].text == "<") ++d; else if (m_toks[q].text == ">") { if (--d == 0) { ++q; break; } } else if (m_toks[q].text == ">>") { d -= 2; if (d <= 0) { ++q; break; } } else if (m_toks[q].text == ";" || m_toks[q].text == "{") break; } }
                if (m_toks[q].text == "::") { ++q; continue; }
                break;
            }
            bool definition = allowClassDef && (m_toks[q].text == "{" || m_toks[q].text == ":");
            if (definition) {
                Decl* cd = classSpecifier(spec, loc());
                if (!cd) return false;
                any = true;
                continue;
            }
            spec.elaborated = w; advance();
            if (!qualifiedName(spec.name, true, true)) return false;
            // "class X;" / "struct X* p;" declare X in the enclosing scope
            if (spec.name.parts.size() == 1 && !spec.name.global && !spec.name.parts[0].hasTemplateArgs) declareType(spec.name.parts[0].ident);
            spec.kind = DeclSpec::Named; any = true; continue;
        }
        if (w == "enum") {
            size_t p = m_pos + 1;
            if (m_toks[p].kind == PpToken::Ident && !isKeyword(m_toks[p].text)) ++p;
            if (allowClassDef && m_toks[p].text == "{") { if (!enumSpecifier(spec, loc())) return false; any = true; continue; }
            spec.elaborated = "enum"; advance();
            if (!qualifiedName(spec.name, false, true)) return false;
            spec.kind = DeclSpec::Named; any = true; continue;
        }
        if (w == "typename") {
            advance();
            spec.elaborated = "typename";
            if (!qualifiedName(spec.name, true, true)) return false;
            spec.kind = DeclSpec::Named; any = true; continue;
        }
        if (isKeyword(w)) break;
        // a (qualified) name: a type only if the parser knows it as one (or cannot know)
        size_t save = m_pos;
        QualifiedName n;
        ++m_tentative;
        bool ok = qualifiedName(n, true, true);
        --m_tentative;
        if (!ok || n.parts.empty() || !isTypeName(n)) { m_pos = save; break; }
        // "Point(int x)" inside class Point is the constructor, not a declaration of type Point
        if (n.parts.size() == 1 && !n.global && !n.parts[0].hasTemplateArgs && at("(") && n.parts[0].ident == currentClassName()) { m_pos = save; break; }
        // "T x" needs a declarator or ';' or ')' after an unknown qualified type; "a::b = 3" is an expression
        spec.kind = DeclSpec::Named; spec.name = n; any = true;
        (void)inClass;
        continue;
    }
    return any;
}


bool Parser::parameterList(DeclaratorPart& fn)
{
    // after '('
    if (accept(")")) return true;
    if (at("void") && peek().text == ")") { advance(); advance(); return true; }
    for (;;) {
        if (accept("...")) { fn.variadic = true; break; }
        Param* p = m_arena.param();
        p->loc = loc();
        if (!declSpecifiers(p->spec, false, true) || !p->spec.hasType()) { error("expected a parameter declaration"); return false; }
        Declarator* d = m_arena.declarator();
        if (!declarator(*d, true, false, true)) return false;
        p->decl = d;
        if (accept("=")) { ++m_noGreater; p->defaultValue = assignment(); --m_noGreater; if (!p->defaultValue) return false; }
        fn.params.push_back(p);
        if (accept(",")) { if (accept("...")) { fn.variadic = true; break; } continue; }
        break;
    }
    return expect(")", "after the parameter list");
}

bool Parser::declaratorSuffixes(Declarator& d, bool inParams)
{
    std::vector<DeclaratorPart> suffixes;
    for (;;) {
        if (at("[")) {
            DeclaratorPart p; p.kind = DeclaratorPart::Array;
            advance();
            if (!at("]")) { p.arraySize = conditional(); if (!p.arraySize) return false; }
            if (!expect("]", "after the array size")) return false;
            suffixes.push_back(p);
            continue;
        }
        if (at("(")) {
            // a function declarator, unless this is "T x(args)" direct-initialisation: a named declarator outside a
            // parameter list whose parenthesis does not start with a type (or ')' / 'void')
            const bool maybeInit = !inParams && !d.name.empty() && suffixes.empty() && peek().text != ")" && peek().text != "void" && peek().text != "...";
            if (maybeInit) {
                size_t save = m_pos; advance();
                bool typeStart = looksLikeTypeStart();
                m_pos = save;
                if (!typeStart) break;
            }
            DeclaratorPart p; p.kind = DeclaratorPart::Function;
            size_t parenAt = m_pos;
            size_t errs = m_diags.size();
            int errorsBefore = m_errors;
            advance();
            if (maybeInit) ++m_tentative;
            bool params = parameterList(p);
            if (maybeInit) --m_tentative;
            if (!params) {
                if (!maybeInit) return false;
                // "T x(U(1, 2), v)": the parenthesis starts with a type name but holds initializer arguments, not
                // parameters; the initializer is read after the declarator
                m_pos = parenAt; m_diags.resize(errs); m_errors = errorsBefore;
                break;
            }
            while (at("const") || at("volatile")) { if (at("const")) p.isConst = true; else p.isVolatile = true; advance(); }
            if (at("throw") && peek().text == "(") {
                advance(); advance();
                p.hasThrowSpec = true;
                while (!at(")") && !atEnd()) { TypeId* t = m_arena.typeId(); if (!typeIdAt(*t)) return false; p.throwSpec.push_back(t); if (!accept(",")) break; }
                if (!expect(")", "after the exception specification")) return false;
            }
            suffixes.push_back(p);
            continue;
        }
        break;
    }
    // suffixes bind closer to the name than the prefix operators: applied in reverse source order
    for (size_t i = suffixes.size(); i > 0; --i) d.parts.push_back(suffixes[i - 1]);
    (void)inParams;
    return true;
}

bool Parser::declarator(Declarator& d, bool abstractAllowed, bool nameRequired, bool inParams)
{
    d.loc = loc();
    std::vector<DeclaratorPart> prefix;
    // ptr-operators
    for (;;) {
        if (at("*")) { DeclaratorPart p; p.kind = DeclaratorPart::Pointer; advance(); while (at("const") || at("volatile") || at("__restrict")) { if (at("const")) p.isConst = true; else if (at("volatile")) p.isVolatile = true; advance(); } prefix.push_back(p); continue; }
        if (at("&")) { DeclaratorPart p; p.kind = DeclaratorPart::Reference; advance(); prefix.push_back(p); continue; }
        if (at("__cdecl") || at("__stdcall") || at("__fastcall") || at("__ptr64")) { advance(); continue; }
        // C::* member pointer
        if (atIdent() || at("::")) {
            size_t e; bool ht;
            if (qualifiedNameAhead(e, ht) && m_toks[e].text == "::" && m_toks[e + 1].text == "*") {
                DeclaratorPart p; p.kind = DeclaratorPart::MemberPointer;
                if (!qualifiedName(p.memberClass, true, true)) return false;
                advance(); advance();      // :: *
                while (at("const") || at("volatile")) { if (at("const")) p.isConst = true; else p.isVolatile = true; advance(); }
                prefix.push_back(p);
                continue;
            }
        }
        break;
    }
    // nested declarator or the declarator-id
    Declarator* nested = 0;
    bool haveName = false;
    if (at("(")) {
        // "( declarator )" when the contents start like a declarator; otherwise it is a parameter list of an abstract function declarator
        const PpToken& n1 = peek();
        bool nestedDecl = n1.text == "*" || n1.text == "&" || n1.text == "::" || n1.text == "__cdecl" || n1.text == "__stdcall"
                          || (n1.kind == PpToken::Ident && !isKeyword(n1.text) && (peek(2).text == ")" || peek(2).text == "[" || peek(2).text == "::") && !isTypeName(QualifiedName()) )
                          || (n1.kind == PpToken::Ident && !isKeyword(n1.text) && peek(2).text == "::" && peek(3).text == "*");
        if (nestedDecl && n1.kind == PpToken::Ident && peek(2).text == ")" ) {
            // "(x)" could also be a parameter list "(T)" of an abstract declarator: it is a parameter list when x is a type
            QualifiedName probe; NameComponent c; c.ident = n1.text; probe.parts.push_back(c);
            if (isTypeName(probe)) nestedDecl = false;
        }
        if (nestedDecl) {
            advance();
            nested = m_arena.declarator();
            if (!declarator(*nested, abstractAllowed, nameRequired, inParams)) return false;
            if (!expect(")", "after the nested declarator")) return false;
            haveName = !nested->name.empty();
        }
    }
    if (!nested) {
        if (atIdent() || at("::") || at("~") || at("operator")) {
            // the declarator-id: a parameter's type was consumed by its decl-specifiers, so a name here
            // is a name even when it is also a type elsewhere ("const char* set")
            if (!qualifiedName(d.name, true, false)) return false;
            haveName = true;
        }
        if (!haveName && !abstractAllowed) { error(nameRequired ? "expected a declarator name" : "expected a declarator"); return false; }
    }
    // the parameters of an out-of-line member see the class's nested types: "bool Layouts::field(Field& out)"
    const NamedScope* owner = d.name.parts.size() > 1 ? findNamedScope(d.name, d.name.parts.size() - 1) : 0;
    if (owner) { Scope s; s.isClass = true; s.types = owner->types; s.templates = owner->templates; s.namespaces = owner->namespaces; m_scopes.push_back(s); }
    bool suffixOk = declaratorSuffixes(d, inParams);
    if (owner) m_scopes.pop_back();
    if (!suffixOk) return false;
    // bit-field
    if (!inParams && at(":") && !d.isFunction() && haveName && peek().text != ":") {
        advance();
        d.bitfield = conditional();
        if (!d.bitfield) return false;
    }
    // final order: prefix operators, then suffixes (already appended), then the nested declarator's parts
    std::vector<DeclaratorPart> suffixes = d.parts;
    d.parts = prefix;
    d.parts.insert(d.parts.end(), suffixes.begin(), suffixes.end());
    if (nested) {
        if (!nested->name.empty()) d.name = nested->name;
        if (!nested->parts.empty()) { DeclaratorPart paren; paren.kind = DeclaratorPart::Paren; (void)paren; d.parts.insert(d.parts.end(), nested->parts.begin(), nested->parts.end()); }
    }
    return true;
}

bool Parser::typeIdAt(TypeId& t)
{
    if (!declSpecifiers(t.spec, false, false) || !t.spec.hasType()) { error("expected a type"); return false; }
    Declarator* d = m_arena.declarator();
    if (!declarator(*d, true, false, true)) return false;
    if (!d->name.empty()) { error("a type cannot have a name here"); return false; }
    t.decl = d;
    return true;
}

bool Parser::tryTypeId(TypeId*& out)
{
    size_t save = m_pos;
    size_t errs = m_diags.size();
    ++m_tentative;
    TypeId* t = m_arena.typeId();
    bool ok = typeIdAt(*t);
    --m_tentative;
    if (!ok) { m_pos = save; m_diags.resize(errs); return false; }
    out = t;
    return true;
}


void Parser::registerDeclaredNames(const DeclSpec& spec, const Declarator& d)
{
    if (d.name.parts.size() != 1 || d.name.global) return;
    if (spec.isTypedef) declareType(d.name.parts[0].ident);
    else declareOther(d.name.parts[0].ident);
}

bool Parser::braceElements(std::vector<Expr*>& out)
{
    while (!at("}") && !atEnd()) {
        if (at("{")) {
            Expr* e = m_arena.expr(Expr::InitList, loc());
            advance();
            if (!braceElements(e->kids)) return false;
            out.push_back(e);
        } else { Expr* e = assignment(); if (!e) return false; out.push_back(e); }
        if (!accept(",")) break;
    }
    return expect("}", "after the initializer list");
}

bool Parser::initializer(Initializer& init)
{
    if (accept("=")) {
        if (at("{")) {
            init.kind = Initializer::Brace;
            advance();
            return braceElements(init.args);
        }
        init.kind = Initializer::Assign;
        Expr* e = assignment();
        if (!e) return false;
        init.args.push_back(e);
        return true;
    }
    if (at("(")) {
        init.kind = Initializer::Parens;
        advance();
        if (!callArguments(init.args)) return false;
        return true;
    }
    return true;
}

bool Parser::memInitializers(Decl* fn)
{
    // after ':'
    for (;;) {
        MemInit mi;
        mi.loc = loc();
        if (!qualifiedName(mi.name, true, true)) return false;
        if (!expect("(", "in the constructor initializer")) return false;
        if (!callArguments(mi.args)) return false;
        fn->memInits.push_back(mi);
        if (!accept(",")) break;
    }
    return true;
}

// a declarator, or in a class an unnamed bit-field ": width" (": 0" closes the bit-field allocation unit)
bool Parser::memberDeclarator(Declarator& d, bool inClass)
{
    if (inClass && at(":")) {
        advance();
        d.bitfield = conditional();
        return d.bitfield != 0;
    }
    return declarator(d, false, true, false);
}

Decl* Parser::simpleOrFunction(bool inClass, const DeclSpec& spec, Loc here, bool firstParsed, Declarator* first)
{
    Declarator* d = first;
    if (!firstParsed) {
        if (at(";")) {                         // "class X;" / "struct S { ... };" / "enum E { ... };"
            advance();
            Decl* decl = m_arena.decl(Decl::Simple, here);
            decl->spec = spec;
            return decl;
        }
        d = m_arena.declarator();
        if (!memberDeclarator(*d, inClass)) return 0;
    }
    // function definition?
    if (d->isFunction() && (at("{") || at(":") || at("try"))) {
        Decl* fn = m_arena.decl(Decl::Function, here);
        fn->spec = spec;
        fn->declarator = d;
        // the function's own scope: a named scope when it is an out-of-line member so that the class's
        // type names are visible in the body
        bool pushedClass = false;
        const NamedScope* owner = d->name.parts.size() > 1 ? findNamedScope(d->name, d->name.parts.size() - 1) : 0;
        if (owner) {
            Scope s; s.name = ""; s.isClass = true;
            s.types = owner->types; s.templates = owner->templates; s.namespaces = owner->namespaces;
            m_scopes.push_back(s); pushedClass = true;
        }
        pushScope("", false);
        // parameter names are not types; template parameters of the enclosing template are already in scope.
        // They hide templates of the same name in the body
        if (const DeclaratorPart* fp = d->functionPart())
            for (size_t k = 0; k < fp->params.size(); ++k)
                if (fp->params[k]->decl && fp->params[k]->decl->name.parts.size() == 1) declareOther(fp->params[k]->decl->name.parts[0].ident);
        if (accept(":")) { if (!memInitializers(fn)) { popScope(); if (pushedClass) m_scopes.pop_back(); return 0; } }
        if (at("try")) { Loc tl = loc(); advance(); if (accept(":")) { if (!memInitializers(fn)) { popScope(); if (pushedClass) m_scopes.pop_back(); return 0; } } fn->body = tryBlock(tl); }
        else fn->body = compound();
        popScope();
        if (pushedClass) m_scopes.pop_back();
        if (!fn->body) return 0;
        return fn;
    }
    Decl* decl = m_arena.decl(Decl::Simple, here);
    decl->spec = spec;
    for (;;) {
        InitDeclarator id;
        id.decl = d;
        if (d->isFunction() && at("=") && peek().text == "0" && peek(2).text == ";") { advance(); advance(); id.pureVirtual = true; }
        else if (!initializer(id.init)) return 0;
        registerDeclaredNames(spec, *d);
        decl->declarators.push_back(id);
        if (!accept(",")) break;
        d = m_arena.declarator();
        if (!memberDeclarator(*d, inClass)) return 0;
    }
    if (!expect(";", "after the declaration")) return 0;
    return decl;
}

Decl* Parser::classSpecifier(DeclSpec& spec, Loc here)
{
    ClassDecl* c = m_arena.classDecl();
    c->key = cur().text;
    c->loc = here;
    c->pack = cur().pack;               // #pragma pack takes effect at the class key that follows it
    if (spec.align > 0) { c->declAlign = spec.align; spec.align = 0; }   // "__declspec(align(N)) struct S {...}" aligns the type
    advance();
    while (at("__declspec")) { advance(); int a = declspecArguments(); if (a > c->declAlign) c->declAlign = a; }
    if (!at("{")) { if (!qualifiedName(c->name, true, true)) return 0; }
    // the class name is a type from here on (also inside its own body)
    std::string simple = c->name.parts.empty() ? "" : c->name.parts.back().ident;
    if (!simple.empty() && c->name.parts.size() == 1) declareType(simple);
    if (accept(":")) {
        for (;;) {
            BaseSpec b;
            b.loc = loc();
            for (;;) {
                if (accept("virtual")) { b.isVirtual = true; continue; }
                if (at("public")) { b.access = AccessPublic; advance(); continue; }
                if (at("protected")) { b.access = AccessProtected; advance(); continue; }
                if (at("private")) { b.access = AccessPrivate; advance(); continue; }
                break;
            }
            if (!qualifiedName(b.name, true, true)) return 0;
            c->bases.push_back(b);
            if (!accept(",")) break;
        }
    }
    if (!expect("{", "after the class head")) return 0;
    c->isDefinition = true;
    // members: inside the class the simple name is a named scope
    std::string scopeName = simple.empty() ? "(anonymous)" : simple;
    pushScope(scopeName, true);
    declareType(simple);
    // the base classes' member type names are visible in the body, and through this class's name
    for (size_t b = 0; b < c->bases.size(); ++b) {
        const NamedScope* ns = findNamedScope(c->bases[b].name, c->bases[b].name.parts.size());
        if (!ns) continue;
        for (std::set<std::string>::const_iterator it = ns->types.begin(); it != ns->types.end(); ++it) declareType(*it);
        for (std::set<std::string>::const_iterator it = ns->templates.begin(); it != ns->templates.end(); ++it) {
            m_scopes.back().templates.insert(*it);
            if (!m_qualifierStack.empty()) m_named[m_qualifierStack.back()].templates.insert(*it);
        }
    }
    Access access = c->key == "class" ? AccessPrivate : AccessPublic;
    while (!at("}") && !atEnd()) {
        if (accept(";")) continue;
        Decl* m = declaration(true, access);
        if (m) { m->access = access; c->members.push_back(m); }
    }
    popScope();
    if (!expect("}", "here the end of the class")) return 0;
    spec.kind = DeclSpec::Class;
    spec.classDef = c;
    Decl* d = m_arena.decl(Decl::Class, here);
    d->classDef = c;
    return d;
}

Decl* Parser::enumSpecifier(DeclSpec& spec, Loc here)
{
    EnumDecl* e = m_arena.enumDecl();
    e->loc = here;
    advance();
    if (atIdent()) { e->name = cur().text; advance(); declareType(e->name); }
    if (!expect("{", "after enum")) return 0;
    e->isDefinition = true;
    while (!at("}") && !atEnd()) {
        Enumerator en;
        en.loc = loc();
        if (!atIdent()) { error("expected an enumerator name"); return 0; }
        en.name = cur().text; advance();
        if (accept("=")) { en.value = conditional(); if (!en.value) return 0; }
        e->enumerators.push_back(en);
        if (!accept(",")) break;
    }
    if (!expect("}", "here the end of the enum")) return 0;
    spec.kind = DeclSpec::Enum;
    spec.enumDef = e;
    Decl* d = m_arena.decl(Decl::Enum, here);
    d->enumDef = e;
    return d;
}

Decl* Parser::namespaceDecl(Loc here)
{
    advance();     // namespace
    Decl* d = m_arena.decl(Decl::Namespace, here);
    if (atIdent()) { d->name = cur().text; advance(); }
    if (accept("=")) {                        // namespace alias
        d->kind = Decl::NamespaceAlias;
        if (!qualifiedName(d->target, false, false)) return 0;
        if (!expect(";", "after the namespace alias")) return 0;
        declareNamespace(d->name);
        return d;
    }
    if (!expect("{", "after the namespace name")) return 0;
    declareNamespace(d->name);
    pushScope(d->name.empty() ? "(anonymous)" : d->name, false);
    Access acc = AccessDefault;
    while (!at("}") && !atEnd()) {
        if (accept(";")) continue;
        Decl* m = declaration(false, acc);
        if (m) d->decls.push_back(m);
    }
    // names of an anonymous namespace are visible in the enclosing scope
    if (d->name.empty() && m_scopes.size() >= 2) {
        Scope& inner = m_scopes.back(); Scope& outer = m_scopes[m_scopes.size() - 2];
        outer.types.insert(inner.types.begin(), inner.types.end());
        outer.templates.insert(inner.templates.begin(), inner.templates.end());
        outer.namespaces.insert(inner.namespaces.begin(), inner.namespaces.end());
    }
    popScope();
    if (!expect("}", "here the end of the namespace")) return 0;
    return d;
}

Decl* Parser::usingDecl(Loc here)
{
    advance();     // using
    Decl* d = m_arena.decl(at("namespace") ? Decl::UsingDirective : Decl::UsingDecl, here);
    if (d->kind == Decl::UsingDirective) advance();
    else accept("typename");
    if (!qualifiedName(d->target, true, false)) return 0;
    if (!expect(";", "after the using declaration")) return 0;
    if (d->kind == Decl::UsingDecl && !d->target.parts.empty()) {
        // "using std::string;" brings a (possible) type name in: register it when the source scope says it is one
        const std::string& id = d->target.parts.back().ident;
        // "using ::strlen;" names a global entity: a type only when the global scope declared it as one
        bool isType = d->target.global && d->target.parts.size() == 1 ? (!m_scopes.empty() && m_scopes[0].types.count(id) > 0) : isTypeName(d->target);
        if (isType) declareType(id);
    } else if (d->kind == Decl::UsingDirective && !d->target.parts.empty()) {
        // the types of the named namespace become visible
        std::map<std::string, NamedScope>::const_iterator it = m_named.find(d->target.text());
        if (it != m_named.end() && !m_scopes.empty()) {
            m_scopes.back().types.insert(it->second.types.begin(), it->second.types.end());
            m_scopes.back().templates.insert(it->second.templates.begin(), it->second.templates.end());
        }
    }
    return d;
}

Decl* Parser::templateDecl(bool inClass, Access& access, Loc here)
{
    advance();     // template
    Decl* d = m_arena.decl(Decl::Template, here);
    if (!at("<")) {
        // explicit instantiation: template class X<int>; / template void f<int>(int);
        d->kind = Decl::ExplicitInstantiation;
        Decl* inner = declaration(inClass, access);
        if (!inner) return 0;
        d->decls.push_back(inner);
        return d;
    }
    advance();
    pushScope("", false);          // template parameters live in their own scope around the declaration
    if (accept(">")) d->explicitSpecialization = true;
    else {
        for (;;) {
            TemplateParam* p = m_arena.templateParam();
            p->loc = loc();
            if (at("class") || at("typename")) {
                if (!(peek().kind == PpToken::Ident && !isKeyword(peek().text)) && peek().text != "=" && peek().text != "," && peek().text != ">") {
                    // "class X" could also start a non-type parameter of class type "class X* p"; treat as type parameter
                }
                advance();
                p->kind = TemplateParam::Type;
                if (atIdent()) { p->name = cur().text; advance(); declareType(p->name); }
                if (accept("=")) { TypeId* t = m_arena.typeId(); ++m_noGreater; bool ok = typeIdAt(*t); --m_noGreater; if (!ok) { popScope(); return 0; } p->defaultType = t; }
            } else if (at("template")) {
                advance();
                p->kind = TemplateParam::Template;
                if (!expect("<", "in the template template parameter")) { popScope(); return 0; }
                int depth = 1;
                while (depth > 0 && !atEnd()) { if (at("<")) ++depth; else if (at(">")) --depth; else if (at(">>")) depth -= 2; advance(); }
                accept("class"); accept("typename");
                if (atIdent()) { p->name = cur().text; advance(); declareTemplate(p->name); declareType(p->name); }
                if (accept("=")) { ++m_noGreater; QualifiedName q; bool ok = qualifiedName(q, true, true); --m_noGreater; if (!ok) { popScope(); return 0; } }
            } else {
                p->kind = TemplateParam::NonType;
                Param* np = m_arena.param();
                np->loc = loc();
                ++m_noGreater;
                bool ok = declSpecifiers(np->spec, false, false) && np->spec.hasType();
                if (ok) { Declarator* dd = m_arena.declarator(); ok = declarator(*dd, true, false, true); np->decl = dd; if (ok && !dd->name.empty()) p->name = dd->name.text(); }
                if (ok && accept("=")) { np->defaultValue = assignment(); ok = np->defaultValue != 0; }
                --m_noGreater;
                if (!ok) { error("bad template parameter"); popScope(); return 0; }
                p->nonType = np;
            }
            d->templateParams.push_back(p);
            if (accept(",")) continue;
            break;
        }
        if (at(">>")) m_toks[m_pos].text = ">";
        if (!expect(">", "after the template parameters")) { popScope(); return 0; }
    }
    ++m_templateDepth;
    // the declared template's name goes into the enclosing scope: peek here it
    {
        size_t p = m_pos;
        // skip to the name: "class X", "struct X", "typename T ... f", or the declarator of a function
        while (p < m_toks.size() && m_toks[p].kind == PpToken::Ident && (isDeclSpecKeyword(m_toks[p].text) || m_toks[p].text == "template")) ++p;
        if ((m_toks[p].text == "class" || m_toks[p].text == "struct" || m_toks[p].text == "union") && m_toks[p + 1].kind == PpToken::Ident && !isKeyword(m_toks[p + 1].text)) {
            // class template: known as both a template and a type in the enclosing scope
            Scope& outer = m_scopes[m_scopes.size() - 2];
            outer.templates.insert(m_toks[p + 1].text); outer.types.insert(m_toks[p + 1].text);
            if (!outer.name.empty() && m_qualifierStack.size() >= 1) { m_named[m_qualifierStack.back()].templates.insert(m_toks[p + 1].text); m_named[m_qualifierStack.back()].types.insert(m_toks[p + 1].text); }
            m_scopes.back().templates.insert(m_toks[p + 1].text); m_scopes.back().types.insert(m_toks[p + 1].text);
        }
    }
    Decl* inner = declaration(inClass, access);
    --m_templateDepth;
    popScope();
    if (!inner) return 0;
    d->decls.push_back(inner);
    // a function template: its name is a template in the enclosing scope
    if (inner->kind == Decl::Function && inner->declarator && inner->declarator->name.parts.size() == 1) declareTemplate(inner->declarator->name.parts[0].ident);
    if (inner->kind == Decl::Simple)
        for (size_t i = 0; i < inner->declarators.size(); ++i)
            if (inner->declarators[i].decl && inner->declarators[i].decl->isFunction() && inner->declarators[i].decl->name.parts.size() == 1) declareTemplate(inner->declarators[i].decl->name.parts[0].ident);
    return d;
}

Decl* Parser::linkageSpec(Loc here)
{
    advance();     // extern
    Decl* d = m_arena.decl(Decl::LinkageSpec, here);
    d->name = cur().text.size() >= 2 ? cur().text.substr(1, cur().text.size() - 2) : cur().text;
    advance();
    Access acc = AccessDefault;
    if (accept("{")) {
        while (!at("}") && !atEnd()) { if (accept(";")) continue; Decl* m = declaration(false, acc); if (m) d->decls.push_back(m); }
        if (!expect("}", "here the end of the linkage specification")) return 0;
    } else {
        Decl* m = declaration(false, acc);
        if (!m) return 0;
        d->decls.push_back(m);
    }
    return d;
}

Decl* Parser::declaration(bool inClass, Access& access)
{
    Loc here = loc();
    size_t start = m_pos;
    Decl* result = 0;
    if (at("namespace")) result = namespaceDecl(here);
    else if (at("using")) result = usingDecl(here);
    else if (at("template")) result = templateDecl(inClass, access, here);
    else if (at("extern") && peek().kind == PpToken::String) result = linkageSpec(here);
    else if (at("asm")) { advance(); if (at("(")) skipBalanced(); accept(";"); result = m_arena.decl(Decl::Empty, here); }
    else if (inClass && (at("public") || at("protected") || at("private")) && peek().text == ":") {
        access = at("public") ? AccessPublic : at("protected") ? AccessProtected : AccessPrivate;
        advance(); advance();
        result = m_arena.decl(Decl::AccessLabel, here);
    }
    else if (at("friend") && (peek().text == "class" || peek().text == "struct" || peek().text == "union") && peek(3).text == ";") {
        advance();
        result = m_arena.decl(Decl::Friend, here);
        result->spec.elaborated = cur().text; advance();
        if (!qualifiedName(result->spec.name, true, true)) result = 0; else { result->spec.kind = DeclSpec::Named; if (!expect(";", "after the friend declaration")) result = 0; }
    }
    else {
        DeclSpec spec;
        bool any = declSpecifiers(spec, inClass, true);
        if (!spec.hasType()) {
            // constructor, destructor or conversion function: a declarator without a type
            std::string cls = currentClassName();
            bool ctorLike = false;
            if (at("~") || at("operator")) ctorLike = true;
            else if (atIdent() || at("::")) {
                size_t e; bool ht;
                if (qualifiedNameAhead(e, ht)) {
                    // "X(" where X is the class, or "A::X(" / "A::~X(" / "A::X<T>::X("
                    size_t last = e; (void)last;
                    std::string lastName; std::string prevName;
                    for (size_t p = m_pos; p < e; ++p) if (m_toks[p].kind == PpToken::Ident) { prevName = lastName; lastName = m_toks[p].text; }
                    if (m_toks[e].text == "(" && (lastName == cls || (!prevName.empty() && (prevName == lastName)))) ctorLike = true;
                    if (m_toks[e].text == "::" && m_toks[e + 1].text == "~") ctorLike = true;
                    if (m_toks[e].text == "::" && m_toks[e + 1].text == "operator") ctorLike = true;
                }
            }
            if (!ctorLike) {
                if (!any && !atIdent() && !at("::")) { error("expected a declaration"); skipTo(";", "}"); return m_arena.decl(Decl::Error, here); }
                if (!any || !atIdent()) {
                    // an identifier that is not a type: report it and recover
                    error("unknown type name '" + cur().text + "'");
                    skipTo(";", "}");
                    return m_arena.decl(Decl::Error, here);
                }
                error("expected a type");
                skipTo(";", "}");
                return m_arena.decl(Decl::Error, here);
            }
        }
        if (spec.kind == DeclSpec::Class && spec.classDef && at(";")) {
            // "class X { ... };" : the class itself is the declaration
            advance();
            result = m_arena.decl(Decl::Class, here);
            result->classDef = spec.classDef;
        } else if (spec.kind == DeclSpec::Enum && spec.enumDef && at(";")) {
            advance();
            result = m_arena.decl(Decl::Enum, here);
            result->enumDef = spec.enumDef;
        } else {
            result = simpleOrFunction(inClass, spec, here, false, 0);
        }
    }
    if (!result) {
        if (m_pos == start) advance();
        skipTo(";", "}");
        return m_arena.decl(Decl::Error, here);
    }
    return result;
}

void Parser::assumeType(const std::string& name)
{
    if (!m_scopes.empty()) m_scopes[0].types.insert(name);
}

int Parser::parse(TranslationUnit& tu)
{
    Access acc = AccessDefault;
    while (!atEnd()) {
        if (accept(";")) continue;
        if (at("}")) { error("unexpected '}'"); advance(); continue; }
        Decl* d = declaration(false, acc);
        if (d) tu.decls.push_back(d);
    }
    return m_errors;
}


Stmt* Parser::compound()
{
    Loc here = loc();
    if (!expect("{", "to start a block")) return 0;
    Stmt* s = m_arena.stmt(Stmt::Compound, here);
    pushScope("", false);
    while (!at("}") && !atEnd()) {
        Stmt* k = statement();
        if (k) s->kids.push_back(k);
    }
    popScope();
    if (!expect("}", "here the end of the block")) { return s; }
    return s;
}

Stmt* Parser::declarationStatement()
{
    // tentative: a declaration must start with decl-specifiers that name a type (or a storage class)
    size_t save = m_pos;
    size_t errs = m_diags.size();
    int errorsBefore = m_errors;
    Loc here = loc();
    const std::set<std::string> othersBefore = m_scopes.back().others;   // a failed attempt declares nothing
    ++m_tentative;
    DeclSpec spec;
    bool any = declSpecifiers(spec, false, true);
    Decl* d = 0;
    if (any && spec.hasType()) d = simpleOrFunction(false, spec, here, false, 0);
    --m_tentative;
    if (!d) { m_pos = save; m_diags.resize(errs); m_errors = errorsBefore; m_scopes.back().others = othersBefore; return 0; }
    Stmt* s = m_arena.stmt(Stmt::DeclStmt, here);
    s->decl = d;
    return s;
}

bool Parser::condition(Stmt* s)
{
    // "( declaration = expr )" or "( expression )"
    size_t save = m_pos;
    if (looksLikeTypeStart()) {
        size_t errs = m_diags.size(); int eb = m_errors;
        ++m_tentative;
        DeclSpec spec;
        bool ok = declSpecifiers(spec, false, false) && spec.hasType();
        Declarator* d = 0;
        if (ok) { d = m_arena.declarator(); ok = declarator(*d, false, true, false) && at("="); }
        --m_tentative;
        if (ok) {
            advance();
            Expr* e = assignment();
            if (!e) return false;
            Decl* decl = m_arena.decl(Decl::Simple, s->loc);
            decl->spec = spec;
            InitDeclarator id; id.decl = d; id.init.kind = Initializer::Assign; id.init.args.push_back(e);
            decl->declarators.push_back(id);
            s->decl = decl;
            registerDeclaredNames(spec, *d);
            return true;
        }
        m_pos = save; m_diags.resize(errs); m_errors = eb;
    }
    s->expr = expression();
    return s->expr != 0;
}

Stmt* Parser::tryBlock(Loc here)
{
    Stmt* s = m_arena.stmt(Stmt::Try, here);
    Stmt* body = compound();
    if (!body) return 0;
    s->kids.push_back(body);
    while (at("catch")) {
        Stmt* h = m_arena.stmt(Stmt::Handler, loc());
        advance();
        if (!expect("(", "after catch")) return 0;
        pushScope("", false);
        if (accept("...")) h->catchAll = true;
        else {
            Decl* d = m_arena.decl(Decl::Simple, h->loc);
            if (!declSpecifiers(d->spec, false, false) || !d->spec.hasType()) { error("expected an exception declaration"); popScope(); return 0; }
            InitDeclarator id; id.decl = m_arena.declarator();
            if (!declarator(*id.decl, true, false, true)) { popScope(); return 0; }
            d->declarators.push_back(id);
            h->decl = d;
        }
        if (!expect(")", "after the exception declaration")) { popScope(); return 0; }
        Stmt* hb = compound();
        popScope();
        if (!hb) return 0;
        h->kids.push_back(hb);
        s->kids.push_back(h);
    }
    if (s->kids.size() == 1) error("try without a catch clause");
    return s;
}

Stmt* Parser::statement()
{
    Loc here = loc();
    size_t start = m_pos;
    Stmt* s = 0;
    const std::string& w = cur().text;
    bool kw = cur().kind == PpToken::Ident;
    if (at("{")) s = compound();
    else if (accept(";")) s = m_arena.stmt(Stmt::Empty, here);
    else if (kw && w == "if") {
        advance(); s = m_arena.stmt(Stmt::If, here);
        if (!expect("(", "after if") || !condition(s) || !expect(")", "after the if condition")) s = 0;
        else { Stmt* t = statement(); if (!t) s = 0; else { s->kids.push_back(t); if (accept("else")) { Stmt* e = statement(); if (!e) s = 0; else s->kids.push_back(e); } } }
    }
    else if (kw && w == "switch") {
        advance(); s = m_arena.stmt(Stmt::Switch, here);
        if (!expect("(", "after switch") || !condition(s) || !expect(")", "after the switch condition")) s = 0;
        else { Stmt* b = statement(); if (!b) s = 0; else s->kids.push_back(b); }
    }
    else if (kw && w == "while") {
        advance(); s = m_arena.stmt(Stmt::While, here);
        if (!expect("(", "after while") || !condition(s) || !expect(")", "after the while condition")) s = 0;
        else { Stmt* b = statement(); if (!b) s = 0; else s->kids.push_back(b); }
    }
    else if (kw && w == "do") {
        advance(); s = m_arena.stmt(Stmt::DoWhile, here);
        Stmt* b = statement();
        if (!b || !expect("while", "after the do body") || !expect("(", "after while")) s = 0;
        else { s->kids.push_back(b); s->expr = expression(); if (!s->expr || !expect(")", "after the condition") || !expect(";", "after do-while")) s = 0; }
    }
    else if (kw && w == "for") {
        advance(); s = m_arena.stmt(Stmt::For, here);
        pushScope("", false);
        if (!expect("(", "after for")) s = 0;
        else {
            if (accept(";")) s->init = 0;
            else {
                Stmt* init = declarationStatement();
                if (!init) { Expr* e = expression(); if (!e || !expect(";", "after the for initializer")) s = 0; else { init = m_arena.stmt(Stmt::ExprStmt, here); init->expr = e; } }
                if (s) s->init = init;
            }
            if (s && !at(";")) { if (!condition(s)) s = 0; }
            if (s && !expect(";", "after the for condition")) s = 0;
            if (s && !at(")")) { s->expr2 = expression(); if (!s->expr2) s = 0; }
            if (s && !expect(")", "after the for header")) s = 0;
            if (s) { Stmt* b = statement(); if (!b) s = 0; else s->kids.push_back(b); }
        }
        popScope();
    }
    else if (kw && w == "break") { advance(); s = m_arena.stmt(Stmt::Break, here); if (!expect(";", "after break")) s = 0; }
    else if (kw && w == "continue") { advance(); s = m_arena.stmt(Stmt::Continue, here); if (!expect(";", "after continue")) s = 0; }
    else if (kw && w == "return") {
        advance(); s = m_arena.stmt(Stmt::Return, here);
        if (!at(";")) { s->expr = expression(); if (!s->expr) s = 0; }
        if (s && !expect(";", "after return")) s = 0;
    }
    else if (kw && w == "goto") { advance(); s = m_arena.stmt(Stmt::Goto, here); if (atIdent()) { s->label = cur().text; advance(); } else { error("expected a label after goto"); s = 0; } if (s && !expect(";", "after goto")) s = 0; }
    else if (kw && w == "case") {
        advance(); s = m_arena.stmt(Stmt::Case, here);
        ++m_noGreater; s->expr = conditional(); --m_noGreater;
        if (!s->expr || !expect(":", "after the case value")) s = 0;
        else { Stmt* b = at("}") ? m_arena.stmt(Stmt::Empty, here) : statement(); if (!b) s = 0; else s->kids.push_back(b); }
    }
    else if (kw && w == "default" && peek().text == ":") { advance(); advance(); s = m_arena.stmt(Stmt::Default, here); Stmt* b = at("}") ? m_arena.stmt(Stmt::Empty, here) : statement(); if (!b) s = 0; else s->kids.push_back(b); }
    else if (kw && w == "try") { advance(); s = tryBlock(here); }
    else if (kw && w == "asm" ) { advance(); if (at("(")) skipBalanced(); accept(";"); s = m_arena.stmt(Stmt::Asm, here); }
    else if (atIdent() && peek().text == ":" && peek(2).text != ":") {      // label
        s = m_arena.stmt(Stmt::Label, here);
        s->label = cur().text; advance(); advance();
        Stmt* b = at("}") ? m_arena.stmt(Stmt::Empty, here) : statement();
        if (!b) s = 0; else s->kids.push_back(b);
    }
    else {
        s = declarationStatement();
        if (!s) {
            Expr* e = expression();
            if (e && expect(";", "after the expression")) { s = m_arena.stmt(Stmt::ExprStmt, here); s->expr = e; }
            else s = 0;
        }
    }
    if (!s) {
        if (m_pos == start) advance();
        skipTo(";", "}");
        return m_arena.stmt(Stmt::Error, here);
    }
    return s;
}


int Parser::precedence(const std::string& op, bool& rightAssoc) const
{
    rightAssoc = false;
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|") return 3;
    if (op == "^") return 4;
    if (op == "&") return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == "<=" || op == ">=") return 7;
    if (op == ">") return m_noGreater > 0 ? -1 : 7;
    if (op == "<<") return 8;
    if (op == ">>") return m_noGreater > 0 ? -1 : 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    if (op == ".*" || op == "->*") return 11;
    return -1;
}

Expr* Parser::expression()
{
    Expr* e = assignment();
    if (!e) return 0;
    while (at(",")) {
        Loc here = loc();
        advance();
        Expr* r = assignment();
        if (!r) return 0;
        Expr* c = m_arena.expr(Expr::Comma, here);
        c->kids.push_back(e); c->kids.push_back(r);
        e = c;
    }
    return e;
}

Expr* Parser::assignment()
{
    if (at("throw")) {
        Loc here = loc(); advance();
        Expr* t = m_arena.expr(Expr::Throw, here);
        if (!at(";") && !at(")") && !at(",") && !at(":")) { Expr* v = assignment(); if (!v) return 0; t->kids.push_back(v); }
        return t;
    }
    Expr* lhs = conditional();
    if (!lhs) return 0;
    if (cur().kind == PpToken::Punct) {
        const std::string& op = cur().text;
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || (op == ">>=" && m_noGreater == 0)) {
            Loc here = loc();
            advance();
            Expr* rhs = assignment();
            if (!rhs) return 0;
            Expr* a = m_arena.expr(Expr::Assign, here);
            a->text = op;
            a->kids.push_back(lhs); a->kids.push_back(rhs);
            return a;
        }
    }
    return lhs;
}

Expr* Parser::conditional()
{
    Expr* c = binary(1);
    if (!c) return 0;
    if (at("?")) {
        Loc here = loc();
        advance();
        Expr* a = expression();
        if (!a || !expect(":", "in the conditional expression")) return 0;
        Expr* b = assignment();
        if (!b) return 0;
        Expr* e = m_arena.expr(Expr::Cond, here);
        e->kids.push_back(c); e->kids.push_back(a); e->kids.push_back(b);
        return e;
    }
    return c;
}

Expr* Parser::binary(int minPrec)
{
    Expr* lhs = unary();
    if (!lhs) return 0;
    for (;;) {
        if (cur().kind != PpToken::Punct) break;
        std::string op = cur().text;
        bool ra;
        int prec = precedence(op, ra);
        if (prec < minPrec) break;
        Loc here = loc();
        advance();
        Expr* rhs = binary(prec + 1);
        if (!rhs) return 0;
        Expr* b = m_arena.expr(Expr::Binary, here);
        b->text = op;
        b->kids.push_back(lhs); b->kids.push_back(rhs);
        lhs = b;
    }
    return lhs;
}

bool Parser::tryCastExpr(Expr*& out)
{
    // here '(' : "( type-id ) cast-expression"
    size_t save = m_pos;
    Loc here = loc();
    advance();
    TypeId* t = 0;
    if (!looksLikeTypeStart() || !tryTypeId(t) || !at(")")) { m_pos = save; return false; }
    advance();
    // what follows must be able to start an expression (not an operator that would make "(a) + b" a binary expression)
    const PpToken& n = cur();
    bool startsOperand = n.kind == PpToken::Ident || n.kind == PpToken::Number || n.kind == PpToken::String || n.kind == PpToken::Char
                         || (n.kind == PpToken::Punct && (n.text == "(" || n.text == "!" || n.text == "~" || n.text == "++" || n.text == "--" || n.text == "::"));
    bool ambiguousUnary = n.kind == PpToken::Punct && (n.text == "-" || n.text == "+" || n.text == "*" || n.text == "&");
    if (!startsOperand && !(ambiguousUnary && t->spec.kind != DeclSpec::Named)) {
        if (!ambiguousUnary) { m_pos = save; return false; }
    }
    if (ambiguousUnary && t->spec.kind == DeclSpec::Named && t->decl && t->decl->parts.empty()) {
        // "(x) - 1": x is a known type only when the scope says so; a type-cast "(T) -1" is rare: prefer the expression
        QualifiedName n2 = t->spec.name;
        if (!isTypeName(n2) || (n2.parts.size() == 1 && !n2.global && !isTypeName(n2))) { m_pos = save; return false; }
    }
    Expr* operand = unary();
    if (!operand) { m_pos = save; return false; }
    Expr* c = m_arena.expr(Expr::Cast, here);
    c->type = t;
    c->kids.push_back(operand);
    out = c;
    return true;
}

Expr* Parser::unary()
{
    Loc here = loc();
    if (cur().kind == PpToken::Punct) {
        const std::string& op = cur().text;
        if (op == "+" || op == "-" || op == "!" || op == "~" || op == "*" || op == "&" || op == "++" || op == "--") {
            advance();
            Expr* o = unary();
            if (!o) return 0;
            Expr* u = m_arena.expr(Expr::Unary, here);
            u->text = op;
            u->kids.push_back(o);
            return u;
        }
        if (op == "(") { Expr* c; if (tryCastExpr(c)) return c; }
        if (op == "::" && (peek().text == "new" || peek().text == "delete")) { advance(); Expr* g = unary(); if (g && (g->kind == Expr::New || g->kind == Expr::Delete)) g->globalScope = true; return g; }
    }
    if (cur().kind == PpToken::Ident) {
        const std::string& w = cur().text;
        if (w == "sizeof") {
            advance();
            Expr* e;
            if (at("(")) {
                size_t save = m_pos; advance();
                TypeId* t = 0;
                if (looksLikeTypeStart() && tryTypeId(t) && at(")")) { advance(); e = m_arena.expr(Expr::SizeofType, here); e->type = t; return e; }
                m_pos = save;
            }
            Expr* o = unary();
            if (!o) return 0;
            e = m_arena.expr(Expr::SizeofExpr, here); e->kids.push_back(o);
            return e;
        }
        if (w == "new") return newExpr(here);
        if (w == "delete") {
            advance();
            Expr* d = m_arena.expr(Expr::Delete, here);
            if (at("[") && peek().text == "]") { advance(); advance(); d->arrayDelete = true; }
            Expr* o = unary();
            if (!o) return 0;
            d->kids.push_back(o);
            return d;
        }
    }
    return postfix();
}

Expr* Parser::newExpr(Loc here)
{
    advance();    // new
    Expr* n = m_arena.expr(Expr::New, here);
    // placement: "( args )" when the parenthesis does not hold a type
    if (at("(")) {
        size_t save = m_pos; advance();
        TypeId* probe = 0;
        bool isType = looksLikeTypeStart() && tryTypeId(probe) && at(")");
        m_pos = save;
        if (!isType) { advance(); if (!callArguments(n->placement)) return 0; }
    }
    TypeId* t = m_arena.typeId();
    if (accept("(")) {
        if (!typeIdAt(*t) || !expect(")", "after the type in new")) return 0;
    } else {
        // new-type-id: specifiers then ptr-operators and array bounds (no parentheses)
        if (!declSpecifiers(t->spec, false, false) || !t->spec.hasType()) { error("expected a type after new"); return 0; }
        Declarator* d = m_arena.declarator();
        for (;;) {
            if (at("*")) { DeclaratorPart p; p.kind = DeclaratorPart::Pointer; advance(); while (at("const") || at("volatile")) { if (at("const")) p.isConst = true; advance(); } d->parts.push_back(p); continue; }
            if (at("&")) { DeclaratorPart p; p.kind = DeclaratorPart::Reference; advance(); d->parts.push_back(p); continue; }
            break;
        }
        std::vector<DeclaratorPart> arrays;
        while (at("[")) { DeclaratorPart p; p.kind = DeclaratorPart::Array; advance(); if (!at("]")) { p.arraySize = expression(); if (!p.arraySize) return 0; } if (!expect("]", "after the array size in new")) return 0; arrays.push_back(p); }
        for (size_t i = arrays.size(); i > 0; --i) d->parts.push_back(arrays[i - 1]);
        t->decl = d;
    }
    n->type = t;
    if (at("(")) { advance(); n->hasInit = true; if (!callArguments(n->kids)) return 0; }
    return n;
}

bool Parser::callArguments(std::vector<Expr*>& args)
{
    // after '('
    if (accept(")")) return true;
    ++m_noGreater; --m_noGreater;    // arguments are full expressions: '>' is an operator again
    int savedNoGreater = m_noGreater; m_noGreater = 0;
    for (;;) {
        Expr* e = assignment();
        if (!e) { m_noGreater = savedNoGreater; return false; }
        args.push_back(e);
        if (!accept(",")) break;
    }
    m_noGreater = savedNoGreater;
    return expect(")", "after the arguments");
}

Expr* Parser::postfix()
{
    Expr* e = primary();
    if (!e) return 0;
    for (;;) {
        Loc here = loc();
        if (at("(")) {
            advance();
            Expr* c = m_arena.expr(Expr::Call, here);
            c->kids.push_back(e);
            if (!callArguments(c->kids)) return 0;
            e = c;
            continue;
        }
        if (at("[")) {
            advance();
            int saved = m_noGreater; m_noGreater = 0;
            Expr* idx = expression();
            m_noGreater = saved;
            if (!idx || !expect("]", "after the index")) return 0;
            Expr* x = m_arena.expr(Expr::Index, here);
            x->kids.push_back(e); x->kids.push_back(idx);
            e = x;
            continue;
        }
        if (at(".") || at("->")) {
            bool arrow = at("->");
            advance();
            accept("template");
            Expr* m = m_arena.expr(Expr::Member, here);
            m->arrowMember = arrow;
            m->kids.push_back(e);
            if (!qualifiedName(m->name, true, false)) return 0;
            e = m;
            continue;
        }
        if (at("++") || at("--")) {
            Expr* p = m_arena.expr(Expr::PostIncDec, here);
            p->text = cur().text; advance();
            p->kids.push_back(e);
            e = p;
            continue;
        }
        break;
    }
    return e;
}

Expr* Parser::primary()
{
    Loc here = loc();
    const PpToken& t = cur();
    switch (t.kind) {
    case PpToken::Number: { Expr* e = m_arena.expr(t.text.find_first_of(".eE") != std::string::npos && t.text.compare(0, 2, "0x") != 0 && t.text.compare(0, 2, "0X") != 0 ? Expr::FloatLit : Expr::IntLit, here); e->text = t.text; advance(); return e; }
    case PpToken::Char: { Expr* e = m_arena.expr(Expr::CharLit, here); e->text = t.text; advance(); return e; }
    case PpToken::String: {
        Expr* e = m_arena.expr(Expr::StringLit, here); e->text = t.text; advance();
        // adjacent literals concatenate: kept as written, one space apart, since each piece's escapes are decoded
        // on their own ("\x41" "b" is "Ab"; stringLiteralUnits)
        while (cur().kind == PpToken::String) { e->text += " " + cur().text; advance(); }
        return e;
    }
    case PpToken::Punct:
        if (t.text == "(") {
            advance();
            int saved = m_noGreater; m_noGreater = 0;
            Expr* inner = expression();
            m_noGreater = saved;
            if (!inner || !expect(")", "after the parenthesized expression")) return 0;
            Expr* p = m_arena.expr(Expr::Paren, here);
            p->kids.push_back(inner);
            return p;
        }
        if (t.text == "::") break;      // qualified name
        error("expected an expression");
        return 0;
    case PpToken::Ident:
        if (t.text == "true" || t.text == "false") { Expr* e = m_arena.expr(Expr::BoolLit, here); e->text = t.text; advance(); return e; }
        if (t.text == "this") { advance(); return m_arena.expr(Expr::This, here); }
        if (t.text == "static_cast" || t.text == "dynamic_cast" || t.text == "reinterpret_cast" || t.text == "const_cast") {
            Expr* e = m_arena.expr(Expr::NamedCast, here); e->text = t.text; advance();
            if (!expect("<", "after the cast keyword")) return 0;
            ++m_noGreater;
            e->type = m_arena.typeId();
            bool ok = typeIdAt(*e->type);
            --m_noGreater;
            if (!ok) return 0;
            if (at(">>")) m_toks[m_pos].text = ">";
            if (!expect(">", "after the cast type") || !expect("(", "after the cast type")) return 0;
            int saved = m_noGreater; m_noGreater = 0;
            Expr* o = expression();
            m_noGreater = saved;
            if (!o || !expect(")", "after the cast operand")) return 0;
            e->kids.push_back(o);
            return e;
        }
        if (t.text == "typeid") {
            Expr* e = m_arena.expr(Expr::Typeid, here); advance();
            if (!expect("(", "after typeid")) return 0;
            TypeId* ty = 0;
            if (looksLikeTypeStart() && tryTypeId(ty) && at(")")) e->type = ty;
            else { Expr* o = expression(); if (!o) return 0; e->kids.push_back(o); }
            if (!expect(")", "after typeid")) return 0;
            return e;
        }
        if (isTypeKeyword(t.text) || t.text == "typename") {
            // functional cast with a builtin type: int(x), unsigned(y)
            TypeId* ty = m_arena.typeId();
            if (!declSpecifiers(ty->spec, false, false)) return 0;
            Expr* e = m_arena.expr(Expr::FunctionalCast, here); e->type = ty;
            if (!expect("(", "after the type in a functional cast")) return 0;
            if (!callArguments(e->kids)) return 0;
            return e;
        }
        if (isKeyword(t.text)) { error("unexpected keyword"); return 0; }
        break;
    default:
        error("expected an expression");
        return 0;
    }
    // a qualified name; a known type followed by '(' is a functional cast
    QualifiedName n;
    if (!qualifiedName(n, true, false)) return 0;
    if (at("(") && isTypeName(n) && (n.parts.size() == 1 ? true : findNamedScope(n, n.parts.size() - 1) != 0)) {
        TypeId* ty = m_arena.typeId();
        ty->spec.kind = DeclSpec::Named; ty->spec.name = n; ty->spec.loc = here;
        Expr* e = m_arena.expr(Expr::FunctionalCast, here); e->type = ty;
        advance();
        if (!callArguments(e->kids)) return 0;
        return e;
    }
    Expr* e = m_arena.expr(Expr::Name, here);
    e->name = n;
    return e;
}

} // namespace cxx
} // namespace cppparse
