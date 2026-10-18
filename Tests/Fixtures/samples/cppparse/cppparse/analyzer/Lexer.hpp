// Lexer.hpp - turns C++ source text into a flat list of tokens.
//
// Comments are dropped. Preprocessor lines are kept as single TokPreproc
// tokens (with line continuations joined) so the parser can record every
// directive and track #if conditions while it walks the declarations.
//
// Strict C++98.
#ifndef CPPPARSE_LEXER_HPP
#define CPPPARSE_LEXER_HPP

#include <string>
#include <vector>

namespace cppparse {

enum TokenType { TokEnd, TokIdentifier, TokNumber, TokString, TokChar, TokPunct, TokPreproc };

struct Token {
    TokenType   type;
    std::string text;
    int         line;
    Token() : type(TokEnd), line(0) {}
    Token(TokenType t, const std::string& s, int l) : type(t), text(s), line(l) {}
};

class Lexer {
public:
    explicit Lexer(const std::string& source);
    void run(std::vector<Token>& out);
    int  lineCount() const { return m_lines; }

private:
    void readDirective(std::vector<Token>& out);
    void readString(std::vector<Token>& out, char quote, const std::string& prefix);
    void readNumber(std::vector<Token>& out);
    void readPunct(std::vector<Token>& out);

    const std::string& m_src;
    size_t             m_pos;
    int                m_line;
    int                m_lines;
};

} // namespace cppparse

#endif
