// Optimizer.cpp - IR optimisation passes.
#include "Optimizer.hpp"
#include "PathUtil.hpp"
#include "Ssa.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace cppparse {

namespace {

struct Def { int block; int index; Def() : block(-1), index(-1) {} };

void collectDefs(const IrFunction& f, std::map<int, Def>& defs)
{
    defs.clear();
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            if (f.blocks[b].insts[i].dst >= 0) { Def d; d.block = static_cast<int>(b); d.index = static_cast<int>(i); defs[f.blocks[b].insts[i].dst] = d; }
}

void countDefs(const IrFunction& f, std::map<int, int>& defs)
{
    defs.clear();
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            if (f.blocks[b].insts[i].dst >= 0) defs[f.blocks[b].insts[i].dst]++;
}

void countUses(const IrFunction& f, std::map<int, int>& uses)
{
    uses.clear();
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            for (size_t a = 0; a < f.blocks[b].insts[i].args.size(); ++a)
                if (f.blocks[b].insts[i].args[a].kind == IrOperand::Temp) uses[f.blocks[b].insts[i].args[a].id]++;
}

bool isPow2(long v) { return v > 0 && (v & (v - 1)) == 0; }

// a floating value converted to the integer type t as cl's code converts it at run time (X86.cpp): signed and
// narrow targets through a 32-bit cvttsd2si, unsigned int through a 64-bit one, unsigned long long by ranges;
// an out-of-range result is the "integer indefinite" 80000000h / 8000000000000000h
long long cvtt64(double x) { return x >= -9223372036854775808.0 && x < 9223372036854775808.0 ? static_cast<long long>(x) : (-9223372036854775807LL - 1); }
long long cvtt32(double x) { return x > -2147483649.0 && x < 2147483648.0 ? static_cast<long long>(x) : -2147483648LL; }
long long floatToInt(double x, int size, bool isUnsigned)
{
    if (size == 8 && isUnsigned) {
        if (!(x >= 9223372036854775808.0)) return cvtt64(x);
        const double y = x - 9223372036854775808.0;
        return y >= 9223372036854775808.0 ? cvtt64(y) : static_cast<long long>(static_cast<unsigned long long>(cvtt64(y)) + 9223372036854775808ULL);
    }
    if (size == 8) return cvtt64(x);
    if (size == 4 && isUnsigned) return static_cast<long long>(static_cast<unsigned long long>(cvtt64(x)) & 0xFFFFFFFFULL);
    const long long v = cvtt32(x);
    if (size == 2) return isUnsigned ? (v & 0xFFFF) : static_cast<short>(v & 0xFFFF);
    if (size == 1) return isUnsigned ? (v & 0xFF) : static_cast<signed char>(v & 0xFF);
    return v;
}
int  log2of(long v) { int n = 0; while (v > 1) { v >>= 1; ++n; } return n; }

std::string instKey(const IrInst& in)
{
    // addresses are the same whatever the constness of the pointee type
    bool addr = in.op == IrInst::AddrField || in.op == IrInst::AddrIndex || in.op == IrInst::AddrLocal || in.op == IrInst::AddrGlobal;
    std::string k = intToString(static_cast<int>(in.op)) + "|" + in.sub + "|" + intToString(static_cast<int>(in.extra)) + "|" + (in.type && !addr ? in.type->str() : "") + "|";
    for (size_t a = 0; a < in.args.size(); ++a) k += operandKey(in.args[a]) + ",";   // floating constants by their bits
    return k;
}

long foldInt(const std::string& sub, long x, long y, bool& ok)
{
    ok = true;
    if (sub == "add") return x + y;
    if (sub == "sub") return x - y;
    if (sub == "mul") return x * y;
    // unsigned operations see the 32-bit pattern of the constants (wider constants are sign-extended)
    const unsigned long ux = static_cast<unsigned long>(x), uy = static_cast<unsigned long>(y);
    if (sub == "sdiv" || sub == "srem") { if (y == 0) { ok = false; return 0; } return sub == "sdiv" ? x / y : x % y; }
    if (sub == "udiv" || sub == "urem") { if (y == 0 || x < 0 || y < 0) { ok = false; return 0; } return static_cast<long>(sub == "udiv" ? ux / uy : ux % uy); }
    if (sub == "shl") return (y >= 0 && y < 32) ? static_cast<long>(ux << y) : 0;
    if (sub == "ashr") return (y >= 0 && y < 32) ? (x >> y) : 0;
    if (sub == "lshr") { if (x < 0) { ok = false; return 0; } return (y >= 0 && y < 32) ? static_cast<long>(ux >> y) : 0; }
    if (sub == "and") return x & y;
    if (sub == "or") return x | y;
    if (sub == "xor") return x ^ y;
    if (sub == "eq") return x == y;
    if (sub == "ne") return x != y;
    if (sub == "slt") return x < y;
    if (sub == "sle") return x <= y;
    if (sub == "sgt") return x > y;
    if (sub == "sge") return x >= y;
    if (sub == "ult") return ux < uy;
    if (sub == "ule") return ux <= uy;
    if (sub == "ugt") return ux > uy;
    if (sub == "uge") return ux >= uy;
    ok = false;
    return 0;
}

// an operation of a 64-bit type on the sign-extended 32-bit immediates of the IR: folded only while the result
// stays in that range (1 << 40 used to fold to 0)
bool foldWideInt(const std::string& sub, long a, long b, long& out)
{
    const long long x = a, y = b;
    const unsigned long long ux = static_cast<unsigned long long>(x), uy = static_cast<unsigned long long>(y);
    unsigned long long r;
    if (sub == "add") r = ux + uy;
    else if (sub == "sub") r = ux - uy;
    else if (sub == "mul") r = ux * uy;
    else if (sub == "sdiv" || sub == "srem") { if (y == 0 || y == -1) return false; r = static_cast<unsigned long long>(sub == "sdiv" ? x / y : x % y); }
    else if (sub == "udiv" || sub == "urem") { if (y == 0) return false; r = sub == "udiv" ? ux / uy : ux % uy; }
    else if (sub == "shl" || sub == "ashr" || sub == "lshr") {
        if (y < 0 || y > 63) return false;
        r = sub == "shl" ? ux << y : sub == "lshr" ? ux >> y : static_cast<unsigned long long>(x >> y);
    }
    else if (sub == "and") r = ux & uy;
    else if (sub == "or") r = ux | uy;
    else if (sub == "xor") r = ux ^ uy;
    else return false;
    const long long s = static_cast<long long>(r);
    if (static_cast<long long>(static_cast<long>(s)) != s) return false;
    out = static_cast<long>(s);
    return true;
}

bool isWideInt(const Type* t)
{
    return t && t->kind != Type::Double && t->builtinSize() == 8;
}

// is the sign bit of d set (-0.0 included)?
bool signBit(double d)
{
    unsigned char bytes[sizeof(double)];
    std::memcpy(bytes, &d, sizeof(double));
    return (bytes[sizeof(double) - 1] & 0x80) != 0;   // little-endian: the sign is in the last byte
}

double foldFloat(const std::string& sub, double x, double y, bool& ok, bool& isCmp, long& cmp)
{
    ok = true; isCmp = false; cmp = 0;
    if (sub == "fadd") return x + y;
    if (sub == "fsub") return x - y;
    if (sub == "fmul") return x * y;
    if (sub == "fdiv") { if (y == 0.0) { ok = false; return 0; } return x / y; }
    isCmp = true;
    if (sub == "feq") cmp = x == y; else if (sub == "fne") cmp = x != y; else if (sub == "flt") cmp = x < y;
    else if (sub == "fle") cmp = x <= y; else if (sub == "fgt") cmp = x > y; else if (sub == "fge") cmp = x >= y;
    else ok = false;
    return 0;
}

void replaceTempUses(IrFunction& f, int from, const IrOperand& to)
{
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            for (size_t a = 0; a < f.blocks[b].insts[i].args.size(); ++a) {
                IrOperand& o = f.blocks[b].insts[i].args[a];
                if (o.kind == IrOperand::Temp && o.id == from) { const Type* t = o.type; o = to; if (!o.type) o.type = t; }
            }
}

} // namespace

Optimizer::Optimizer(int level, const std::map<std::string, IrFunction*>& functions)
    : m_level(level), m_functions(functions), m_inlined(0)
{
}

// ---- analyses ------------------------------------------------------------------------------

