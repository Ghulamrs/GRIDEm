// Optimizer.hpp - version 3: IR optimisation passes, selected by -O level.
//
//   -O0  nothing
//   -O1  constant propagation and folding, store-to-load forwarding and copy
//        propagation, algebraic simplification and strength reduction, local
//        common-subexpression elimination, dead code and dead store
//        elimination, branch folding, unreachable block removal, block merging
//   -O2  -O1 plus loop-invariant code motion and inlining of small functions
//   -O3  -O2 plus inlining of larger functions and full unrolling of small
//        constant-trip loops
//
// Strict C++98.
#ifndef CPPPARSE_OPTIMIZER_HPP
#define CPPPARSE_OPTIMIZER_HPP

#include "Ir.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

struct LoopInfo {
    int              header;
    int              latch;      // block with the back edge to the header
    std::vector<int> blocks;     // including the header
    int              preheader;  // -1 until created
    LoopInfo() : header(-1), latch(-1), preheader(-1) {}
};

class Optimizer {
public:
    Optimizer(int level, const std::map<std::string, IrFunction*>& functions);
    // ssaForm, when given, receives the IR in SSA form after the SSA passes (before phi removal)
    void run(IrFunction& f, std::vector<std::string>& log, std::string* ssaForm = 0);

    // the same work split into phases so that the local phases run in parallel across functions:
    //   runLocal          -O1 passes and loop-invariant code motion (reads only this function)
    //   runInterprocedural inlining and unrolling (reads other functions' IR: run sequentially)
    //   runSsa            SSA construction, value numbering, SSA destruction, final clean-up
    void runLocal(IrFunction& f, std::vector<std::string>& log);
    void runInterprocedural(IrFunction& f, std::vector<std::string>& log);
    void runSsa(IrFunction& f, std::vector<std::string>& log, std::string* ssaForm);

    // analyses (also used by lowering)
    static void dominators(const IrFunction& f, std::vector<int>& idom);
    static void naturalLoops(const IrFunction& f, std::vector<LoopInfo>& loops);
    static bool dominates(const std::vector<int>& idom, int a, int b);

private:
    int  constantFold(IrFunction& f);
    int  copyPropagation(IrFunction& f);
    int  algebraic(IrFunction& f);
    int  localCse(IrFunction& f);
    int  deadCode(IrFunction& f);
    int  deadStores(IrFunction& f);
    int  simplifyCfg(IrFunction& f);
    int  licm(IrFunction& f);
    int  inlineCalls(IrFunction& f, int maxInsts, int maxInstsInLoop);   // the second limit for calls inside a loop
    int  unrollLoops(IrFunction& f, int maxTrip, int maxBody);
    int  level1(IrFunction& f, std::vector<std::string>& log);

    int                                        m_level;
    const std::map<std::string, IrFunction*>&  m_functions;
    int                                        m_inlined;
};

} // namespace cppparse

#endif
