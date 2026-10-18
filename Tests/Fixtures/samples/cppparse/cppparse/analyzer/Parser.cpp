// Parser.cpp - tolerant declaration parser for C++98 headers and sources.
#include "Parser.hpp"
#include "PathUtil.hpp"

#include <cctype>

namespace cppparse {

namespace {

bool isAccessWord(const std::string& s)
{
    return s == "public" || s == "protected" || s == "private";
}

Access accessFromWord(const std::string& s)
{
    if (s == "public") return AccessPublic;
    if (s == "protected") return AccessProtected;
    return AccessPrivate;
}

bool isClassKey(const std::string& s)
{
    return s == "class" || s == "struct" || s == "union";
}

bool isAllCaps(const std::string& s)
{
    bool letter = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (islower(c)) return false;
        if (isupper(c)) letter = true;
    }
    return letter;
}

bool isTypeKeyword(const std::string& s)
{
    return s == "int" || s == "char" || s == "short" || s == "long" || s == "unsigned" ||
           s == "signed" || s == "float" || s == "double" || s == "void" || s == "bool" ||
           s == "const" || s == "volatile" || s == "wchar_t" || s == "__int64" || s == "__int32";
}

// words that can never start a local variable declaration
bool isStatementKeyword(const std::string& s)
{
    return s == "return" || s == "if" || s == "else" || s == "while" || s == "for" ||
           s == "do" || s == "switch" || s == "case" || s == "default" || s == "break" ||
           s == "continue" || s == "goto" || s == "delete" || s == "new" || s == "throw" ||
           s == "try" || s == "catch" || s == "typedef" || s == "using" || s == "namespace" ||
           s == "class" || s == "struct" || s == "union" || s == "enum" || s == "sizeof" ||
           s == "asm" || s == "__asm" || s == "operator" || s == "template" || s == "this" ||
           s == "true" || s == "false" || s == "public" || s == "private" || s == "protected";
}

bool isStorageWord(const std::string& s)
{
    return s == "static" || s == "register" || s == "volatile" || s == "mutable" ||
           s == "typename" || s == "extern";
}

std::string firstWord(const std::string& s)
{
    std::string w;
    for (size_t i = 0; i < s.size(); ++i) {
        if (isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') w += s[i];
        else break;
    }
    return w;
}

} // namespace

// ---- token joining -----------------------------------------------------------

std::string joinTokens(const std::vector<Token>& v, size_t from, size_t to)
{
    std::string out;
    for (size_t i = from; i < to && i < v.size(); ++i) {
        const std::string& s = v[i].text;
        bool space = !out.empty();
        if (space) {
            const std::string& prev = v[i - 1].text;
            bool prevWord = (v[i - 1].type == TokIdentifier || v[i - 1].type == TokNumber);
            if (prev == "::" || prev == "(" || prev == "[" || prev == "<" || prev == "~" ||
                prev == "!" || prev == ".")
                space = false;
            if (s == "::" || s == "," || s == ")" || s == "]" || s == ";" || s == "." ||
                s == "(" || s == "[")
                space = false;
            if (s == ">" && prev != ">") space = false;
            if ((s == "*" || s == "&") && (prevWord || prev == ">" || prev == ")" || prev == "*" || prev == "&"))
                space = false;
            if ((prev == "*" || prev == "&") && (s == "*" || s == "&" || s == ")" )) space = false;
            if (s == "<" && prevWord) space = false;
            // function pointer declarators: "void (*name)(int)"
            if (s == "(" && prevWord && i + 1 < v.size() && (v[i + 1].text == "*" || v[i + 1].text == "&")) space = true;
            if ((prev == "*" || prev == "&") && i >= 2 && v[i - 2].text == "(") space = false;
        }
        if (space) out += ' ';
        out += s;
    }
    return out;
}

std::string joinTokens(const std::vector<Token>& v)
{
    return joinTokens(v, 0, v.size());
}

// ---- construction -----------------------------------------------------------

Parser::Parser(SourceFile& out, const std::vector<Token>& tokens)
    : m_file(out), m_tokens(tokens), m_pos(0), m_externC(false)
{
    m_end.type = TokEnd;
    m_end.line = tokens.empty() ? 0 : tokens.back().line;
}

void Parser::parse()
{
    parseScopeBody(false);
    if (!m_cond.empty()) {
        diag(m_end.line, "unbalanced #if: " + intToString(static_cast<int>(m_cond.size())) +
             " level(s) still open at end of file");
    }
}

// ---- token access -----------------------------------------------------------

const Token& Parser::cur()
{
    while (m_pos < m_tokens.size() && m_tokens[m_pos].type == TokPreproc) {
        processDirective(m_pos);
        ++m_pos;
    }
    if (m_pos < m_tokens.size()) return m_tokens[m_pos];
    return m_end;
}

const Token& Parser::peek(size_t n)
{
    cur();
    size_t j = m_pos;
    for (size_t k = 0; k < n; ++k) {
        j = rawNext(j);
        if (j >= m_tokens.size()) return m_end;
    }
    if (j >= m_tokens.size()) return m_end;
    return m_tokens[j];
}

void Parser::advance()
{
    cur();
    if (m_pos < m_tokens.size()) ++m_pos;
}

bool Parser::atEnd()
{
    return cur().type == TokEnd;
}

bool Parser::is(const char* text)
{
    const Token& t = cur();
    return t.type != TokEnd && t.text == text;
}

bool Parser::isPunct(const char* text)
{
    const Token& t = cur();
    return t.type == TokPunct && t.text == text;
}

size_t Parser::rawNext(size_t idx) const
{
    size_t j = idx + 1;
    while (j < m_tokens.size() && m_tokens[j].type == TokPreproc) ++j;
    return j;
}

size_t Parser::matchClose(size_t openIdx) const
{
    if (openIdx >= m_tokens.size()) return std::string::npos;
    const std::string& open = m_tokens[openIdx].text;
    std::string close;
    if (open == "(") close = ")";
    else if (open == "[") close = "]";
    else if (open == "{") close = "}";
    else if (open == "<") close = ">";
    else return std::string::npos;

    int depth = 0;
    for (size_t j = openIdx; j < m_tokens.size(); ++j) {
        const Token& t = m_tokens[j];
        if (t.type != TokPunct) continue;
        if (t.text == open) ++depth;
        else if (t.text == close) { --depth; if (depth == 0) return j; }
        else if (open == "<" && t.text == ">>") { depth -= 2; if (depth <= 0) return j; }
        else if (open == "<" && (t.text == ";" || t.text == "{" || t.text == "}")) return std::string::npos;
    }
    return std::string::npos;
}

// ---- preprocessor -----------------------------------------------------------

Directive Parser::describeDirective(const Token& t, std::string& word, std::string& rest) const
{
    Directive d;
    d.line = t.line;
    d.text = t.text;
    word = firstWord(t.text);
    rest = trim(t.text.substr(word.size()));

    if (word.empty()) {
        d.kind = (!rest.empty() && isdigit(static_cast<unsigned char>(rest[0]))) ? Directive::Line : Directive::Other;
        d.argument = rest;
        return d;
    }
    if (word == "include" || word == "include_next" || word == "import") {
        d.kind = Directive::Include;
        if (!rest.empty() && rest[0] == '<') {
            d.systemInclude = true;
            size_t e = rest.find('>');
            d.argument = rest.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        } else if (!rest.empty() && rest[0] == '"') {
            size_t e = rest.find('"', 1);
            d.argument = rest.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        } else {
            d.argument = rest; // #include MACRO
        }
    } else if (word == "define") {
        d.kind = Directive::Define;
        size_t i = 0;
        while (i < rest.size() && (isalnum(static_cast<unsigned char>(rest[i])) || rest[i] == '_')) ++i;
        d.argument = rest.substr(0, i);
        if (i < rest.size() && rest[i] == '(') {
            size_t e = rest.find(')', i);
            if (e == std::string::npos) e = rest.size() - 1;
            d.macroParams = rest.substr(i, e - i + 1);
            i = e + 1;
        }
        d.macroBody = trim(rest.substr(i));
    } else if (word == "undef")   { d.kind = Directive::Undef;   d.argument = rest; }
    else if (word == "ifdef")     { d.kind = Directive::Ifdef;   d.argument = rest; }
    else if (word == "ifndef")    { d.kind = Directive::Ifndef;  d.argument = rest; }
    else if (word == "if")        { d.kind = Directive::If;      d.argument = rest; }
    else if (word == "elif")      { d.kind = Directive::Elif;    d.argument = rest; }
    else if (word == "else")      { d.kind = Directive::Else;    d.argument = rest; }
    else if (word == "endif")     { d.kind = Directive::Endif;   d.argument = rest; }
    else if (word == "pragma")    { d.kind = Directive::Pragma;  d.argument = rest; }
    else if (word == "error")     { d.kind = Directive::Error;   d.argument = rest; }
    else if (word == "warning")   { d.kind = Directive::Warning; d.argument = rest; }
    else if (word == "line")      { d.kind = Directive::Line;    d.argument = rest; }
    else                          { d.kind = Directive::Other;   d.argument = rest; }
    return d;
}

void Parser::processDirective(size_t idx)
{
    const Token& t = m_tokens[idx];
    std::string word, rest;
    Directive d = describeDirective(t, word, rest);

    switch (d.kind) {
    case Directive::Ifndef: {
        // include guard: "#ifndef X" as the first real directive, immediately followed by "#define X"
        bool onlyPragmasBefore = true;
        for (size_t i = 0; i < m_file.directives.size(); ++i)
            if (m_file.directives[i].kind != Directive::Pragma) { onlyPragmasBefore = false; break; }
        bool guard = false;
        if (onlyPragmasBefore && m_cond.empty() && idx + 1 < m_tokens.size() &&
            m_tokens[idx + 1].type == TokPreproc) {
            std::string w2, r2;
            Directive next = describeDirective(m_tokens[idx + 1], w2, r2);
            if (next.kind == Directive::Define && next.argument == rest && !rest.empty()) guard = true;
        }
        CondLevel lvl;
        lvl.guard = guard;
        if (guard) { m_guardMacro = rest; m_file.includeGuard = rest; d.isIncludeGuard = true; }
        else { lvl.current = "!defined(" + rest + ")"; lvl.negated = "defined(" + rest + ")"; }
        m_cond.push_back(lvl);
        break;
    }
    case Directive::Ifdef: {
        CondLevel lvl;
        lvl.current = "defined(" + rest + ")";
        lvl.negated = "!defined(" + rest + ")";
        m_cond.push_back(lvl);
        break;
    }
    case Directive::If: {
        CondLevel lvl;
        if (rest == "1") { lvl.current = ""; lvl.negated = "0"; }
        else if (rest == "0") { lvl.current = "0"; lvl.negated = ""; }
        else { lvl.current = "(" + rest + ")"; lvl.negated = "!(" + rest + ")"; }
        m_cond.push_back(lvl);
        break;
    }
    case Directive::Elif: {
        if (m_cond.empty()) { diag(t.line, "#elif without #if"); break; }
        CondLevel& lvl = m_cond.back();
        lvl.current = lvl.negated.empty() ? "(" + rest + ")" : lvl.negated + " && (" + rest + ")";
        lvl.negated = lvl.negated.empty() ? "!(" + rest + ")" : lvl.negated + " && !(" + rest + ")";
        if (rest == "0") skipDisabledRegion();
        break;
    }
    case Directive::Else: {
        if (m_cond.empty()) { diag(t.line, "#else without #if"); break; }
        CondLevel& lvl = m_cond.back();
        lvl.current = lvl.negated;
        if (lvl.guard) lvl.current = "";
        break;
    }
    case Directive::Endif: {
        if (m_cond.empty()) { diag(t.line, "#endif without #if"); break; }
        if (m_cond.back().guard) d.isIncludeGuard = true;
        m_cond.pop_back();
        break;
    }
    case Directive::Define:
        if (!m_guardMacro.empty() && d.argument == m_guardMacro && m_file.directives.size() > 0 &&
            m_file.directives.back().kind == Directive::Ifndef && m_file.directives.back().isIncludeGuard)
            d.isIncludeGuard = true;
        break;
    case Directive::Pragma:
        if (rest == "once") m_file.pragmaOnce = true;
        break;
    default:
        break;
    }

    m_file.directives.push_back(d);
    if (d.kind == Directive::If && rest == "0") skipDisabledRegion();
}

// Called with m_pos at an "#if 0" (or "#elif 0") token. Records the directives
// of the dead region as inactive and moves the cursor to just before the
// matching #else / #elif / #endif, which the normal cur() loop then processes.
void Parser::skipDisabledRegion()
{
    int depth = 0;
    size_t j = m_pos + 1;
    for (; j < m_tokens.size(); ++j) {
        const Token& tk = m_tokens[j];
        if (tk.type != TokPreproc) continue;
        std::string w, r;
        Directive d = describeDirective(tk, w, r);
        if (d.kind == Directive::If || d.kind == Directive::Ifdef || d.kind == Directive::Ifndef) {
            ++depth;
        } else if (d.kind == Directive::Endif) {
            if (depth == 0) break;
            --depth;
        } else if ((d.kind == Directive::Else || d.kind == Directive::Elif) && depth == 0) {
            break;
        }
        d.inactive = true;
        m_file.directives.push_back(d);
    }
    if (j < m_tokens.size()) m_pos = j - 1;
    else m_pos = m_tokens.size() - 1;
}

std::string Parser::currentCondition() const
{
    std::string out;
    for (size_t i = 0; i < m_cond.size(); ++i) {
        if (m_cond[i].guard || m_cond[i].current.empty()) continue;
        if (!out.empty()) out += " && ";
        out += m_cond[i].current;
    }
    return out;
}

std::string Parser::scopeName() const
{
    std::string out;
    for (size_t i = 0; i < m_scopes.size(); ++i) {
        if (i) out += "::";
        out += m_scopes[i];
    }
    return out;
}

void Parser::diag(int line, const std::string& msg)
{
    m_file.diagnostics.push_back("L" + intToString(line) + ": " + msg);
}

// ---- recovery / skipping ----------------------------------------------------

// cur() is at an opening bracket; consumes through the matching closer.
// If collect is given, the tokens between (exclusive) are appended to it.
void Parser::skipBalanced(std::vector<Token>* collect)
{
    const Token& openTok = cur();
    if (openTok.type != TokPunct) return;
    std::string open = openTok.text, close;
    if (open == "(") close = ")";
    else if (open == "[") close = "]";
    else if (open == "{") close = "}";
    else if (open == "<") close = ">";
    else return;

    int depth = 0;
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) return;
        if (t.type == TokPunct) {
            if (t.text == open) {
                ++depth;
                if (depth > 1 && collect) collect->push_back(t);
                advance();
                continue;
            }
            if (t.text == close) {
                --depth;
                if (depth == 0) { advance(); return; }
                if (collect) collect->push_back(t);
                advance();
                continue;
            }
            if (open == "<" && t.text == ">>") {
                depth -= 2;
                if (depth <= 0) { advance(); return; }
                if (collect) collect->push_back(t);
                advance();
                continue;
            }
            if (open == "<" && (t.text == ";" || t.text == "{" || t.text == "}")) return; // not a template
        }
        if (collect) collect->push_back(t);
        advance();
    }
}

