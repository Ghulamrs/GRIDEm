// Parser.hpp - tolerant declaration parser for C++98 headers and sources.
//
// The parser walks the token stream once and records:
//   * every preprocessor directive, with the #if conditions active around
//     each declaration (include guards are recognised and not counted);
//   * namespaces, classes/structs/unions (templates, specializations,
//     multiple/virtual inheritance, nested classes, access sections);
//   * methods and free functions, declarations and definitions, including
//     constructor initializer lists, exception specifications and
//     function-try-blocks;
//   * fields, globals, static member definitions, typedefs, enums, usings;
//   * inside function bodies: local variable declarations and try/catch/throw.
//
// It is a declaration-level parser, not a compiler: expressions and
// statements inside bodies are skipped by brace matching. Unrecognised
// constructs are reported in SourceFile::diagnostics and skipped.
//
// Strict C++98.
#ifndef CPPPARSE_PARSER_HPP
#define CPPPARSE_PARSER_HPP

#include "Lexer.hpp"
#include "Model.hpp"

#include <string>
#include <vector>

namespace cppparse {

class Parser {
public:
    Parser(SourceFile& out, const std::vector<Token>& tokens);
    void parse();

private:
    struct CondLevel {
        std::string current;   // condition in force for the current branch ("" = always)
        std::string negated;   // conjunction of the negated earlier branches
        bool        guard;     // the file's include guard
        CondLevel() : guard(false) {}
    };

    // ---- token access (directives are processed as the cursor passes them)
    const Token& cur();
    const Token& peek(size_t n);
    void         advance();
    bool         atEnd();
    bool         is(const char* text);
    bool         isPunct(const char* text);
    bool         isIdent() { return cur().type == TokIdentifier; }
    size_t       rawNext(size_t idx) const;        // next non-directive index after idx
    size_t       matchClose(size_t openIdx) const; // index of the matching ')' '[' '{' '<'

    // ---- preprocessor
    void        processDirective(size_t idx);
    Directive   describeDirective(const Token& t, std::string& word, std::string& rest) const;
    void        skipDisabledRegion();
    std::string currentCondition() const;
    std::string scopeName() const;
    void        diag(int line, const std::string& msg);

    // ---- grammar
    void         parseScopeBody(bool untilBrace);
    void         parseNamespace();
    void         parseUsing(ClassInfo* owner);
    TemplateInfo parseTemplateHeader();
    ClassInfo*   parseClass(ClassInfo* owner, Access access, const TemplateInfo& tmpl, bool fromTypedef);
    void         parseBases(ClassInfo& cls);
    void         parseClassBody(ClassInfo& cls);
    void         parseEnum(ClassInfo* owner, Access access, bool fromTypedef);
    void         parseTypedef(ClassInfo* owner, Access access);
    void         parseFriend(ClassInfo& cls, Access access);
    void         parseDeclaration(ClassInfo* owner, Access access, const TemplateInfo& tmpl);
    void         parseFunctionTail(FunctionInfo& fn, ClassInfo* owner);
    void         parseParameters(FunctionInfo& fn);
    void         splitParameters(const std::vector<Token>& toks, std::vector<Parameter>& out) const;
    void         collectOperatorName(std::vector<Token>& head, bool& conversion);
    void         scanBody(FunctionInfo& fn);
    bool         tryLocalDeclaration(FunctionInfo& fn);
    void         note(FunctionInfo& fn, const char* code, const std::string& text, int line);
    void         recordCall(FunctionInfo& fn, const std::string& prevPrev, const std::string& prev,
                            const std::string& name, int argCount, int line);
    int          countArgs(size_t openIdx) const;                 // raw token index of '('
    void         scanInitializer(FunctionInfo& fn, const std::vector<Token>& toks);
    void         checkCondition(FunctionInfo& fn, const std::string& keyword, int line, const std::string& prevToken);
    void         collectTrailingDeclarators(ClassInfo* owner, Access access, const std::string& baseType,
                                            bool fromTypedef, ClassInfo* cls);
    void         emitVariable(ClassInfo* owner, VariableInfo& v);
    void         skipBalanced(std::vector<Token>* collect);  // cur() at ( [ { <
    void         skipStatement();                            // recovery: to ';' or past a block
    bool         looksLikeFunctionPointer();
    bool         looksLikeMacroInvocation(const std::vector<Token>& head);
    bool         parensContainLiteral();

    // ---- state
    SourceFile&               m_file;
    const std::vector<Token>& m_tokens;
    size_t                    m_pos;
    Token                     m_end;
    std::vector<CondLevel>    m_cond;
    std::vector<std::string>  m_scopes;
    std::string               m_guardMacro;
    bool                      m_externC;
};

// helpers shared with other units
std::string joinTokens(const std::vector<Token>& v, size_t from, size_t to);
std::string joinTokens(const std::vector<Token>& v);

} // namespace cppparse

#endif
