// Ir.cpp - IR printing, class layouts and the AST -> IR builder.
#include "Ir.hpp"
#include "Parallel.hpp"
#include "PathUtil.hpp"
#include "TypeCheck.hpp"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace cppparse {

// ---- operands and instructions ---------------------------------------------------------

std::string IrOperand::str() const
{
    switch (kind) {
    case Temp: return "t" + intToString(id);
    case Imm: return "#" + intToString(static_cast<int>(ival));
    case FImm: {
        // the shortest form when it reads back as the same value, else all 17 digits: the text is also the
        // operand's identity in the optimiser's value-numbering keys (0.1 and 0.1000001 are not one constant)
        if (fval == 0.0) { unsigned char b[sizeof(double)]; std::memcpy(b, &fval, sizeof(double)); return (b[sizeof(double) - 1] & 0x80) ? "#-0" : "#0"; }
        std::ostringstream s; s << fval;
        if (std::strtod(s.str().c_str(), 0) != fval) { std::ostringstream p; p.precision(17); p << fval; return "#" + p.str(); }
        return "#" + s.str();
    }
    case Local: return "local" + intToString(id);
    case Global: return "@" + name;
    case Block: return "B" + intToString(id);
    case Str: return "\"" + name + "\"";
    default: return "?";
    }
}

bool IrInst::pure() const
{
    switch (op) {
    case Const: case FConst: case StrConst: case Copy: case AddrLocal: case AddrGlobal: case AddrField: case AddrIndex:
    case Bin: case Un: case Cmp: case Conv: case Phi:
        return true;
    default:
        return false;
    }
}

bool IrInst::readsMemory() const
{
    return op == LoadLocal || op == LoadGlobal || op == LoadMem;
}

namespace {

const char* opName(IrInst::Op op)
{
    switch (op) {
    case IrInst::Const: return "const";
    case IrInst::FConst: return "fconst";
    case IrInst::StrConst: return "str";
    case IrInst::Copy: return "copy";
    case IrInst::LoadLocal: return "loadlocal";
    case IrInst::StoreLocal: return "storelocal";
    case IrInst::AddrLocal: return "addrlocal";
    case IrInst::LoadGlobal: return "loadglobal";
    case IrInst::StoreGlobal: return "storeglobal";
    case IrInst::AddrGlobal: return "addrglobal";
    case IrInst::LoadMem: return "loadmem";
    case IrInst::StoreMem: return "storemem";
    case IrInst::AddrField: return "addrfield";
    case IrInst::AddrIndex: return "addrindex";
    case IrInst::Bin: return "bin";
    case IrInst::Un: return "un";
    case IrInst::Cmp: return "cmp";
    case IrInst::Conv: return "conv";
    case IrInst::Call: return "call";
    case IrInst::VCall: return "vcall";
    case IrInst::Br: return "br";
    case IrInst::CondBr: return "condbr";
    case IrInst::Ret: return "ret";
    case IrInst::Throw: return "throw";
    case IrInst::TryBegin: return "trybegin";
    case IrInst::TryEnd: return "tryend";
    case IrInst::CatchParam: return "catchparam";
    case IrInst::CatchEnd: return "catchend";
    case IrInst::Unreachable: return "unreachable";
    case IrInst::Phi: return "phi";
    default: return "nop";
    }
}

} // namespace

std::string IrInst::str() const
{
    std::string s;
    if (dst >= 0) s += "t" + intToString(dst) + " = ";
    s += opName(op);
    if (!sub.empty()) s += " " + sub;
    if (op == AddrField || op == AddrIndex || op == VCall) s += " +" + intToString(static_cast<int>(extra));
    if (op == Phi) {
        for (size_t i = 0; i + 1 < args.size(); i += 2) s += (i ? ", [" : " [") + args[i].str() + ", " + args[i + 1].str() + "]";
    } else {
        for (size_t i = 0; i < args.size(); ++i) s += (i ? ", " : " ") + args[i].str();
    }
    if (type && dst >= 0) s += "   : " + type->str();
    return s;
}

int IrFunction::newBlock(const std::string& label)
{
    IrBlock b;
    b.id = static_cast<int>(blocks.size());
    b.label = label;
    blocks.push_back(b);
    return b.id;
}