// Recovery: skip to and past the next ';' at depth 0, or past a '{...}' block.
// A '}' that closes the enclosing scope is left alone.
void Parser::skipStatement()
{
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) return;
        if (t.type == TokPunct) {
            if (t.text == ";") { advance(); return; }
            if (t.text == "}") return;
            if (t.text == "{") { skipBalanced(0); if (isPunct(";")) advance(); return; }
            if (t.text == "(" || t.text == "[") { skipBalanced(0); continue; }
        }
        advance();
    }
}

// ---- top level --------------------------------------------------------------

void Parser::parseScopeBody(bool untilBrace)
{
    TemplateInfo noTmpl;
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) return;

        if (t.type == TokPunct) {
            if (t.text == "}") {
                if (untilBrace) { advance(); return; }
                diag(t.line, "stray '}' at file scope");
                advance();
                continue;
            }
            if (t.text == ";") { advance(); continue; }
            if (t.text == "{") {
                diag(t.line, "unexpected '{' at namespace scope, block skipped");
                skipBalanced(0);
                continue;
            }
            parseDeclaration(0, AccessNone, noTmpl);
            continue;
        }

        if (t.type == TokIdentifier) {
            const std::string& w = t.text;
            if (w == "namespace") { parseNamespace(); continue; }
            if (w == "using") { parseUsing(0); continue; }
            if (w == "extern" && peek(1).type == TokString) {
                advance(); advance();
                if (isPunct("{")) {
                    advance();
                    bool saved = m_externC;
                    m_externC = true;
                    parseScopeBody(true);
                    m_externC = saved;
                } else {
                    parseDeclaration(0, AccessNone, noTmpl);
                }
                continue;
            }
            if (w == "template") {
                if (peek(1).text != "<") {
                    // explicit instantiation: template class Foo<int>;
                    int line = t.line;
                    std::vector<Token> toks;
                    advance();
                    while (!atEnd() && !isPunct(";")) { toks.push_back(cur()); advance(); }
                    if (isPunct(";")) advance();
                    diag(line, "explicit instantiation: template " + joinTokens(toks) + ";");
                    continue;
                }
                TemplateInfo tmpl = parseTemplateHeader();
                while (is("template") && peek(1).text == "<") {
                    TemplateInfo more = parseTemplateHeader();
                    tmpl.headers.insert(tmpl.headers.end(), more.headers.begin(), more.headers.end());
                    tmpl.params.insert(tmpl.params.end(), more.params.begin(), more.params.end());
                }
                if (isClassKey(cur().text)) parseClass(0, AccessNone, tmpl, false);
                else parseDeclaration(0, AccessNone, tmpl);
                continue;
            }
            if (isClassKey(w)) { parseClass(0, AccessNone, noTmpl, false); continue; }
            if (w == "enum") { parseEnum(0, AccessNone, false); continue; }
            if (w == "typedef") { parseTypedef(0, AccessNone); continue; }
            if (w == "static_assert" || w == "__pragma" || w == "_Static_assert") {
                advance();
                if (isPunct("(")) skipBalanced(0);
                if (isPunct(";")) advance();
                continue;
            }
            if (w == "asm" || w == "__asm") { diag(t.line, "asm block skipped"); skipStatement(); continue; }
            parseDeclaration(0, AccessNone, noTmpl);
            continue;
        }

        diag(t.line, "unexpected token '" + t.text + "' at namespace scope");
        advance();
    }
}

void Parser::parseNamespace()
{
    int line = cur().line;
    advance(); // namespace
    std::string name = "(anonymous)";
    if (isIdent()) { name = cur().text; advance(); }
    if (isPunct("=")) {
        // namespace alias
        UsingInfo u;
        u.file = m_file.path;
        u.line = line;
        std::vector<Token> toks;
        advance();
        while (!atEnd() && !isPunct(";")) { toks.push_back(cur()); advance(); }
        if (isPunct(";")) advance();
        u.text = "namespace " + name + " = " + joinTokens(toks);
        m_file.usings.push_back(u);
        return;
    }
    if (!isPunct("{")) { diag(line, "malformed namespace declaration"); skipStatement(); return; }
    advance();
    m_scopes.push_back(name);
    addUnique(m_file.namespaces, scopeName());
    parseScopeBody(true);
    m_scopes.pop_back();
}

void Parser::parseUsing(ClassInfo* owner)
{
    UsingInfo u;
    u.file = m_file.path;
    u.line = cur().line;
    std::vector<Token> toks;
    while (!atEnd() && !isPunct(";") && !isPunct("}")) { toks.push_back(cur()); advance(); }
    if (isPunct(";")) advance();
    u.text = joinTokens(toks);
    if (owner) owner->usings.push_back(u);
    else m_file.usings.push_back(u);
}

TemplateInfo Parser::parseTemplateHeader()
{
    TemplateInfo t;
    t.isTemplate = true;
    advance(); // template
    if (!isPunct("<")) return t;
    std::vector<Token> toks;
    skipBalanced(&toks);
    std::string header = joinTokens(toks);
    t.headers.push_back(header);
    // split parameters at top-level commas
    std::vector<Token> curParam;
    int depth = 0;
    for (size_t i = 0; i < toks.size(); ++i) {
        const Token& k = toks[i];
        if (k.type == TokPunct) {
            if (k.text == "<" || k.text == "(") ++depth;
            else if (k.text == ">" || k.text == ")") --depth;
            else if (k.text == "," && depth == 0) {
                t.params.push_back(joinTokens(curParam));
                curParam.clear();
                continue;
            }
        }
        curParam.push_back(k);
    }
    if (!curParam.empty()) t.params.push_back(joinTokens(curParam));
    return t;
}

