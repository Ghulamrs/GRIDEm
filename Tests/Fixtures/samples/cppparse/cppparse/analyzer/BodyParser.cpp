// BodyParser.cpp - statement and expression parser producing the AST.
#include "BodyParser.hpp"
#include "Parser.hpp"
#include "PathUtil.hpp"

#include <cstdlib>

namespace cppparse {

namespace {

bool isAssignOp(const std::string& s)
{
    return s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=" || s == "<<=" || s == ">>=" ||
           s == "&=" || s == "|=" || s == "^=";
}

bool isSpecifier(const std::string& w)
{
    return w == "const" || w == "volatile" || w == "static" || w == "register" || w == "typename" ||
           w == "struct" || w == "class" || w == "union" || w == "enum" || w == "mutable";
}

bool isBuiltinWord(const std::string& w)
{
    return w == "void" || w == "bool" || w == "char" || w == "short" || w == "int" || w == "long" || w == "float" ||
           w == "double" || w == "unsigned" || w == "signed" || w == "wchar_t" || w == "size_t" || w == "ptrdiff_t" ||
           w == "__int64" || w == "__int32" || w == "intptr_t" || w == "uintptr_t" || w == "ssize_t" || w == "time_t" ||
           w == "clock_t" || w == "int8_t" || w == "uint8_t" || w == "int16_t" || w == "uint16_t" || w == "int32_t" ||
           w == "uint32_t" || w == "int64_t" || w == "uint64_t";
}

bool isKeyword(const std::string& w)
{
    return w == "if" || w == "else" || w == "while" || w == "for" || w == "do" || w == "switch" || w == "case" || w == "default" ||
           w == "break" || w == "continue" || w == "return" || w == "try" || w == "catch" || w == "throw" || w == "new" ||
           w == "delete" || w == "sizeof" || w == "this" || w == "true" || w == "false" || w == "typedef" || w == "goto" ||
           w == "static_cast" || w == "dynamic_cast" || w == "reinterpret_cast" || w == "const_cast" || w == "operator";
}

} // namespace

BodyParser::BodyParser(const CodeBase& cb, TypeTable& types, AstArena& arena, const FunctionInfo& fn,
                       const std::vector<Token>& tokens)
    : m_cb(cb), m_types(types), m_arena(arena), m_fn(fn), m_tokens(tokens), m_pos(fn.bodyBegin), m_end(fn.bodyEnd),
      m_out(0), m_owner(fn.ownerClass)
{
    m_scope = fn.ownerClass ? fn.ownerClass->qualifiedName : fn.scope;
    m_endToken.type = TokEnd;
    m_endToken.line = fn.endLine;
    for (size_t i = 0; i < fn.localTypedefs.size(); ++i) m_localTypes.insert(fn.localTypedefs[i]);
}

// ---- tokens ------------------------------------------------------------------------

const Token& BodyParser::cur() const
{
    size_t p = m_pos;
    while (p < m_end && m_tokens[p].type == TokPreproc) ++p;
    return p < m_end ? m_tokens[p] : m_endToken;
}

const Token& BodyParser::peek(size_t n) const
{
    size_t p = m_pos;
    while (p < m_end && m_tokens[p].type == TokPreproc) ++p;
    for (size_t k = 0; k < n; ++k) {
        ++p;
        while (p < m_end && m_tokens[p].type == TokPreproc) ++p;
    }
    return p < m_end ? m_tokens[p] : m_endToken;
}

void BodyParser::advance()
{
    while (m_pos < m_end && m_tokens[m_pos].type == TokPreproc) ++m_pos;
    if (m_pos < m_end) ++m_pos;
    while (m_pos < m_end && m_tokens[m_pos].type == TokPreproc) ++m_pos;
}

bool BodyParser::is(const char* text) const
{
    const Token& t = cur();
    return t.type != TokEnd && t.text == text;
}

bool BodyParser::accept(const char* text)
{
    if (!is(text)) return false;
    advance();
    return true;
}

bool BodyParser::expect(const char* text, const char* context)
{
    if (accept(text)) return true;
    error(std::string("expected '") + text + "' " + context + ", found '" + (cur().type == TokEnd ? "end of body" : cur().text) + "'");
    return false;
}

size_t BodyParser::skipBalancedAt(size_t pos) const
{
    const std::string& open = m_tokens[pos].text;
    std::string close = open == "(" ? ")" : open == "[" ? "]" : open == "{" ? "}" : ">";
    int depth = 0;
    for (size_t j = pos; j < m_end; ++j) {
        const Token& t = m_tokens[j];
        if (t.type != TokPunct) continue;
        if (t.text == open) ++depth;
        else if (t.text == close) { if (--depth == 0) return j + 1; }
        else if (open == "<" && (t.text == ";" || t.text == "{" || t.text == "}")) return m_end;
    }
    return m_end;
}

void BodyParser::error(const std::string& msg)
{
    if (m_out) m_out->errors.push_back("L" + intToString(cur().line) + ": " + msg);
}

// skip to the next ';' at depth 0 (consumed) or to a '}' (not consumed)
void BodyParser::recover()
{
    int depth = 0;
    while (!atEnd()) {
        const Token& t = cur();
        if (t.type == TokEnd) return;
        if (t.type == TokPunct) {
            if (t.text == "(" || t.text == "[") ++depth;
            else if (t.text == ")" || t.text == "]") { if (depth > 0) --depth; }
            else if (t.text == "{") { m_pos = skipBalancedAt(m_pos); return; }
            else if (t.text == "}") return;
            else if (t.text == ";" && depth == 0) { advance(); return; }
        }
        advance();
    }
}

// ---- types --------------------------------------------------------------------------

bool BodyParser::isTypeWord(const std::string& w) const
{
    if (isBuiltinWord(w)) return true;
    if (m_localTypes.count(w)) return true;
    return m_types.isTypeName(w, m_scope, m_owner);
}

// Reads a type (specifiers, builtin words or a qualified name with template
// arguments, trailing const) starting at 'pos'. 'known' tells whether the
// base name is a type the tool knows.
bool BodyParser::scanType(size_t pos, size_t& endPos, std::string& text, bool& known) const
{
    size_t p = pos;
    bool any = false, builtin = false;
    std::string name;
    known = false;
    std::vector<std::string> parts;
    while (p < m_end) {
        const Token& t = m_tokens[p];
        if (t.type == TokPreproc) { ++p; continue; }
        if (t.type != TokIdentifier && !(t.type == TokPunct && t.text == "::" && name.empty())) break;
        if (t.type == TokIdentifier && isSpecifier(t.text)) { parts.push_back(t.text); ++p; any = true; continue; }
        if (t.type == TokIdentifier && isBuiltinWord(t.text)) { parts.push_back(t.text); ++p; any = true; builtin = true; continue; }
        if (builtin || !name.empty()) break;
        if (t.type == TokIdentifier && isKeyword(t.text)) break;
        // qualified name
        std::string n;
        size_t q = p;
        if (m_tokens[q].text == "::") { n = "::"; ++q; }
        for (;;) {
            while (q < m_end && m_tokens[q].type == TokPreproc) ++q;
            if (q >= m_end || m_tokens[q].type != TokIdentifier) break;
            n += m_tokens[q].text;
            ++q;
            size_t save = q;
            while (q < m_end && m_tokens[q].type == TokPreproc) ++q;
            if (q < m_end && m_tokens[q].text == "<") {
                // template arguments: certain for known types; for qualified names
                // (std::vector<...>) accepted when the closing '>' is followed by
                // something that continues a declaration
                bool knownType = m_types.isTypeName(n, m_scope, m_owner) || m_localTypes.count(n) != 0;
                size_t close = skipBalancedAt(q);
                bool accept = false;
                if (close < m_end && close > q) {
                    if (knownType) accept = true;
                    else if (n.find("::") != std::string::npos) {
                        size_t after = close;
                        while (after < m_end && m_tokens[after].type == TokPreproc) ++after;
                        if (after < m_end) {
                            const Token& t2 = m_tokens[after];
                            accept = (t2.type == TokIdentifier && !isKeyword(t2.text)) ||
                                     (t2.type == TokPunct && (t2.text == "::" || t2.text == "*" || t2.text == "&" || t2.text == ">" || t2.text == ")" || t2.text == "(" || t2.text == ","));
                        }
                    }
                }
                if (accept) {
                    std::vector<Token> args(m_tokens.begin() + static_cast<long>(q), m_tokens.begin() + static_cast<long>(close));
                    n += joinTokens(args);
                    q = close;
                    save = q;
                    while (q < m_end && m_tokens[q].type == TokPreproc) ++q;
                }
            }
            if (q < m_end && m_tokens[q].text == "::" && q + 1 < m_end && m_tokens[q + 1].type == TokIdentifier) { n += "::"; ++q; continue; }
            q = save;
            break;
        }
        if (n.empty() || n == "::") break;
        name = n;
        parts.push_back(n);
        p = q;
        any = true;
        break;
    }
    if (!any) return false;
    // trailing const / specifiers
    while (p < m_end && m_tokens[p].type == TokIdentifier && (m_tokens[p].text == "const" || m_tokens[p].text == "volatile")) { parts.push_back(m_tokens[p].text); ++p; }
    if (name.empty() && !builtin) return false;
    known = builtin || (!name.empty() && (m_localTypes.count(stripTemplateArgs(name)) != 0 || m_types.isTypeName(stripTemplateArgs(name), m_scope, m_owner)));
    // "const X", "struct X", "std::X" and "X<...>" are type positions even when X is not known
    if (!known) {
        for (size_t i = 0; i < parts.size(); ++i)
            if (parts[i] == "const" || parts[i] == "struct" || parts[i] == "class" || parts[i] == "union" || parts[i] == "enum" ||
                parts[i] == "unsigned" || parts[i] == "signed" || parts[i] == "typename" || parts[i] == "static" || parts[i] == "volatile") known = true;
        if (name.find("::") != std::string::npos || name.find('<') != std::string::npos) known = true;
    }
    text.clear();
    for (size_t i = 0; i < parts.size(); ++i) { if (i) text += " "; text += parts[i]; }
    endPos = p;
    return true;
}

// At a statement start: "T x", "T* x", "T& x", "T x(", "T x[", "T x =", "T x,"
bool BodyParser::looksLikeDeclaration() const
{
    size_t p = m_pos;
    while (p < m_end && m_tokens[p].type == TokPreproc) ++p;
    if (p >= m_end || m_tokens[p].type != TokIdentifier) return false;
    size_t endPos;
    std::string text;
    bool known;
    if (!scanType(p, endPos, text, known)) return false;
    size_t q = endPos;
    bool ptr = false;
    while (q < m_end && m_tokens[q].type == TokPunct && (m_tokens[q].text == "*" || m_tokens[q].text == "&")) { ptr = true; ++q; while (q < m_end && m_tokens[q].type == TokIdentifier && m_tokens[q].text == "const") ++q; }
    if (q >= m_end || m_tokens[q].type != TokIdentifier || isKeyword(m_tokens[q].text)) return false;
    size_t r = q + 1;
    if (r >= m_end) return false;
    const std::string& after = m_tokens[r].text;
    if (!(after == ";" || after == "=" || after == "(" || after == "[" || after == ",")) return false;
    if (!known && ptr) {
        // "a * b" with unknown a is an expression, but "X* y = ..." is a declaration, and so is
        // "DIR* d;" when X looks like a type name (capitalised, all caps or ending in _t)
        if (after == "=") return true;
        std::string base = stripTemplateArgs(text);
        size_t sp = base.rfind(' ');
        if (sp != std::string::npos) base = base.substr(sp + 1);
        bool typeish = !base.empty() && (isupper(static_cast<unsigned char>(base[0])) || endsWith(base, "_t"));
        return typeish && after == ";";
    }
    return true;
}

// ---- statements ---------------------------------------------------------------------

bool BodyParser::parse(FunctionAst& out)
{
    m_out = &out;
    out.fn = &m_fn;
    out.parsed = true;
    if (!is("{")) { error("function body does not start with '{'"); out.parsed = false; return false; }
    out.body = parseBlock();
    if (!out.errors.empty()) out.parsed = false;
    return out.parsed;
}

void BodyParser::parseInitializers(FunctionAst& out, size_t begin, size_t end)
{
    size_t savePos = m_pos, saveEnd = m_end;
    m_out = &out;
    m_pos = begin;
    m_end = end;
    while (!atEnd() && cur().type != TokEnd) {
        InitEntry e;
        e.line = cur().line;
        std::string name;
        while (isIdent() || is("::")) {
            name += cur().text;
            advance();
            if (is("<") && (m_types.isTypeName(stripTemplateArgs(name), m_scope, m_owner) || name.find("::") != std::string::npos)) {
                size_t close = skipBalancedAt(m_pos);
                if (close < m_end) m_pos = close;
            }
        }
        e.name = stripTemplateArgs(name);
        if (name.empty() || !is("(")) { error("cannot parse the constructor initializer list"); break; }
        advance();
        if (!is(")")) for (;;) { e.args.push_back(parseAssign()); if (!accept(",")) break; }
        expect(")", "after initializer arguments");
        out.inits.push_back(e);
        if (!accept(",")) break;
    }
    m_pos = savePos;
    m_end = saveEnd;
}

Stmt* BodyParser::parseBlock()
{
    Stmt* b = m_arena.stmt(Stmt::Block, cur().line);
    expect("{", "to open a block");
    while (!atEnd() && !is("}")) {
        if (cur().type == TokEnd) break;
        size_t before = m_pos;
        Stmt* s = parseStatement();
        if (s) b->kids.push_back(s);
        if (m_pos == before) { error("cannot parse statement starting at '" + cur().text + "'"); advance(); recover(); }
    }
    expect("}", "to close a block");
    return b;
}

Stmt* BodyParser::parseSimple(Stmt::Kind k)
{
    Stmt* s = m_arena.stmt(k, cur().line);
    advance();
    if (k == Stmt::Return && !is(";")) s->expr = parseExpr();
    expect(";", "after statement");
    return s;
}

Stmt* BodyParser::parseStatement()
{
    const Token& t = cur();
    int line = t.line;
    if (t.type == TokPunct) {
        if (t.text == "{") return parseBlock();
        if (t.text == ";") { advance(); return m_arena.stmt(Stmt::Empty, line); }
    }
    if (t.type == TokIdentifier) {
        const std::string& w = t.text;
        if (w == "if") {
            Stmt* s = m_arena.stmt(Stmt::If, line);
            advance();
            expect("(", "after if");
            s->cond = parseExpr();
            expect(")", "after if condition");
            s->kids.push_back(parseStatement());
            if (accept("else")) s->kids.push_back(parseStatement());
            return s;
        }
        if (w == "while") {
            Stmt* s = m_arena.stmt(Stmt::While, line);
            advance();
            expect("(", "after while");
            s->cond = parseExpr();
            expect(")", "after while condition");
            s->kids.push_back(parseStatement());
            return s;
        }
        if (w == "do") {
            Stmt* s = m_arena.stmt(Stmt::DoWhile, line);
            advance();
            s->kids.push_back(parseStatement());
            expect("while", "after do body");
            expect("(", "after while");
            s->cond = parseExpr();
            expect(")", "after do-while condition");
            expect(";", "after do-while");
            return s;
        }
        if (w == "for") return parseFor();
        if (w == "switch") {
            Stmt* s = m_arena.stmt(Stmt::Switch, line);
            advance();
            expect("(", "after switch");
            s->cond = parseExpr();
            expect(")", "after switch condition");
            s->kids.push_back(parseStatement());
            return s;
        }
        if (w == "case") {
            Stmt* s = m_arena.stmt(Stmt::Case, line);
            advance();
            s->expr = parseCond();
            expect(":", "after case value");
            return s;
        }
        if (w == "default") { Stmt* s = m_arena.stmt(Stmt::Default, line); advance(); expect(":", "after default"); return s; }
        if (w == "break") return parseSimple(Stmt::Break);
        if (w == "continue") return parseSimple(Stmt::Continue);
        if (w == "return") return parseSimple(Stmt::Return);
        if (w == "try") return parseTry();
        if (w == "typedef") {
            std::string last;
            std::vector<Token> toks;
            advance();
            while (!atEnd() && !is(";") && !is("}")) { if (isIdent()) last = cur().text; toks.push_back(cur()); advance(); }
            accept(";");
            if (!last.empty()) {
                m_localTypes.insert(last);
                if (!toks.empty()) toks.pop_back();   // drop the alias name: the rest is the target type
                m_out->localTypedefs.push_back(std::make_pair(last, joinTokens(toks)));
            }
            return m_arena.stmt(Stmt::Empty, line);
        }
        if ((w == "class" || w == "struct" || w == "union" || w == "enum") && (peek(1).text == "{" || peek(2).text == "{" || peek(2).text == ":")) {
            // local type definition: remember the name as an opaque type, skip the body
            Stmt* s = m_arena.stmt(Stmt::Unsupported, line);
            s->text = "local " + w;
            advance();
            std::string name;
            if (isIdent()) {
                name = cur().text;
                m_localTypes.insert(name);
                m_out->localTypedefs.push_back(std::make_pair(name, "struct " + name + "@local"));
                s->text += " " + name;
            }
            if (is("{") || peek(1).text == "{") {
                if (!is("{")) advance();
                size_t close = skipBalancedAt(m_pos);
                if (w == "enum") {
                    // record the enumerators: name [= integer]
                    long value = 0;
                    int depth = 0;
                    for (size_t k = m_pos + 1; k + 1 < close; ++k) {
                        const Token& t2 = m_tokens[k];
                        if (t2.type == TokPunct) { if (t2.text == "(") ++depth; else if (t2.text == ")") --depth; continue; }
                        if (depth != 0 || t2.type != TokIdentifier) continue;
                        size_t prev = k - 1;
                        if (m_tokens[prev].text != "{" && m_tokens[prev].text != ",") continue;
                        std::string en = t2.text;
                        if (k + 2 < close && m_tokens[k + 1].text == "=" && m_tokens[k + 2].type == TokNumber) value = std::strtol(m_tokens[k + 2].text.c_str(), 0, 0);
                        m_out->localEnumerators.push_back(std::make_pair(en, value));
                        ++value;
                    }
                }
                m_pos = close;
            }
            while (!atEnd() && !is(";") && !is("}")) advance();
            accept(";");
            return s;
        }
        if (w == "goto" || w == "asm" || w == "__asm") {
            Stmt* s = m_arena.stmt(Stmt::Unsupported, line);
            s->text = w;
            error("'" + w + "' is outside the version 3 subset; statement skipped");
            advance();
            recover();
            return s;
        }
        if (looksLikeDeclaration()) return parseDeclaration();
        // label?  "name :" (not "::")
        if (peek(1).text == ":" ) {
            Stmt* s = m_arena.stmt(Stmt::Unsupported, line);
            s->text = "label " + w;
            error("label '" + w + "' is outside the version 3 subset");
            advance(); advance();
            return s;
        }
    }
    Stmt* s = m_arena.stmt(Stmt::ExprStmt, line);
    s->expr = parseExpr();
    if (!expect(";", "after expression")) recover();
    return s;
}

Stmt* BodyParser::parseDeclaration()
{
    int line = cur().line;
    size_t endPos;
    std::string typeText;
    bool known;
    scanType(m_pos, endPos, typeText, known);
    m_pos = endPos;
    Stmt* group = m_arena.stmt(Stmt::Block, line);
    group->text = "declgroup";
    for (;;) {
        Stmt* d = m_arena.stmt(Stmt::Decl, cur().line);
        std::string suffix;
        while (is("*") || is("&") || is("const")) { suffix += cur().text; advance(); }
        d->typeText = typeText + suffix;
        if (!isIdent()) { error("expected a variable name in declaration"); recover(); return group; }
        d->name = cur().text;
        advance();
        while (is("[")) {
            advance();
            std::string dim;
            while (!atEnd() && !is("]")) { dim += cur().text; advance(); }
            expect("]", "after array size");
            d->typeText += "[" + dim + "]";
        }
        if (accept("=")) {
            if (is("{")) {
                // aggregate initializer: kept out of the subset
                m_pos = skipBalancedAt(m_pos);
                d->text = "braced-initializer";
            } else {
                d->expr = parseAssign();
            }
        } else if (is("(")) {
            d->ctorStyle = true;
            advance();
            if (!is(")")) {
                for (;;) {
                    d->args.push_back(parseAssign());
                    if (!accept(",")) break;
                }
            }
            expect(")", "after constructor arguments");
        }
        AstLocal l;
        l.name = d->name;
        l.typeText = d->typeText;
        l.line = d->line;
        m_out->locals.push_back(l);
        d->localIndex = static_cast<int>(m_out->locals.size() - 1);
        group->kids.push_back(d);
        if (accept(",")) continue;
        break;
    }
    if (!expect(";", "after declaration")) recover();
    return group->kids.size() == 1 ? group->kids[0] : group;
}

Stmt* BodyParser::parseFor()
{
    Stmt* s = m_arena.stmt(Stmt::For, cur().line);
    advance();
    expect("(", "after for");
    if (!is(";")) {
        if (looksLikeDeclaration()) s->init = parseDeclaration();   // consumes the ';'
        else {
            Stmt* e = m_arena.stmt(Stmt::ExprStmt, cur().line);
            e->expr = parseExpr();
            s->init = e;
            expect(";", "after for initializer");
        }
    } else {
        advance();
    }
    if (!is(";")) s->cond = parseExpr();
    expect(";", "after for condition");
    if (!is(")")) s->step = parseExpr();
    expect(")", "after for step");
    s->kids.push_back(parseStatement());
    return s;
}

Stmt* BodyParser::parseTry()
{
    Stmt* s = m_arena.stmt(Stmt::Try, cur().line);
    advance();
    s->kids.push_back(parseBlock());
    while (is("catch")) {
        Stmt* c = m_arena.stmt(Stmt::Catch, cur().line);
        advance();
        expect("(", "after catch");
        if (is("...")) { c->typeText = "..."; advance(); }
        else {
            size_t endPos;
            std::string text;
            bool known;
            if (scanType(m_pos, endPos, text, known)) {
                m_pos = endPos;
                while (is("*") || is("&") || is("const")) { text += cur().text; advance(); }
                c->typeText = text;
                if (isIdent()) { c->name = cur().text; advance(); }
            } else {
                error("cannot parse catch parameter");
                while (!atEnd() && !is(")")) advance();
            }
        }
        expect(")", "after catch parameter");
        if (!c->name.empty()) {
            AstLocal l;
            l.name = c->name;
            l.typeText = c->typeText;
            l.line = c->line;
            m_out->locals.push_back(l);
            c->localIndex = static_cast<int>(m_out->locals.size() - 1);
        }
        c->kids.push_back(parseBlock());
        s->kids.push_back(c);
    }
    if (s->kids.size() == 1) error("try without catch");
    return s;
}

// ---- expressions --------------------------------------------------------------------

int BodyParser::binaryPrec(const std::string& op) const
{
    if (op == "*" || op == "/" || op == "%") return 13;
    if (op == "+" || op == "-") return 12;
    if (op == "<<" || op == ">>") return 11;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 10;
    if (op == "==" || op == "!=") return 9;
    if (op == "&") return 8;
    if (op == "^") return 7;
    if (op == "|") return 6;
    if (op == "&&") return 5;
    if (op == "||") return 4;
    return -1;
}

Expr* BodyParser::parseExpr()
{
    Expr* e = parseAssign();
    while (is(",")) {
        Expr* c = m_arena.expr(Expr::Comma, cur().line);
        advance();
        c->kids.push_back(e);
        c->kids.push_back(parseAssign());
        e = c;
    }
    return e;
}

Expr* BodyParser::parseAssign()
{
    if (is("throw")) {
        Expr* t = m_arena.expr(Expr::Throw, cur().line);
        advance();
        if (!is(";") && !is(")") && !is(",") && !is("}")) t->kids.push_back(parseAssign());
        return t;
    }
    Expr* lhs = parseCond();
    if (cur().type == TokPunct && isAssignOp(cur().text)) {
        Expr* a = m_arena.expr(Expr::Assign, cur().line);
        a->text = cur().text;
        advance();
        a->kids.push_back(lhs);
        a->kids.push_back(parseAssign());
        return a;
    }
    return lhs;
}

Expr* BodyParser::parseCond()
{
    Expr* c = parseBinary(4);
    if (is("?")) {
        Expr* q = m_arena.expr(Expr::Cond, cur().line);
        advance();
        q->kids.push_back(c);
        q->kids.push_back(parseExpr());
        expect(":", "in conditional expression");
        q->kids.push_back(parseCond());
        return q;
    }
    return c;
}

Expr* BodyParser::parseBinary(int minPrec)
{
    Expr* lhs = parseUnary();
    for (;;) {
        const Token& t = cur();
        if (t.type != TokPunct) break;
        int prec = binaryPrec(t.text);
        if (prec < 0 || prec < minPrec) break;
        Expr* b = m_arena.expr(Expr::Binary, t.line);
        b->text = t.text;
        advance();
        b->kids.push_back(lhs);
        b->kids.push_back(parseBinary(prec + 1));
        lhs = b;
    }
    return lhs;
}

Expr* BodyParser::parseUnary()
{
    const Token& t = cur();
    if (t.type == TokPunct && (t.text == "-" || t.text == "+" || t.text == "!" || t.text == "~" || t.text == "*" || t.text == "&" || t.text == "++" || t.text == "--")) {
        Expr* u = m_arena.expr(Expr::Unary, t.line);
        u->text = t.text;
        advance();
        u->kids.push_back(parseUnary());
        return u;
    }
    if (t.type == TokIdentifier) {
        if (t.text == "sizeof") {
            Expr* s = m_arena.expr(Expr::SizeOf, t.line);
            advance();
            if (is("(")) {
                size_t endPos;
                std::string text;
                bool known;
                size_t inner = m_pos + 1;
                while (inner < m_end && m_tokens[inner].type == TokPreproc) ++inner;
                if (scanType(inner, endPos, text, known) && known) {
                    size_t q = endPos;
                    while (q < m_end && (m_tokens[q].text == "*" || m_tokens[q].text == "&")) { text += m_tokens[q].text; ++q; }
                    if (q < m_end && m_tokens[q].text == ")") { s->typeText = text; m_pos = q + 1; return s; }
                }
                advance();
                s->kids.push_back(parseExpr());
                expect(")", "after sizeof");
            } else {
                s->kids.push_back(parseUnary());
            }
            return s;
        }
        if (t.text == "new") return parseNew(t.line);
        if (t.text == "delete") {
            Expr* d = m_arena.expr(Expr::Delete, t.line);
            advance();
            if (is("[")) { advance(); expect("]", "after delete["); d->newArray = true; }
            d->kids.push_back(parseUnary());
            return d;
        }
        if (t.text == "static_cast" || t.text == "dynamic_cast" || t.text == "reinterpret_cast" || t.text == "const_cast") {
            Expr* c = m_arena.expr(Expr::Cast, t.line);
            c->text = t.text;
            advance();
            if (expect("<", "after cast keyword")) {
                std::string text;
                int depth = 1;
                while (!atEnd() && depth > 0) {
                    if (is("<")) ++depth;
                    else if (is(">")) { if (--depth == 0) break; }
                    text += (text.empty() ? "" : " ") + cur().text;
                    advance();
                }
                expect(">", "after cast type");
                c->typeText = text;
            }
            expect("(", "after cast type");
            c->kids.push_back(parseExpr());
            expect(")", "after cast operand");
            return parsePostfix(c);
        }
    }
    return parsePostfix(parsePrimary());
}

Expr* BodyParser::parseNew(int line)
{
    Expr* n = m_arena.expr(Expr::New, line);
    advance();
    if (is("(")) {   // placement new: skip the placement arguments
        m_pos = skipBalancedAt(m_pos);
    }
    bool paren = accept("(");
    size_t endPos;
    std::string text;
    bool known;
    if (!scanType(m_pos, endPos, text, known)) { error("expected a type after new"); return n; }
    m_pos = endPos;
    while (is("*") || is("&")) { text += cur().text; advance(); }
    n->typeText = text;
    if (paren) expect(")", "after new type");
    if (is("[")) {
        advance();
        n->newArray = true;
        n->kids.push_back(parseExpr());
        expect("]", "after array size");
        return n;
    }
    if (is("(")) {
        advance();
        if (!is(")")) for (;;) { n->kids.push_back(parseAssign()); if (!accept(",")) break; }
        expect(")", "after constructor arguments");
    }
    return n;
}

void BodyParser::parseArgs(Expr* call)
{
    expect("(", "to start arguments");
    if (!is(")")) {
        for (;;) {
            call->kids.push_back(parseAssign());
            if (!accept(",")) break;
        }
    }
    expect(")", "to end arguments");
}

Expr* BodyParser::parsePostfix(Expr* e)
{
    for (;;) {
        const Token& t = cur();
        if (t.type != TokPunct) break;
        if (t.text == "(") {
            Expr* c = m_arena.expr(Expr::Call, t.line);
            c->kids.push_back(e);
            parseArgs(c);
            e = c;
            continue;
        }
        if (t.text == "[") {
            Expr* ix = m_arena.expr(Expr::Index, t.line);
            advance();
            ix->kids.push_back(e);
            ix->kids.push_back(parseExpr());
            expect("]", "after index");
            e = ix;
            continue;
        }
        if (t.text == "." || t.text == "->") {
            Expr* m = m_arena.expr(Expr::Member, t.line);
            m->arrow = (t.text == "->");
            advance();
            if (is("~")) { advance(); m->text = "~"; }
            if (isIdent()) { m->text += cur().text; advance(); }
            else { error("expected a member name after '" + t.text + "'"); }
            if (is("<") && m_types.isTypeName(m->text, m_scope, m_owner)) m_pos = skipBalancedAt(m_pos);
            m->kids.push_back(e);
            e = m;
            continue;
        }
        if (t.text == "++" || t.text == "--") {
            Expr* p = m_arena.expr(Expr::Postfix, t.line);
            p->text = t.text;
            advance();
            p->kids.push_back(e);
            e = p;
            continue;
        }
        break;
    }
    return e;
}

bool BodyParser::startsExpression(size_t pos) const
{
    while (pos < m_end && m_tokens[pos].type == TokPreproc) ++pos;
    if (pos >= m_end) return false;
    const Token& t = m_tokens[pos];
    if (t.type == TokIdentifier) return !isKeyword(t.text) || t.text == "this" || t.text == "new" || t.text == "sizeof" || t.text == "true" || t.text == "false";
    if (t.type == TokNumber || t.type == TokString || t.type == TokChar) return true;
    if (t.type == TokPunct) return t.text == "(" || t.text == "-" || t.text == "+" || t.text == "!" || t.text == "~" || t.text == "*" || t.text == "&" || t.text == "++" || t.text == "--";
    return false;
}

Expr* BodyParser::parseName()
{
    Expr* n = m_arena.expr(Expr::Name, cur().line);
    std::string name;
    if (is("::")) { name = "::"; advance(); }
    for (;;) {
        if (!isIdent()) break;
        name += cur().text;
        advance();
        if (is("<")) {
            // template arguments: certain for known types; for qualified names (std::vector<T>)
            // accepted when the closing '>' is followed by '(' or '::'
            bool knownType = m_types.isTypeName(stripTemplateArgs(name), m_scope, m_owner) || m_localTypes.count(stripTemplateArgs(name)) != 0;
            size_t close = skipBalancedAt(m_pos);
            bool accept = false;
            if (close < m_end && close > m_pos) {
                if (knownType) accept = true;
                else if (name.find("::") != std::string::npos) {
                    size_t after = close;
                    while (after < m_end && m_tokens[after].type == TokPreproc) ++after;
                    if (after < m_end && (m_tokens[after].text == "(" || m_tokens[after].text == "::")) accept = true;
                }
            }
            if (accept) {
                std::vector<Token> args(m_tokens.begin() + static_cast<long>(m_pos), m_tokens.begin() + static_cast<long>(close));
                n->typeText = joinTokens(args);
                m_pos = close;
            }
        }
        if (is("::") && (peek(1).type == TokIdentifier || peek(1).text == "~")) {
            advance();
            name += "::";
            if (is("~")) { name += "~"; advance(); }
            continue;
        }
        break;
    }
    n->text = name;
    return n;
}

Expr* BodyParser::parsePrimary()
{
    const Token& t = cur();
    int line = t.line;
    if (t.type == TokNumber) {
        bool isFloat = false;
        bool hex = t.text.size() > 1 && t.text[0] == '0' && (t.text[1] == 'x' || t.text[1] == 'X');
        for (size_t i = 0; i < t.text.size() && !hex; ++i)
            if (t.text[i] == '.' || t.text[i] == 'e' || t.text[i] == 'E' || t.text[i] == 'f' || t.text[i] == 'F') isFloat = true;
        Expr* e = m_arena.expr(isFloat ? Expr::FloatLit : Expr::IntLit, line);
        e->text = t.text;
        advance();
        return e;
    }
    if (t.type == TokString) {
        Expr* e = m_arena.expr(Expr::StringLit, line);
        e->text = t.text;
        advance();
        while (cur().type == TokString) { e->text += cur().text; advance(); }   // adjacent literals
        return e;
    }
    if (t.type == TokChar) { Expr* e = m_arena.expr(Expr::CharLit, line); e->text = t.text; advance(); return e; }
    if (t.type == TokPunct && t.text == "(") {
        // cast?
        size_t inner = m_pos + 1;
        while (inner < m_end && m_tokens[inner].type == TokPreproc) ++inner;
        size_t endPos;
        std::string text;
        bool known;
        if (inner < m_end && m_tokens[inner].type == TokIdentifier && scanType(inner, endPos, text, known) && known) {
            size_t q = endPos;
            while (q < m_end && (m_tokens[q].text == "*" || m_tokens[q].text == "&" || m_tokens[q].text == "const")) { text += m_tokens[q].text; ++q; }
            if (q < m_end && m_tokens[q].text == ")" && startsExpression(q + 1)) {
                Expr* c = m_arena.expr(Expr::Cast, line);
                c->text = "(cast)";
                c->typeText = text;
                m_pos = q + 1;
                c->kids.push_back(parseUnary());
                return c;
            }
        }
        advance();
        Expr* e = parseExpr();
        expect(")", "to close parenthesis");
        return e;
    }
    if (t.type == TokIdentifier) {
        if (t.text == "this") { advance(); return m_arena.expr(Expr::This, line); }
        if (t.text == "true" || t.text == "false") { Expr* e = m_arena.expr(Expr::BoolLit, line); e->text = t.text; advance(); return e; }
        if (isBuiltinWord(t.text) || t.text == "const" || t.text == "unsigned") {
            // functional cast "int(x)" / "unsigned long(x)"
            size_t endPos;
            std::string text;
            bool known;
            if (scanType(m_pos, endPos, text, known)) {
                m_pos = endPos;
                Expr* c = m_arena.expr(Expr::Cast, line);
                c->text = "(functional)";
                c->typeText = text;
                if (expect("(", "after type in functional cast")) {
                    if (!is(")")) c->kids.push_back(parseExpr());
                    expect(")", "after functional cast");
                }
                return c;
            }
        }
        if (!isKeyword(t.text) || t.text == "operator") return parseName();
        error("unexpected keyword '" + t.text + "' in expression");
        advance();
        return m_arena.expr(Expr::Error, line);
    }
    if (t.type == TokPunct && t.text == "::") return parseName();
    error("unexpected '" + (t.type == TokEnd ? std::string("end of body") : t.text) + "' in expression");
    if (t.type != TokEnd) advance();
    return m_arena.expr(Expr::Error, line);
}

} // namespace cppparse