void Optimizer::dominators(const IrFunction& f, std::vector<int>& idom)
{
    size_t n = f.blocks.size();
    idom.assign(n, -1);
    if (n == 0) return;
    // reverse postorder
    std::vector<int> order;
    std::vector<bool> seen(n, false);
    std::vector<std::pair<int, size_t> > stack;
    stack.push_back(std::make_pair(0, static_cast<size_t>(0)));
    seen[0] = true;
    while (!stack.empty()) {
        int b = stack.back().first;
        size_t& i = stack.back().second;
        const std::vector<int>& succs = f.blocks[static_cast<size_t>(b)].succs;
        if (i < succs.size()) {
            int s = succs[i++];
            if (s >= 0 && static_cast<size_t>(s) < n && !seen[static_cast<size_t>(s)]) { seen[static_cast<size_t>(s)] = true; stack.push_back(std::make_pair(s, static_cast<size_t>(0))); }
        } else { order.push_back(b); stack.pop_back(); }
    }
    std::reverse(order.begin(), order.end());
    std::vector<int> rpoIndex(n, -1);
    for (size_t i = 0; i < order.size(); ++i) rpoIndex[static_cast<size_t>(order[i])] = static_cast<int>(i);
    idom[0] = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 1; k < order.size(); ++k) {
            int b = order[k];
            const std::vector<int>& preds = f.blocks[static_cast<size_t>(b)].preds;
            int newIdom = -1;
            for (size_t p = 0; p < preds.size(); ++p) {
                int pr = preds[p];
                if (idom[static_cast<size_t>(pr)] < 0) continue;
                if (newIdom < 0) { newIdom = pr; continue; }
                int a = pr, c = newIdom;
                while (a != c) {
                    while (rpoIndex[static_cast<size_t>(a)] > rpoIndex[static_cast<size_t>(c)]) a = idom[static_cast<size_t>(a)];
                    while (rpoIndex[static_cast<size_t>(c)] > rpoIndex[static_cast<size_t>(a)]) c = idom[static_cast<size_t>(c)];
                }
                newIdom = a;
            }
            if (newIdom >= 0 && idom[static_cast<size_t>(b)] != newIdom) { idom[static_cast<size_t>(b)] = newIdom; changed = true; }
        }
    }
}

bool Optimizer::dominates(const std::vector<int>& idom, int a, int b)
{
    if (a == b) return true;
    int x = b;
    for (int guard = 0; guard < 100000 && x >= 0; ++guard) {
        if (x == a) return true;
        if (x == 0) return false;
        int next = idom[static_cast<size_t>(x)];
        if (next == x) return false;
        x = next;
    }
    return false;
}

void Optimizer::naturalLoops(const IrFunction& f, std::vector<LoopInfo>& loops)
{
    loops.clear();
    std::vector<int> idom;
    dominators(f, idom);
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        if (idom[b] < 0) continue;   // unreachable
        for (size_t s = 0; s < f.blocks[b].succs.size(); ++s) {
            int h = f.blocks[b].succs[s];
            if (!dominates(idom, h, static_cast<int>(b))) continue;
            LoopInfo L;
            L.header = h;
            L.latch = static_cast<int>(b);
            std::set<int> body;
            body.insert(h);
            std::vector<int> work;
            if (static_cast<int>(b) != h) { body.insert(static_cast<int>(b)); work.push_back(static_cast<int>(b)); }
            while (!work.empty()) {
                int x = work.back(); work.pop_back();
                const std::vector<int>& preds = f.blocks[static_cast<size_t>(x)].preds;
                for (size_t p = 0; p < preds.size(); ++p) if (body.insert(preds[p]).second) work.push_back(preds[p]);
            }
            for (std::set<int>::iterator it = body.begin(); it != body.end(); ++it) L.blocks.push_back(*it);
            // merge loops with the same header
            bool merged = false;
            for (size_t k = 0; k < loops.size(); ++k)
                if (loops[k].header == h) {
                    for (size_t x = 0; x < L.blocks.size(); ++x) addUniqueInt(loops[k].blocks, L.blocks[x]);
                    merged = true;
                    break;
                }
            if (!merged) loops.push_back(L);
        }
    }
}

// ---- passes -----------------------------------------------------------------------------------

int Optimizer::constantFold(IrFunction& f)
{
    int changes = 0;
    std::map<int, Def> defs;
    collectDefs(f, defs);
    std::map<int, int> defCount;
    countDefs(f, defCount);
    // constant propagation: temp operands defined by const -> immediates (single-definition temps only)
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            for (size_t a = 0; a < in.args.size(); ++a) {
                IrOperand& o = in.args[a];
                if (o.kind != IrOperand::Temp) continue;
                if (defCount[o.id] != 1) continue;
                std::map<int, Def>::iterator d = defs.find(o.id);
                if (d == defs.end()) continue;
                const IrInst& di = f.blocks[static_cast<size_t>(d->second.block)].insts[static_cast<size_t>(d->second.index)];
                if (di.op == IrInst::Const && di.args.size() == 1) { const Type* t = o.type; o = di.args[0]; o.type = t ? t : di.type; ++changes; }
                else if (di.op == IrInst::FConst && di.args.size() == 1) { const Type* t = o.type; o = di.args[0]; o.type = t ? t : di.type; ++changes; }
            }
        }
    // folding
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            if ((in.op == IrInst::Bin || in.op == IrInst::Cmp) && in.args.size() == 2 && in.args[0].isConst() && in.args[1].isConst()) {
                bool fl = in.args[0].kind == IrOperand::FImm || in.args[1].kind == IrOperand::FImm || in.sub[0] == 'f';
                if (fl) {
                    double x = in.args[0].kind == IrOperand::FImm ? in.args[0].fval : static_cast<double>(in.args[0].ival);
                    double y = in.args[1].kind == IrOperand::FImm ? in.args[1].fval : static_cast<double>(in.args[1].ival);
                    // float operands and results are single precision: rounded as addss / ucomiss would see them
                    if (in.args[0].type && in.args[0].type->kind == Type::Float) x = static_cast<double>(static_cast<float>(x));
                    if (in.args[1].type && in.args[1].type->kind == Type::Float) y = static_cast<double>(static_cast<float>(y));
                    bool ok, isCmp; long c;
                    double r = foldFloat(in.sub, x, y, ok, isCmp, c);
                    if (!ok) continue;
                    if (!isCmp && in.type && in.type->kind == Type::Float) r = static_cast<double>(static_cast<float>(r));
                    IrInst n; n.dst = in.dst; n.line = in.line; n.type = in.type;
                    if (isCmp) { n.op = IrInst::Const; n.args.push_back(IrOperand::imm(c, in.type)); }
                    else { n.op = IrInst::FConst; n.args.push_back(IrOperand::fimm(r, in.type)); }
                    in = n; ++changes;
                } else {
                    bool ok;
                    long r = 0;
                    if (in.op == IrInst::Bin && isWideInt(in.type)) ok = foldWideInt(in.sub, in.args[0].ival, in.args[1].ival, r);
                    else r = foldInt(in.sub, in.args[0].ival, in.args[1].ival, ok);
                    if (!ok) continue;
                    IrInst n; n.op = IrInst::Const; n.dst = in.dst; n.line = in.line; n.type = in.type;
                    n.args.push_back(IrOperand::imm(r, in.type));
                    in = n; ++changes;
                }
            } else if (in.op == IrInst::Un && in.args.size() == 1 && in.args[0].isConst()) {
                IrInst n; n.dst = in.dst; n.line = in.line; n.type = in.type;
                if (in.sub == "neg" && in.args[0].kind == IrOperand::Imm) { n.op = IrInst::Const; n.args.push_back(IrOperand::imm(-in.args[0].ival, in.type)); }
                else if (in.sub == "fneg" && in.args[0].kind == IrOperand::FImm) { n.op = IrInst::FConst; n.args.push_back(IrOperand::fimm(-in.args[0].fval, in.type)); }
                else if (in.sub == "not" && in.args[0].kind == IrOperand::Imm) { n.op = IrInst::Const; n.args.push_back(IrOperand::imm(~in.args[0].ival, in.type)); }
                else continue;
                in = n; ++changes;
            } else if (in.op == IrInst::Conv && in.args.size() == 1 && in.args[0].isConst() && in.type) {
                IrInst n; n.dst = in.dst; n.line = in.line; n.type = in.type;
                const IrOperand& o = in.args[0];
                if (in.type->isFloating()) {
                    double d = o.kind == IrOperand::FImm ? o.fval : static_cast<double>(o.ival);
                    // a float source or result is single precision (constants are kept as doubles): rounded here
                    if (o.kind == IrOperand::FImm && o.type && o.type->kind == Type::Float) d = static_cast<double>(static_cast<float>(d));
                    // an unsigned source: its immediate is a 32-bit pattern, sign-extended to 64 bits
                    if (o.kind == IrOperand::Imm && in.sub == "uitofp" && o.type && o.type->builtinSize() == 4) d = static_cast<double>(static_cast<unsigned long>(o.ival));
                    else if (o.kind == IrOperand::Imm && in.sub == "uitofp" && o.ival < 0) continue;   // >= 2^63: rounded at run time
                    if (in.type->kind == Type::Float) d = static_cast<double>(static_cast<float>(d));
                    n.op = IrInst::FConst; n.args.push_back(IrOperand::fimm(d, in.type));
                }
                else if (in.type->isIntegral() || in.type->isPointer()) {
                    long v = o.ival;
                    if (o.kind == IrOperand::FImm) {
                        // the IR's immediates are 32-bit, sign-extended to 64 bits (zero-extended for unsigned int):
                        // a result outside that range stays a run-time conversion
                        const int size = in.type->builtinSize();
                        const long long r = floatToInt(o.fval, size, in.type->isUnsigned);
                        if (size == 8 && (r < -2147483647LL - 1 || r > 2147483647LL)) continue;
                        v = static_cast<long>(static_cast<int>(static_cast<unsigned int>(static_cast<unsigned long long>(r) & 0xFFFFFFFFULL)));
                    }
                    // a negative 32-bit pattern zero-extended needs a 64-bit constant, which the IR does not have
                    if (in.sub == "zext" && v < 0 && in.type->builtinSize() > 4) continue;
                    if (in.sub == "trunc") { int bits = in.type->builtinSize() * 8; if (bits < 32) v = in.type->isUnsigned ? (v & ((1L << bits) - 1)) : ((v << (32 - bits)) >> (32 - bits)); }
                    if (in.type->kind == Type::Bool) v = v != 0;
                    n.op = IrInst::Const; n.args.push_back(IrOperand::imm(v, in.type));
                } else continue;
                in = n; ++changes;
            } else if (in.op == IrInst::Copy && in.args.size() == 1 && in.args[0].isConst()) {
                in.op = in.args[0].kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const;
                ++changes;
            } else if (in.op == IrInst::CondBr && in.args.size() == 3 && in.args[0].isConst()) {
                bool taken = in.args[0].kind == IrOperand::FImm ? in.args[0].fval != 0.0 : in.args[0].ival != 0;
                IrOperand target = taken ? in.args[1] : in.args[2];
                in.op = IrInst::Br; in.args.clear(); in.args.push_back(target);
                ++changes;
            }
        }
    return changes;
}