// ---- classes ----------------------------------------------------------------

ClassInfo* Parser::parseClass(ClassInfo* owner, Access access, const TemplateInfo& tmpl, bool fromTypedef)
{
    const Token& kw = cur();
    int line = kw.line;
    ClassInfo::Kind kind = ClassInfo::Class;
    if (kw.text == "struct") kind = ClassInfo::Struct;
    else if (kw.text == "union") kind = ClassInfo::Union;
    advance();

    // head: everything up to ':' (single), '{', ';', '=' or ','
    std::vector<Token> head;
    int angle = 0, paren = 0;
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) break;
        if (t.type == TokPunct) {
            if (angle == 0 && paren == 0 &&
                (t.text == ":" || t.text == "{" || t.text == ";" || t.text == "=" || t.text == "," ||
                 t.text == ")" || t.text == "}"))
                break;
            if (t.text == "<") ++angle;
            else if (t.text == ">") { if (angle > 0) --angle; }
            else if (t.text == ">>") { angle -= 2; if (angle < 0) angle = 0; }
            else if (t.text == "(") ++paren;
            else if (t.text == ")") --paren;
        }
        head.push_back(t);
        advance();
    }

    // identifier chains in the head: "A::B<T>" counts as one chain
    struct Chain { size_t begin, end; };
    std::vector<Chain> chains;
    for (size_t i = 0; i < head.size();) {
        if (head[i].type == TokIdentifier || head[i].text == "::") {
            Chain c; c.begin = i;
            for (;;) {
                if (i < head.size() && head[i].text == "::") { ++i; continue; }
                if (i < head.size() && head[i].type == TokIdentifier) {
                    ++i;
                    if (i < head.size() && head[i].text == "<") {
                        int d = 0;
                        for (; i < head.size(); ++i) {
                            if (head[i].text == "<") ++d;
                            else if (head[i].text == ">") { --d; if (d == 0) { ++i; break; } }
                            else if (head[i].text == ">>") { d -= 2; if (d <= 0) { ++i; break; } }
                        }
                    }
                    if (i < head.size() && head[i].text == "::") continue;
                }
                break;
            }
            c.end = i;
            chains.push_back(c);
            if (c.end == c.begin) ++i;
            continue;
        }
        ++i;
    }

    const std::string term = cur().type == TokEnd ? ";" : cur().text;

    // "struct stat st;" / "class Foo* p = ...;" : a variable of elaborated type, not a class
    bool elaboratedVariable = false;
    if (term == ";" || term == "=" || term == ",") {
        if (chains.size() >= 2 && !isAllCaps(joinTokens(head, chains[0].begin, chains[0].end))) elaboratedVariable = true;
        if (chains.size() >= 1 && chains.back().end < head.size()) elaboratedVariable = true; // "struct Foo *p"
    }
    if (elaboratedVariable) {
        VariableInfo v;
        v.line = line;
        v.access = access;
        v.condition = currentCondition();
        v.file = m_file.path;
        v.scope = scopeName();
        v.type = std::string(ClassInfo::kindName(kind)) + " " + joinTokens(head, 0, chains.back().begin);
        // name = last identifier
        for (size_t i = head.size(); i > 0; --i)
            if (head[i - 1].type == TokIdentifier) { v.name = head[i - 1].text; v.type = std::string(ClassInfo::kindName(kind)) + " " + joinTokens(head, 0, i - 1); break; }
        if (term == "=") {
            std::vector<Token> init;
            advance();
            while (!atEnd() && !isPunct(";")) { if (isPunct("(") || isPunct("{") || isPunct("[")) { init.push_back(cur()); skipBalanced(&init); init.push_back(Token(TokPunct, ")", 0)); continue; } init.push_back(cur()); advance(); }
            v.initializer = joinTokens(init);
        } else if (term == ",") {
            skipStatement();
            emitVariable(owner, v);
            return 0;
        }
        if (isPunct(";")) advance();
        emitVariable(owner, v);
        return 0;
    }

    ClassInfo* cls = new ClassInfo;
    cls->kind = kind;
    cls->line = line;
    cls->endLine = line;
    cls->file = m_file.path;
    cls->access = access;
    cls->owner = owner;
    cls->scope = scopeName();
    cls->tmpl = tmpl;
    cls->condition = currentCondition();

    if (chains.empty()) {
        cls->name = "(anonymous)";
        cls->attributes = joinTokens(head);
    } else {
        const Chain& nc = chains.back();
        cls->attributes = trim(joinTokens(head, 0, nc.begin));
        // split name chain into qualifier part and specialization args
        std::vector<Token> nameToks(head.begin() + static_cast<long>(nc.begin), head.begin() + static_cast<long>(nc.end));
        std::string full = joinTokens(nameToks);
        size_t lt = std::string::npos;
        {
            // specialization args are the trailing <...> of the chain
            int d = 0;
            for (size_t i = nameToks.size(); i > 0; --i) {
                const std::string& s = nameToks[i - 1].text;
                if (s == ">") ++d;
                else if (s == "<") { --d; if (d == 0) { lt = i - 1; break; } }
            }
            if (lt != std::string::npos && lt + 1 < nameToks.size() && nameToks.back().text == ">") {
                cls->tmpl.isTemplate = true;
                cls->tmpl.specializationArgs = joinTokens(nameToks, lt, nameToks.size());
                full = joinTokens(nameToks, 0, lt);
            }
        }
        cls->name = full;
    }

    std::string prefix = owner ? owner->qualifiedName : cls->scope;
    cls->qualifiedName = prefix.empty() ? cls->name : prefix + "::" + cls->name;
    if (cls->name.find("::") != std::string::npos && !owner) cls->qualifiedName = cls->scope.empty() ? cls->name : cls->scope + "::" + cls->name;

    m_file.allClasses.push_back(cls);
    if (owner) owner->nested.push_back(cls);
    else m_file.classes.push_back(cls);

    if (term == ";" || term == "}" || term == ")") {
        if (isPunct(";")) advance();
        cls->isDefinition = false;
        return cls;
    }
    if (term == "=" || term == ",") { skipStatement(); return cls; }

    if (term == ":") parseBases(*cls);

    if (!isPunct("{")) {
        diag(line, "expected '{' after class head of " + cls->qualifiedName);
        skipStatement();
        return cls;
    }
    cls->isDefinition = true;
    parseClassBody(*cls);

    // trailing declarators: "} x, *y;" or typedef aliases
    collectTrailingDeclarators(owner, access, cls->name, fromTypedef, cls);
    for (size_t i = 0; i < cls->methods.size(); ++i)
        if (cls->methods[i].isPureVirtual) { cls->isAbstract = true; break; }
    return cls;
}

void Parser::parseBases(ClassInfo& cls)
{
    advance(); // ':'
    for (;;) {
        BaseClass b;
        b.access = (cls.kind == ClassInfo::Class) ? AccessPrivate : AccessPublic;
        for (;;) {
            if (is("virtual")) { b.isVirtual = true; advance(); continue; }
            if (isIdent() && isAccessWord(cur().text)) { b.access = accessFromWord(cur().text); advance(); continue; }
            break;
        }
        std::vector<Token> toks;
        int angle = 0;
        for (;;) {
            const Token& t = cur();
            if (t.type == TokEnd) break;
            if (t.type == TokPunct) {
                if (angle == 0 && (t.text == "," || t.text == "{" || t.text == ";")) break;
                if (t.text == "<") ++angle;
                else if (t.text == ">") { if (angle > 0) --angle; }
                else if (t.text == ">>") { angle -= 2; if (angle < 0) angle = 0; }
            }
            toks.push_back(t);
            advance();
        }
        b.name = joinTokens(toks);
        if (!b.name.empty()) cls.bases.push_back(b);
        if (isPunct(",")) { advance(); continue; }
        break;
    }
}

void Parser::parseClassBody(ClassInfo& cls)
{
    advance(); // '{'
    Access access = (cls.kind == ClassInfo::Class) ? AccessPrivate : AccessPublic;
    TemplateInfo noTmpl;

    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) { diag(cls.line, "unterminated class body of " + cls.qualifiedName); return; }

        if (t.type == TokPunct) {
            if (t.text == "}") { cls.endLine = t.line; advance(); return; }
            if (t.text == ";") { advance(); continue; }
            if (t.text == "{") { diag(t.line, "unexpected '{' in class body"); skipBalanced(0); continue; }
            parseDeclaration(&cls, access, noTmpl);
            continue;
        }

        if (t.type == TokIdentifier) {
            const std::string& w = t.text;
            if (isAccessWord(w) && peek(1).text == ":") { access = accessFromWord(w); advance(); advance(); continue; }
            if (w == "friend") { parseFriend(cls, access); continue; }
            if (w == "template") {
                if (peek(1).text != "<") { diag(t.line, "unexpected 'template' in class body"); skipStatement(); continue; }
                TemplateInfo tmpl = parseTemplateHeader();
                while (is("template") && peek(1).text == "<") {
                    TemplateInfo more = parseTemplateHeader();
                    tmpl.headers.insert(tmpl.headers.end(), more.headers.begin(), more.headers.end());
                    tmpl.params.insert(tmpl.params.end(), more.params.begin(), more.params.end());
                }
                if (isClassKey(cur().text)) parseClass(&cls, access, tmpl, false);
                else if (is("friend")) parseFriend(cls, access);
                else parseDeclaration(&cls, access, tmpl);
                continue;
            }
            if (isClassKey(w)) { parseClass(&cls, access, noTmpl, false); continue; }
            if (w == "enum") { parseEnum(&cls, access, false); continue; }
            if (w == "typedef") { parseTypedef(&cls, access); continue; }
            if (w == "using") { parseUsing(&cls); continue; }
            if (w == "static_assert" || w == "__pragma") {
                advance();
                if (isPunct("(")) skipBalanced(0);
                if (isPunct(";")) advance();
                continue;
            }
            parseDeclaration(&cls, access, noTmpl);
            continue;
        }

        diag(t.line, "unexpected token '" + t.text + "' in class body");
        advance();
    }
}

