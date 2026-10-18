// Lower.cpp - linear-scan register allocation, frame layout and assembly emission.
#include "Lower.hpp"
#include "Optimizer.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace cppparse {

std::string mangle(const std::string& name)
{
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_') out += c;
        else if (c == ':' && i + 1 < name.size() && name[i + 1] == ':') { out += "__"; ++i; }
        else if (c == ' ' || c == ',' || c == '(' || c == ')') { if (!out.empty() && out[out.size() - 1] != '_') out += '_'; }
        else if (c == '*') out += 'P';
        else if (c == '&') out += 'R';
        else if (c == '~') out += 'D';
        else if (c == '<' || c == '>' || c == '[' || c == ']') out += '_';
        else if (c == '=') out += "eq";
        else if (c == '+') out += "pl";
        else if (c == '-') out += "mi";
        else if (c == '!') out += "nt";
        else if (c == '/') out += "dv";
        else if (c == '%') out += "rm";
        else if (c == '|') out += "or";
        else if (c == '^') out += "xr";
    }
    while (!out.empty() && out[out.size() - 1] == '_') out.erase(out.size() - 1);
    return out;
}

namespace {

const int kIntRegs = 6;    // R0..R5 allocatable, R6/R7 scratch
const int kFloatRegs = 6;

struct Interval {
    int  temp;
    int  start, end;
    bool isFloat;
    int  reg;          // -1 = spilled
    int  slot;         // frame offset when spilled
    bool crossesCall;
    Interval() : temp(-1), start(1 << 30), end(-1), isFloat(false), reg(-1), slot(0), crossesCall(false) {}
};

struct ByStart { bool operator()(const Interval* a, const Interval* b) const { return a->start != b->start ? a->start < b->start : a->temp < b->temp; } };

class FunctionLowerer {
public:
    FunctionLowerer(const IrFunction& f, LowerStats& stats)
        : m_f(f), m_stats(stats), m_frameSize(0), m_instIndex(0) {}

    std::string run();

private:
    struct Pos { int block; int index; };

    void computeLinear();
    void computeIntervals();
    void allocate();
    void layoutFrame();
    void emitFunction();
    void emitInst(const IrInst& in, int blockIndex, size_t instIndex);

    std::string use(const IrOperand& o, int scratch);
    std::string def(int temp, bool isFloat, int& spillOff);
    void        finish(int temp, int spillOff, bool isFloat, const std::string& reg);
    std::string sizeSuffix(const Type* t) const;
    bool        isFloatType(const Type* t) const { return t && (t->kind == Type::Float || t->kind == Type::Double); }
    bool        tempIsFloat(int temp) const { return isFloatType(m_f.typeOf(temp)); }
    std::string localRef(int local) const { return "[FP-" + intToString(m_localOffset[static_cast<size_t>(local)]) + "]"; }
    std::string label(int block) const { return m_fnLabel + "_B" + intToString(block); }
    void        line(const std::string& s) { m_out << "        " << s << "\n"; ++m_stats.instructions; }
    void        comment(const std::string& s) { m_out << "        ; " << s << "\n"; }

    const IrFunction&      m_f;
    LowerStats&            m_stats;
    std::ostringstream     m_out;
    std::string            m_fnLabel;
    std::vector<Pos>       m_linear;
    std::vector<int>       m_blockStart, m_blockEnd;
    std::map<int, Interval> m_intervals;
    std::vector<int>       m_calls;
    std::vector<int>       m_localOffset;
    int                    m_frameSize;
    int                    m_instIndex;
    std::vector<std::string> m_strings;
};

void FunctionLowerer::computeLinear()
{
    m_blockStart.assign(m_f.blocks.size(), 0);
    m_blockEnd.assign(m_f.blocks.size(), 0);
    int k = 0;
    for (size_t b = 0; b < m_f.blocks.size(); ++b) {
        m_blockStart[b] = k;
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) { Pos p; p.block = static_cast<int>(b); p.index = static_cast<int>(i); m_linear.push_back(p); ++k; }
        m_blockEnd[b] = k - 1;
    }
}