int Optimizer::copyPropagation(IrFunction& f)
{
    int changes = 0;
    std::map<int, int> defCount;
    countDefs(f, defCount);
    // copy t = t2  ->  uses of t become t2  (only when t and t2 each have a single definition)
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            if (in.op == IrInst::Copy && in.args.size() == 1 && in.args[0].kind == IrOperand::Temp && in.dst >= 0 &&
                defCount[in.dst] == 1 && defCount[in.args[0].id] == 1) {
                replaceTempUses(f, in.dst, in.args[0]);
                in.op = IrInst::Nop; in.dst = -1; in.args.clear();
                ++changes;
            }
        }
    // store-to-load forwarding and redundant loads within a block
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::map<int, IrOperand> known;   // local -> operand holding its current value (temp or constant)
        std::vector<IrInst>& insts = f.blocks[b].insts;
        for (size_t i = 0; i < insts.size(); ++i) {
            IrInst& in = insts[i];
            if (in.op == IrInst::AddrLocal && in.args.size() == 1) { known.erase(in.args[0].id); continue; }
            if (in.op == IrInst::StoreLocal && in.args.size() == 2 && in.args[0].kind == IrOperand::Local) {
                int local = in.args[0].id;
                if (f.locals[static_cast<size_t>(local)].addressTaken) { known.erase(local); continue; }
                if (in.args[1].kind == IrOperand::Temp || in.args[1].isConst()) known[local] = in.args[1]; else known.erase(local);
                continue;
            }
            if (in.op == IrInst::LoadLocal && in.args.size() == 1 && in.args[0].kind == IrOperand::Local) {
                int local = in.args[0].id;
                if (f.locals[static_cast<size_t>(local)].addressTaken) continue;
                std::map<int, IrOperand>::iterator k = known.find(local);
                if (k != known.end() && !(k->second.kind == IrOperand::Temp && k->second.id == in.dst)) {
                    const Type* t = in.type;
                    if (k->second.kind == IrOperand::Temp) {
                        replaceTempUses(f, in.dst, IrOperand::temp(k->second.id, t));
                        in.op = IrInst::Nop; in.dst = -1; in.args.clear();
                    } else {
                        // the stored value is a constant: the load becomes a constant definition
                        in.op = k->second.kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const;
                        in.args.clear();
                        in.args.push_back(k->second);
                        in.args[0].type = t;
                    }
                    ++changes;
                } else if (in.dst >= 0) known[local] = IrOperand::temp(in.dst, in.type);
                continue;
            }
            if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::StoreMem || in.op == IrInst::CatchParam) {
                // calls and stores through pointers can modify address-taken locals only, which are never forwarded
                continue;
            }
        }
    }
    // memory store-to-load forwarding within a block: "storemem A, v" then "loadmem A" with the
    // same address temporary. Another store invalidates everything unless both addresses are
    // fields at different offsets of the same base; calls invalidate everything.
    std::map<int, Def> defs;
    collectDefs(f, defs);
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::map<int, IrOperand> mem;   // address temp -> value
        std::vector<IrInst>& insts = f.blocks[b].insts;
        for (size_t i = 0; i < insts.size(); ++i) {
            IrInst& in = insts[i];
            if (in.op == IrInst::StoreMem && in.args.size() == 2 && in.args[0].kind == IrOperand::Temp) {
                int addr = in.args[0].id;
                std::map<int, Def>::iterator da = defs.find(addr);
                const IrInst* ad = da == defs.end() ? 0 : &f.blocks[static_cast<size_t>(da->second.block)].insts[static_cast<size_t>(da->second.index)];
                for (std::map<int, IrOperand>::iterator m = mem.begin(); m != mem.end();) {
                    bool keep = false;
                    if (m->first == addr) keep = true;
                    else {
                        std::map<int, Def>::iterator db = defs.find(m->first);
                        const IrInst* bd = db == defs.end() ? 0 : &f.blocks[static_cast<size_t>(db->second.block)].insts[static_cast<size_t>(db->second.index)];
                        if (ad && bd && ad->op == IrInst::AddrField && bd->op == IrInst::AddrField && ad->args.size() == 1 && bd->args.size() == 1 &&
                            ad->args[0].kind == IrOperand::Temp && bd->args[0].kind == IrOperand::Temp && ad->args[0].id == bd->args[0].id && ad->extra != bd->extra)
                            keep = true;
                    }
                    if (keep) ++m; else mem.erase(m++);
                }
                if (in.args[1].kind == IrOperand::Temp || in.args[1].isConst()) mem[addr] = in.args[1]; else mem.erase(addr);
                continue;
            }
            if (in.op == IrInst::LoadMem && in.args.size() == 1 && in.args[0].kind == IrOperand::Temp) {
                std::map<int, IrOperand>::iterator m = mem.find(in.args[0].id);
                if (m != mem.end()) {
                    const Type* t = in.type;
                    if (m->second.kind == IrOperand::Temp) {
                        replaceTempUses(f, in.dst, IrOperand::temp(m->second.id, t));
                        in.op = IrInst::Nop; in.dst = -1; in.args.clear();
                    } else {
                        in.op = m->second.kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const;
                        in.args.clear();
                        in.args.push_back(m->second);
                        in.args[0].type = t;
                    }
                    ++changes;
                } else if (in.dst >= 0) mem[in.args[0].id] = IrOperand::temp(in.dst, in.type);
                continue;
            }
            if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::CatchParam || in.op == IrInst::StoreGlobal) mem.clear();
        }
    }
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::vector<IrInst> kept;
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) if (f.blocks[b].insts[i].op != IrInst::Nop) kept.push_back(f.blocks[b].insts[i]);
        f.blocks[b].insts.swap(kept);
    }
    // drop nops
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::vector<IrInst> kept;
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) if (f.blocks[b].insts[i].op != IrInst::Nop) kept.push_back(f.blocks[b].insts[i]);
        f.blocks[b].insts.swap(kept);
    }
    return changes;
}