void Parser::parseFriend(ClassInfo& cls, Access access)
{
    FriendInfo f;
    f.file = m_file.path;
    f.line = cur().line;
    advance(); // friend
    std::vector<Token> toks;
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) break;
        if (t.type == TokPunct && t.text == ";") { advance(); break; }
        if (t.type == TokPunct && t.text == "{") {
            // friend function defined in place: parse it as a method flagged friend
            m_pos -= 0; // (cursor already at '{')
            break;
        }
        if (t.type == TokPunct && (t.text == "(" || t.text == "[")) { toks.push_back(t); skipBalanced(&toks); toks.push_back(Token(TokPunct, t.text == "(" ? ")" : "]", t.line)); continue; }
        toks.push_back(t);
        advance();
    }
    f.text = joinTokens(toks);
    if (isPunct("{")) {
        FunctionInfo fn;
        fn.isFriend = true;
        fn.file = m_file.path;
        fn.line = f.line;
        fn.access = access;
        fn.name = f.text;
        fn.hasBody = true;
        fn.condition = currentCondition();
        scanBody(fn);
        f.text += " { ... }  [defined in class, " + intToString(fn.exceptions.tryBlocks) + " try]";
    }
    cls.friends.push_back(f);
}

void Parser::parseEnum(ClassInfo* owner, Access access, bool fromTypedef)
{
    EnumInfo e;
    e.file = m_file.path;
    e.line = cur().line;
    e.access = access;
    e.scope = owner ? owner->qualifiedName : scopeName();
    e.condition = currentCondition();
    advance(); // enum
    if (is("class") || is("struct")) advance(); // tolerate C++11 scoped enums
    if (isIdent()) { e.name = cur().text; advance(); }
    if (isPunct(":")) { // tolerate C++11 underlying type
        advance();
        while (!atEnd() && !isPunct("{") && !isPunct(";")) advance();
    }
    if (isPunct(";")) { advance(); if (!e.name.empty()) { if (owner) owner->enums.push_back(e); else m_file.enums.push_back(e); } return; }
    if (!isPunct("{")) {
        // "enum Color c;" elaborated variable
        VariableInfo v;
        v.line = e.line; v.access = access; v.file = m_file.path; v.scope = scopeName(); v.condition = e.condition;
        v.type = "enum " + e.name;
        if (isIdent()) { v.name = cur().text; }
        skipStatement();
        emitVariable(owner, v);
        return;
    }
    advance(); // '{'
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) break;
        if (t.type == TokPunct && t.text == "}") { advance(); break; }
        if (t.type == TokIdentifier) {
            std::string name = t.text;
            advance();
            if (isPunct("=")) {
                std::vector<Token> val;
                advance();
                while (!atEnd() && !isPunct(",") && !isPunct("}")) {
                    if (isPunct("(")) { val.push_back(cur()); skipBalanced(&val); val.push_back(Token(TokPunct, ")", 0)); continue; }
                    val.push_back(cur());
                    advance();
                }
                name += " = " + joinTokens(val);
            }
            e.enumerators.push_back(name);
            if (isPunct(",")) advance();
            continue;
        }
        advance();
    }
    if (e.name.empty()) e.name = "(anonymous)";
    if (owner) owner->enums.push_back(e); else m_file.enums.push_back(e);
    collectTrailingDeclarators(owner, access, "enum " + e.name, fromTypedef, 0);
}

// After a class/enum body: "} a, *b;" declares variables, or typedef aliases
// when the body came from "typedef struct {...} Name;".
void Parser::collectTrailingDeclarators(ClassInfo* owner, Access access, const std::string& baseType,
                                        bool fromTypedef, ClassInfo* cls)
{
    std::vector<Token> toks;
    int line = cur().line;
    // "class A { ... }  class B" : the ';' after the body is missing
    if (cur().type == TokIdentifier) {
        const std::string& w = cur().text;
        if (isClassKey(w) || w == "enum" || w == "template" || w == "typedef" || w == "namespace" || w == "using" ||
            w == "static" || w == "inline" || w == "virtual" || w == "extern" || w == "friend" || w == "explicit" ||
            w == "operator" || isAccessWord(w) || (isTypeKeyword(w) && w != "const")) {
            diag(line, "missing ';' after the body of " + (cls ? cls->qualifiedName : baseType));
            return;
        }
    }
    while (!atEnd() && !isPunct(";") && !isPunct("}")) {
        if (isPunct("(") || isPunct("[") || isPunct("{")) {
            const std::string open = cur().text;
            toks.push_back(cur());
            skipBalanced(&toks);
            toks.push_back(Token(TokPunct, open == "(" ? ")" : (open == "[" ? "]" : "}"), line));
            continue;
        }
        toks.push_back(cur());
        advance();
    }
    if (isPunct(";")) advance();
    if (toks.empty()) return;

    // split at commas
    std::vector<std::vector<Token> > decls;
    decls.push_back(std::vector<Token>());
    int depth = 0;
    for (size_t i = 0; i < toks.size(); ++i) {
        const std::string& s = toks[i].text;
        if (s == "(" || s == "[" || s == "{") ++depth;
        else if (s == ")" || s == "]" || s == "}") --depth;
        if (s == "," && depth == 0) { decls.push_back(std::vector<Token>()); continue; }
        decls.back().push_back(toks[i]);
    }
    for (size_t d = 0; d < decls.size(); ++d) {
        const std::vector<Token>& dt = decls[d];
        std::string name;
        size_t nameIdx = std::string::npos;
        for (size_t i = 0; i < dt.size(); ++i)
            if (dt[i].type == TokIdentifier && (i + 1 >= dt.size() || dt[i + 1].text != "::")) { name = dt[i].text; nameIdx = i; if (i + 1 < dt.size() && dt[i + 1].text == "=") break; }
        if (name.empty()) continue;
        if (fromTypedef) {
            TypedefInfo td;
            td.file = m_file.path; td.line = line; td.access = access; td.condition = currentCondition();
            td.scope = owner ? owner->qualifiedName : scopeName();
            td.name = name;
            td.text = "typedef " + baseType + " " + joinTokens(dt);
            if (owner) owner->typedefs.push_back(td); else m_file.typedefs.push_back(td);
            if (cls && cls->name == "(anonymous)") {
                cls->name = name;
                cls->isTypedefName = true;
                std::string prefix = owner ? owner->qualifiedName : cls->scope;
                cls->qualifiedName = prefix.empty() ? name : prefix + "::" + name;
            }
        } else {
            VariableInfo v;
            v.file = m_file.path; v.line = line; v.access = access; v.condition = currentCondition();
            v.scope = owner ? "" : scopeName();
            v.name = name;
            v.type = baseType + (nameIdx > 0 ? " " + joinTokens(dt, 0, nameIdx) : "");
            if (nameIdx + 1 < dt.size()) {
                if (dt[nameIdx + 1].text == "=") v.initializer = joinTokens(dt, nameIdx + 2, dt.size());
                else v.type += joinTokens(dt, nameIdx + 1, dt.size());
            }
            emitVariable(owner, v);
        }
    }
}

void Parser::parseTypedef(ClassInfo* owner, Access access)
{
    int line = cur().line;
    advance(); // typedef
    if (isClassKey(cur().text)) {
        // typedef struct [Name] { ... } Alias;  or  typedef struct Name Alias;
        size_t save = m_pos;
        size_t j = m_pos;
        bool hasBody = false;
        for (; j < m_tokens.size(); ++j) {
            if (m_tokens[j].type != TokPunct) continue;
            if (m_tokens[j].text == "{") { hasBody = true; break; }
            if (m_tokens[j].text == ";") break;
        }
        if (hasBody) { parseClass(owner, access, TemplateInfo(), true); return; }
        m_pos = save;
    } else if (is("enum")) {
        size_t j = m_pos;
        bool hasBody = false;
        for (; j < m_tokens.size(); ++j) {
            if (m_tokens[j].type != TokPunct) continue;
            if (m_tokens[j].text == "{") { hasBody = true; break; }
            if (m_tokens[j].text == ";") break;
        }
        if (hasBody) { parseEnum(owner, access, true); return; }
    }

    TypedefInfo td;
    td.file = m_file.path;
    td.line = line;
    td.access = access;
    td.condition = currentCondition();
    td.scope = owner ? owner->qualifiedName : scopeName();
    std::vector<Token> toks;
    while (!atEnd() && !isPunct(";") && !isPunct("}")) {
        if (isPunct("(") || isPunct("[")) {
            const std::string open = cur().text;
            toks.push_back(cur());
            skipBalanced(&toks);
            toks.push_back(Token(TokPunct, open == "(" ? ")" : "]", line));
            continue;
        }
        toks.push_back(cur());
        advance();
    }
    if (isPunct(";")) advance();
    // name: identifier after '*' inside the first parenthesis (function pointer), else last identifier
    for (size_t i = 0; i + 1 < toks.size(); ++i) {
        if (toks[i].text == "(" && (toks[i + 1].text == "*" || toks[i + 1].text == "&")) {
            for (size_t k = i + 2; k < toks.size() && toks[k].text != ")"; ++k)
                if (toks[k].type == TokIdentifier) { td.name = toks[k].text; }
            break;
        }
    }
    if (td.name.empty())
        for (size_t i = toks.size(); i > 0; --i)
            if (toks[i - 1].type == TokIdentifier) { td.name = toks[i - 1].text; break; }
    td.text = "typedef " + joinTokens(toks);
    if (owner) owner->typedefs.push_back(td); else m_file.typedefs.push_back(td);
}

// ---- declarations -----------------------------------------------------------

void Parser::emitVariable(ClassInfo* owner, VariableInfo& v)
{
    if (owner) owner->fields.push_back(v);
    else m_file.variables.push_back(v);
}

bool Parser::looksLikeFunctionPointer()
{
    cur();
    size_t open = m_pos;
    size_t close = matchClose(open);
    if (close == std::string::npos) return false;
    size_t after = rawNext(close);
    if (after >= m_tokens.size() || m_tokens[after].text != "(") return false;
    for (size_t j = open + 1; j < close; ++j) {
        if (m_tokens[j].type == TokPunct && (m_tokens[j].text == "*" || m_tokens[j].text == "&" || m_tokens[j].text == "^"))
            return true;
    }
    return false;
}

