// Pp.cpp - version 6: C++98 preprocessor. See Pp.hpp.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1     // localtime for __DATE__ / __TIME__
#endif
#include "Pp.hpp"
#include "Literal.hpp"
#include "PathUtil.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace cppparse {

namespace {

bool identStart(char c) { return isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string numText(long v) { std::ostringstream s; s << v; return s.str(); }

const char* const kPuncts[] = {
    "...", "<<=", ">>=", "->*", "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", ".*",
    "{", "}", "[", "]", "#", "(", ")", ";", ":", "?", ".", "+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<", ">", ",", 0
};

PpToken makeTok(PpToken::Kind k, const std::string& text, int file, int line)
{
    PpToken t; t.kind = k; t.text = text; t.file = file; t.line = line; return t;
}

} // namespace


void ppLex(const std::string& source, int fileIndex, std::vector<PpToken>& out)
{
    // phase 1/2: remove line splices, remembering the line of every remaining character
    std::string src;
    std::vector<int> lineOf;
    src.reserve(source.size());
    lineOf.reserve(source.size() + 1);
    int line = 1;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (c == '\\' && i + 1 < source.size() && (source[i + 1] == '\n' || (source[i + 1] == '\r' && i + 2 < source.size() && source[i + 2] == '\n'))) {
            i += source[i + 1] == '\r' ? 2 : 1;
            ++line;
            continue;
        }
        if (c == '\r') continue;
        src += c;
        lineOf.push_back(line);
        if (c == '\n') ++line;
    }
    lineOf.push_back(line);