void FunctionLowerer::computeIntervals()
{
    for (size_t k = 0; k < m_linear.size(); ++k) {
        const IrInst& in = m_f.blocks[static_cast<size_t>(m_linear[k].block)].insts[static_cast<size_t>(m_linear[k].index)];
        int pos = static_cast<int>(k);
        if (in.dst >= 0) {
            Interval& iv = m_intervals[in.dst];
            iv.temp = in.dst; iv.isFloat = isFloatType(in.type);
            if (pos < iv.start) iv.start = pos;
            if (pos > iv.end) iv.end = pos;
        }
        for (size_t a = 0; a < in.args.size(); ++a) {
            if (in.args[a].kind != IrOperand::Temp) continue;
            Interval& iv = m_intervals[in.args[a].id];
            iv.temp = in.args[a].id;
            if (iv.start > pos) iv.start = pos;
            if (pos > iv.end) iv.end = pos;
            if (in.args[a].type && isFloatType(in.args[a].type)) iv.isFloat = true;
        }
        if (in.op == IrInst::Call || in.op == IrInst::VCall) m_calls.push_back(pos);
    }
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        if (!it->second.isFloat) it->second.isFloat = tempIsFloat(it->first);
    // an interval spans every block the value is live through (see X86Function::computeIntervals)
    std::vector<std::set<int> > liveIn, liveOut;
    m_f.computeLiveness(liveIn, liveOut);
    for (size_t b = 0; b < m_f.blocks.size(); ++b) {
        for (std::set<int>::const_iterator t = liveIn[b].begin(); t != liveIn[b].end(); ++t) {
            std::map<int, Interval>::iterator it = m_intervals.find(*t);
            if (it != m_intervals.end() && m_blockStart[b] < it->second.start) it->second.start = m_blockStart[b];
        }
        for (std::set<int>::const_iterator t = liveOut[b].begin(); t != liveOut[b].end(); ++t) {
            std::map<int, Interval>::iterator it = m_intervals.find(*t);
            if (it != m_intervals.end() && m_blockEnd[b] > it->second.end) it->second.end = m_blockEnd[b];
        }
    }
    // extend intervals over loops (back edges)
    for (size_t b = 0; b < m_f.blocks.size(); ++b)
        for (size_t s = 0; s < m_f.blocks[b].succs.size(); ++s) {
            int target = m_f.blocks[b].succs[s];
            if (target < 0 || static_cast<size_t>(target) >= m_f.blocks.size()) continue;
            if (m_blockStart[static_cast<size_t>(target)] > m_blockStart[b]) continue;   // forward edge
            int loopStart = m_blockStart[static_cast<size_t>(target)], loopEnd = m_blockEnd[b];
            for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) {
                Interval& iv = it->second;
                if (iv.start < loopStart && iv.end >= loopStart && iv.end < loopEnd) iv.end = loopEnd;
            }
        }
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        for (size_t c = 0; c < m_calls.size(); ++c)
            if (it->second.start < m_calls[c] && it->second.end > m_calls[c]) { it->second.crossesCall = true; break; }
}