// "DECLARE_MESSAGE_MAP()" / "BEGIN_MESSAGE_MAP(A, B)": an all-caps identifier
// followed by parentheses and then by something that cannot continue a
// function declaration.
bool Parser::looksLikeMacroInvocation(const std::vector<Token>& head)
{
    if (head.size() != 1 || head[0].type != TokIdentifier || !isAllCaps(head[0].text)) return false;
    cur();
    size_t close = matchClose(m_pos);
    if (close == std::string::npos) return true;
    // a literal or an operator at the top level of the parentheses that is not a
    // default argument ("N > 0", "x + 1", "\"text\"") cannot be a parameter list
    {
        int depth = 0;
        for (size_t j = m_pos + 1; j < close; ++j) {
            const Token& t = m_tokens[j];
            if (t.type == TokPreproc) continue;
            if (t.type == TokPunct) {
                if (t.text == "(" || t.text == "[" || t.text == "{" || t.text == "<") ++depth;
                else if (t.text == ")" || t.text == "]" || t.text == "}" || t.text == ">") { if (depth > 0) --depth; else if (t.text == ">") return true; }
                else if (depth == 0 && (t.text == "+" || t.text == "-" || t.text == "/" || t.text == "%" || t.text == "==" ||
                                        t.text == "!=" || t.text == "<=" || t.text == ">=" || t.text == "&&" || t.text == "||" ||
                                        t.text == "." || t.text == "->" || t.text == "!"))
                    return true;
                continue;
            }
            if (depth == 0 && (t.type == TokNumber || t.type == TokString || t.type == TokChar)) {
                size_t prev = j - 1;
                while (prev > m_pos && m_tokens[prev].type == TokPreproc) --prev;
                if (m_tokens[prev].text != "=") return true;
            }
        }
    }
    size_t after = rawNext(close);
    if (after >= m_tokens.size()) return true;
    const std::string& s = m_tokens[after].text;
    if (s == ";" || s == "{" || s == "const" || s == ":" || s == "=" || s == "throw" ||
        s == "try" || s == "," || s == "volatile")
        return false;
    return true;
}

bool Parser::parensContainLiteral()
{
    cur();
    size_t close = matchClose(m_pos);
    if (close == std::string::npos) return false;
    for (size_t j = m_pos + 1; j < close; ++j) {
        TokenType ty = m_tokens[j].type;
        if (ty == TokNumber || ty == TokString || ty == TokChar) {
            // a literal right after '=' is a default argument, not a constructor argument
            size_t p = j - 1;
            while (p > m_pos && m_tokens[p].type == TokPreproc) --p;
            if (m_tokens[p].text == "=") continue;
            return true;
        }
        if (ty == TokPunct && (m_tokens[j].text == "+" || m_tokens[j].text == "-" || m_tokens[j].text == "/" ||
                               m_tokens[j].text == "." || m_tokens[j].text == "->" || m_tokens[j].text == "=="))
            return true;
    }
    return false;
}

void Parser::collectOperatorName(std::vector<Token>& head, bool& conversion)
{
    int line = cur().line;
    advance(); // operator
    conversion = false;
    std::string name = "operator";
    const Token& t = cur();
    if (t.type == TokPunct && t.text == "(" && peek(1).text == ")") { name += "()"; advance(); advance(); }
    else if (t.type == TokPunct && t.text == "[" && peek(1).text == "]") { name += "[]"; advance(); advance(); }
    else if (t.type == TokIdentifier && (t.text == "new" || t.text == "delete")) {
        name += " " + t.text;
        advance();
        if (isPunct("[") && peek(1).text == "]") { name += "[]"; advance(); advance(); }
    }
    else if (t.type == TokPunct && t.text != "(") { name += t.text; advance(); if (isPunct(">") && name == "operator>" ) {} }
    else {
        // conversion operator: operator const char*()
        std::vector<Token> ty;
        int angle = 0;
        for (;;) {
            const Token& k = cur();
            if (k.type == TokEnd) break;
            if (k.type == TokPunct) {
                if (k.text == "(" && angle == 0) break;
                if (k.text == "<") ++angle;
                else if (k.text == ">") { if (angle > 0) --angle; }
            }
            ty.push_back(k);
            advance();
        }
        name += " " + joinTokens(ty);
        conversion = true;
    }
    head.push_back(Token(TokIdentifier, name, line));
}

