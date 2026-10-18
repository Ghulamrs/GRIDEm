// BackEnd.hpp - the phases from IR to assembly, shared by the version 3 and version 6 front ends:
// local optimisation, inlining/unrolling, SSA, VM64 lowering and x86-64 code generation, each
// per function on the worker pool except inlining.
//
// Strict C++98.
#ifndef CPPPARSE_BACKEND_HPP
#define CPPPARSE_BACKEND_HPP

#include "Ir.hpp"
#include "X86.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

struct BackEndOptions {
    int  optLevel;
    int  threads;
    bool x86;
    BackEndOptions() : optLevel(0), threads(1), x86(true) {}
};

struct BackEndResult {
    std::string irText;         // before/SSA/after dumps of every function
    std::string log;            // optimisation log lines
    std::string asmText;        // VM64 listing
    std::string x86Text;        // MASM, when x86 was requested
    std::map<std::string, std::string> symbolNames;   // x86 symbol -> readable name, for the map file
    int  irBefore, irAfter, asmInstructions, spills;
    long tLocal, tInter, tSsa, tLower;
    BackEndResult() : irBefore(0), irAfter(0), asmInstructions(0), spills(0), tLocal(0), tInter(0), tSsa(0), tLower(0) {}
};

// x86 may be 0 when no x86-64 output is wanted; its vtables, globals and names must be registered before
void runBackEnd(std::vector<IrFunction*>& functions, X86Module* x86, const BackEndOptions& opt, BackEndResult& out);

} // namespace cppparse

#endif