int Optimizer::algebraic(IrFunction& f)
{
    int changes = 0;
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            if (in.op != IrInst::Bin || in.args.size() != 2) continue;
            IrOperand& x = in.args[0];
            IrOperand& y = in.args[1];
            const std::string& s = in.sub;
            bool yImm = y.kind == IrOperand::Imm, xImm = x.kind == IrOperand::Imm;
            IrInst n; n.dst = in.dst; n.line = in.line; n.type = in.type;
            if ((s == "add" || s == "sub" || s == "or" || s == "xor" || s == "shl" || s == "ashr" || s == "lshr") && yImm && y.ival == 0) { n.op = IrInst::Copy; n.args.push_back(x); in = n; ++changes; continue; }
            if ((s == "add" || s == "or" || s == "xor") && xImm && x.ival == 0) { n.op = IrInst::Copy; n.args.push_back(y); in = n; ++changes; continue; }
            if ((s == "mul" || s == "sdiv" || s == "udiv") && yImm && y.ival == 1) { n.op = IrInst::Copy; n.args.push_back(x); in = n; ++changes; continue; }
            if (s == "mul" && xImm && x.ival == 1) { n.op = IrInst::Copy; n.args.push_back(y); in = n; ++changes; continue; }
            if ((s == "mul" || s == "and") && ((yImm && y.ival == 0) || (xImm && x.ival == 0))) { n.op = IrInst::Const; n.args.push_back(IrOperand::imm(0, in.type)); in = n; ++changes; continue; }
            if ((s == "srem" || s == "urem") && yImm && y.ival == 1) { n.op = IrInst::Const; n.args.push_back(IrOperand::imm(0, in.type)); in = n; ++changes; continue; }
            if (s == "sub" && x.kind == IrOperand::Temp && y.kind == IrOperand::Temp && x.id == y.id) { n.op = IrInst::Const; n.args.push_back(IrOperand::imm(0, in.type)); in = n; ++changes; continue; }
            if (s == "mul" && yImm && isPow2(y.ival) && y.ival > 1) { in.sub = "shl"; y.ival = log2of(y.ival); ++changes; continue; }
            if (s == "mul" && xImm && isPow2(x.ival) && x.ival > 1) { in.sub = "shl"; long k = log2of(x.ival); x = y; y = IrOperand::imm(k, in.type); ++changes; continue; }
            if (s == "udiv" && yImm && isPow2(y.ival) && y.ival > 1) { in.sub = "lshr"; y.ival = log2of(y.ival); ++changes; continue; }
            if (s == "urem" && yImm && isPow2(y.ival) && y.ival > 1) { in.sub = "and"; y.ival = y.ival - 1; ++changes; continue; }
            // floating point: only the identities exact under IEEE for every x (signed zeros, NaN, infinities):
            // x * 1, x / 1, x - (+0) and x + (-0). Not x + 0 (-0 + 0 is +0), x * 0, x - x, 0 - x
            if ((s == "fmul" || s == "fdiv") && y.kind == IrOperand::FImm && y.fval == 1.0) { n.op = IrInst::Copy; n.args.push_back(x); in = n; ++changes; continue; }
            if ((s == "fadd" || s == "fsub") && y.kind == IrOperand::FImm && y.fval == 0.0 && signBit(y.fval) == (s == "fadd")) { n.op = IrInst::Copy; n.args.push_back(x); in = n; ++changes; continue; }
        }
    return changes;
}

int Optimizer::localCse(IrFunction& f)
{
    int changes = 0;
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::map<std::string, int> seen;
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            if (!in.pure() || in.dst < 0 || in.op == IrInst::Copy) continue;
            if (in.op == IrInst::Const || in.op == IrInst::FConst || in.op == IrInst::StrConst) continue;   // cheaper to rematerialise
            std::string key = instKey(in);
            std::map<std::string, int>::iterator it = seen.find(key);
            if (it == seen.end()) { seen[key] = in.dst; continue; }
            IrInst n; n.op = IrInst::Copy; n.dst = in.dst; n.type = in.type; n.line = in.line;
            n.args.push_back(IrOperand::temp(it->second, in.type));
            in = n;
            ++changes;
        }
    }
    return changes;
}

int Optimizer::deadCode(IrFunction& f)
{
    int changes = 0;
    for (;;) {
        std::map<int, int> uses;
        countUses(f, uses);
        int removed = 0;
        for (size_t b = 0; b < f.blocks.size(); ++b) {
            std::vector<IrInst> kept;
            for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
                const IrInst& in = f.blocks[b].insts[i];
                bool dead = in.dst >= 0 && uses[in.dst] == 0 && (in.pure() || in.readsMemory());
                if (in.op == IrInst::Nop) dead = true;
                if (dead) ++removed; else kept.push_back(in);
            }
            f.blocks[b].insts.swap(kept);
        }
        changes += removed;
        if (!removed) break;
    }
    return changes;
}

int Optimizer::deadStores(IrFunction& f)
{
    int changes = 0;
    // locals never loaded (and not address-taken, not parameters read by the caller's convention) : all stores are dead
    std::vector<bool> loaded(f.locals.size(), false);
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            if ((in.op == IrInst::LoadLocal || in.op == IrInst::AddrLocal) && !in.args.empty() && in.args[0].kind == IrOperand::Local) loaded[static_cast<size_t>(in.args[0].id)] = true;
        }
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::vector<IrInst> kept;
        std::map<int, size_t> lastStore;   // local -> index in kept of the last store without a load since
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            if (in.op == IrInst::StoreLocal && in.args.size() == 2 && in.args[0].kind == IrOperand::Local) {
                int local = in.args[0].id;
                if (!loaded[static_cast<size_t>(local)] && !f.locals[static_cast<size_t>(local)].addressTaken) { ++changes; continue; }
                std::map<int, size_t>::iterator ls = lastStore.find(local);
                if (ls != lastStore.end() && !f.locals[static_cast<size_t>(local)].addressTaken) { kept[ls->second].op = IrInst::Nop; ++changes; }
                lastStore[local] = kept.size();
                kept.push_back(in);
                continue;
            }
            if (in.op == IrInst::LoadLocal && !in.args.empty()) lastStore.erase(in.args[0].id);
            if (in.op == IrInst::AddrLocal && !in.args.empty()) lastStore.erase(in.args[0].id);
            if (in.op == IrInst::Call || in.op == IrInst::VCall || in.isTerminator()) lastStore.clear();
            kept.push_back(in);
        }
        std::vector<IrInst> clean;
        for (size_t i = 0; i < kept.size(); ++i) if (kept[i].op != IrInst::Nop) clean.push_back(kept[i]);
        f.blocks[b].insts.swap(clean);
    }
    return changes;
}

int Optimizer::simplifyCfg(IrFunction& f)
{
    int changes = 0;
    f.computeCfg();
    // condbr with equal targets
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        if (f.blocks[b].insts.empty()) continue;
        IrInst& last = f.blocks[b].insts.back();
        if (last.op == IrInst::CondBr && last.args.size() == 3 && last.args[1].id == last.args[2].id) {
            IrOperand t = last.args[1];
            last.op = IrInst::Br; last.args.clear(); last.args.push_back(t);
            ++changes;
        }
    }
    // jump threading: a block holding only "br X" is bypassed (not when X starts with phis: the
    // incoming-edge bookkeeping would change)
    for (size_t b = 1; b < f.blocks.size(); ++b) {
        const IrBlock& blk = f.blocks[b];
        if (blk.insts.size() != 1 || blk.insts[0].op != IrInst::Br) continue;
        int target = blk.insts[0].args[0].id;
        if (target == static_cast<int>(b)) continue;
        const IrBlock& tb = f.blocks[static_cast<size_t>(target)];
        if (!tb.insts.empty() && tb.insts[0].op == IrInst::Phi) continue;
        for (size_t p = 0; p < f.blocks.size(); ++p)
            for (size_t i = 0; i < f.blocks[p].insts.size(); ++i) {
                IrInst& in = f.blocks[p].insts[i];
                if (in.op != IrInst::Br && in.op != IrInst::CondBr && in.op != IrInst::TryBegin) continue;
                for (size_t a = 0; a < in.args.size(); ++a)
                    if (in.args[a].kind == IrOperand::Block && in.args[a].id == static_cast<int>(b)) { in.args[a].id = target; ++changes; }
            }
    }
    f.computeCfg();
    // reachability
    std::vector<bool> reach(f.blocks.size(), false);
    std::vector<int> work;
    work.push_back(0);
    reach[0] = true;
    while (!work.empty()) {
        int x = work.back(); work.pop_back();
        const std::vector<int>& succs = f.blocks[static_cast<size_t>(x)].succs;
        for (size_t s = 0; s < succs.size(); ++s)
            if (succs[s] >= 0 && static_cast<size_t>(succs[s]) < reach.size() && !reach[static_cast<size_t>(succs[s])]) { reach[static_cast<size_t>(succs[s])] = true; work.push_back(succs[s]); }
    }
    // merge: A ends with br B, B has A as its only predecessor
    for (size_t a = 0; a < f.blocks.size(); ++a) {
        if (!reach[a] || f.blocks[a].insts.empty()) continue;
        IrInst& last = f.blocks[a].insts.back();
        if (last.op != IrInst::Br) continue;
        int bId = last.args[0].id;
        if (bId == static_cast<int>(a) || bId == 0) continue;
        IrBlock& bb = f.blocks[static_cast<size_t>(bId)];
        if (bb.preds.size() != 1 || bb.preds[0] != static_cast<int>(a)) continue;
        bool hasCatch = !bb.insts.empty() && bb.insts[0].op == IrInst::CatchParam;
        if (hasCatch) continue;
        bool hasPhi = !bb.insts.empty() && bb.insts[0].op == IrInst::Phi;
        if (hasPhi) continue;   // simplifyPhis turns the single-operand phi into a copy first
        f.blocks[a].insts.pop_back();
        f.blocks[a].insts.insert(f.blocks[a].insts.end(), bb.insts.begin(), bb.insts.end());
        bb.insts.clear();
        // phis in the successors of B now receive their value from A
        for (size_t s = 0; s < bb.succs.size(); ++s) {
            IrBlock& sb = f.blocks[static_cast<size_t>(bb.succs[s])];
            for (size_t i = 0; i < sb.insts.size(); ++i) {
                IrInst& phi = sb.insts[i];
                if (phi.op != IrInst::Phi) break;
                for (size_t x = 1; x < phi.args.size(); x += 2) if (phi.args[x].kind == IrOperand::Block && phi.args[x].id == bId) phi.args[x].id = static_cast<int>(a);
            }
        }
        reach[static_cast<size_t>(bId)] = false;
        ++changes;
        f.computeCfg();
        // the merged block may now end with another br: loop again on this block
        --a;
    }
    // remove unreachable blocks and renumber
    std::vector<int> remap(f.blocks.size(), -1);
    std::vector<IrBlock> kept;
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        if (!reach[b]) { ++changes; continue; }
        remap[b] = static_cast<int>(kept.size());
        kept.push_back(f.blocks[b]);
    }
    if (kept.size() != f.blocks.size()) {
        for (size_t b = 0; b < kept.size(); ++b) {
            kept[b].id = static_cast<int>(b);
            for (size_t i = 0; i < kept[b].insts.size(); ++i)
                for (size_t x = 0; x < kept[b].insts[i].args.size(); ++x)
                    if (kept[b].insts[i].args[x].kind == IrOperand::Block) {
                        int old = kept[b].insts[i].args[x].id;
                        kept[b].insts[i].args[x].id = (old >= 0 && static_cast<size_t>(old) < remap.size() && remap[static_cast<size_t>(old)] >= 0) ? remap[static_cast<size_t>(old)] : 0;
                    }
        }
        f.blocks.swap(kept);
    }
    f.computeCfg();
    return changes;
}