void Parser::parseDeclaration(ClassInfo* owner, Access access, const TemplateInfo& tmpl)
{
    const int startLine = cur().line;
    const std::string condition = currentCondition();

    bool isStatic = false, isInline = false, isVirtual = false, isExplicit = false;
    bool isExtern = m_externC, isMutable = false;
    std::string attributes;

    // leading specifiers
    for (;;) {
        const Token& t = cur();
        if (t.type != TokIdentifier) break;
        const std::string& w = t.text;
        if (w == "static") { isStatic = true; advance(); continue; }
        if (w == "inline" || w == "__inline" || w == "__forceinline") { isInline = true; advance(); continue; }
        if (w == "virtual") { isVirtual = true; advance(); continue; }
        if (w == "explicit") { isExplicit = true; advance(); continue; }
        if (w == "extern") { isExtern = true; advance(); if (cur().type == TokString) advance(); continue; }
        if (w == "mutable") { isMutable = true; advance(); continue; }
        if (w == "register" || w == "__thread" || w == "thread_local") { advance(); continue; }
        if (w == "__declspec" || w == "__attribute__" || w == "alignas" || w == "__based") {
            advance();
            if (isPunct("(")) {
                std::vector<Token> a;
                skipBalanced(&a);
                if (!attributes.empty()) attributes += " ";
                attributes += w + "(" + joinTokens(a) + ")";
            }
            continue;
        }
        break;
    }

    // collect the type + declarator head
    std::vector<Token> head;
    size_t nameStart = std::string::npos;
    bool prevScope = false;
    bool conversion = false;
    int angle = 0, paren = 0, bracket = 0;
    enum Stop { StopNone, StopParen, StopAssign, StopSemi, StopComma, StopBrace, StopColon, StopBracket, StopEnd };
    Stop stop = StopNone;

    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) { stop = StopEnd; break; }
        if (t.type == TokPunct) {
            const std::string& p = t.text;
            if (angle == 0 && paren == 0 && bracket == 0) {
                if (p == "(") { stop = StopParen; break; }
                if (p == "=") { stop = StopAssign; break; }
                if (p == ";") { stop = StopSemi; break; }
                if (p == ",") { stop = StopComma; break; }
                if (p == "{" || p == "}") { stop = StopBrace; break; }
                if (p == ":") { stop = StopColon; break; }
                if (p == "[") { stop = StopBracket; break; }
                if (p == ")") { stop = StopBrace; break; }
            }
            if (p == "<") ++angle;
            else if (p == ">") { if (angle > 0) --angle; }
            else if (p == ">>") { angle -= 2; if (angle < 0) angle = 0; }
            else if (p == "(") ++paren;
            else if (p == ")") --paren;
            else if (p == "[") ++bracket;
            else if (p == "]") --bracket;

            if (p == "::" && angle == 0 && paren == 0) {
                if (nameStart == std::string::npos || !prevScope) {
                    // leading "::name"; a chain continuing after an identifier or after the
                    // closing '>' of its template arguments ("Foo<T>::bar") keeps its start
                    if (head.empty() || (head.back().type != TokIdentifier && head.back().text != ">"))
                        nameStart = head.size();
                }
                prevScope = true;
                head.push_back(t);
                advance();
                continue;
            }
            if (p == "~" && angle == 0 && paren == 0) {
                if (!prevScope) nameStart = head.size();
                head.push_back(t);
                advance();
                prevScope = true;
                continue;
            }
            if (angle == 0 && paren == 0) prevScope = false;
            head.push_back(t);
            advance();
            continue;
        }
        if (t.type == TokIdentifier) {
            if (t.text == "operator" && angle == 0 && paren == 0) {
                if (!prevScope) nameStart = head.size();
                collectOperatorName(head, conversion);
                stop = isPunct("(") ? StopParen : StopSemi;
                break;
            }
            if (t.text == "__declspec" && peek(1).text == "(") {
                advance();
                std::vector<Token> a;
                skipBalanced(&a);
                if (!attributes.empty()) attributes += " ";
                attributes += "__declspec(" + joinTokens(a) + ")";
                continue;
            }
            if (angle == 0 && paren == 0 && bracket == 0 && !prevScope) nameStart = head.size();
            if (angle == 0 && paren == 0) prevScope = false;
            head.push_back(t);
            advance();
            continue;
        }
        if (angle == 0 && paren == 0) prevScope = false;
        head.push_back(t);
        advance();
    }

    // find where the name chain starts: walk back over "<...>" "::" ident groups from nameStart
    // (nameStart already points at the start of the last chain; nothing more to do)

    if (stop == StopEnd) {
        if (!head.empty()) diag(startLine, "declaration cut off at end of file: " + joinTokens(head));
        return;
    }

    if (stop == StopBrace) {
        if (isPunct("{")) {
            diag(startLine, "unrecognised construct before '{' skipped: " + joinTokens(head));
            skipBalanced(0);
            if (isPunct(";")) advance();
        } else {
            if (!head.empty()) diag(startLine, "unrecognised tokens skipped: " + joinTokens(head));
            if (isPunct(")")) advance();
        }
        return;
    }

    // ---- function?
    if (stop == StopParen) {
        if (looksLikeMacroInvocation(head)) {
            std::vector<Token> args;
            skipBalanced(&args);
            diag(startLine, "macro invocation: " + head[0].text + "(" + joinTokens(args) + ")");
            if (isPunct(";")) advance();
            return;
        }
        if (looksLikeFunctionPointer()) {
            // variable of function pointer type: void (*fp)(int) [= ...];
            VariableInfo v;
            v.line = startLine; v.access = access; v.file = m_file.path; v.condition = condition;
            v.scope = owner ? "" : scopeName();
            v.isStatic = isStatic; v.isExtern = isExtern; v.isMutable = isMutable;
            std::vector<Token> decl;
            skipBalanced(&decl);
            size_t nameIdx = std::string::npos;
            for (size_t i = 0; i < decl.size(); ++i)
                if (decl[i].type == TokIdentifier && (i == 0 || decl[i - 1].text != "::") &&
                    (i + 1 >= decl.size() || decl[i + 1].text != "::")) { v.name = decl[i].text; nameIdx = i; }
            if (nameIdx != std::string::npos) decl.erase(decl.begin() + static_cast<long>(nameIdx));
            std::vector<Token> params;
            if (isPunct("(")) skipBalanced(&params);
            // type is kept as "void (*)(int)"; report writers insert the name after "(*"
            v.type = joinTokens(head) + " (" + joinTokens(decl) + ")(" + joinTokens(params) + ")";
            while (!atEnd() && !isPunct(";") && !isPunct("}")) {
                if (isPunct("=")) {
                    std::vector<Token> init; advance();
                    while (!atEnd() && !isPunct(";") && !isPunct(",")) { if (isPunct("(") || isPunct("{")) { init.push_back(cur()); skipBalanced(&init); init.push_back(Token(TokPunct, ")", 0)); continue; } init.push_back(cur()); advance(); }
                    v.initializer = joinTokens(init);
                    continue;
                }
                advance();
            }
            if (isPunct(";")) advance();
            emitVariable(owner, v);
            return;
        }

        if (nameStart == std::string::npos || nameStart >= head.size()) {
            diag(startLine, "unrecognised declaration skipped: " + joinTokens(head));
            skipStatement();
            return;
        }

        FunctionInfo fn;
        fn.file = m_file.path;
        fn.line = startLine;
        fn.endLine = startLine;
        fn.access = access;
        fn.condition = condition;
        fn.attributes = attributes;
        fn.tmpl = tmpl;
        fn.isStatic = isStatic; fn.isInline = isInline; fn.isVirtual = isVirtual;
        fn.isExplicit = isExplicit; fn.isExtern = isExtern;
        fn.scope = owner ? "" : scopeName();
        fn.isConversion = conversion;

        // split name chain into qualifier and unqualified name
        std::vector<Token> chain(head.begin() + static_cast<long>(nameStart), head.end());
        size_t lastScope = std::string::npos;
        int d = 0;
        for (size_t i = 0; i < chain.size(); ++i) {
            const std::string& s = chain[i].text;
            if (s == "<") ++d;
            else if (s == ">") { if (d > 0) --d; }
            else if (s == ">>") { d -= 2; if (d < 0) d = 0; }
            else if (s == "::" && d == 0) lastScope = i;
        }
        if (lastScope != std::string::npos) {
            fn.qualifierText = joinTokens(chain, 0, lastScope);
            fn.qualifier = stripTemplateArgs(fn.qualifierText);
            fn.name = joinTokens(chain, lastScope + 1, chain.size());
        } else {
            fn.name = joinTokens(chain);
        }
        fn.returnType = trim(joinTokens(head, 0, nameStart));
        if (fn.returnType == "typename") fn.returnType = "";

        fn.isDestructor = !fn.name.empty() && fn.name[0] == '~';
        fn.isOperator = startsWith(fn.name, "operator");
        std::string ownerName = owner ? owner->name : lastComponent(fn.qualifier);
        if (!fn.isDestructor && !fn.isOperator && fn.returnType.empty() && !ownerName.empty() &&
            stripTemplateArgs(fn.name) == stripTemplateArgs(ownerName))
            fn.isConstructor = true;
        if (fn.isDestructor || fn.isConstructor || fn.isConversion) fn.returnType = "";

        // most vexing parse: "Foo x(1, 2);" at namespace scope is a variable
        if (!owner && !tmpl.isTemplate && fn.qualifier.empty() && !fn.returnType.empty() &&
            !fn.isOperator && parensContainLiteral()) {
            VariableInfo v;
            v.line = startLine; v.access = access; v.file = m_file.path; v.condition = condition;
            v.scope = scopeName(); v.isStatic = isStatic; v.isExtern = isExtern;
            v.type = fn.returnType; v.name = fn.name;
            std::vector<Token> init;
            skipBalanced(&init);
            v.initializer = "(" + joinTokens(init) + ")";
            skipStatement();
            emitVariable(owner, v);
            return;
        }

        parseParameters(fn);
        parseFunctionTail(fn, owner);
        if (owner) { fn.ownerClass = owner; owner->methods.push_back(fn); }
        else m_file.functions.push_back(fn);
        return;
    }

    // ---- variable(s)
    if (stop == StopColon && !owner) {
        diag(startLine, "unexpected ':' after: " + joinTokens(head));
        skipStatement();
        return;
    }
    if (nameStart == std::string::npos || nameStart >= head.size()) {
        if (head.empty() && (stop == StopSemi || stop == StopComma)) { advance(); return; }
        diag(startLine, "unrecognised declaration skipped: " + joinTokens(head));
        skipStatement();
        return;
    }

    // base type = head before the name, minus trailing '*' '&' 'const'
    size_t baseEnd = nameStart;
    while (baseEnd > 0 && (head[baseEnd - 1].text == "*" || head[baseEnd - 1].text == "&" || head[baseEnd - 1].text == "const"))
        --baseEnd;
    std::string baseType = joinTokens(head, 0, baseEnd);

    VariableInfo v;
    v.line = startLine; v.access = access; v.file = m_file.path; v.condition = condition;
    v.scope = owner ? "" : scopeName();
    v.isStatic = isStatic; v.isExtern = isExtern; v.isMutable = isMutable;
    v.isConst = false;
    for (size_t i = 0; i < nameStart; ++i) if (head[i].text == "const") v.isConst = true;
    v.type = joinTokens(head, 0, nameStart);
    {
        std::vector<Token> chain(head.begin() + static_cast<long>(nameStart), head.end());
        size_t lastScope = std::string::npos;
        for (size_t i = 0; i < chain.size(); ++i) if (chain[i].text == "::") lastScope = i;
        if (lastScope != std::string::npos) {
            v.qualifier = stripTemplateArgs(joinTokens(chain, 0, lastScope));
            v.name = joinTokens(chain, lastScope + 1, chain.size());
        } else {
            v.name = joinTokens(chain);
        }
    }

    for (;;) {
        // suffixes of the current declarator
        if (isPunct("[")) {
            while (isPunct("[")) {
                std::vector<Token> dim;
                skipBalanced(&dim);
                v.type += "[" + joinTokens(dim) + "]";
            }
        }
        if (isPunct(":") && owner) {
            std::vector<Token> bits;
            advance();
            while (!atEnd() && !isPunct(";") && !isPunct(",") && !isPunct("}")) { bits.push_back(cur()); advance(); }
            v.bitField = joinTokens(bits);
        }
        if (isPunct("=")) {
            std::vector<Token> init;
            advance();
            int tdepth = 0;
            for (;;) {
                const Token& t = cur();
                if (t.type == TokEnd) break;
                if (t.type == TokPunct) {
                    if (t.text == ";" || t.text == "}") break;
                    if (t.text == "," && tdepth == 0) break;
                    if (t.text == "(" || t.text == "{" || t.text == "[") {
                        init.push_back(t);
                        skipBalanced(&init);
                        init.push_back(Token(TokPunct, t.text == "(" ? ")" : (t.text == "{" ? "}" : "]"), t.line));
                        continue;
                    }
                    if (t.text == "<" && !init.empty() && init.back().type == TokIdentifier) ++tdepth;
                    else if (t.text == ">" && tdepth > 0) --tdepth;
                }
                init.push_back(t);
                advance();
            }
            v.initializer = joinTokens(init);
        }
        if (isPunct("(")) {
            // "Foo x(args);" inside a class is invalid, at namespace scope it is handled above;
            // reaching here means e.g. "int (x);" - keep it as initializer text.
            std::vector<Token> init;
            skipBalanced(&init);
            v.initializer = "(" + joinTokens(init) + ")";
        }
        emitVariable(owner, v);

        if (isPunct(",")) {
            advance();
            // next declarator: [* & const]* name
            VariableInfo n;
            n.line = cur().line; n.access = access; n.file = m_file.path; n.condition = condition;
            n.scope = v.scope; n.isStatic = isStatic; n.isExtern = isExtern; n.isMutable = isMutable; n.isConst = v.isConst;
            std::vector<Token> pre;
            while (!atEnd() && !isIdent() && !isPunct(";") && !isPunct("}")) { pre.push_back(cur()); advance(); }
            if (isIdent()) { n.name = cur().text; advance(); }
            n.type = baseType + (pre.empty() ? "" : " " + joinTokens(pre));
            v = n;
            continue;
        }
        break;
    }
    if (isPunct(";")) advance();
    else if (!isPunct("}")) { diag(startLine, "expected ';' after declaration of " + v.name); skipStatement(); }
}

void Parser::parseParameters(FunctionInfo& fn)
{
    std::vector<Token> toks;
    skipBalanced(&toks); // cur() was at '('
    fn.paramText = joinTokens(toks);
    if (fn.paramText == "void") { fn.paramText = ""; return; }
    splitParameters(toks, fn.params);
}

void Parser::splitParameters(const std::vector<Token>& toks, std::vector<Parameter>& out) const
{
    std::vector<std::vector<Token> > pieces;
    pieces.push_back(std::vector<Token>());
    int depth = 0;
    for (size_t i = 0; i < toks.size(); ++i) {
        const std::string& s = toks[i].text;
        if (toks[i].type == TokPunct) {
            if (s == "(" || s == "[" || s == "{" || s == "<") ++depth;
            else if (s == ")" || s == "]" || s == "}" || s == ">") --depth;
            else if (s == ">>") depth -= 2;
            else if (s == "," && depth <= 0) { depth = 0; pieces.push_back(std::vector<Token>()); continue; }
        }
        pieces.back().push_back(toks[i]);
    }
    for (size_t p = 0; p < pieces.size(); ++p) {
        const std::vector<Token>& pt = pieces[p];
        if (pt.empty()) continue;
        Parameter prm;
        size_t eq = pt.size();
        int d = 0;
        for (size_t i = 0; i < pt.size(); ++i) {
            const std::string& s = pt[i].text;
            if (s == "(" || s == "<" || s == "[") ++d;
            else if (s == ")" || s == ">" || s == "]") --d;
            else if (s == "=" && d == 0) { eq = i; break; }
        }
        if (eq < pt.size()) prm.defaultValue = joinTokens(pt, eq + 1, pt.size());
        std::vector<Token> decl(pt.begin(), pt.begin() + static_cast<long>(eq));
        if (decl.empty()) continue;

        size_t nameIdx = std::string::npos;
        // function pointer parameter: type (*name)(args)
        for (size_t i = 0; i + 1 < decl.size(); ++i) {
            if (decl[i].text == "(" && (decl[i + 1].text == "*" || decl[i + 1].text == "&")) {
                for (size_t k = i + 2; k < decl.size() && decl[k].text != ")"; ++k)
                    if (decl[k].type == TokIdentifier) nameIdx = k;
                break;
            }
        }
        if (nameIdx == std::string::npos) {
            size_t last = decl.size();
            // strip trailing [..] groups
            while (last > 0 && decl[last - 1].text == "]") {
                size_t k = last - 1;
                int bd = 0;
                for (; k > 0; --k) { if (decl[k].text == "]") ++bd; else if (decl[k].text == "[") { --bd; if (bd == 0) break; } }
                last = k;
            }
            if (last >= 2 && decl[last - 1].type == TokIdentifier && decl[last - 2].text != "::" &&
                !isTypeKeyword(decl[last - 1].text))
                nameIdx = last - 1;
        }
        if (nameIdx != std::string::npos) {
            prm.name = decl[nameIdx].text;
            std::vector<Token> ty(decl);
            ty.erase(ty.begin() + static_cast<long>(nameIdx));
            prm.type = joinTokens(ty);
        } else {
            prm.type = joinTokens(decl);
        }
        out.push_back(prm);
    }
}