void FunctionLowerer::allocate()
{
    std::vector<Interval*> order;
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) order.push_back(&it->second);
    std::sort(order.begin(), order.end(), ByStart());
    std::vector<Interval*> active;
    bool intFree[kIntRegs], fltFree[kFloatRegs];
    for (int i = 0; i < kIntRegs; ++i) intFree[i] = true;
    for (int i = 0; i < kFloatRegs; ++i) fltFree[i] = true;
    for (size_t k = 0; k < order.size(); ++k) {
        Interval* iv = order[k];
        // expire
        for (size_t a = 0; a < active.size();) {
            if (active[a]->end < iv->start) {
                if (active[a]->reg >= 0) { if (active[a]->isFloat) fltFree[active[a]->reg] = true; else intFree[active[a]->reg] = true; }
                active.erase(active.begin() + static_cast<long>(a));
            } else ++a;
        }
        if (iv->crossesCall) { iv->reg = -1; ++m_stats.spills; continue; }
        bool* freeRegs = iv->isFloat ? fltFree : intFree;
        int n = iv->isFloat ? kFloatRegs : kIntRegs;
        int got = -1;
        for (int r = 0; r < n; ++r) if (freeRegs[r]) { got = r; break; }
        if (got >= 0) { iv->reg = got; freeRegs[got] = false; active.push_back(iv); continue; }
        // spill the active interval of the same class that ends last
        Interval* victim = 0;
        for (size_t a = 0; a < active.size(); ++a)
            if (active[a]->isFloat == iv->isFloat && active[a]->reg >= 0 && (!victim || active[a]->end > victim->end)) victim = active[a];
        if (victim && victim->end > iv->end) {
            iv->reg = victim->reg;
            victim->reg = -1;
            ++m_stats.spills;
            active.push_back(iv);
        } else {
            iv->reg = -1;
            ++m_stats.spills;
        }
    }
}

void FunctionLowerer::layoutFrame()
{
    int off = 0;
    m_localOffset.assign(m_f.locals.size(), 0);
    for (size_t l = 0; l < m_f.locals.size(); ++l) {
        int size = m_f.locals[l].size > 0 ? m_f.locals[l].size : 8;
        int align = m_f.locals[l].align > 0 ? m_f.locals[l].align : 8;
        off = (off + size + align - 1) / align * align;
        m_localOffset[l] = off;
    }
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it)
        if (it->second.reg < 0) { off = (off + 8 + 7) / 8 * 8; it->second.slot = off; }
    m_frameSize = (off + 15) / 16 * 16;
    m_stats.frameSize = m_frameSize;
}

std::string FunctionLowerer::sizeSuffix(const Type* t) const
{
    if (!t) return "";
    int n = t->builtinSize();
    if (t->kind == Type::Enum) n = 4;
    if (n == 1) return ".b";
    if (n == 2) return ".h";
    if (n == 4) return ".w";
    return "";
}

// materialise an operand into a register; scratch 0 -> R6/F6, 1 -> R7/F7
std::string FunctionLowerer::use(const IrOperand& o, int scratch)
{
    bool fl = o.kind == IrOperand::FImm || (o.type && isFloatType(o.type)) || (o.kind == IrOperand::Temp && tempIsFloat(o.id));
    std::string sc = fl ? (scratch ? "F7" : "F6") : (scratch ? "R7" : "R6");
    switch (o.kind) {
    case IrOperand::Temp: {
        std::map<int, Interval>::iterator it = m_intervals.find(o.id);
        if (it == m_intervals.end()) { line("MOV " + sc + ", #0        ; undefined temporary"); return sc; }
        if (it->second.reg >= 0) return (it->second.isFloat ? "F" : "R") + intToString(it->second.reg);
        line(std::string(it->second.isFloat ? "FLD " : "LD ") + sc + ", [FP-" + intToString(it->second.slot) + "]");
        return sc;
    }
    case IrOperand::Imm: line("MOV " + sc + ", #" + intToString(static_cast<int>(o.ival))); return sc;
    case IrOperand::FImm: { std::ostringstream s; s << "FMOV " << sc << ", #" << o.fval; line(s.str()); return sc; }
    case IrOperand::Local: line("LEA " + sc + ", " + localRef(o.id)); return sc;
    case IrOperand::Global: line("LEA " + sc + ", " + mangle(o.name)); return sc;
    case IrOperand::Str: { m_strings.push_back(o.name); line("LEA " + sc + ", " + m_fnLabel + "_str" + intToString(static_cast<int>(m_strings.size() - 1))); return sc; }
    default: return sc;
    }
}

