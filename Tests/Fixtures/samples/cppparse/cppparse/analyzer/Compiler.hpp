// Compiler.hpp - version 3 driver: AST -> type check -> IR -> optimise -> lower.
// Strict C++98.
#ifndef CPPPARSE_COMPILER_HPP
#define CPPPARSE_COMPILER_HPP

#include "CodeBase.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

struct CompileOptions {
    int  optLevel;       // 0..3
    bool emitAst;
    bool x86;            // version 4: also generate x86-64 MASM
    int  threads;        // worker threads for the per-function phases
    bool msvcNames;      // version 5: MSVC-compatible decorated names in the x86-64 output
    bool tableEh;        // try regions as unwind range tables with landing pads (--eh=table) instead of setjmp
    CompileOptions() : optLevel(0), emitAst(true), x86(true), threads(1), msvcNames(false), tableEh(true) {}
};

struct CompileSummary {
    int functionsSeen, functionsCompiled, functionsSkipped, parseFailures, typeFailures;
    int irBefore, irAfter, asmInstructions, spills;
    int classInstantiations, functionInstantiations;
    long compileMs;
    CompileSummary() : functionsSeen(0), functionsCompiled(0), functionsSkipped(0), parseFailures(0), typeFailures(0),
                       irBefore(0), irAfter(0), asmInstructions(0), spills(0), classInstantiations(0), functionInstantiations(0), compileMs(0) {}
};

class Compiler {
public:
    Compiler(CodeBase& cb, const CompileOptions& opt);
    ~Compiler();
    void run();

    const std::vector<Finding>& findings() const { return m_findings; }
    const CompileSummary&       summary()  const { return m_summary; }
    const std::string& astText()    const { return m_ast; }
    const std::string& irText()     const { return m_ir; }
    const std::string& optLog()     const { return m_log; }
    const std::string& asmText()    const { return m_asm; }
    const std::string& x86Text()    const { return m_x86; }
    const std::string& layoutText() const { return m_layouts; }
    const std::map<std::string, std::string>& symbolNames() const { return m_symbolNames; }   // x86 symbol -> readable name
    void summaryLines(std::vector<std::string>& out) const;

private:
    Compiler(const Compiler&);
    Compiler& operator=(const Compiler&);

    CodeBase&            m_cb;
    CompileOptions       m_opt;
    std::vector<Finding> m_findings;
    CompileSummary       m_summary;
    std::string          m_ast, m_ir, m_log, m_asm, m_x86, m_layouts;
    std::map<std::string, std::string> m_symbolNames;
};

} // namespace cppparse

#endif
