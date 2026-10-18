// Lexer.cpp - tokenizer for C++ source text.
#include "Lexer.hpp"
#include "PathUtil.hpp"

#include <cctype>

namespace cppparse {

namespace {

bool isIdentStart(char c)
{
    return isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isIdentChar(char c)
{
    return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// three- and two-character punctuators, longest first
const char* const kPunct3[] = { "->*", "<<=", ">>=", "...", 0 };
const char* const kPunct2[] = { "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
                                "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                "##", ".*", 0 };

} // namespace

Lexer::Lexer(const std::string& source)
    : m_src(source), m_pos(0), m_line(1), m_lines(0)
{
}

void Lexer::run(std::vector<Token>& out)
{
    const std::string& s = m_src;
    const size_t n = s.size();
    bool lineStart = true;

    while (m_pos < n) {
        char c = s[m_pos];

        if (c == '\n') { ++m_line; ++m_pos; lineStart = true; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { ++m_pos; continue; }

        // line splice: backslash followed by newline
        if (c == '\\' && m_pos + 1 < n && (s[m_pos + 1] == '\n' ||
            (s[m_pos + 1] == '\r' && m_pos + 2 < n && s[m_pos + 2] == '\n'))) {
            m_pos += (s[m_pos + 1] == '\r') ? 3 : 2;
            ++m_line;
            continue;
        }

        // comments
        if (c == '/' && m_pos + 1 < n && s[m_pos + 1] == '/') {
            while (m_pos < n && s[m_pos] != '\n') ++m_pos;
            continue;
        }
        if (c == '/' && m_pos + 1 < n && s[m_pos + 1] == '*') {
            m_pos += 2;
            while (m_pos < n && !(s[m_pos] == '*' && m_pos + 1 < n && s[m_pos + 1] == '/')) {
                if (s[m_pos] == '\n') ++m_line;
                ++m_pos;
            }
            if (m_pos < n) m_pos += 2;
            continue;
        }

        // preprocessor directive: '#' as the first non-blank character of a line
        if (c == '#' && lineStart) {
            readDirective(out);
            lineStart = true;      // readDirective consumed the newline
            continue;
        }
        lineStart = false;

        if (isIdentStart(c)) {
            size_t start = m_pos;
            while (m_pos < n && isIdentChar(s[m_pos])) ++m_pos;
            std::string word = s.substr(start, m_pos - start);
            // wide / raw string prefixes: L"..." L'...'
            if ((word == "L" || word == "u8" || word == "u" || word == "U") &&
                m_pos < n && (s[m_pos] == '"' || s[m_pos] == '\'')) {
                readString(out, s[m_pos], word);
                continue;
            }
            out.push_back(Token(TokIdentifier, word, m_line));
            continue;
        }

        if (isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && m_pos + 1 < n && isdigit(static_cast<unsigned char>(s[m_pos + 1])))) {
            readNumber(out);
            continue;
        }

        if (c == '"' || c == '\'') { readString(out, c, ""); continue; }

        if (ispunct(static_cast<unsigned char>(c))) { readPunct(out); continue; }

        ++m_pos; // anything else (control characters, high bytes) is skipped
    }

    m_lines = m_line;
    if (n > 0 && s[n - 1] == '\n') m_lines = m_line - 1;
    out.push_back(Token(TokEnd, "", m_line));
}

void Lexer::readDirective(std::vector<Token>& out)
{
    const std::string& s = m_src;
    const size_t n = s.size();
    int startLine = m_line;
    ++m_pos; // '#'

    std::string text;
    bool inString = false;
    char quote = 0;
    while (m_pos < n) {
        char c = s[m_pos];
        if (c == '\n') { ++m_line; ++m_pos; break; }
        if (c == '\r') { ++m_pos; continue; }
        if (inString) {
            text += c;
            if (c == '\\' && m_pos + 1 < n) { text += s[m_pos + 1]; m_pos += 2; continue; }
            if (c == quote) inString = false;
            ++m_pos;
            continue;
        }
        if (c == '\\' && m_pos + 1 < n && (s[m_pos + 1] == '\n' ||
            (s[m_pos + 1] == '\r' && m_pos + 2 < n && s[m_pos + 2] == '\n'))) {
            m_pos += (s[m_pos + 1] == '\r') ? 3 : 2;
            ++m_line;
            text += ' ';
            continue;
        }
        if (c == '/' && m_pos + 1 < n && s[m_pos + 1] == '/') {
            while (m_pos < n && s[m_pos] != '\n') ++m_pos;
            continue;
        }
        if (c == '/' && m_pos + 1 < n && s[m_pos + 1] == '*') {
            m_pos += 2;
            while (m_pos < n && !(s[m_pos] == '*' && m_pos + 1 < n && s[m_pos + 1] == '/')) {
                if (s[m_pos] == '\n') ++m_line;
                ++m_pos;
            }
            if (m_pos < n) m_pos += 2;
            text += ' ';
            continue;
        }
        if (c == '"' || c == '\'') { inString = true; quote = c; }
        text += c;
        ++m_pos;
    }
    out.push_back(Token(TokPreproc, trim(text), startLine));
}

void Lexer::readString(std::vector<Token>& out, char quote, const std::string& prefix)
{
    const std::string& s = m_src;
    const size_t n = s.size();
    std::string text = prefix;
    text += quote;
    ++m_pos;
    while (m_pos < n) {
        char c = s[m_pos];
        if (c == '\\' && m_pos + 1 < n) {
            text += c; text += s[m_pos + 1];
            if (s[m_pos + 1] == '\n') ++m_line;
            m_pos += 2;
            continue;
        }
        if (c == '\n') { ++m_line; break; } // unterminated literal: stop at end of line
        text += c;
        ++m_pos;
        if (c == quote) break;
    }
    out.push_back(Token(quote == '"' ? TokString : TokChar, text, m_line));
}

void Lexer::readNumber(std::vector<Token>& out)
{
    const std::string& s = m_src;
    const size_t n = s.size();
    size_t start = m_pos;
    bool hex = (s[m_pos] == '0' && m_pos + 1 < n && (s[m_pos + 1] == 'x' || s[m_pos + 1] == 'X'));
    while (m_pos < n) {
        char c = s[m_pos];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') { ++m_pos; continue; }
        if ((c == '+' || c == '-') && !hex && m_pos > start &&
            (s[m_pos - 1] == 'e' || s[m_pos - 1] == 'E')) { ++m_pos; continue; }
        break;
    }
    out.push_back(Token(TokNumber, s.substr(start, m_pos - start), m_line));
}

void Lexer::readPunct(std::vector<Token>& out)
{
    const std::string& s = m_src;
    const size_t n = s.size();
    for (int i = 0; kPunct3[i]; ++i) {
        if (m_pos + 3 <= n && s.compare(m_pos, 3, kPunct3[i]) == 0) {
            out.push_back(Token(TokPunct, kPunct3[i], m_line));
            m_pos += 3;
            return;
        }
    }
    for (int i = 0; kPunct2[i]; ++i) {
        if (m_pos + 2 <= n && s.compare(m_pos, 2, kPunct2[i]) == 0) {
            out.push_back(Token(TokPunct, kPunct2[i], m_line));
            m_pos += 2;
            return;
        }
    }
    out.push_back(Token(TokPunct, std::string(1, s[m_pos]), m_line));
    ++m_pos;
}

} // namespace cppparse
