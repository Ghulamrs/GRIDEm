// Ir.hpp - version 3: three-address intermediate representation.
//
// One IrFunction per compiled function: a list of basic blocks holding
// instructions. Temporaries (t1, t2, ...) are assigned exactly once by the
// builder; locals and parameters are memory slots accessed with load/store
// (loadlocal / storelocal), so the optimizer can forward stores to loads and
// remove dead stores without an SSA construction. Class layouts give field
// offsets and vtable slots.
//
// Instructions:
//   const / fconst / str      t = immediate
//   copy                      t = t2
//   loadlocal / storelocal    t = local[i]   /  local[i] = t
//   addrlocal                 t = &local[i]
//   loadglobal / storeglobal / addrglobal      (by name)
//   loadmem / storemem        t = *addr  /  *addr = t
//   addrfield                 t = base + offset        (sub = field name)
//   addrindex                 t = base + index * size
//   bin  (sub: add sub mul sdiv udiv srem urem shl ashr lshr and or xor fadd fsub fmul fdiv)
//   un   (sub: neg fneg not)
//   cmp  (sub: eq ne slt sle sgt sge ult ule ugt uge feq fne flt fle fgt fge)
//   conv (sub: sext zext trunc sitofp uitofp fptosi fpext fptrunc ptrtoint inttoptr bitcast)
//   call (sub: callee name)   vcall (sub: method, args[0] = this, last arg = slot)
//   br / condbr / ret / throw / unreachable
//   trybegin (args: handler blocks, extra: region id) / tryend (extra: region id, also emitted
//   before return/break/continue that leave the region) / catchparam (sub: catch type, t = object
//   address) / catchend (end of a handler body)
//
// Strict C++98.
#ifndef CPPPARSE_IR_HPP
#define CPPPARSE_IR_HPP

#include "Ast.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cppparse {


struct IrOperand {
    enum Kind { None, Temp, Imm, FImm, Local, Global, Block, Str };
    Kind        kind;
    int         id;      // temp id, local index, block id
    long        ival;
    double      fval;
    std::string name;    // global name, string literal
    const Type* type;

    IrOperand() : kind(None), id(-1), ival(0), fval(0), type(0) {}
    static IrOperand temp(int id, const Type* t) { IrOperand o; o.kind = Temp; o.id = id; o.type = t; return o; }
    static IrOperand imm(long v, const Type* t) { IrOperand o; o.kind = Imm; o.ival = v; o.type = t; return o; }
    static IrOperand fimm(double v, const Type* t) { IrOperand o; o.kind = FImm; o.fval = v; o.type = t; return o; }
    static IrOperand local(int idx, const Type* t) { IrOperand o; o.kind = Local; o.id = idx; o.type = t; return o; }
    static IrOperand global(const std::string& n, const Type* t) { IrOperand o; o.kind = Global; o.name = n; o.type = t; return o; }
    static IrOperand block(int id) { IrOperand o; o.kind = Block; o.id = id; return o; }
    static IrOperand str(const std::string& s, const Type* t) { IrOperand o; o.kind = Str; o.name = s; o.type = t; return o; }
    bool isConst() const { return kind == Imm || kind == FImm; }
    std::string str() const;
};

struct IrInst {
    enum Op { Const, FConst, StrConst, Copy, LoadLocal, StoreLocal, AddrLocal, LoadGlobal, StoreGlobal, AddrGlobal,
              LoadMem, StoreMem, AddrField, AddrIndex, Bin, Un, Cmp, Conv, Call, VCall, Br, CondBr, Ret, Throw,
              TryBegin, TryEnd, CatchParam, CatchEnd, Unreachable, Nop, Phi };
    Op                     op;
    std::string            sub;    // sub-operation, callee name, field name
    int                    dst;    // result temp or -1
    const Type*            type;   // result type (or stored value type)
    std::vector<IrOperand> args;
    int                    line;
    long                   extra;  // offset / element size / vtable slot

    IrInst() : op(Nop), dst(-1), type(0), line(0), extra(0) {}
    bool pure() const;             // result-only, no side effects, no memory read
    bool readsMemory() const;
    bool isTerminator() const { return op == Br || op == CondBr || op == Ret || op == Throw || op == Unreachable; }
    std::string str() const;
};

struct IrBlock {
    int                 id;
    std::string         label;
    std::vector<IrInst> insts;
    std::vector<int>    succs;
    std::vector<int>    preds;
    IrBlock() : id(0) {}
};

struct IrLocal {
    std::string name;
    const Type* type;
    int         size;
    int         align;
    bool        addressTaken;
    bool        isParam;
    bool        isThis;
    bool        nonNull;     // a parameter that always holds a valid address: this, a reference, an object passed by
                             // address, the hidden result pointer (loads through it cannot fault)
    IrLocal() : type(0), size(8), align(8), addressTaken(false), isParam(false), isThis(false), nonNull(false) {}
};

struct IrFunction {
    std::string              name;       // "geom::Circle::area(...) const"
    const FunctionInfo*      fn;
    std::vector<IrBlock>     blocks;
    std::vector<IrLocal>     locals;     // parameters first ("this" at 0 for methods)
    int                      nextTemp;
    const Type*              retType;
    int                      paramCount;
    bool                     hasThis;
    bool                     hasTry;
    bool                     linkOnce;   // inline, template instantiation or implicit member: one copy survives linking (COMDAT)
    bool                     internal;   // internal linkage (a static function, a member of an anonymous namespace): not PUBLIC
    bool                     variadic;   // ends in "...": the parameters live in their home slots, where va_start finds them
    std::vector<std::string> notes;      // constructs that were approximated
    std::map<int, const Type*> tempTypes;

    IrFunction() : fn(0), nextTemp(1), retType(0), paramCount(0), hasThis(false), hasTry(false), linkOnce(false), internal(false), variadic(false) {}
    int  newTemp(const Type* t) { tempTypes[nextTemp] = t; return nextTemp++; }
    int  newBlock(const std::string& label);
    void computeCfg();
    // temporaries live on entry to and on exit from every block, with the successors taken from the branches;
    // extraSuccs (optional, one list per block) adds edges no instruction names: the back end's exception edges
    // from a block whose calls may throw to the handlers of the table regions around them
    void computeLiveness(std::vector<std::set<int> >& liveIn, std::vector<std::set<int> >& liveOut,
                         const std::vector<std::vector<int> >* extraSuccs = 0) const;
    int  instCount() const;
    const Type* typeOf(int temp) const;
};

std::string printIr(const IrFunction& f);

} // namespace cppparse

#endif
