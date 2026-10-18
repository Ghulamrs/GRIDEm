// Pp.hpp - version 6: a C++98 preprocessor.
//
// Translation phases 1-4: line splices, comments, pp-tokens, directives and
// macro expansion. One Preprocessor handles one translation unit and touches
// nothing shared, so translation units can be preprocessed in parallel.
//
//   #include "x" / <x> / MACRO    search: the including file's directory (quoted form), then -I dirs
//   #define / #undef               object-like and function-like macros, # and ## operators,
//                                  argument prescan, rescanning, hide sets (no recursion)
//   #if / #ifdef / #ifndef / #elif / #else / #endif
//                                  full constant expressions with defined, arithmetic, ?:, characters
//   #error / #warning / #pragma once / #pragma pack / #line (ignored)
//   __FILE__ __LINE__ __DATE__ __TIME__ __cplusplus (199711L) __cppparse__
//
// The result is a flat token list with (file, line) positions, the list of
// files that were included (and those that were not found), every macro
// definition, and diagnostics. text() renders the stream like cl /EP.
//
// Strict C++98.
#ifndef CPPPARSE_PP_HPP
#define CPPPARSE_PP_HPP

#include "Target.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cppparse {

struct PpToken {
    enum Kind { Ident, Number, String, Char, Punct, Other, Newline, End };
    Kind                     kind;
    std::string              text;
    int                      file;     // index into Preprocessor::files()
    int                      line;
    bool                     space;    // preceded by white space
    bool                     bol;      // first token on its line
    std::vector<std::string> hide;     // macro names that must not expand this token again
    int                      pack;     // #pragma pack value in force where the token was output (0: the default)
    PpToken() : kind(End), file(0), line(0), space(false), bol(false), pack(0) {}
    bool is(const char* s) const { return text == s; }
};

struct PpMacro {
    std::string              name;
    bool                     functionLike;
    std::vector<std::string> params;
    std::vector<PpToken>     body;
    std::string              file;
    int                      line;
    PpMacro() : functionLike(false), line(0) {}
    std::string bodyText() const;
};

struct PpDiag {
    enum Severity { Error, Warning };
    Severity    severity;
    std::string file;
    int         line;
    std::string message;
    PpDiag() : severity(Error), line(0) {}
};

struct PpOptions {
    std::vector<std::string> includeDirs;
    std::vector<std::string> defines;      // "NAME" or "NAME=value"
    std::vector<std::string> undefines;
    const Target*            target;       // its macros are predefined; 0: the default target
    PpOptions() : target(0) {}
};

class Preprocessor {
public:
    explicit Preprocessor(const PpOptions& opt);

    bool run(const std::string& path);                             // false: unreadable main file or #error
    bool runText(const std::string& name, const std::string& text);

    const std::vector<PpToken>&            tokens() const          { return m_out; }
    const std::vector<std::string>&        files() const           { return m_files; }       // [0] is the main file
    const std::vector<std::string>&        includes() const        { return m_includes; }    // resolved, first inclusion order
    const std::vector<std::string>&        missingIncludes() const { return m_missing; }
    const std::vector<PpDiag>&             diagnostics() const     { return m_diags; }
    const std::map<std::string, PpMacro>&  macros() const          { return m_macros; }
    int                                    errorCount() const;
    std::string                            text() const;           // like cl /EP

private:
    struct Cond { bool active; bool taken; bool seenElse; int line; };

    void processFile(int fileIndex, const std::string& source, int depth);
    void directive(std::vector<PpToken>& line, int fileIndex, std::vector<Cond>& conds, int depth);
    void defineDirective(std::vector<PpToken>& line, size_t at, int fileIndex);
    void includeDirective(std::vector<PpToken>& line, size_t at, int fileIndex, int depth);
    bool evalCondition(std::vector<PpToken>& line, size_t at, int fileIndex);
    void expandChunk(std::vector<PpToken>& chunk);
    void expandTokens(std::vector<PpToken>& in, std::vector<PpToken>& out);
    bool substitute(const PpMacro& mac, const std::vector<std::vector<PpToken> >& args, const PpToken& ident, std::vector<PpToken>& out);
    void stringize(const std::vector<PpToken>& toks, PpToken& out) const;
    bool paste(const PpToken& a, const PpToken& b, PpToken& out) const;
    void diag(PpDiag::Severity s, int fileIndex, int line, const std::string& message);
    bool conditionsActive(const std::vector<Cond>& conds) const;
    int  addFile(const std::string& path);
    void predefine(const std::string& def);

    PpOptions                       m_opt;
    std::vector<PpToken>            m_out;
    std::vector<std::string>        m_files;
    std::vector<std::string>        m_includes;
    std::vector<std::string>        m_missing;
    std::vector<PpDiag>             m_diags;
    std::map<std::string, PpMacro>  m_macros;
    std::set<std::string>           m_pragmaOnce;
    int                             m_pack;          // #pragma pack: the current value (0: the default)
    std::vector<std::pair<std::string, int> > m_packStack;   // push records: identifier (may be empty), value
    void packPragma(const std::vector<PpToken>& line, int fileIndex);
    std::map<std::string, std::string> m_fileCache;
    int                             m_errors;
};

// tokenizes one file into pp-tokens (Newline tokens separate the lines); used by the preprocessor and tests
void ppLex(const std::string& source, int fileIndex, std::vector<PpToken>& out);

} // namespace cppparse

#endif