    size_t pos = 0, n = src.size();
    bool space = false, bol = true;
    while (pos < n) {
        char c = src[pos];
        if (c == '\n') {
            PpToken t = makeTok(PpToken::Newline, "\n", fileIndex, lineOf[pos]);
            out.push_back(t);
            ++pos; space = false; bol = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') { ++pos; space = true; continue; }
        if (c == '/' && pos + 1 < n && src[pos + 1] == '/') { while (pos < n && src[pos] != '\n') ++pos; space = true; continue; }
        if (c == '/' && pos + 1 < n && src[pos + 1] == '*') {
            size_t e = src.find("*/", pos + 2);
            pos = e == std::string::npos ? n : e + 2;
            space = true;
            continue;
        }
        PpToken t;
        t.file = fileIndex; t.line = lineOf[pos]; t.space = space; t.bol = bol;
        size_t s = pos;
        if (identStart(c)) {
            while (pos < n && identChar(src[pos])) ++pos;
            t.kind = PpToken::Ident;
            // L"..." and L'...'
            if (pos - s == 1 && c == 'L' && pos < n && (src[pos] == '"' || src[pos] == '\'')) { c = src[pos]; goto literal; }
        } else if (isdigit(static_cast<unsigned char>(c)) || (c == '.' && pos + 1 < n && isdigit(static_cast<unsigned char>(src[pos + 1])))) {
            ++pos;
            while (pos < n) {
                char d = src[pos];
                if ((d == '+' || d == '-') && (src[pos - 1] == 'e' || src[pos - 1] == 'E')) { ++pos; continue; }
                if (identChar(d) || d == '.') { ++pos; continue; }
                break;
            }
            t.kind = PpToken::Number;
        } else if (c == '"' || c == '\'') {
        literal:
            char q = c;
            ++pos;
            while (pos < n && src[pos] != q && src[pos] != '\n') { if (src[pos] == '\\' && pos + 1 < n && src[pos + 1] != '\n') ++pos; ++pos; }
            if (pos < n && src[pos] == q) ++pos;
            t.kind = q == '"' ? PpToken::String : PpToken::Char;
        } else {
            bool found = false;
            for (int k = 0; kPuncts[k]; ++k) {
                size_t len = std::string(kPuncts[k]).size();
                if (src.compare(pos, len, kPuncts[k]) == 0) { pos += len; found = true; break; }
            }
            if (!found) ++pos;
            t.kind = found ? PpToken::Punct : PpToken::Other;
        }
        t.text = src.substr(s, pos - s);
        out.push_back(t);
        space = false; bol = false;
    }
    if (out.empty() || out.back().kind != PpToken::Newline) out.push_back(makeTok(PpToken::Newline, "\n", fileIndex, lineOf[n]));
}


std::string PpMacro::bodyText() const
{
    std::string s;
    for (size_t i = 0; i < body.size(); ++i) { if (i && body[i].space) s += ' '; s += body[i].text; }
    return s;
}

Preprocessor::Preprocessor(const PpOptions& opt) : m_opt(opt), m_errors(0), m_pack(0)
{
    predefine("__cplusplus=199711L");
    predefine("__cppparse__=6");
    predefine("__STDC__=1");
    const Target& target = opt.target ? *opt.target : defaultTarget();
    for (size_t i = 0; i < target.predefines.size(); ++i) predefine(target.predefines[i]);
    for (size_t i = 0; i < opt.defines.size(); ++i) predefine(opt.defines[i]);
    for (size_t i = 0; i < opt.undefines.size(); ++i) m_macros.erase(opt.undefines[i]);
}

void Preprocessor::predefine(const std::string& def)
{
    size_t eq = def.find('=');
    std::string name = eq == std::string::npos ? def : def.substr(0, eq);
    std::string value = eq == std::string::npos ? std::string("1") : def.substr(eq + 1);
    PpMacro m;
    m.name = trim(name);
    m.file = "<command line>";
    std::vector<PpToken> toks;
    ppLex(value, -1, toks);
    for (size_t i = 0; i < toks.size(); ++i) if (toks[i].kind != PpToken::Newline) m.body.push_back(toks[i]);
    if (!m.body.empty()) m.body[0].space = false;
    m_macros[m.name] = m;
}

int Preprocessor::errorCount() const { return m_errors; }

void Preprocessor::diag(PpDiag::Severity s, int fileIndex, int line, const std::string& message)
{
    PpDiag d;
    d.severity = s;
    d.file = fileIndex >= 0 && fileIndex < static_cast<int>(m_files.size()) ? m_files[static_cast<size_t>(fileIndex)] : "";
    d.line = line;
    d.message = message;
    m_diags.push_back(d);
    if (s == PpDiag::Error) ++m_errors;
}

int Preprocessor::addFile(const std::string& path)
{
    m_files.push_back(path);
    return static_cast<int>(m_files.size() - 1);
}

bool Preprocessor::run(const std::string& path)
{
    std::string text;
    if (!readFile(path, text)) { diag(PpDiag::Error, -1, 0, "cannot read " + path); return false; }
    return runText(path, text);
}

bool Preprocessor::runText(const std::string& name, const std::string& text)
{
    int f = addFile(name);
    processFile(f, text, 0);
    return m_errors == 0;
}

bool Preprocessor::conditionsActive(const std::vector<Cond>& conds) const
{
    for (size_t i = 0; i < conds.size(); ++i) if (!conds[i].active) return false;
    return true;
}

void Preprocessor::processFile(int fileIndex, const std::string& source, int depth)
{
    std::vector<PpToken> toks;
    ppLex(source, fileIndex, toks);
    std::vector<Cond> conds;
    std::vector<PpToken> chunk;      // text lines between directives: macro invocations may span them
    size_t i = 0;
    while (i < toks.size()) {
        // one logical line
        size_t e = i;
        while (e < toks.size() && toks[e].kind != PpToken::Newline) ++e;
        bool isDirective = i < e && toks[i].kind == PpToken::Punct && toks[i].is("#") && toks[i].bol;
        if (isDirective) {
            if (!chunk.empty()) { expandChunk(chunk); chunk.clear(); }
            std::vector<PpToken> line(toks.begin() + static_cast<long>(i) + 1, toks.begin() + static_cast<long>(e));
            directive(line, fileIndex, conds, depth);
        } else if (conditionsActive(conds)) {
            chunk.insert(chunk.end(), toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(e));
        }
        i = e + 1;
    }
    if (!chunk.empty()) expandChunk(chunk);
    for (size_t c = 0; c < conds.size(); ++c) diag(PpDiag::Error, fileIndex, conds[c].line, "#if without #endif at end of file");
}

void Preprocessor::directive(std::vector<PpToken>& line, int fileIndex, std::vector<Cond>& conds, int depth)
{
    if (line.empty()) return;                                   // "#" alone: the null directive
    std::string name = line[0].kind == PpToken::Ident ? line[0].text : "";
    int ln = line[0].line;
    bool active = conditionsActive(conds);

    // conditionals are processed even inside skipped regions (to keep the nesting right)
    if (name == "if" || name == "ifdef" || name == "ifndef") {
        Cond c; c.taken = false; c.seenElse = false; c.line = ln; c.active = false;
        if (active) {
            bool v;
            if (name == "if") v = evalCondition(line, 1, fileIndex);
            else {
                if (line.size() < 2 || line[1].kind != PpToken::Ident) { diag(PpDiag::Error, fileIndex, ln, "#" + name + " needs a macro name"); v = false; }
                else v = m_macros.count(line[1].text) != 0;
                if (name == "ifndef") v = !v;
            }
            c.active = v; c.taken = v;
        } else {
            c.taken = true;                                     // nothing in this group may become active
        }
        conds.push_back(c);
        return;
    }
    if (name == "elif" || name == "else" || name == "endif") {
        if (conds.empty()) { diag(PpDiag::Error, fileIndex, ln, "#" + name + " without #if"); return; }
        Cond& c = conds.back();
        if (name == "endif") { conds.pop_back(); return; }
        if (c.seenElse) diag(PpDiag::Error, fileIndex, ln, "#" + name + " after #else");
        if (name == "else") { c.seenElse = true; c.active = !c.taken; c.taken = true; return; }
        // elif
        if (c.taken) { c.active = false; return; }
        std::vector<Cond> outer(conds.begin(), conds.end() - 1);
        bool v = conditionsActive(outer) && evalCondition(line, 1, fileIndex);
        c.active = v; c.taken = v;
        return;
    }
    if (!active) return;

    if (name == "define") { defineDirective(line, 1, fileIndex); return; }
    if (name == "undef") {
        if (line.size() < 2 || line[1].kind != PpToken::Ident) { diag(PpDiag::Error, fileIndex, ln, "#undef needs a macro name"); return; }
        m_macros.erase(line[1].text);
        return;
    }
    if (name == "include") { includeDirective(line, 1, fileIndex, depth); return; }
    if (name == "error" || name == "warning") {
        std::string msg;
        for (size_t i = 1; i < line.size(); ++i) { if (i > 1 && line[i].space) msg += ' '; msg += line[i].text; }
        diag(name == "error" ? PpDiag::Error : PpDiag::Warning, fileIndex, ln, "#" + name + " " + msg);
        return;
    }
    if (name == "pragma") {
        if (line.size() >= 2 && line[1].text == "once" && fileIndex >= 0) m_pragmaOnce.insert(toLower(normalizePath(m_files[static_cast<size_t>(fileIndex)])));
        if (line.size() >= 2 && line[1].text == "pack") packPragma(line, fileIndex);
        return;                                                 // other pragmas are ignored
    }
    if (name == "line" || name == "ident" || name == "sccs" || name == "assert" || name == "unassert" || name == "import" || name == "include_next") {
        if (name == "import" || name == "include_next") diag(PpDiag::Warning, fileIndex, ln, "#" + name + " is not supported");
        return;
    }
    if (line[0].kind == PpToken::Number) return;                // "# 12 file": a line marker
    diag(PpDiag::Warning, fileIndex, ln, "unknown directive #" + line[0].text);
}

void Preprocessor::defineDirective(std::vector<PpToken>& line, size_t at, int fileIndex)
{
    int ln = line.empty() ? 0 : line[0].line;
    if (at >= line.size() || line[at].kind != PpToken::Ident) { diag(PpDiag::Error, fileIndex, ln, "#define needs a macro name"); return; }
    PpMacro m;
    m.name = line[at].text;
    m.file = fileIndex >= 0 ? m_files[static_cast<size_t>(fileIndex)] : "";
    m.line = ln;
    size_t i = at + 1;
    if (i < line.size() && line[i].is("(") && !line[i].space) {           // function-like: '(' right after the name
        m.functionLike = true;
        ++i;
        bool expectName = true;
        while (i < line.size() && !line[i].is(")")) {
            if (expectName) {
                if (line[i].kind != PpToken::Ident && !line[i].is("...")) { diag(PpDiag::Error, fileIndex, ln, "bad parameter list in #define " + m.name); return; }
                m.params.push_back(line[i].text);
                expectName = false;
            } else {
                if (!line[i].is(",")) { diag(PpDiag::Error, fileIndex, ln, "expected ',' in the parameter list of " + m.name); return; }
                expectName = true;
            }
            ++i;
        }
        if (i >= line.size()) { diag(PpDiag::Error, fileIndex, ln, "missing ')' in #define " + m.name); return; }
        ++i;
    }
    for (; i < line.size(); ++i) m.body.push_back(line[i]);
    if (!m.body.empty()) {
        m.body[0].space = false;
        if (m.body[0].is("##") || m.body.back().is("##")) diag(PpDiag::Error, fileIndex, ln, "'##' cannot appear at either end of the macro " + m.name);
        if (m.functionLike)
            for (size_t k = 0; k < m.body.size(); ++k)
                if (m.body[k].is("#") && (k + 1 >= m.body.size() || m.body[k + 1].kind != PpToken::Ident)) diag(PpDiag::Error, fileIndex, ln, "'#' is not followed by a macro parameter in " + m.name);
    }
    std::map<std::string, PpMacro>::iterator old = m_macros.find(m.name);
    if (old != m_macros.end() && (old->second.functionLike != m.functionLike || old->second.params != m.params || old->second.bodyText() != m.bodyText()))
        diag(PpDiag::Warning, fileIndex, ln, "macro " + m.name + " redefined (was defined at " + old->second.file + ":" + numText(old->second.line) + ")");
    m_macros[m.name] = m;
}

void Preprocessor::includeDirective(std::vector<PpToken>& line, size_t at, int fileIndex, int depth)
{
    int ln = line.empty() ? 0 : line[0].line;
    if (depth > 200) { diag(PpDiag::Error, fileIndex, ln, "#include nested too deeply"); return; }
    std::vector<PpToken> rest(line.begin() + static_cast<long>(at), line.end());
    if (rest.empty()) { diag(PpDiag::Error, fileIndex, ln, "#include needs a file name"); return; }
    if (rest[0].kind == PpToken::Ident) {                        // #include MACRO
        std::vector<PpToken> expanded;
        expandTokens(rest, expanded);
        rest.swap(expanded);
        if (rest.empty()) { diag(PpDiag::Error, fileIndex, ln, "#include needs a file name"); return; }
    }
    std::string name;
    bool angle = false;
    if (rest[0].kind == PpToken::String) name = rest[0].text.substr(1, rest[0].text.size() - 2);
    else if (rest[0].is("<")) {
        angle = true;
        size_t i = 1;
        for (; i < rest.size() && !rest[i].is(">"); ++i) { if (i > 1 && rest[i].space) name += ' '; name += rest[i].text; }
        if (i >= rest.size()) { diag(PpDiag::Error, fileIndex, ln, "missing '>' in #include"); return; }
    } else { diag(PpDiag::Error, fileIndex, ln, "bad #include"); return; }

    std::string found;
    if (!angle && fileIndex >= 0) {
        std::string here = dirName(m_files[static_cast<size_t>(fileIndex)]);
        std::string cand = here.empty() || here == "." ? name : joinPath(here, name);
        if (fileExists(cand)) found = cand;
    }
    for (size_t d = 0; found.empty() && d < m_opt.includeDirs.size(); ++d) {
        std::string cand = joinPath(m_opt.includeDirs[d], name);
        if (fileExists(cand)) found = cand;
    }
    if (found.empty()) {
        m_missing.push_back(name);
        diag(PpDiag::Warning, fileIndex, ln, "include file not found: " + std::string(angle ? "<" : "\"") + name + (angle ? ">" : "\""));
        return;
    }
    std::string key = toLower(normalizePath(found));
    if (m_pragmaOnce.count(key)) return;
    bool seen = false;
    for (size_t i = 0; i < m_includes.size() && !seen; ++i) seen = toLower(normalizePath(m_includes[i])) == key;
    if (!seen) m_includes.push_back(found);
    std::map<std::string, std::string>::iterator c = m_fileCache.find(key);
    if (c == m_fileCache.end()) {
        std::string text;
        if (!readFile(found, text)) { diag(PpDiag::Error, fileIndex, ln, "cannot read " + found); return; }
        c = m_fileCache.insert(std::make_pair(key, text)).first;
    }
    int f = addFile(found);
    processFile(f, c->second, depth + 1);
}


namespace {

struct CondParser {
    const std::vector<PpToken>& t;
    size_t i;
    bool bad;
    CondParser(const std::vector<PpToken>& toks) : t(toks), i(0), bad(false) {}

