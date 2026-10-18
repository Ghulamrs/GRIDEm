// BodyParser.hpp - version 3: parses the tokens of a function body into an AST.
//
// Covers the C++98 statement and expression grammar used in ordinary code:
// declarations (with constructor-style and '=' initializers), if/else,
// while, do, for, switch/case, break/continue/return, try/catch/throw,
// blocks; all operators with correct precedence, calls, member access,
// indexing, casts (C style, functional, static_cast & co.), sizeof,
// new/delete, this, literals. Unsupported constructs (goto, local classes,
// brace initializers) are recorded as Unsupported statements and reported.
//
// Strict C++98.
#ifndef CPPPARSE_BODYPARSER_HPP
#define CPPPARSE_BODYPARSER_HPP

#include "Ast.hpp"

#include <set>
#include <string>
#include <vector>

namespace cppparse {

class BodyParser {
public:
    BodyParser(const CodeBase& cb, TypeTable& types, AstArena& arena, const FunctionInfo& fn,
               const std::vector<Token>& tokens);
    bool parse(FunctionAst& out);
    // parses "a(x), b(y, z)" from the token range [begin, end) into out.inits
    void parseInitializers(FunctionAst& out, size_t begin, size_t end);

private:
    // tokens
    const Token& cur() const;
    const Token& peek(size_t n) const;
    void         advance();
    bool         is(const char* text) const;
    bool         isIdent() const { return cur().type == TokIdentifier; }
    bool         accept(const char* text);
    bool         expect(const char* text, const char* context);
    bool         atEnd() const { return m_pos >= m_end; }
    size_t       skipBalancedAt(size_t pos) const;   // index after the matching closer

    // types
    bool scanType(size_t pos, size_t& endPos, std::string& text, bool& known) const;
    bool looksLikeDeclaration() const;
    bool isTypeWord(const std::string& w) const;

    // statements
    Stmt* parseBlock();
    Stmt* parseStatement();
    Stmt* parseDeclaration();
    Stmt* parseFor();
    Stmt* parseTry();
    Stmt* parseSimple(Stmt::Kind k);

    // expressions
    Expr* parseExpr();
    Expr* parseAssign();
    Expr* parseCond();
    Expr* parseBinary(int minPrec);
    Expr* parseUnary();
    Expr* parsePostfix(Expr* e);
    Expr* parsePrimary();
    Expr* parseName();
    Expr* parseNew(int line);
    void  parseArgs(Expr* call);
    int   binaryPrec(const std::string& op) const;
    bool  startsExpression(size_t pos) const;

    void  error(const std::string& msg);
    void  recover();

    const CodeBase&           m_cb;
    TypeTable&                m_types;
    AstArena&                 m_arena;
    const FunctionInfo&       m_fn;
    const std::vector<Token>& m_tokens;
    size_t                    m_pos;
    size_t                    m_end;
    FunctionAst*              m_out;
    std::string               m_scope;
    const ClassInfo*          m_owner;
    std::set<std::string>     m_localTypes;
    Token                     m_endToken;
};

} // namespace cppparse

#endif