void IrFunction::computeCfg()
{
    for (size_t i = 0; i < blocks.size(); ++i) { blocks[i].succs.clear(); blocks[i].preds.clear(); }
    for (size_t i = 0; i < blocks.size(); ++i) {
        IrBlock& b = blocks[i];
        for (size_t k = 0; k < b.insts.size(); ++k) {
            const IrInst& in = b.insts[k];
            if (in.op == IrInst::Br || in.op == IrInst::CondBr || in.op == IrInst::TryBegin)
                for (size_t a = 0; a < in.args.size(); ++a)
                    if (in.args[a].kind == IrOperand::Block) addUniqueInt(b.succs, in.args[a].id);
        }
    }
    for (size_t i = 0; i < blocks.size(); ++i)
        for (size_t s = 0; s < blocks[i].succs.size(); ++s)
            if (blocks[i].succs[s] >= 0 && blocks[i].succs[s] < static_cast<int>(blocks.size()))
                addUniqueInt(blocks[static_cast<size_t>(blocks[i].succs[s])].preds, static_cast<int>(i));
}

void IrFunction::computeLiveness(std::vector<std::set<int> >& liveIn, std::vector<std::set<int> >& liveOut,
                                 const std::vector<std::vector<int> >* extraSuccs) const
{
    const size_t n = blocks.size();
    std::vector<std::vector<int> > succ(n);
    if (extraSuccs)
        for (size_t b = 0; b < n && b < extraSuccs->size(); ++b)
            for (size_t s = 0; s < (*extraSuccs)[b].size(); ++s)
                if ((*extraSuccs)[b][s] >= 0 && static_cast<size_t>((*extraSuccs)[b][s]) < n) addUniqueInt(succ[b], (*extraSuccs)[b][s]);
    std::vector<std::set<int> > used(n), defined(n);
    for (size_t b = 0; b < n; ++b)
        for (size_t k = 0; k < blocks[b].insts.size(); ++k) {
            const IrInst& in = blocks[b].insts[k];
            for (size_t a = 0; a < in.args.size(); ++a) {
                if (in.args[a].kind == IrOperand::Temp && !defined[b].count(in.args[a].id)) used[b].insert(in.args[a].id);
                if (in.args[a].kind == IrOperand::Block && (in.op == IrInst::Br || in.op == IrInst::CondBr || in.op == IrInst::TryBegin)
                    && in.args[a].id >= 0 && static_cast<size_t>(in.args[a].id) < n) addUniqueInt(succ[b], in.args[a].id);
            }
            if (in.dst >= 0) defined[b].insert(in.dst);
        }
    liveIn.assign(n, std::set<int>());
    liveOut.assign(n, std::set<int>());
    // the sets only grow, so a pass that leaves every size unchanged has reached the fixed point
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t b = n; b-- > 0;) {
            std::set<int>& out = liveOut[b];
            for (size_t s = 0; s < succ[b].size(); ++s) out.insert(liveIn[static_cast<size_t>(succ[b][s])].begin(), liveIn[static_cast<size_t>(succ[b][s])].end());
            std::set<int>& in = liveIn[b];
            const size_t before = in.size();
            in.insert(used[b].begin(), used[b].end());
            for (std::set<int>::const_iterator t = out.begin(); t != out.end(); ++t) if (!defined[b].count(*t)) in.insert(*t);
            if (in.size() != before) changed = true;
        }
    }
}

int IrFunction::instCount() const
{
    int n = 0;
    for (size_t i = 0; i < blocks.size(); ++i) n += static_cast<int>(blocks[i].insts.size());
    return n;
}

const Type* IrFunction::typeOf(int temp) const
{
    std::map<int, const Type*>::const_iterator it = tempTypes.find(temp);
    return it == tempTypes.end() ? 0 : it->second;
}

std::string printIr(const IrFunction& f)
{
    std::ostringstream s;
    s << "function " << f.name << (f.retType ? "  -> " + f.retType->str() : "") << "\n";
    for (size_t i = 0; i < f.locals.size(); ++i) {
        const IrLocal& l = f.locals[i];
        s << "  local" << i << ": " << (l.type ? l.type->str() : "?") << " " << l.name << "  size " << l.size
          << (l.isParam ? "  (parameter)" : "") << (l.addressTaken ? "  address-taken" : "") << "\n";
    }
    for (size_t i = 0; i < f.notes.size(); ++i) s << "  note: " << f.notes[i] << "\n";
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        const IrBlock& blk = f.blocks[b];
        s << "B" << blk.id << ":" << (blk.label.empty() ? "" : "   ; " + blk.label);
        if (!blk.preds.empty()) { s << "   preds:"; for (size_t p = 0; p < blk.preds.size(); ++p) s << " B" << blk.preds[p]; }
        s << "\n";
        for (size_t k = 0; k < blk.insts.size(); ++k) {
            const IrInst& in = blk.insts[k];
            s << "    " << padRight(in.str(), 60) << (in.line ? " ; L" + intToString(in.line) : "") << "\n";
        }
    }
    return s.str();
}

} // namespace cppparse