void Parser::parseFunctionTail(FunctionInfo& fn, ClassInfo* owner)
{
    (void)owner;
    for (;;) {
        if (is("const")) { fn.isConst = true; advance(); continue; }
        if (is("volatile")) { fn.isVolatile = true; advance(); continue; }
        if (is("throw")) {
            advance();
            std::vector<Token> spec;
            if (isPunct("(")) skipBalanced(&spec);
            fn.exceptionSpec = "throw(" + joinTokens(spec) + ")";
            continue;
        }
        if (is("override") || is("final") || is("noexcept")) { advance(); if (isPunct("(")) skipBalanced(0); continue; }
        if (is("__attribute__")) { advance(); if (isPunct("(")) skipBalanced(0); continue; }
        break;
    }

    if (isPunct("=")) {
        advance();
        if (cur().type == TokNumber && cur().text == "0") { fn.isPureVirtual = true; advance(); }
        else if (is("default") || is("delete")) { fn.linkNote = "= " + cur().text; advance(); }
        else { diag(fn.line, "unexpected initializer after function declarator " + fn.name); skipStatement(); return; }
    }

    if (isPunct(";")) { advance(); return; }
    if (isPunct(",")) { diag(fn.line, "multiple declarators after function " + fn.name + ", rest skipped"); skipStatement(); return; }
    if (isPunct("}") || atEnd()) return;

    if (is("try")) { fn.exceptions.functionTryBlock = true; fn.exceptions.tryBlocks++; advance(); }

    if (isPunct(":")) {
        advance();
        fn.hasInitializerList = true;
        std::vector<Token> init;
        int angle = 0;
        for (;;) {
            const Token& t = cur();
            if (t.type == TokEnd) break;
            if (t.type == TokPunct) {
                if (t.text == "{" && angle == 0) break;
                if (t.text == "(" ) { init.push_back(t); skipBalanced(&init); init.push_back(Token(TokPunct, ")", t.line)); continue; }
                if (t.text == "<") ++angle;
                else if (t.text == ">") { if (angle > 0) --angle; }
                if (t.text == ";") break;
            }
            init.push_back(t);
            advance();
        }
        fn.initializerList = joinTokens(init);
    }

    if (isPunct("{")) {
        fn.hasBody = true;
        cur();
        fn.bodyBegin = m_pos;
        scanBody(fn);
        fn.bodyEnd = m_pos;
        if (fn.exceptions.functionTryBlock) {
            while (is("catch")) {
                advance();
                fn.exceptions.catchHandlers++;
                std::vector<Token> ty;
                if (isPunct("(")) skipBalanced(&ty);
                fn.exceptions.caughtTypes.push_back(joinTokens(ty));
                if (isPunct("{")) scanBody(fn);
            }
        }
        if (isPunct(";")) advance();
        return;
    }

    diag(fn.line, "unexpected '" + cur().text + "' after function declarator " + fn.name);
    skipStatement();
}

// ---- function bodies --------------------------------------------------------

void Parser::note(FunctionInfo& fn, const char* code, const std::string& text, int line)
{
    BodyNote n;
    n.code = code;
    n.text = text;
    n.line = line;
    fn.notes.push_back(n);
}

int Parser::countArgs(size_t openIdx) const
{
    size_t close = matchClose(openIdx);
    if (close == std::string::npos) return 0;
    int depth = 0, args = 0;
    bool any = false;
    for (size_t j = openIdx + 1; j < close; ++j) {
        const Token& k = m_tokens[j];
        if (k.type == TokPreproc) continue;
        any = true;
        if (k.type != TokPunct) continue;
        if (k.text == "(" || k.text == "[" || k.text == "{") ++depth;
        else if (k.text == ")" || k.text == "]" || k.text == "}") --depth;
        else if (k.text == "," && depth == 0) ++args;
    }
    return any ? args + 1 : 0;
}

void Parser::recordCall(FunctionInfo& fn, const std::string& prevPrev, const std::string& prev,
                        const std::string& name, int argCount, int line)
{
    CallInfo c;
    c.name = name;
    c.argCount = argCount;
    c.line = line;
    if (prev == ".") { c.kind = CallInfo::Member; c.receiver = prevPrev; }
    else if (prev == "->") { c.kind = CallInfo::Pointer; c.receiver = prevPrev; }
    else if (prev == "::") { c.kind = CallInfo::Scoped; c.qualifier = prevPrev; }
    else if (prev == "new") c.kind = CallInfo::New;
    else c.kind = CallInfo::Plain;
    if (!c.receiver.empty() && !(isalpha(static_cast<unsigned char>(c.receiver[0])) || c.receiver[0] == '_')) c.receiver = "";
    if (!c.qualifier.empty() && !(isalpha(static_cast<unsigned char>(c.qualifier[0])) || c.qualifier[0] == '_')) c.qualifier = "";
    fn.calls.push_back(c);
}

// records identifier uses and calls found in an initializer token list
void Parser::scanInitializer(FunctionInfo& fn, const std::vector<Token>& toks)
{
    for (size_t i = 0; i < toks.size(); ++i) {
        const Token& t = toks[i];
        if (t.type != TokIdentifier) continue;
        if (isStatementKeyword(t.text) || isTypeKeyword(t.text) || t.text == "sizeof") continue;
        fn.bodyIdents.push_back(t.text);
        if (i + 1 < toks.size() && toks[i + 1].text == "(") {
            int depth = 0, args = 0;
            bool any = false;
            for (size_t j = i + 2; j < toks.size(); ++j) {
                const std::string& s = toks[j].text;
                if (s == "(" || s == "[" || s == "{") ++depth;
                else if (s == ")" || s == "]" || s == "}") { if (depth == 0) break; --depth; }
                else if (s == "," && depth == 0) ++args;
                if (toks[j].type != TokPunct || (s != ")" && s != "(")) any = true;
                else if (depth > 0) any = true;
            }
            std::string prev = i > 0 ? toks[i - 1].text : "", prevPrev = i > 1 ? toks[i - 2].text : "";
            recordCall(fn, prevPrev, prev, t.text, any ? args + 1 : 0, t.line);
        }
    }
}

// cur() is at the '(' after if/while/for/switch: looks for "if (x = y)" and
// for a ';' directly after the closing parenthesis.
void Parser::checkCondition(FunctionInfo& fn, const std::string& keyword, int line, const std::string& prevToken)
{
    cur();
    size_t open = m_pos;
    size_t close = matchClose(open);
    if (close == std::string::npos) return;
    if (keyword == "if" || keyword == "while") {
        int d = 0;
        for (size_t j = open + 1; j < close; ++j) {
            const Token& k = m_tokens[j];
            if (k.type != TokPunct) continue;
            if (k.text == "(" || k.text == "[") ++d;
            else if (k.text == ")" || k.text == "]") --d;
            else if (k.text == "=" && d == 0) {
                size_t p = j - 1;
                while (p > open && m_tokens[p].type == TokPreproc) --p;
                size_t pp = p > open ? p - 1 : open;
                while (pp > open && m_tokens[pp].type == TokPreproc) --pp;
                bool declShape = p > open && m_tokens[p].type == TokIdentifier && pp > open &&
                                 (m_tokens[pp].type == TokIdentifier || m_tokens[pp].text == "*" ||
                                  m_tokens[pp].text == "&" || m_tokens[pp].text == ">");
                if (!declShape) note(fn, "assign-in-condition", "assignment '=' inside the " + keyword + " condition (did you mean '=='?)", line);
                break;
            }
        }
    }
    size_t after = rawNext(close);
    bool doWhile = (keyword == "while" && prevToken == "}");   // "do { ... } while (cond);"
    if (keyword != "switch" && !doWhile && after < m_tokens.size() && m_tokens[after].text == ";")
        note(fn, "empty-statement", "';' directly after " + keyword + " (...): the " + keyword + " controls an empty statement", m_tokens[after].line);
}