namespace {

bool isFloatingType(const Type* t) { return t && (t->kind == Type::Float || t->kind == Type::Double); }

// an address that is valid for the whole function: a field (of a field ...) of *this, of an object a reference
// parameter refers to, or of a local object. A load from it may be done early without risk of a fault.
bool dereferenceable(const IrFunction& f, const IrOperand& o, const std::map<int, IrInst>& defs, int depth)
{
    if (o.kind != IrOperand::Temp || depth > 8) return false;
    std::map<int, IrInst>::const_iterator d = defs.find(o.id);
    if (d == defs.end()) return false;
    const IrInst& in = d->second;
    if (in.op == IrInst::AddrField || in.op == IrInst::Copy) return !in.args.empty() && dereferenceable(f, in.args[0], defs, depth + 1);
    if (in.op == IrInst::AddrLocal) return true;
    if (in.op == IrInst::LoadLocal && !in.args.empty() && in.args[0].kind == IrOperand::Local) {
        const int id = in.args[0].id;
        const IrLocal& l = f.locals[static_cast<size_t>(id)];
        if (l.isParam) return l.isThis || l.nonNull || (l.type && l.type->kind == Type::Reference);
        // a local assigned exactly once, from such an address (an inlined call's copy of 'this' or of a reference)
        if (l.addressTaken) return false;
        const IrOperand* value = 0;
        int stores = 0;
        for (size_t b = 0; b < f.blocks.size(); ++b)
            for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
                const IrInst& s = f.blocks[b].insts[i];
                if (s.op == IrInst::StoreLocal && s.args.size() == 2 && s.args[0].kind == IrOperand::Local && s.args[0].id == id) { ++stores; value = &s.args[1]; }
            }
        return stores == 1 && dereferenceable(f, *value, defs, depth + 1);
    }
    return false;
}

} // namespace

int Optimizer::licm(IrFunction& f)
{
    int changes = 0;
    f.computeCfg();
    std::vector<LoopInfo> loops;
    naturalLoops(f, loops);
    // every temp's defining instruction (temps are assigned once before SSA), for the dereferenceability test;
    // copies, since hoisting moves instructions between blocks
    std::map<int, IrInst> defs;
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            if (f.blocks[b].insts[i].dst >= 0) {
                const int d = f.blocks[b].insts[i].dst;
                if (defs.count(d)) defs[d] = IrInst();   // a temp assigned twice is not trusted (a Nop proves nothing)
                else defs[d] = f.blocks[b].insts[i];
            }
    for (size_t L = 0; L < loops.size(); ++L) {
        LoopInfo& loop = loops[L];
        std::set<int> inLoop(loop.blocks.begin(), loop.blocks.end());
        // temps defined inside the loop; locals stored or address-taken inside the loop
        std::set<int> loopTemps;
        std::set<int> modifiedLocals;
        bool hasCall = false;
        bool onlyFloatStores = true;   // every store in the loop writes a double / float (C++ aliasing: no int or pointer)
        for (size_t k = 0; k < loop.blocks.size(); ++k) {
            const IrBlock& blk = f.blocks[static_cast<size_t>(loop.blocks[k])];
            for (size_t i = 0; i < blk.insts.size(); ++i) {
                const IrInst& in = blk.insts[i];
                if (in.dst >= 0) loopTemps.insert(in.dst);
                if ((in.op == IrInst::StoreLocal || in.op == IrInst::AddrLocal) && !in.args.empty() && in.args[0].kind == IrOperand::Local) modifiedLocals.insert(in.args[0].id);
                if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::Throw || in.op == IrInst::TryBegin ||
                    in.op == IrInst::TryEnd || in.op == IrInst::CatchParam || in.op == IrInst::CatchEnd) hasCall = true;
                if ((in.op == IrInst::StoreMem || in.op == IrInst::StoreGlobal ||
                     (in.op == IrInst::StoreLocal && !in.args.empty() && in.args[0].kind == IrOperand::Local && f.locals[static_cast<size_t>(in.args[0].id)].addressTaken)) &&
                    !isFloatingType(in.type)) onlyFloatStores = false;
            }
        }
        // preheader: the unique predecessor of the header outside the loop
        const IrBlock& header = f.blocks[static_cast<size_t>(loop.header)];
        std::vector<int> outside;
        for (size_t p = 0; p < header.preds.size(); ++p) if (!inLoop.count(header.preds[p])) outside.push_back(header.preds[p]);
        if (outside.size() != 1) continue;
        int pre = outside[0];
        IrBlock& preBlock = f.blocks[static_cast<size_t>(pre)];
        if (preBlock.insts.empty() || preBlock.insts.back().op != IrInst::Br) continue;   // keep it simple: only a plain jump into the header
        // hoist to fixpoint
        bool progress = true;
        while (progress) {
            progress = false;
            for (size_t k = 0; k < loop.blocks.size(); ++k) {
                IrBlock& blk = f.blocks[static_cast<size_t>(loop.blocks[k])];
                for (size_t i = 0; i < blk.insts.size(); ++i) {
                    IrInst& in = blk.insts[i];
                    // never a phi: its value depends on the edge it is entered by, even when every argument is a constant
                    bool candidate = in.pure() && in.dst >= 0 && in.op != IrInst::Copy && in.op != IrInst::Phi;
                    if (in.op == IrInst::LoadLocal && !in.args.empty() && in.args[0].kind == IrOperand::Local && !modifiedLocals.count(in.args[0].id) &&
                        !f.locals[static_cast<size_t>(in.args[0].id)].addressTaken && !hasCall) candidate = true;
                    // an int or pointer field the loop cannot change (no call, only floating-point stores) at an
                    // address that cannot fault: b.cols(), the data pointer of a vector member
                    if (in.op == IrInst::LoadMem && in.dst >= 0 && !hasCall && onlyFloatStores && in.type && !isFloatingType(in.type) &&
                        !in.args.empty() && dereferenceable(f, in.args[0], defs, 0)) candidate = true;
                    if (!candidate) continue;
                    bool invariant = true;
                    for (size_t a = 0; a < in.args.size(); ++a)
                        if (in.args[a].kind == IrOperand::Temp && loopTemps.count(in.args[a].id)) { invariant = false; break; }
                    if (!invariant) continue;
                    IrInst moved = in;
                    preBlock.insts.insert(preBlock.insts.end() - 1, moved);
                    blk.insts.erase(blk.insts.begin() + static_cast<long>(i));
                    loopTemps.erase(moved.dst);
                    ++changes;
                    progress = true;
                    --i;
                }
            }
        }
    }
    return changes;
}