std::string FunctionLowerer::def(int temp, bool isFloat, int& spillOff)
{
    spillOff = 0;
    std::map<int, Interval>::iterator it = m_intervals.find(temp);
    if (it == m_intervals.end()) return isFloat ? "F6" : "R6";
    if (it->second.reg >= 0) return (it->second.isFloat ? "F" : "R") + intToString(it->second.reg);
    spillOff = it->second.slot;
    return it->second.isFloat ? "F6" : "R6";
}

void FunctionLowerer::finish(int temp, int spillOff, bool isFloat, const std::string& reg)
{
    (void)temp;
    if (spillOff > 0) line(std::string(isFloat ? "FST " : "ST ") + "[FP-" + intToString(spillOff) + "], " + reg);
}

void FunctionLowerer::emitInst(const IrInst& in, int blockIndex, size_t instIndex)
{
    (void)instIndex;
    comment(in.str());
    bool dstFloat = in.dst >= 0 && tempIsFloat(in.dst);
    int spill = 0;
    switch (in.op) {
    case IrInst::Const: { std::string rd = def(in.dst, false, spill); line("MOV " + rd + ", #" + intToString(static_cast<int>(in.args[0].ival))); finish(in.dst, spill, false, rd); break; }
    case IrInst::FConst: { std::string rd = def(in.dst, true, spill); std::ostringstream s; s << "FMOV " << rd << ", #" << in.args[0].fval; line(s.str()); finish(in.dst, spill, true, rd); break; }
    case IrInst::StrConst: { std::string rd = def(in.dst, false, spill); m_strings.push_back(in.args[0].name); line("LEA " + rd + ", " + m_fnLabel + "_str" + intToString(static_cast<int>(m_strings.size() - 1))); finish(in.dst, spill, false, rd); break; }
    case IrInst::Copy: { std::string rs = use(in.args[0], 1); std::string rd = def(in.dst, dstFloat, spill); if (rd != rs) line(std::string(dstFloat ? "FMOV " : "MOV ") + rd + ", " + rs); finish(in.dst, spill, dstFloat, rd); break; }
    case IrInst::LoadLocal: { std::string rd = def(in.dst, dstFloat, spill); line(std::string(dstFloat ? "FLD" : "LD") + sizeSuffix(in.type) + " " + rd + ", " + localRef(in.args[0].id)); finish(in.dst, spill, dstFloat, rd); break; }
    case IrInst::StoreLocal: { bool fl = in.args[1].kind == IrOperand::FImm || (in.args[1].kind == IrOperand::Temp && tempIsFloat(in.args[1].id)); std::string rv = use(in.args[1], 0); line(std::string(fl ? "FST" : "ST") + sizeSuffix(in.type) + " " + localRef(in.args[0].id) + ", " + rv); break; }
    case IrInst::AddrLocal: { std::string rd = def(in.dst, false, spill); line("LEA " + rd + ", " + localRef(in.args[0].id)); finish(in.dst, spill, false, rd); break; }
    case IrInst::LoadGlobal: { std::string rd = def(in.dst, dstFloat, spill); line(std::string(dstFloat ? "FLD" : "LD") + sizeSuffix(in.type) + " " + rd + ", [" + mangle(in.args[0].name) + "]"); finish(in.dst, spill, dstFloat, rd); break; }
    case IrInst::StoreGlobal: { bool fl = in.args[1].kind == IrOperand::FImm || (in.args[1].kind == IrOperand::Temp && tempIsFloat(in.args[1].id)); std::string rv = use(in.args[1], 0); line(std::string(fl ? "FST" : "ST") + sizeSuffix(in.type) + " [" + mangle(in.args[0].name) + "], " + rv); break; }
    case IrInst::AddrGlobal: { std::string rd = def(in.dst, false, spill); line("LEA " + rd + ", " + mangle(in.args[0].name)); finish(in.dst, spill, false, rd); break; }
    case IrInst::LoadMem: { std::string ra = use(in.args[0], 1); std::string rd = def(in.dst, dstFloat, spill); line(std::string(dstFloat ? "FLD" : "LD") + sizeSuffix(in.type) + " " + rd + ", [" + ra + "]"); finish(in.dst, spill, dstFloat, rd); break; }
    case IrInst::StoreMem: { std::string ra = use(in.args[0], 1); bool fl = in.args[1].kind == IrOperand::FImm || (in.args[1].kind == IrOperand::Temp && tempIsFloat(in.args[1].id)); std::string rv = use(in.args[1], 0); line(std::string(fl ? "FST" : "ST") + sizeSuffix(in.type) + " [" + ra + "], " + rv); break; }
    case IrInst::AddrField: { std::string rb = use(in.args[0], 1); std::string rd = def(in.dst, false, spill); line("LEA " + rd + ", [" + rb + "+" + intToString(static_cast<int>(in.extra)) + "]      ; " + in.sub); finish(in.dst, spill, false, rd); break; }
    case IrInst::AddrIndex: {
        std::string rb = use(in.args[0], 1);
        std::string ri = use(in.args[1], 0);
        long size = in.extra > 0 ? in.extra : 1;
        bool pow2 = (size & (size - 1)) == 0;
        int k = 0; long v = size; while (v > 1) { v >>= 1; ++k; }
        if (size == 1) {}
        else if (pow2) line("SHL R6, " + ri + ", #" + intToString(k));
        else line("MUL R6, " + ri + ", #" + intToString(static_cast<int>(size)));
        std::string rd = def(in.dst, false, spill);
        line("ADD " + rd + ", " + rb + ", " + (size == 1 ? ri : std::string("R6")));
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Bin: {
        static const char* const names[][2] = { {"add","ADD"}, {"sub","SUB"}, {"mul","MUL"}, {"sdiv","IDIV"}, {"udiv","UDIV"}, {"srem","IREM"}, {"urem","UREM"},
                                                {"shl","SHL"}, {"ashr","SAR"}, {"lshr","SHR"}, {"and","AND"}, {"or","OR"}, {"xor","XOR"},
                                                {"fadd","FADD"}, {"fsub","FSUB"}, {"fmul","FMUL"}, {"fdiv","FDIV"}, {0, 0} };
        std::string mn = "ADD";
        for (int i = 0; names[i][0]; ++i) if (in.sub == names[i][0]) mn = names[i][1];
        std::string ra = use(in.args[0], 0), rb = use(in.args[1], 1);
        std::string rd = def(in.dst, dstFloat, spill);
        line(mn + " " + rd + ", " + ra + ", " + rb);
        finish(in.dst, spill, dstFloat, rd);
        break;
    }
    case IrInst::Un: { std::string ra = use(in.args[0], 1); std::string rd = def(in.dst, dstFloat, spill); line(std::string(in.sub == "fneg" ? "FNEG" : in.sub == "not" ? "NOT" : "NEG") + " " + rd + ", " + ra); finish(in.dst, spill, dstFloat, rd); break; }
    case IrInst::Cmp: {
        bool fl = in.sub[0] == 'f';
        std::string ra = use(in.args[0], 0), rb = use(in.args[1], 1);
        line(std::string(fl ? "FCMP " : "CMP ") + ra + ", " + rb);
        std::string cc;
        std::string s = fl ? in.sub.substr(1) : in.sub;
        if (s == "eq") cc = "EQ"; else if (s == "ne") cc = "NE";
        else if (s == "slt" || s == "lt") cc = "LT"; else if (s == "sle" || s == "le") cc = "LE";
        else if (s == "sgt" || s == "gt") cc = "GT"; else if (s == "sge" || s == "ge") cc = "GE";
        else if (s == "ult") cc = "B"; else if (s == "ule") cc = "BE"; else if (s == "ugt") cc = "A"; else cc = "AE";
        std::string rd = def(in.dst, false, spill);
        line("SET" + cc + " " + rd);
        finish(in.dst, spill, false, rd);
        break;
    }
    case IrInst::Conv: {
        std::string ra = use(in.args[0], 1);
        std::string rd = def(in.dst, dstFloat, spill);
        std::string mn;
        if (in.sub == "sext") mn = "SEXT" + sizeSuffix(in.args[0].type);
        else if (in.sub == "zext") mn = "ZEXT" + sizeSuffix(in.args[0].type);
        else if (in.sub == "trunc") mn = "TRUNC" + sizeSuffix(in.type);
        else if (in.sub == "sitofp") mn = "CVTI2F"; else if (in.sub == "uitofp") mn = "CVTU2F";
        else if (in.sub == "fptosi") mn = "CVTF2I"; else if (in.sub == "fpext" || in.sub == "fptrunc") mn = "CVTF2F";
        else mn = "MOV";
        if (mn == "MOV" && rd == ra) { finish(in.dst, spill, dstFloat, rd); break; }
        line(mn + " " + rd + ", " + ra);
        finish(in.dst, spill, dstFloat, rd);
        break;
    }
    case IrInst::Call: case IrInst::VCall: {
        // push all arguments right to left, pop the first four into the argument registers
        int ints = 0, flts = 0;
        for (size_t a = in.args.size(); a > 0; --a) {
            const IrOperand& o = in.args[a - 1];
            bool fl = o.kind == IrOperand::FImm || (o.kind == IrOperand::Temp && tempIsFloat(o.id)) || (o.type && isFloatType(o.type) && o.kind != IrOperand::Imm);
            std::string r = use(o, 0);
            line(std::string(fl ? "FPUSH " : "PUSH ") + r);
        }
        int stackArgs = 0;
        for (size_t a = 0; a < in.args.size(); ++a) {
            const IrOperand& o = in.args[a];
            bool fl = o.kind == IrOperand::FImm || (o.kind == IrOperand::Temp && tempIsFloat(o.id)) || (o.type && isFloatType(o.type) && o.kind != IrOperand::Imm);
            if (fl) { if (flts < 4) line("FPOP F" + intToString(flts++)); else ++stackArgs; }
            else { if (ints < 4) line("POP R" + intToString(ints++)); else ++stackArgs; }
        }
        if (in.op == IrInst::VCall) {
            line("LD R7, [R0+0]           ; vptr");
            line("LD R7, [R7+" + intToString(static_cast<int>(in.extra * 8)) + "]          ; slot " + intToString(static_cast<int>(in.extra)) + " " + in.sub);
            line("CALL R7");
        } else {
            line("CALL " + mangle(in.sub));
        }
        if (stackArgs) line("ADD SP, SP, #" + intToString(stackArgs * 8));
        if (in.dst >= 0) {
            std::string rd = def(in.dst, dstFloat, spill);
            std::string src = dstFloat ? "F0" : "R0";
            if (rd != src) line(std::string(dstFloat ? "FMOV " : "MOV ") + rd + ", " + src);
            finish(in.dst, spill, dstFloat, rd);
        }
        break;
    }
    case IrInst::Br: {
        int target = in.args[0].id;
        if (target != blockIndex + 1) line("JMP " + label(target));
        break;
    }
    case IrInst::CondBr: {
        std::string rc = use(in.args[0], 0);
        line("CMP " + rc + ", #0");
        line("JNE " + label(in.args[1].id));
        if (in.args[2].id != blockIndex + 1) line("JMP " + label(in.args[2].id));
        break;
    }
    case IrInst::Ret: {
        if (!in.args.empty()) {
            const IrOperand& o = in.args[0];
            bool fl = o.kind == IrOperand::FImm || (o.kind == IrOperand::Temp && tempIsFloat(o.id));
            std::string r = use(o, 0);
            if (r != (fl ? "F0" : "R0")) line(std::string(fl ? "FMOV F0, " : "MOV R0, ") + r);
        }
        line("LEAVE");
        line("RET");
        break;
    }
    case IrInst::Throw: {
        if (!in.args.empty()) { std::string r = use(in.args[0], 0); if (r != "R0") line("MOV R0, " + r); }
        line("CALL __throw");
        break;
    }
    case IrInst::TryBegin: { std::string hs; for (size_t a = 0; a < in.args.size(); ++a) hs += (a ? ", " : "") + label(in.args[a].id); line("TRY " + hs); break; }
    case IrInst::TryEnd: line("ENDTRY"); break;
    case IrInst::CatchEnd: line("ENDCATCH"); break;
    case IrInst::CatchParam: { std::string rd = def(in.dst, false, spill); line("MOV " + rd + ", R0            ; exception object (" + in.sub + ")"); finish(in.dst, spill, false, rd); break; }
    case IrInst::Unreachable: line("TRAP                  ; " + in.sub); break;
    default: break;
    }
}

void FunctionLowerer::emitFunction()
{
    m_out << mangle(m_f.name) << ":\n";
    m_out << "        ; " << m_f.name << "\n";
    m_out << "        ; frame " << m_frameSize << " bytes";
    for (size_t l = 0; l < m_f.locals.size(); ++l) m_out << (l ? ", " : ": ") << m_f.locals[l].name << "@[FP-" << m_localOffset[l] << "]";
    m_out << "\n";
    int regs = 0, spills = 0;
    for (std::map<int, Interval>::iterator it = m_intervals.begin(); it != m_intervals.end(); ++it) { if (it->second.reg >= 0) ++regs; else ++spills; }
    m_out << "        ; " << m_intervals.size() << " temporaries: " << regs << " in registers, " << spills << " in frame slots\n";
    line("ENTER #" + intToString(m_frameSize));
    // incoming parameters
    int ints = 0, flts = 0, stackIdx = 0;
    for (int p = 0; p < m_f.paramCount; ++p) {
        const IrLocal& l = m_f.locals[static_cast<size_t>(p)];
        bool fl = isFloatType(l.type);
        if (fl) {
            if (flts < 4) line("FST" + sizeSuffix(l.type) + " " + localRef(p) + ", F" + intToString(flts++) + "      ; param " + l.name);
            else { line("FLD F6, [FP+" + intToString(16 + 8 * stackIdx++) + "]"); line("FST " + localRef(p) + ", F6      ; param " + l.name); }
        } else {
            if (ints < 4) line("ST" + sizeSuffix(l.type && l.type->kind != Type::Reference ? l.type : 0) + " " + localRef(p) + ", R" + intToString(ints++) + "      ; param " + l.name);
            else { line("LD R6, [FP+" + intToString(16 + 8 * stackIdx++) + "]"); line("ST " + localRef(p) + ", R6      ; param " + l.name); }
        }
    }
    for (size_t b = 0; b < m_f.blocks.size(); ++b) {
        m_out << label(static_cast<int>(b)) << ":" << (m_f.blocks[b].label.empty() ? "" : "        ; " + m_f.blocks[b].label) << "\n";
        for (size_t i = 0; i < m_f.blocks[b].insts.size(); ++i) emitInst(m_f.blocks[b].insts[i], static_cast<int>(b), i);
    }
    for (size_t s = 0; s < m_strings.size(); ++s) m_out << m_fnLabel << "_str" << s << ":   .asciz " << m_strings[s] << "\n";
    m_out << "\n";
}

std::string FunctionLowerer::run()
{
    m_fnLabel = mangle(m_f.name);
    computeLinear();
    computeIntervals();
    allocate();
    layoutFrame();
    emitFunction();
    return m_out.str();
}

} // namespace

std::string lowerFunction(const IrFunction& f, LowerStats& stats)
{
    FunctionLowerer L(f, stats);
    return L.run();
}

} // namespace cppparse
