// Ssa.hpp - version 4: static single assignment form for the IR.
//
// toSsa() promotes every scalar local that never has its address taken to
// SSA temporaries: stores become definitions, loads become uses, and phi
// instructions (op Phi, args: [value, block] pairs) merge values at join
// points, placed with iterated dominance frontiers. The existing passes
// keep working on SSA (loads and stores of promoted locals are gone);
// gvn() adds global value numbering over the dominator tree and
// simplifyPhis() folds trivial phis. fromSsa() removes the phis again by
// inserting parallel copies on the incoming edges (critical edges are
// split first) so the lowering passes see ordinary code.
//
// Strict C++98.
#ifndef CPPPARSE_SSA_HPP
#define CPPPARSE_SSA_HPP

#include "Ir.hpp"

#include <string>
#include <vector>

namespace cppparse {

struct SsaStats {
    int promotedLocals;
    int phisInserted;
    int phisRemoved;
    int copiesInserted;
    int edgesSplit;
    SsaStats() : promotedLocals(0), phisInserted(0), phisRemoved(0), copiesInserted(0), edgesSplit(0) {}
};

void toSsa(IrFunction& f, SsaStats& stats);
int  simplifyPhis(IrFunction& f);          // phi(x, x, ...) -> copy x; phi(x, self) -> copy x
int  gvn(IrFunction& f);                   // global value numbering (dominator-tree scoped)
void fromSsa(IrFunction& f, SsaStats& stats);
bool isSsa(const IrFunction& f);           // true when the function holds phi instructions
// an operand as a value-numbering key: a floating constant by its exact bits (str() prints six digits)
std::string operandKey(const IrOperand& o);

} // namespace cppparse

#endif