int Optimizer::inlineCalls(IrFunction& f, int maxInsts, int maxInstsInLoop)
{
    // a budget per caller, as cl, clang and gcc limit inlining by the caller's growth (a count per module
    // would stop inlining in every function after the first few)
    const int growthLimit = f.instCount() * 4 + 400;
    const int maxPerCaller = 200;
    // larger callees only where a call repeats: in a loop (blocks a call inlined there adds are in it too)
    f.computeCfg();
    std::vector<LoopInfo> loops;
    naturalLoops(f, loops);
    std::vector<bool> inLoop(f.blocks.size(), false);
    for (size_t L = 0; L < loops.size(); ++L)
        for (size_t k = 0; k < loops[L].blocks.size(); ++k) inLoop[static_cast<size_t>(loops[L].blocks[k])] = true;
    int changes = 0;
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst call = f.blocks[b].insts[i];
            if (call.op != IrInst::Call) continue;
            std::map<std::string, IrFunction*>::const_iterator it = m_functions.find(call.sub);
            if (it == m_functions.end()) continue;
            const IrFunction& g = *it->second;
            const bool hot = b < inLoop.size() && inLoop[b];
            if (&g == &f || g.name == f.name || g.hasTry || g.instCount() > (hot ? maxInstsInLoop : maxInsts)) continue;
            if (changes >= maxPerCaller || f.instCount() + g.instCount() > growthLimit) continue;
            if (g.blocks.empty()) continue;
            bool hasUnreachable = false;
            for (size_t gb = 0; gb < g.blocks.size() && !hasUnreachable; ++gb)
                for (size_t gi = 0; gi < g.blocks[gb].insts.size(); ++gi)
                    if (g.blocks[gb].insts[gi].op == IrInst::Unreachable || g.blocks[gb].insts[gi].op == IrInst::CatchParam) { hasUnreachable = true; break; }
            if (hasUnreachable) continue;
            if (static_cast<int>(call.args.size()) != g.paramCount) continue;

            // split the block after the call
            IrBlock& caller = f.blocks[b];
            std::vector<IrInst> tail(caller.insts.begin() + static_cast<long>(i + 1), caller.insts.end());
            caller.insts.erase(caller.insts.begin() + static_cast<long>(i), caller.insts.end());
            int contId = f.newBlock("inline.cont " + g.name);
            // (f.blocks may have reallocated: re-fetch references by index from here on)
            f.blocks[static_cast<size_t>(contId)].insts = tail;

            // new locals for the callee's locals
            int localBase = static_cast<int>(f.locals.size());
            for (size_t l = 0; l < g.locals.size(); ++l) { IrLocal nl = g.locals[l]; nl.name = g.name + "." + nl.name; nl.isParam = false; nl.isThis = false; f.locals.push_back(nl); }
            int resultLocal = -1;
            if (call.dst >= 0) { IrLocal rl; rl.name = g.name + ".result"; rl.type = call.type; rl.size = 8; rl.align = 8; f.locals.push_back(rl); resultLocal = static_cast<int>(f.locals.size() - 1); }
            // argument stores
            for (size_t a = 0; a < call.args.size(); ++a) {
                IrInst st; st.op = IrInst::StoreLocal; st.type = g.locals[a].type; st.line = call.line;
                st.args.push_back(IrOperand::local(localBase + static_cast<int>(a), g.locals[a].type));
                st.args.push_back(call.args[a]);
                f.blocks[b].insts.push_back(st);
            }
            // clone blocks
            int blockBase = static_cast<int>(f.blocks.size());
            int tempBase = f.nextTemp;
            int maxTemp = 0;
            for (std::map<int, const Type*>::const_iterator t = g.tempTypes.begin(); t != g.tempTypes.end(); ++t) if (t->first > maxTemp) maxTemp = t->first;
            f.nextTemp += maxTemp + 1;
            for (size_t gb = 0; gb < g.blocks.size(); ++gb) {
                int nb = f.newBlock("inline " + g.name + " " + g.blocks[gb].label);
                IrBlock& dst = f.blocks[static_cast<size_t>(nb)];
                for (size_t gi = 0; gi < g.blocks[gb].insts.size(); ++gi) {
                    IrInst in = g.blocks[gb].insts[gi];
                    if (in.dst >= 0) { f.tempTypes[tempBase + in.dst] = in.type; in.dst = tempBase + in.dst; }
                    for (size_t a = 0; a < in.args.size(); ++a) {
                        IrOperand& o = in.args[a];
                        if (o.kind == IrOperand::Temp) o.id += tempBase;
                        else if (o.kind == IrOperand::Local) o.id += localBase;
                        else if (o.kind == IrOperand::Block) o.id += blockBase;
                    }
                    if (in.op == IrInst::Ret) {
                        if (resultLocal >= 0 && !in.args.empty()) {
                            IrInst st; st.op = IrInst::StoreLocal; st.type = call.type; st.line = in.line;
                            st.args.push_back(IrOperand::local(resultLocal, call.type));
                            st.args.push_back(in.args[0]);
                            dst.insts.push_back(st);
                        }
                        IrInst br; br.op = IrInst::Br; br.line = in.line; br.args.push_back(IrOperand::block(contId));
                        dst.insts.push_back(br);
                        continue;
                    }
                    dst.insts.push_back(in);
                }
            }
            // jump into the clone; load the result in the continuation
            IrInst jmp; jmp.op = IrInst::Br; jmp.line = call.line; jmp.args.push_back(IrOperand::block(blockBase));
            f.blocks[b].insts.push_back(jmp);
            if (resultLocal >= 0) {
                IrInst ld; ld.op = IrInst::LoadLocal; ld.dst = call.dst; ld.type = call.type; ld.line = call.line;
                ld.args.push_back(IrOperand::local(resultLocal, call.type));
                f.blocks[static_cast<size_t>(contId)].insts.insert(f.blocks[static_cast<size_t>(contId)].insts.begin(), ld);
            }
            inLoop.resize(f.blocks.size(), hot);   // the continuation and the copied blocks: where the call was
            ++changes;
            ++m_inlined;
            break;   // the block changed: continue with the next block (the continuation is scanned later)
        }
    }
    if (changes) f.computeCfg();
    return changes;
}