// cur() is at '{'. Consumes the balanced block, counting try/catch/throw,
// recording local variable declarations, identifier uses, calls, returns and
// suspicious statement patterns.
void Parser::scanBody(FunctionInfo& fn)
{
    int depth = 0;
    bool stmtStart = false;
    bool afterJump = false;        // the previous statement was return/throw/break/continue
    bool lastWasCatchAll = false;  // the block that just closed was "catch (...)"
    std::string prev, prevPrev;    // the two previous significant tokens
    for (;;) {
        const Token& t = cur();
        if (t.type == TokEnd) { diag(fn.line, "unterminated body of " + fn.name); return; }

        if (t.type == TokPunct) {
            if (t.text == "{") { ++depth; stmtStart = true; afterJump = false; }
            else if (t.text == "}") {
                --depth;
                fn.endLine = t.line;
                advance();
                stmtStart = true;
                afterJump = false;
                prevPrev = prev; prev = "}";
                if (depth == 0) return;
                continue;
            }
            else if (t.text == ";") stmtStart = true;
            else stmtStart = false;
            prevPrev = prev; prev = t.text;
            advance();
            continue;
        }

        if (t.type == TokIdentifier) {
            const std::string w = t.text;
            const int line = t.line;
            if (stmtStart && afterJump && w != "case" && w != "default" && w != "else") {
                note(fn, "unreachable", "statement after return/throw/break/continue can never run", line);
                afterJump = false;
            }
            if (stmtStart && w != "catch") lastWasCatchAll = false;
            const bool jumpStart = stmtStart && (prev == ";" || prev == "{" || prev == "}" || prev == ":");

            if (w == "try") { fn.exceptions.tryBlocks++; advance(); stmtStart = false; prevPrev = prev; prev = w; continue; }
            if (w == "catch") {
                fn.exceptions.catchHandlers++;
                if (lastWasCatchAll) note(fn, "handler-after-catch-all", "catch handler placed after catch (...) can never run", line);
                advance();
                std::vector<Token> ty;
                if (isPunct("(")) skipBalanced(&ty);
                std::string tyText = joinTokens(ty);
                fn.exceptions.caughtTypes.push_back(tyText);
                bool catchAll = (tyText == "...");
                if (!catchAll && !ty.empty()) {
                    bool byRef = false, builtin = false;
                    for (size_t i = 0; i < ty.size(); ++i) {
                        if (ty[i].text == "&" || ty[i].text == "*") byRef = true;
                        if (ty[i].type == TokIdentifier && isTypeKeyword(ty[i].text) && ty[i].text != "const") builtin = true;
                    }
                    if (!byRef && !builtin) note(fn, "catch-by-value", "exception caught by value: catch (" + tyText + "); catch by const reference instead", line);
                }
                lastWasCatchAll = catchAll;
                stmtStart = false; prevPrev = prev; prev = w;
                continue;
            }
            if (w == "throw") { fn.exceptions.throwStatements++; if (jumpStart) afterJump = true; advance(); stmtStart = false; prevPrev = prev; prev = w; continue; }
            if (w == "return") {
                if (peek(1).text == ";") fn.returnsVoid++; else fn.returnsValue++;
                if (jumpStart) afterJump = true;
                advance(); stmtStart = false; prevPrev = prev; prev = w;
                continue;
            }
            if (w == "break" || w == "continue") { if (jumpStart) afterJump = true; advance(); stmtStart = false; prevPrev = prev; prev = w; continue; }
            if (w == "for" || w == "while" || w == "if" || w == "switch") {
                const std::string before = prev;
                advance();
                if (isPunct("(")) { checkCondition(fn, w, line, before); advance(); stmtStart = true; }
                else stmtStart = false;
                prevPrev = prev; prev = "(";
                continue;
            }
            if (w == "typedef") {
                // local typedef: remember the introduced name so calls like "V2d(1, 2)" resolve
                std::string last;
                advance();
                while (!atEnd() && !isPunct(";") && !isPunct("}")) {
                    if (isIdent()) last = cur().text;
                    if (isPunct("(") || isPunct("[")) { std::vector<Token> inner; skipBalanced(&inner); for (size_t i = 0; i < inner.size(); ++i) if (inner[i].type == TokIdentifier) last = inner[i].text; continue; }
                    advance();
                }
                if (!last.empty()) fn.localTypedefs.push_back(last);
                stmtStart = false; prevPrev = prev; prev = w;
                continue;
            }
            if (w == "else" || w == "do") { advance(); stmtStart = true; afterJump = false; prevPrev = prev; prev = w; continue; }
            if (w == "case" || w == "default") {
                while (!atEnd() && !isPunct(":") && !isPunct(";") && !isPunct("}")) advance();
                if (isPunct(":")) advance();
                stmtStart = true; afterJump = false; prevPrev = prev; prev = ":";
                continue;
            }
            if (stmtStart && !isStatementKeyword(w)) {
                if (tryLocalDeclaration(fn)) { stmtStart = false; prevPrev = prev; prev = "decl"; continue; }
            }
            // an ordinary identifier: a use, possibly a call
            bool keyword = isStatementKeyword(w) || isTypeKeyword(w) || w == "sizeof" || w == "typeid" ||
                           w == "static_cast" || w == "dynamic_cast" || w == "reinterpret_cast" || w == "const_cast";
            if (!keyword) {
                fn.bodyIdents.push_back(w);
                if (peek(1).text == "(") recordCall(fn, prevPrev, prev, w, countArgs(rawNext(m_pos)), line);
            }
            advance();
            stmtStart = false; prevPrev = prev; prev = w;
            continue;
        }

        stmtStart = false; prevPrev = prev; prev = t.text;
        advance();
    }
}

// At a statement start inside a body. Decides whether the upcoming tokens form
// a local variable declaration; if so, records it and consumes it up to (not
// including) the terminating ';' / ')'.
bool Parser::tryLocalDeclaration(FunctionInfo& fn)
{
    cur();
    const size_t startIdx = m_pos;
    size_t j = m_pos;
    std::vector<Token> head;
    int angle = 0;
    std::string stop;
    size_t count = 0;
    for (; j < m_tokens.size(); j = rawNext(j)) {
        const Token& t = m_tokens[j];
        if (t.type == TokPreproc) continue;
        if (t.type == TokEnd) return false;
        if (t.type == TokPunct) {
            const std::string& s = t.text;
            if (angle == 0 && (s == ";" || s == "=" || s == "(" || s == "[" || s == "," || s == "{" || s == ")" || s == "}" || s == ":")) { stop = s; break; }
            if (s == "<") { ++angle; head.push_back(t); continue; }
            if (s == ">") { if (angle == 0) return false; --angle; head.push_back(t); continue; }
            if (s == ">>") { if (angle < 2) return false; angle -= 2; head.push_back(t); continue; }
            if (s == "*" || s == "&" || s == "::") { head.push_back(t); continue; }
            if (angle > 0 && (s == "," || s == "(" || s == ")")) { head.push_back(t); continue; }
            return false;
        }
        if (t.type == TokIdentifier) {
            if (isStatementKeyword(t.text) && !(head.empty() && isStorageWord(t.text))) return false;
            head.push_back(t);
            ++count;
            continue;
        }
        if (angle > 0 && t.type == TokNumber) { head.push_back(t); continue; }
        return false;
    }
    if (stop.empty() || angle != 0 || head.size() < 2) return false;
    if (stop == "{" || stop == "}" || stop == ")" || stop == ":") return false;
    if (stop == "," && head.size() < 2) return false;

    const Token& nameTok = head.back();
    if (nameTok.type != TokIdentifier || isTypeKeyword(nameTok.text)) return false;
    if (head.size() >= 2 && head[head.size() - 2].text == "::") return false;
    const Token& first = head[0];
    if (first.type != TokIdentifier) return false;
    if (head.size() >= 2) {
        const std::string& prev = head[head.size() - 2].text;
        bool prevOk = (head[head.size() - 2].type == TokIdentifier) || prev == "*" || prev == "&" || prev == ">";
        if (!prevOk) return false;
    }
    // "a * b;" style expressions are rare at statement start; "x < y;" rejected above

    // commit: consume the head
    while (m_pos < j) advance();
    (void)startIdx;

    size_t nameIdx = head.size() - 1;
    size_t baseEnd = nameIdx;
    while (baseEnd > 0 && (head[baseEnd - 1].text == "*" || head[baseEnd - 1].text == "&" || head[baseEnd - 1].text == "const")) --baseEnd;
    std::string baseType = joinTokens(head, 0, baseEnd);

    LocalVariable v;
    v.line = nameTok.line;
    v.name = nameTok.text;
    v.type = joinTokens(head, 0, nameIdx);
    if (startsWith(v.type, "static ")) v.type = v.type.substr(7);
    if (startsWith(v.type, "register ")) v.type = v.type.substr(9);

    for (;;) {
        bool isArray = false;
        while (isPunct("[")) {
            std::vector<Token> dim;
            skipBalanced(&dim);
            scanInitializer(fn, dim);
            v.type += "[" + joinTokens(dim) + "]";
            isArray = true;
        }
        bool pointerOrRef = v.type.find('*') != std::string::npos || v.type.find('&') != std::string::npos;
        if (isPunct("(")) {
            std::vector<Token> init;
            cur();
            int args = countArgs(m_pos);
            skipBalanced(&init);
            scanInitializer(fn, init);
            v.initializer = "(" + joinTokens(init) + ")";
            if (!pointerOrRef && !isArray) {
                CallInfo c; c.kind = CallInfo::Construct; c.name = baseType; c.argCount = args; c.line = v.line;
                fn.calls.push_back(c);
            }
        } else if (!isPunct("=") && !pointerOrRef && !isArray && !isTypeKeyword(lastComponent(baseType))) {
            // "Foo f;" : default construction
            CallInfo c; c.kind = CallInfo::Construct; c.name = baseType; c.argCount = 0; c.line = v.line;
            fn.calls.push_back(c);
        }
        if (isPunct("=")) {
            std::vector<Token> init;
            advance();
            int tdepth = 0;
            for (;;) {
                const Token& t = cur();
                if (t.type == TokEnd) break;
                if (t.type == TokPunct) {
                    if (t.text == ";" || t.text == "}" || t.text == ")") break;
                    if (t.text == "," && tdepth == 0) break;
                    if (t.text == "(" || t.text == "{" || t.text == "[") {
                        init.push_back(t);
                        skipBalanced(&init);
                        init.push_back(Token(TokPunct, t.text == "(" ? ")" : (t.text == "{" ? "}" : "]"), t.line));
                        continue;
                    }
                    if (t.text == "<" && !init.empty() && init.back().type == TokIdentifier) ++tdepth;
                    else if (t.text == ">" && tdepth > 0) --tdepth;
                }
                if (t.type == TokIdentifier && t.text == "throw") fn.exceptions.throwStatements++;
                init.push_back(t);
                advance();
            }
            scanInitializer(fn, init);
            v.initializer = joinTokens(init);
        }
        fn.locals.push_back(v);

        if (isPunct(",")) {
            advance();
            LocalVariable n;
            n.line = cur().line;
            std::vector<Token> pre;
            while (!atEnd() && !isIdent() && !isPunct(";") && !isPunct("}") && !isPunct(")")) { pre.push_back(cur()); advance(); }
            if (!isIdent()) return true;
            n.name = cur().text;
            advance();
            n.type = baseType + (pre.empty() ? "" : " " + joinTokens(pre));
            v = n;
            continue;
        }
        break;
    }
    return true;
}

} // namespace cppparse
