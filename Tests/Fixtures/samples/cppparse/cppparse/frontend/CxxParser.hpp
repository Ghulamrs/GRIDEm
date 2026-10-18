// CxxParser.hpp - version 6: recursive-descent parser for C++98 over the
// preprocessor's token stream.
//
// Produces the cxx:: syntax tree of one translation unit. The parser keeps
// a scope stack of the type names it has seen (typedefs, classes, enums,
// template type parameters, class names when inside a class) so that the
// declaration / expression ambiguities of C++ are resolved the way the
// language requires: an unqualified identifier is a type only if it was
// declared as one. Unknown *qualified* names (std::vector<int>) are
// accepted as types where the syntax demands a type, which keeps
// translation units parseable without the standard headers. Ambiguous
// statements and casts use tentative parsing with backtracking. Syntax
// errors are reported with file and line; the parser then skips to the
// next ';' or '}' at the same nesting and goes on.
//
// Strict C++98.
#ifndef CPPPARSE_CXXPARSER_HPP
#define CPPPARSE_CXXPARSER_HPP

#include "CxxAst.hpp"
#include "Pp.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cppparse {
namespace cxx {

struct ParseDiag {
    int         file, line;
    std::string message;
    ParseDiag() : file(0), line(0) {}
};

class Parser {
public:
    Parser(const std::vector<PpToken>& tokens, CxxArena& arena);
    // names to treat as types from the start (typedefs of headers that are not available, e.g. size_t)
    void assumeType(const std::string& name);
    // parses the whole token stream into tu; returns the number of syntax errors
    int parse(TranslationUnit& tu);
    const std::vector<ParseDiag>& diagnostics() const { return m_diags; }

private:
    struct Scope {
        std::set<std::string> types;        // type names declared here
        std::set<std::string> templates;    // template names declared here
        std::set<std::string> namespaces;
        std::set<std::string> others;       // variables, parameters, functions declared here: they hide outer templates
        std::string           name;         // class or namespace name ("" for blocks)
        bool                  isClass;
        Scope() : isClass(false) {}
    };
    struct NamedScope {                     // what is known about the members of a named class/namespace
        std::set<std::string> types, templates, namespaces;
    };

    // tokens
    const PpToken& cur() const { return m_toks[m_pos]; }
    const PpToken& peek(size_t n = 1) const { return m_toks[m_pos + n < m_toks.size() ? m_pos + n : m_toks.size() - 1]; }
    bool  at(const char* s) const { return cur().text == s && cur().kind != PpToken::String && cur().kind != PpToken::Char; }
    bool  atIdent() const { return cur().kind == PpToken::Ident && !isKeyword(cur().text); }
    bool  atEnd() const { return m_pos + 1 >= m_toks.size(); }
    bool  accept(const char* s) { if (at(s)) { ++m_pos; return true; } return false; }
    bool  expect(const char* s, const char* context);
    void  advance() { if (!atEnd()) ++m_pos; }
    Loc   loc() const { Loc l; l.file = cur().file; l.line = cur().line; return l; }
    void  error(const std::string& msg);
    void  skipTo(const char* a, const char* b);       // error recovery: skip to a or b at depth 0 (consumes a ';', not a '}')
    void  skipBalanced();                             // skip a balanced (...) [...] {...}
    int   declspecArguments();                        // after __declspec: skips "(...)", returns N of an align(N) in it (0: none)
    static bool isKeyword(const std::string& s);
    static bool isTypeKeyword(const std::string& s);
    static bool isDeclSpecKeyword(const std::string& s);

    // scopes and names
    void  pushScope(const std::string& name, bool isClass);
    void  popScope();
    void  declareType(const std::string& name);
    void  declareTemplate(const std::string& name);
    void  declareNamespace(const std::string& name);
    void  declareOther(const std::string& name);      // a name that is neither a type nor a template
    bool  isTypeName(const QualifiedName& n) const;
    bool  isTemplateName(const QualifiedName& n) const;
    bool  isNamespaceName(const std::string& n) const;
    bool  looksLikeTypeStart() const;                 // the current token can start a type-specifier
    bool  qualifiedNameAhead(size_t& endPos, bool& hasTemplate) const;
    const NamedScope* findNamedScope(const QualifiedName& qualifier, size_t parts) const;
    std::string currentClassName() const;

    // declarations
    Decl* declaration(bool inClass, Access& access);
    bool  memberDeclarator(Declarator& d, bool inClass);
    Decl* simpleOrFunction(bool inClass, const DeclSpec& spec, Loc at, bool firstDeclaratorParsed, Declarator* first);
    bool  declSpecifiers(DeclSpec& spec, bool inClass, bool allowClassDef);
    bool  declarator(Declarator& d, bool abstractAllowed, bool nameRequired, bool inParams);
    bool  declaratorSuffixes(Declarator& d, bool inParams);
    bool  parameterList(DeclaratorPart& fn);
    bool  typeIdAt(TypeId& t);
    bool  tryTypeId(TypeId*& out);                    // tentative
    bool  qualifiedName(QualifiedName& n, bool allowTemplateArgs, bool typeContext);
    bool  templateArguments(std::vector<TemplateArg>& args);
    Decl* classSpecifier(DeclSpec& spec, Loc at);
    Decl* enumSpecifier(DeclSpec& spec, Loc at);
    Decl* namespaceDecl(Loc at);
    Decl* usingDecl(Loc at);
    Decl* templateDecl(bool inClass, Access& access, Loc at);
    Decl* linkageSpec(Loc at);
    bool  memInitializers(Decl* fn);
    bool  initializer(Initializer& init);
    bool  braceElements(std::vector<Expr*>& out);      // after "{": the elements and the closing "}"
    void  registerDeclaredNames(const DeclSpec& spec, const Declarator& d);

    // statements
    Stmt* statement();
    Stmt* compound();
    Stmt* declarationStatement();                      // tentative: 0 when it is not a declaration
    bool  condition(Stmt* s);                          // if/while/for/switch condition: expression or declaration
    Stmt* tryBlock(Loc at);

    // expressions
    Expr* expression();                                // comma expression
    Expr* assignment();
    Expr* conditional();
    Expr* binary(int minPrec);
    Expr* unary();
    Expr* postfix();
    Expr* primary();
    bool  callArguments(std::vector<Expr*>& args);
    Expr* newExpr(Loc at);
    bool  tryCastExpr(Expr*& out);                     // ( type-id ) cast-expression, tentative
    int   precedence(const std::string& op, bool& rightAssoc) const;
    bool  lessIsTemplate(const QualifiedName& n) const;

    std::vector<PpToken>        m_toks;              // a copy with an End sentinel; ">>" may be split in place
    CxxArena&                   m_arena;
    size_t                      m_pos;
    std::vector<Scope>          m_scopes;
    std::map<std::string, NamedScope> m_named;        // qualified class/namespace name -> its known type names
    std::vector<ParseDiag>      m_diags;
    int                         m_tentative;          // > 0: errors are not reported (tentative parse)
    int                         m_errors;
    int                         m_templateDepth;      // inside template <...> declaration (typename allowed)
    int                         m_noGreater;          // > 0 while parsing template arguments: '>' ends the expression
    std::vector<std::string>    m_qualifierStack;     // current qualified scope names for m_named
};

} // namespace cxx
} // namespace cppparse

#endif