int Optimizer::unrollLoops(IrFunction& f, int maxTrip, int maxBody)
{
    int changes = 0;
    f.computeCfg();
    std::vector<LoopInfo> loops;
    naturalLoops(f, loops);
    for (size_t L = 0; L < loops.size(); ++L) {
        const LoopInfo& loop = loops[L];
        std::set<int> inLoop(loop.blocks.begin(), loop.blocks.end());
        const IrBlock& header = f.blocks[static_cast<size_t>(loop.header)];
        if (header.insts.empty() || header.insts.back().op != IrInst::CondBr) continue;
        const IrInst& cbr = header.insts.back();
        int exitBlock = inLoop.count(cbr.args[1].id) ? cbr.args[2].id : cbr.args[1].id;
        int bodyEntry = inLoop.count(cbr.args[1].id) ? cbr.args[1].id : cbr.args[2].id;
        if (inLoop.count(exitBlock)) continue;
        // condition: cmp (loadlocal i) imm
        int condTemp = cbr.args[0].id;
        const IrInst* cmp = 0;
        for (size_t i = 0; i < header.insts.size(); ++i) if (header.insts[i].dst == condTemp) cmp = &header.insts[i];
        if (!cmp || cmp->op != IrInst::Cmp || cmp->args.size() != 2 || cmp->args[1].kind != IrOperand::Imm || cmp->args[0].kind != IrOperand::Temp) continue;
        const IrInst* ld = 0;
        for (size_t i = 0; i < header.insts.size(); ++i) if (header.insts[i].dst == cmp->args[0].id) ld = &header.insts[i];
        if (!ld || ld->op != IrInst::LoadLocal) continue;
        int ivar = ld->args[0].id;
        if (f.locals[static_cast<size_t>(ivar)].addressTaken) continue;
        long limit = cmp->args[1].ival;
        std::string rel = cmp->sub;
        bool trueIsBody = (cbr.args[1].id == bodyEntry);
        // exactly one store to i inside the loop: storelocal i, (bin add (loadlocal i) imm step)
        int stores = 0; long step = 0; int bodyInsts = 0;
        for (size_t k = 0; k < loop.blocks.size(); ++k) {
            const IrBlock& blk = f.blocks[static_cast<size_t>(loop.blocks[k])];
            bodyInsts += static_cast<int>(blk.insts.size());
            for (size_t i = 0; i < blk.insts.size(); ++i) {
                const IrInst& in = blk.insts[i];
                if (in.op == IrInst::Call || in.op == IrInst::VCall || in.op == IrInst::TryBegin || in.op == IrInst::Throw) { stores = 99; break; }
                if (in.op == IrInst::StoreLocal && in.args[0].id == ivar) {
                    ++stores;
                    if (in.args[1].kind != IrOperand::Temp) { stores = 99; break; }
                    const IrInst* add = 0;
                    for (size_t j = 0; j < blk.insts.size(); ++j) if (blk.insts[j].dst == in.args[1].id) add = &blk.insts[j];
                    if (!add || add->op != IrInst::Bin || add->sub != "add" || add->args[1].kind != IrOperand::Imm || add->args[0].kind != IrOperand::Temp) { stores = 99; break; }
                    const IrInst* l2 = 0;
                    for (size_t j = 0; j < blk.insts.size(); ++j) if (blk.insts[j].dst == add->args[0].id) l2 = &blk.insts[j];
                    if (!l2 || l2->op != IrInst::LoadLocal || l2->args[0].id != ivar) { stores = 99; break; }
                    step = add->args[1].ival;
                }
            }
        }
        if (stores != 1 || step <= 0 || bodyInsts > maxBody) continue;
        // initial value: last "storelocal i, imm" in the unique outside predecessor
        std::vector<int> outside;
        for (size_t p = 0; p < header.preds.size(); ++p) if (!inLoop.count(header.preds[p])) outside.push_back(header.preds[p]);
        if (outside.size() != 1) continue;
        int pre = outside[0];
        long start = 0; bool haveStart = false;
        for (size_t i = 0; i < f.blocks[static_cast<size_t>(pre)].insts.size(); ++i) {
            const IrInst& in = f.blocks[static_cast<size_t>(pre)].insts[i];
            if (in.op == IrInst::StoreLocal && in.args[0].id == ivar) { haveStart = in.args[1].kind == IrOperand::Imm; start = in.args[1].ival; }
            if ((in.op == IrInst::LoadLocal || in.op == IrInst::AddrLocal) && in.args[0].id == ivar) haveStart = false;
        }
        if (!haveStart) continue;
        if (f.blocks[static_cast<size_t>(pre)].insts.back().op != IrInst::Br) continue;
        // trip count
        long trip = -1;
        if (rel == "slt" || rel == "ult") trip = limit > start ? (limit - start + step - 1) / step : 0;
        else if (rel == "sle" || rel == "ule") trip = limit >= start ? (limit - start) / step + 1 : 0;
        else if (rel == "ne") { if ((limit - start) % step == 0 && limit >= start) trip = (limit - start) / step; }
        if (!trueIsBody) continue;   // "while (!(i < n))" shapes are not handled
        if (trip < 0 || trip > maxTrip) continue;

        // emit trip copies of the loop blocks
        int prevExit = -1;   // block whose final br must be redirected to the next copy
        int firstCopyHeader = -1;
        for (long c = 0; c < trip; ++c) {
            int blockBase = static_cast<int>(f.blocks.size());
            std::map<int, int> blockMap;
            for (size_t k = 0; k < loop.blocks.size(); ++k) blockMap[loop.blocks[k]] = blockBase + static_cast<int>(k);
            int tempBase = f.nextTemp;
            int maxTemp = 0;
            std::set<int> loopDefs;   // only these are renamed: values from before the loop (hoisted ones too) stay
            for (size_t k = 0; k < loop.blocks.size(); ++k) {
                const IrBlock& blk = f.blocks[static_cast<size_t>(loop.blocks[k])];
                for (size_t i = 0; i < blk.insts.size(); ++i) {
                    if (blk.insts[i].dst > maxTemp) maxTemp = blk.insts[i].dst;
                    if (blk.insts[i].dst >= 0) loopDefs.insert(blk.insts[i].dst);
                }
            }
            f.nextTemp += maxTemp + 1;
            for (size_t k = 0; k < loop.blocks.size(); ++k) {
                int src = loop.blocks[k];
                int nb = f.newBlock("unroll " + intToString(static_cast<int>(c + 1)) + "/" + intToString(static_cast<int>(trip)) + " " + f.blocks[static_cast<size_t>(src)].label);
                IrBlock copy = f.blocks[static_cast<size_t>(src)];
                copy.id = nb; copy.succs.clear(); copy.preds.clear();
                for (size_t i = 0; i < copy.insts.size(); ++i) {
                    IrInst& in = copy.insts[i];
                    if (in.dst >= 0) { f.tempTypes[tempBase + in.dst] = in.type; in.dst += tempBase; }
                    for (size_t a = 0; a < in.args.size(); ++a) {
                        IrOperand& o = in.args[a];
                        if (o.kind == IrOperand::Temp && loopDefs.count(o.id)) o.id += tempBase;
                        else if (o.kind == IrOperand::Block && blockMap.count(o.id)) o.id = blockMap[o.id];
                    }
                }
                f.blocks[static_cast<size_t>(nb)] = copy;
                f.blocks[static_cast<size_t>(nb)].label = "unroll " + intToString(static_cast<int>(c + 1)) + "/" + intToString(static_cast<int>(trip)) + " " + f.blocks[static_cast<size_t>(src)].label;
            }
            int copyHeader = blockMap[loop.header];
            // header copy: the condition is known true -> plain jump into the body
            IrBlock& ch = f.blocks[static_cast<size_t>(copyHeader)];
            IrInst& chLast = ch.insts.back();
            int bodyTarget = blockMap.count(bodyEntry) ? blockMap[bodyEntry] : copyHeader;
            chLast.op = IrInst::Br; chLast.args.clear(); chLast.args.push_back(IrOperand::block(bodyTarget));
            // latch copy: its back edge becomes the link to the next copy (set below)
            int copyLatch = blockMap[loop.latch];
            if (prevExit >= 0) {
                IrInst& pl = f.blocks[static_cast<size_t>(prevExit)].insts.back();
                for (size_t a = 0; a < pl.args.size(); ++a) if (pl.args[a].kind == IrOperand::Block && pl.args[a].id == firstCopyHeader + 0 && false) {}
                // redirect the previous latch's jump to the header copy of this iteration
                for (size_t a = 0; a < pl.args.size(); ++a) if (pl.args[a].kind == IrOperand::Block) pl.args[a].id = copyHeader;
            } else {
                firstCopyHeader = copyHeader;
            }
            prevExit = copyLatch;
        }
        // last latch -> exit; preheader -> first copy (or exit when trip == 0)
        if (prevExit >= 0) {
            IrInst& pl = f.blocks[static_cast<size_t>(prevExit)].insts.back();
            for (size_t a = 0; a < pl.args.size(); ++a) if (pl.args[a].kind == IrOperand::Block) pl.args[a].id = exitBlock;
        }
        IrInst& preBr = f.blocks[static_cast<size_t>(pre)].insts.back();
        preBr.args[0].id = trip > 0 ? firstCopyHeader : exitBlock;
        ++changes;
        break;   // one loop per pass; the CFG changed
    }
    if (changes) f.computeCfg();
    return changes;
}

// ---- driver ---------------------------------------------------------------------------------

int Optimizer::level1(IrFunction& f, std::vector<std::string>& log)
{
    int total = 0;
    for (int round = 0; round < 6; ++round) {
        int c = 0, n;
        n = constantFold(f);   c += n; if (n) log.push_back("    constant-fold: " + intToString(n));
        n = copyPropagation(f); c += n; if (n) log.push_back("    copy-propagation / store-load forwarding: " + intToString(n));
        n = algebraic(f);      c += n; if (n) log.push_back("    algebraic simplification / strength reduction: " + intToString(n));
        n = localCse(f);       c += n; if (n) log.push_back("    local CSE: " + intToString(n));
        n = deadCode(f);       c += n; if (n) log.push_back("    dead code elimination: " + intToString(n));
        n = deadStores(f);     c += n; if (n) log.push_back("    dead store elimination: " + intToString(n));
        n = simplifyCfg(f);    c += n; if (n) log.push_back("    branch folding / block merging / unreachable removal: " + intToString(n));
        total += c;
        if (!c) break;
    }
    return total;
}

void Optimizer::runLocal(IrFunction& f, std::vector<std::string>& log)
{
    if (m_level <= 0) return;
    log.push_back("  " + f.name + "  (" + intToString(f.instCount()) + " instructions, -O" + intToString(m_level) + ")");
    level1(f, log);
    if (m_level >= 2) { int n = licm(f); if (n) log.push_back("    loop-invariant code motion: " + intToString(n)); }
}

void Optimizer::runInterprocedural(IrFunction& f, std::vector<std::string>& log)
{
    if (m_level < 2) return;
    // accessors of a line or two (rows(), cols(): 4 instructions) everywhere; in a loop also operators of a few
    // lines (operator()(r, c) calling index(r, c) is about 15). 24 everywhere made the matrix sample 31% faster
    // and 66% larger; 8 everywhere slower than no change
    int n = inlineCalls(f, 8, 24); if (n) log.push_back("    inlining (small functions): " + intToString(n));
    level1(f, log);
    // the code inlined into a loop brings loop-invariant work with it (b.cols(), i * cols, data pointers)
    if (n) { int h = licm(f); if (h) { log.push_back("    loop-invariant code motion after inlining: " + intToString(h)); level1(f, log); } }
    if (m_level >= 3) {
        n = inlineCalls(f, 40, 40); if (n) log.push_back("    inlining (larger functions): " + intToString(n));
        n = unrollLoops(f, 8, 40); if (n) log.push_back("    loop unrolling: " + intToString(n));
        level1(f, log);
        n = licm(f); if (n) log.push_back("    loop-invariant code motion: " + intToString(n));
        level1(f, log);
    }
}