    bool at(const char* s) const { return i < t.size() && t[i].text == s; }
    long charValue(const std::string& lit)
    {
        int count = 0;
        return charLiteralValue(lit, count);
    }
    long number(const std::string& s)
    {
        std::string digits = s;
        while (!digits.empty() && (tolower(static_cast<unsigned char>(digits[digits.size() - 1])) == 'u' || tolower(static_cast<unsigned char>(digits[digits.size() - 1])) == 'l')) digits.erase(digits.size() - 1);
        return strtol(digits.c_str(), 0, 0);
    }
    long primary()
    {
        if (i >= t.size()) { bad = true; return 0; }
        const PpToken& k = t[i];
        if (k.is("(")) { ++i; long v = ternary(); if (!at(")")) bad = true; else ++i; return v; }
        if (k.is("!")) { ++i; return !primary(); }
        if (k.is("~")) { ++i; return ~primary(); }
        if (k.is("-")) { ++i; return -primary(); }
        if (k.is("+")) { ++i; return primary(); }
        ++i;
        if (k.kind == PpToken::Number) return number(k.text);
        if (k.kind == PpToken::Char) return charValue(k.text);
        if (k.kind == PpToken::Ident) {
            // feature-test operators of newer compilers (__has_include, __has_cpp_attribute, __has_builtin, ...): 0
            if (k.text.compare(0, 6, "__has_") == 0 && at("(")) {
                int depth = 0;
                for (; i < t.size(); ++i) { if (t[i].is("(")) ++depth; else if (t[i].is(")") && --depth == 0) { ++i; break; } }
                return 0;
            }
            return k.text == "true" ? 1 : 0;                                  // unknown identifiers are 0
        }
        bad = true;
        return 0;
    }
    long binary(int level)
    {
        static const char* const ops[][5] = { {"||",0,0,0,0}, {"&&",0,0,0,0}, {"|",0,0,0,0}, {"^",0,0,0,0}, {"&",0,0,0,0}, {"==","!=",0,0,0},
                                             {"<","<=",">",">=",0}, {"<<",">>",0,0,0}, {"+","-",0,0,0}, {"*","/","%",0,0} };
        if (level == 10) return primary();
        long v = binary(level + 1);
        for (;;) {
            std::string op;
            for (int k = 0; k < 5 && ops[level][k]; ++k) if (at(ops[level][k])) op = ops[level][k];
            if (op.empty()) return v;
            ++i;
            long r = binary(level + 1);
            if (op == "||") v = v || r; else if (op == "&&") v = v && r; else if (op == "|") v = v | r; else if (op == "^") v = v ^ r;
            else if (op == "&") v = v & r; else if (op == "==") v = v == r; else if (op == "!=") v = v != r;
            else if (op == "<") v = v < r; else if (op == "<=") v = v <= r; else if (op == ">") v = v > r; else if (op == ">=") v = v >= r;
            else if (op == "<<") v = v << r; else if (op == ">>") v = v >> r; else if (op == "+") v = v + r; else if (op == "-") v = v - r;
            else if (op == "*") v = v * r; else if (op == "/") { if (r == 0) { bad = true; v = 0; } else v = v / r; }
            else if (op == "%") { if (r == 0) { bad = true; v = 0; } else v = v % r; }
        }
    }
    long ternary()
    {
        long c = binary(0);
        if (!at("?")) return c;
        ++i;
        long a = ternary();
        if (!at(":")) { bad = true; return 0; }
        ++i;
        long b = ternary();
        return c ? a : b;
    }
};

} // namespace

bool Preprocessor::evalCondition(std::vector<PpToken>& line, size_t at, int fileIndex)
{
    int ln = line.empty() ? 0 : line[0].line;
    // "defined X" and "defined(X)" are evaluated before macro expansion
    std::vector<PpToken> pre;
    for (size_t i = at; i < line.size(); ++i) {
        if (line[i].kind == PpToken::Ident && line[i].text == "defined") {
            size_t j = i + 1;
            bool paren = j < line.size() && line[j].is("(");
            if (paren) ++j;
            if (j >= line.size() || line[j].kind != PpToken::Ident) { diag(PpDiag::Error, fileIndex, ln, "'defined' needs a macro name"); return false; }
            bool v = m_macros.count(line[j].text) != 0;
            if (paren) { if (j + 1 >= line.size() || !line[j + 1].is(")")) { diag(PpDiag::Error, fileIndex, ln, "missing ')' after defined"); return false; } ++j; }
            pre.push_back(makeTok(PpToken::Number, v ? "1" : "0", fileIndex, ln));
            i = j;
            continue;
        }
        pre.push_back(line[i]);
    }
    std::vector<PpToken> expanded;
    expandTokens(pre, expanded);
    if (expanded.empty()) { diag(PpDiag::Error, fileIndex, ln, "#if with no expression"); return false; }
    CondParser p(expanded);
    long v = p.ternary();
    if (p.bad || p.i != expanded.size()) { diag(PpDiag::Error, fileIndex, ln, "cannot evaluate the #if expression"); return false; }
    return v != 0;
}


void Preprocessor::expandChunk(std::vector<PpToken>& chunk)
{
    std::vector<PpToken> out;
    expandTokens(chunk, out);
    for (size_t i = 0; i < out.size(); ++i) out[i].pack = m_pack;   // the parser reads it at each class key
    m_out.insert(m_out.end(), out.begin(), out.end());
}

// #pragma pack( [show] | [push|pop] [, identifier] [, n] | [n] ), as MSVC: n is 1, 2, 4, 8 or 16; "pack()" restores
// the default; "pop, identifier" pops records until that identifier's record is gone (nothing if it is not on the
// stack); a pop that names n sets n afterwards
void Preprocessor::packPragma(const std::vector<PpToken>& line, int fileIndex)
{
    const int ln = line[0].line;
    std::vector<std::string> args;
    size_t i = 2;
    if (i >= line.size() || !line[i].is("(")) { diag(PpDiag::Warning, fileIndex, ln, "#pragma pack: expected '('"); return; }
    std::string cur;
    for (++i; i < line.size() && !line[i].is(")"); ++i) {
        if (line[i].is(",")) { args.push_back(cur); cur.clear(); } else cur += line[i].text;
    }
    if (i >= line.size()) { diag(PpDiag::Warning, fileIndex, ln, "#pragma pack: expected ')'"); return; }
    if (!cur.empty() || !args.empty()) args.push_back(cur);
    std::string action;
    if (!args.empty() && (args[0] == "push" || args[0] == "pop" || args[0] == "show")) { action = args[0]; args.erase(args.begin()); }
    std::string ident;
    int n = -1;
    for (size_t a = 0; a < args.size(); ++a) {
        const std::string& s = args[a];
        if (s.empty()) continue;
        if (isdigit(static_cast<unsigned char>(s[0]))) {
            n = atoi(s.c_str());
            if (n != 1 && n != 2 && n != 4 && n != 8 && n != 16) { diag(PpDiag::Warning, fileIndex, ln, "#pragma pack: " + s + " is not 1, 2, 4, 8 or 16; ignored"); return; }
        } else ident = s;
    }
    if (action == "show") { diag(PpDiag::Warning, fileIndex, ln, "#pragma pack: the current packing is " + intToString(m_pack ? m_pack : 16)); return; }
    if (action == "push") { m_packStack.push_back(std::make_pair(ident, m_pack)); if (n >= 0) m_pack = n; return; }
    if (action == "pop") {
        if (!ident.empty()) {
            size_t k = m_packStack.size();
            while (k > 0 && m_packStack[k - 1].first != ident) --k;
            if (k > 0) { m_pack = m_packStack[k - 1].second; m_packStack.resize(k - 1); }
        } else if (!m_packStack.empty()) { m_pack = m_packStack.back().second; m_packStack.pop_back(); }
        if (n >= 0) m_pack = n;
        return;
    }
    m_pack = n >= 0 ? n : 0;                                    // pack(n), or pack() for the default
}

namespace {

bool hidden(const PpToken& t, const std::string& name)
{
    for (size_t i = 0; i < t.hide.size(); ++i) if (t.hide[i] == name) return true;
    return false;
}

std::string quoteString(const std::string& s)
{
    std::string r = "\"";
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\\' || s[i] == '"') r += '\\'; r += s[i]; }
    return r + "\"";
}

} // namespace

void Preprocessor::expandTokens(std::vector<PpToken>& in, std::vector<PpToken>& out)
{
    size_t i = 0;
    int guard = 0;
    while (i < in.size()) {
        PpToken t = in[i];
        if (t.kind != PpToken::Ident || hidden(t, t.text)) { out.push_back(t); ++i; continue; }
        if (t.text == "__LINE__") { PpToken n = makeTok(PpToken::Number, numText(t.line), t.file, t.line); n.space = t.space; out.push_back(n); ++i; continue; }
        if (t.text == "__FILE__") { PpToken n = makeTok(PpToken::String, quoteString(t.file >= 0 && t.file < static_cast<int>(m_files.size()) ? m_files[static_cast<size_t>(t.file)] : "?"), t.file, t.line); n.space = t.space; out.push_back(n); ++i; continue; }
        if (t.text == "__DATE__" || t.text == "__TIME__") {
            char buf[32];
            time_t now = time(0);
            strftime(buf, sizeof buf, t.text == "__DATE__" ? "%b %d %Y" : "%H:%M:%S", localtime(&now));
            PpToken n = makeTok(PpToken::String, quoteString(buf), t.file, t.line); n.space = t.space; out.push_back(n); ++i; continue;
        }
        std::map<std::string, PpMacro>::const_iterator m = m_macros.find(t.text);
        if (m == m_macros.end()) { out.push_back(t); ++i; continue; }
        const PpMacro& mac = m->second;
        if (++guard > 200000) { diag(PpDiag::Error, t.file, t.line, "macro expansion too large (" + t.text + ")"); out.push_back(t); ++i; continue; }

        if (!mac.functionLike) {
            std::vector<PpToken> body = mac.body;
            for (size_t k = 0; k < body.size(); ++k) { body[k].file = t.file; body[k].line = t.line; body[k].hide = t.hide; body[k].hide.push_back(t.text); body[k].bol = false; }
            if (!body.empty()) { body[0].space = t.space; body[0].bol = t.bol; }
            in.erase(in.begin() + static_cast<long>(i));
            in.insert(in.begin() + static_cast<long>(i), body.begin(), body.end());
            continue;                                           // rescan
        }
        // function-like: the next token must be '('; an object-like macro standing there is expanded
        // first, as cl and gcc do ("#define PAREN (" ... "SQ PAREN 3)")
        size_t j = i + 1;
        for (int step = 0; j < in.size() && in[j].kind == PpToken::Ident && !hidden(in[j], in[j].text) && step < 64; ++step) {
            std::map<std::string, PpMacro>::const_iterator om = m_macros.find(in[j].text);
            if (om == m_macros.end() || om->second.functionLike) break;
            PpToken o = in[j];
            std::vector<PpToken> body = om->second.body;
            for (size_t k = 0; k < body.size(); ++k) { body[k].file = o.file; body[k].line = o.line; body[k].hide = o.hide; body[k].hide.push_back(o.text); body[k].bol = false; }
            if (!body.empty()) body[0].space = o.space;
            in.erase(in.begin() + static_cast<long>(j));
            in.insert(in.begin() + static_cast<long>(j), body.begin(), body.end());
        }
        if (j >= in.size() || !in[j].is("(")) { out.push_back(t); ++i; continue; }
        std::vector<std::vector<PpToken> > args;
        std::vector<PpToken> cur;
        int depth = 0;
        size_t k = j + 1;
        bool closed = false;
        for (; k < in.size(); ++k) {
            const PpToken& a = in[k];
            if (a.is("(")) ++depth;
            else if (a.is(")")) { if (depth == 0) { closed = true; break; } --depth; }
            else if (a.is(",") && depth == 0) { args.push_back(cur); cur.clear(); continue; }
            cur.push_back(a);
        }
        if (!closed) { diag(PpDiag::Error, t.file, t.line, "unterminated call of macro " + t.text); out.push_back(t); ++i; continue; }
        args.push_back(cur);
        if (mac.params.empty() && args.size() == 1 && args[0].empty()) args.clear();
        // variadic macro (C99 / C++11, used by the MSVC headers): the extra arguments become __VA_ARGS__
        bool variadic = !mac.params.empty() && mac.params.back() == "...";
        if (variadic) {
            size_t fixed = mac.params.size() - 1;
            if (args.size() < fixed) args.resize(fixed);
            std::vector<PpToken> rest;
            for (size_t a = fixed; a < args.size(); ++a) {
                if (a > fixed) { PpToken comma = makeTok(PpToken::Punct, ",", t.file, t.line); rest.push_back(comma); }
                rest.insert(rest.end(), args[a].begin(), args[a].end());
            }
            args.resize(fixed);
            args.push_back(rest);
        }
        if (args.size() != mac.params.size()) {
            diag(PpDiag::Error, t.file, t.line, "macro " + t.text + " takes " + numText(static_cast<long>(mac.params.size())) + " argument(s), " + numText(static_cast<long>(args.size())) + " given");
            out.push_back(t); ++i; continue;
        }
        std::vector<PpToken> result;
        substitute(mac, args, t, result);
        std::vector<std::string> hs = t.hide;
        for (size_t h = 0; h < in[k].hide.size(); ++h) if (!hidden(t, in[k].hide[h])) hs.push_back(in[k].hide[h]);   // closing ')' hide set
        hs.push_back(t.text);
        for (size_t r = 0; r < result.size(); ++r) {
            result[r].file = t.file; result[r].line = t.line; result[r].bol = false;
            for (size_t h = 0; h < hs.size(); ++h) if (!hidden(result[r], hs[h])) result[r].hide.push_back(hs[h]);
        }
        if (!result.empty()) { result[0].space = t.space; result[0].bol = t.bol; }
        in.erase(in.begin() + static_cast<long>(i), in.begin() + static_cast<long>(k) + 1);
        in.insert(in.begin() + static_cast<long>(i), result.begin(), result.end());
    }
}

void Preprocessor::stringize(const std::vector<PpToken>& toks, PpToken& out) const
{
    std::string s;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i && toks[i].space) s += ' ';
        const std::string& x = toks[i].text;
        if (toks[i].kind == PpToken::String || toks[i].kind == PpToken::Char) {
            for (size_t c = 0; c < x.size(); ++c) { if (x[c] == '"' || x[c] == '\\') s += '\\'; s += x[c]; }
        } else s += x;
    }
    out.kind = PpToken::String;
    out.text = "\"" + s + "\"";
}

bool Preprocessor::paste(const PpToken& a, const PpToken& b, PpToken& out) const
{
    std::vector<PpToken> toks;
    ppLex(a.text + b.text, a.file, toks);
    size_t count = 0;
    for (size_t i = 0; i < toks.size(); ++i) if (toks[i].kind != PpToken::Newline) { if (count++ == 0) out = toks[i]; }
    if (count != 1) return false;
    out.file = a.file; out.line = a.line; out.space = a.space; out.hide = a.hide;
    return true;
}

bool Preprocessor::substitute(const PpMacro& mac, const std::vector<std::vector<PpToken> >& args, const PpToken& ident, std::vector<PpToken>& out)
{
    // pass 1: parameters become their (expanded or raw) arguments; # stringizes; ## is kept for pass 2
    std::vector<PpToken> res;
    std::vector<bool> fromParam;     // marks tokens that came from a parameter (for empty-argument placemarkers)
    const std::vector<PpToken>& body = mac.body;
    for (size_t i = 0; i < body.size(); ++i) {
        const PpToken& b = body[i];
        int param = -1;
        if (b.kind == PpToken::Ident)
            for (size_t p = 0; p < mac.params.size(); ++p)
                if (mac.params[p] == b.text || (mac.params[p] == "..." && b.text == "__VA_ARGS__")) param = static_cast<int>(p);
        if (b.is("#") && i + 1 < body.size()) {
            int p2 = -1;
            for (size_t p = 0; p < mac.params.size(); ++p)
                if (mac.params[p] == body[i + 1].text || (mac.params[p] == "..." && body[i + 1].text == "__VA_ARGS__")) p2 = static_cast<int>(p);
            if (p2 >= 0) {
                PpToken s;
                stringize(args[static_cast<size_t>(p2)], s);
                s.space = b.space;
                res.push_back(s); fromParam.push_back(false);
                ++i;
                continue;
            }
        }
        if (param < 0) { res.push_back(b); fromParam.push_back(false); continue; }
        bool nextPaste = i + 1 < body.size() && body[i + 1].is("##");
        bool prevPaste = i > 0 && body[i - 1].is("##");
        const std::vector<PpToken>& arg = args[static_cast<size_t>(param)];
        if (nextPaste || prevPaste) {
            if (arg.empty()) { PpToken pm; pm.kind = PpToken::Other; pm.text = ""; res.push_back(pm); fromParam.push_back(true); }   // placemarker
            for (size_t k = 0; k < arg.size(); ++k) { res.push_back(arg[k]); fromParam.push_back(true); }
            if (!arg.empty()) res[res.size() - arg.size()].space = b.space;
        } else {
            std::vector<PpToken> copy = arg, expanded;
            expandTokens(copy, expanded);
            for (size_t k = 0; k < expanded.size(); ++k) { res.push_back(expanded[k]); fromParam.push_back(true); }
            if (!expanded.empty()) res[res.size() - expanded.size()].space = b.space;
        }
    }
    // pass 2: token pasting
    for (size_t i = 0; i < res.size(); ++i) {
        if (!res[i].is("##") || res[i].kind != PpToken::Punct || i == 0 || i + 1 >= res.size()) continue;
        PpToken left = res[i - 1], right = res[i + 1], joined;
        bool leftEmpty = left.kind == PpToken::Other && left.text.empty();
        bool rightEmpty = right.kind == PpToken::Other && right.text.empty();
        std::vector<PpToken> repl;
        if (leftEmpty && rightEmpty) { }
        else if (leftEmpty) repl.push_back(right);
        else if (rightEmpty) repl.push_back(left);
        else if (paste(left, right, joined)) repl.push_back(joined);
        else { diag(PpDiag::Error, ident.file, ident.line, "pasting \"" + left.text + "\" and \"" + right.text + "\" does not give a valid token"); repl.push_back(left); repl.push_back(right); }
        // the offset is computed before the seek: "begin() + i - 1" would first step past the end of a vector
        // that the erase has shortened
        const long at = static_cast<long>(i) - 1;
        res.erase(res.begin() + at, res.begin() + (at + 3));
        res.insert(res.begin() + at, repl.begin(), repl.end());
        i = i >= 2 ? i - 2 : 0;
    }
    for (size_t i = 0; i < res.size(); ++i)
        if (!(res[i].kind == PpToken::Other && res[i].text.empty())) out.push_back(res[i]);   // drop placemarkers
    return true;
}


std::string Preprocessor::text() const
{
    // a new output line starts at every token that began a source line (like cl /EP: the tokens after
    // a macro call that spans lines stay on the call's line); blank lines keep later lines aligned
    std::string s;
    int curFile = -1, curLine = 0;
    for (size_t i = 0; i < m_out.size(); ++i) {
        const PpToken& t = m_out[i];
        bool newline = false;
        if (t.file != curFile) {
            if (!s.empty() && s[s.size() - 1] != '\n') s += '\n';
            curFile = t.file; curLine = t.line; newline = true;
        } else if (t.bol && i > 0) {
            if (curLine >= t.line) s += '\n';
            while (curLine < t.line) { s += '\n'; ++curLine; }
            newline = true;
        }
        if (!newline && t.space && !s.empty() && s[s.size() - 1] != '\n') s += ' ';
        s += t.text;
    }
    if (!s.empty() && s[s.size() - 1] != '\n') s += '\n';
    return s;
}

} // namespace cppparse