void Optimizer::run(IrFunction& f, std::vector<std::string>& log, std::string* ssaForm)
{
    if (m_level <= 0) return;
    runLocal(f, log);
    runInterprocedural(f, log);
    runSsa(f, log, ssaForm);
}

namespace {

// a function with try regions can still keep most of its locals in registers when no handler ever resumes the
// normal code: the handlers (a cleanup that destroys the objects built so far and rethrows) are entered by
// longjmp, which restores only the callee-saved registers as they were at the setjmp, so the locals a handler
// reads must stay in memory; every other local is read only on the normal path and can be promoted. Marks the
// locals the handlers use as address-taken (in memory) and returns true, or returns false, changing nothing,
// when a handler can flow back into the normal code (a catch that does not rethrow).
bool pinHandlerLocals(IrFunction& f)
{
    f.computeCfg();
    const size_t n = f.blocks.size();
    // the normal code: reachable from the entry through branches only
    std::vector<bool> normal(n, false), handler(n, false);
    std::vector<int> work(1, 0);
    if (n) normal[0] = true;
    while (!work.empty()) {
        int b = work.back(); work.pop_back();
        const IrBlock& blk = f.blocks[static_cast<size_t>(b)];
        for (size_t i = 0; i < blk.insts.size(); ++i) {
            const IrInst& in = blk.insts[i];
            if (in.op != IrInst::Br && in.op != IrInst::CondBr) continue;
            for (size_t a = 0; a < in.args.size(); ++a)
                if (in.args[a].kind == IrOperand::Block && in.args[a].id >= 0 && static_cast<size_t>(in.args[a].id) < n && !normal[static_cast<size_t>(in.args[a].id)]) {
                    normal[static_cast<size_t>(in.args[a].id)] = true; work.push_back(in.args[a].id);
                }
        }
    }
    // the handler code: everything reachable from a handler entry
    for (size_t b = 0; b < n; ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            if (in.op != IrInst::TryBegin) continue;
            for (size_t a = 0; a < in.args.size(); ++a)
                if (in.args[a].kind == IrOperand::Block && in.args[a].id >= 0 && static_cast<size_t>(in.args[a].id) < n && !handler[static_cast<size_t>(in.args[a].id)]) {
                    handler[static_cast<size_t>(in.args[a].id)] = true; work.push_back(in.args[a].id);
                }
        }
    while (!work.empty()) {
        int b = work.back(); work.pop_back();
        const std::vector<int>& s = f.blocks[static_cast<size_t>(b)].succs;
        for (size_t k = 0; k < s.size(); ++k)
            if (s[k] >= 0 && static_cast<size_t>(s[k]) < n && !handler[static_cast<size_t>(s[k])]) { handler[static_cast<size_t>(s[k])] = true; work.push_back(s[k]); }
    }
    for (size_t b = 0; b < n; ++b) if (handler[b] && normal[b]) return false;   // a handler resumes the normal code
    for (size_t b = 0; b < n; ++b) {
        if (!handler[b]) continue;
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            for (size_t a = 0; a < in.args.size(); ++a)
                if (in.args[a].kind == IrOperand::Local && in.args[a].id >= 0 && static_cast<size_t>(in.args[a].id) < f.locals.size())
                    f.locals[static_cast<size_t>(in.args[a].id)].addressTaken = true;
        }
    }
    return true;
}

// the blocks reachable from a handler entry (a TryBegin's handler operands)
std::vector<bool> handlerBlocks(IrFunction& f)
{
    f.computeCfg();
    const size_t n = f.blocks.size();
    std::vector<bool> handler(n, false);
    std::vector<int> work;
    for (size_t b = 0; b < n; ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            if (in.op != IrInst::TryBegin) continue;
            for (size_t a = 0; a < in.args.size(); ++a)
                if (in.args[a].kind == IrOperand::Block && in.args[a].id >= 0 && static_cast<size_t>(in.args[a].id) < n && !handler[static_cast<size_t>(in.args[a].id)]) {
                    handler[static_cast<size_t>(in.args[a].id)] = true; work.push_back(in.args[a].id);
                }
        }
    while (!work.empty()) {
        int b = work.back(); work.pop_back();
        const std::vector<int>& s = f.blocks[static_cast<size_t>(b)].succs;
        for (size_t k = 0; k < s.size(); ++k)
            if (s[k] >= 0 && static_cast<size_t>(s[k]) < n && !handler[static_cast<size_t>(s[k])]) { handler[static_cast<size_t>(s[k])] = true; work.push_back(s[k]); }
    }
    return handler;
}

// after the SSA phase: a handler, entered by longjmp, may use only values it computes itself or values computed
// at the entry before the first try region begins (setjmp saves the registers holding them, nothing changes them
// later). A value computed after a setjmp would be read from a register longjmp has reset.
bool handlersSelfContained(IrFunction& f)
{
    std::vector<bool> handler = handlerBlocks(f);
    // the entry instructions before the first TryBegin
    size_t firstTry = 0;
    if (!f.blocks.empty()) {
        const std::vector<IrInst>& e = f.blocks[0].insts;
        while (firstTry < e.size() && e[firstTry].op != IrInst::TryBegin) ++firstTry;
    }
    std::set<int> allowed, forbidden;
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const int d = f.blocks[b].insts[i].dst;
            if (d < 0) continue;
            if (handler[b] || (b == 0 && i < firstTry)) allowed.insert(d); else forbidden.insert(d);
        }
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        if (!handler[b]) continue;
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            const IrInst& in = f.blocks[b].insts[i];
            for (size_t a = 0; a < in.args.size(); ++a)
                if (in.args[a].kind == IrOperand::Temp && (forbidden.count(in.args[a].id) || !allowed.count(in.args[a].id))) return false;
        }
    }
    return true;
}

} // namespace

void Optimizer::runSsa(IrFunction& f, std::vector<std::string>& log, std::string* ssaForm)
{
    if (m_level <= 0) return;
    int before = f.instCount();
    // SSA phase last: promote scalars, value-number over the dominator tree, fold, leave SSA, clean up
    if (!f.hasTry || pinHandlerLocals(f)) {
        // with try regions: no global value numbering (it would hand a handler a value computed after the
        // setjmp), and the result is checked; the function is kept as it was if a handler still needs one
        const bool guarded = f.hasTry;
        IrFunction saved;
        if (guarded) saved = f;
        SsaStats ss;
        toSsa(f, ss);
        log.push_back("    SSA construction: " + intToString(ss.promotedLocals) + " locals promoted, " + intToString(ss.phisInserted) + " phis");
        for (int round = 0; round < 6; ++round) {
            int c = 0, n;
            n = constantFold(f);   c += n; if (n) log.push_back("    [ssa] constant-fold: " + intToString(n));
            n = copyPropagation(f); c += n; if (n) log.push_back("    [ssa] copy-propagation: " + intToString(n));
            n = algebraic(f);      c += n; if (n) log.push_back("    [ssa] algebraic simplification: " + intToString(n));
            n = guarded ? 0 : gvn(f); c += n; if (n) log.push_back("    [ssa] global value numbering: " + intToString(n));
            n = simplifyPhis(f);   c += n; if (n) log.push_back("    [ssa] phi simplification: " + intToString(n));
            n = deadCode(f);       c += n; if (n) log.push_back("    [ssa] dead code elimination: " + intToString(n));
            n = simplifyCfg(f);    c += n; if (n) log.push_back("    [ssa] branch folding / block merging: " + intToString(n));
            if (!c) break;
        }
        if (m_level >= 2) {
            // in SSA form a value is assigned once, so invariance is exact: what inlining stored into a local copy
            // in every iteration (the callee's 'this') is now the value from outside the loop it always was
            int h = licm(f);
            if (h) {
                log.push_back("    [ssa] loop-invariant code motion: " + intToString(h));
                int n;
                n = copyPropagation(f); if (n) log.push_back("    [ssa] copy-propagation: " + intToString(n));
                n = deadCode(f);        if (n) log.push_back("    [ssa] dead code elimination: " + intToString(n));
            }
        }
        if (ssaForm) *ssaForm = printIr(f);
        fromSsa(f, ss);
        log.push_back("    SSA destruction: " + intToString(ss.phisRemoved) + " phis replaced by " + intToString(ss.copiesInserted) + " copies, " + intToString(ss.edgesSplit) + " critical edges split");
        if (guarded && !handlersSelfContained(f)) {
            f = saved;
            log.push_back("    SSA undone: a handler would read a value computed after its setjmp");
        }
    } else {
        log.push_back("    SSA skipped: a catch handler resumes the normal code");
    }
    level1(f, log);
    int after = f.instCount();
    log.push_back("    result: " + intToString(before) + " -> " + intToString(after) + " instructions in the final phase, " + intToString(static_cast<int>(f.blocks.size())) + " blocks");
}

} // namespace cppparse
