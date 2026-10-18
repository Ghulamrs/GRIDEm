// Coff.cpp - version 5: MASM-subset assembler and COFF object writer (x86-64).
//
// Encoding notes (Intel SDM volume 2):
//   REX = 0100WRXB   W: 64-bit operand   R: ModRM.reg bit 3   X: SIB.index bit 3   B: ModRM.rm / SIB.base bit 3
//   ModRM = mod(2) reg(3) rm(3); rm=100 means a SIB byte follows; mod=00 rm=101 means RIP+disp32
//   SIB = scale(2) index(3) base(3); index=100 means no index; base=101 with mod=00 means disp32 and no base
//   mandatory prefixes 66/F2/F3 go before REX; 0F is the two-byte opcode escape
// Relocations: IMAGE_REL_AMD64_REL32 (+n when n immediate bytes follow the field), IMAGE_REL_AMD64_ADDR64,
// IMAGE_REL_AMD64_ADDR32NB (the function table in .pdata).
// Unwind info: a PROC FRAME's prologue directives become its UNWIND_INFO in .xdata and a RUNTIME_FUNCTION entry
// (begin, end, unwind info) in .pdata, as ml64 writes them; a COMDAT function's go into associative COMDAT sections.
//
// Strict C++98.
#include "Coff.hpp"
#include "Parallel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace cppparse {

namespace {


std::string trimWs(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lowerText(const std::string& s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i) r[i] = static_cast<char>(tolower(static_cast<unsigned char>(r[i])));
    return r;
}

std::string upperText(const std::string& s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i) r[i] = static_cast<char>(toupper(static_cast<unsigned char>(r[i])));
    return r;
}

std::string numText(long v) { std::ostringstream s; s << v; return s.str(); }

bool isIdentStart(char c) { return isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@' || c == '$' || c == '?'; }
bool isIdentChar(char c) { return isIdentStart(c) || isdigit(static_cast<unsigned char>(c)); }

// splits operands on commas that are outside brackets
void splitOperands(const std::string& text, std::vector<std::string>& out)
{
    int depth = 0;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '[') ++depth;
        else if (c == ']') --depth;
        if (c == ',' && depth == 0) { out.push_back(trimWs(cur)); cur.clear(); }
        else cur += c;
    }
    if (!trimWs(cur).empty()) out.push_back(trimWs(cur));
}

// unsigned 64-bit value kept in two halves (C++98 has no 64-bit integer type)
struct U64 {
    unsigned long lo, hi;
    U64() : lo(0), hi(0) {}
};

void u64MulAdd(U64& v, unsigned long mul, unsigned long add)   // v = v * mul + add, mul <= 16
{
    unsigned long limb[4] = { v.lo & 0xFFFFUL, (v.lo >> 16) & 0xFFFFUL, v.hi & 0xFFFFUL, (v.hi >> 16) & 0xFFFFUL };
    unsigned long carry = add;
    for (int i = 0; i < 4; ++i) {
        unsigned long t = limb[i] * mul + carry;
        limb[i] = t & 0xFFFFUL;
        carry = t >> 16;
    }
    v.lo = limb[0] | (limb[1] << 16);
    v.hi = limb[2] | (limb[3] << 16);
}

void u64Negate(U64& v)
{
    v.lo = ~v.lo; v.hi = ~v.hi;
    unsigned long lo = (v.lo + 1) & 0xFFFFFFFFUL;
    if (lo == 0) v.hi = (v.hi + 1) & 0xFFFFFFFFUL;
    v.lo = lo;
}

// decimal, or hexadecimal with a trailing 'h'
bool parseU64(const std::string& text, U64& out)
{
    std::string t = lowerText(trimWs(text));
    bool neg = false;
    if (!t.empty() && (t[0] == '-' || t[0] == '+')) { neg = t[0] == '-'; t = t.substr(1); }
    if (t.empty()) return false;
    U64 v;
    if (t[t.size() - 1] == 'h') {
        for (size_t i = 0; i + 1 < t.size(); ++i) {
            char c = t[i];
            unsigned long d;
            if (c >= '0' && c <= '9') d = static_cast<unsigned long>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned long>(c - 'a' + 10);
            else return false;
            u64MulAdd(v, 16, d);
        }
    } else {
        for (size_t i = 0; i < t.size(); ++i) {
            if (!isdigit(static_cast<unsigned char>(t[i]))) return false;
            u64MulAdd(v, 10, static_cast<unsigned long>(t[i] - '0'));
        }
    }
    if (neg) u64Negate(v);
    out = v;
    return true;
}

bool parseLong(const std::string& text, long& out)
{
    U64 v;
    if (!parseU64(text, v)) return false;
    // accept values that fit in a signed or unsigned 32-bit field
    if (v.hi != 0 && v.hi != 0xFFFFFFFFUL) return false;
    out = static_cast<long>(static_cast<unsigned int>(v.lo));
    return true;
}

bool isNumberText(const std::string& t)
{
    U64 v;
    return !t.empty() && (isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-' || t[0] == '+') && parseU64(t, v);
}


struct RegInfo { const char* name; int num; int size; };

const RegInfo kRegs[] = {
    {"rax",0,8},{"rcx",1,8},{"rdx",2,8},{"rbx",3,8},{"rsp",4,8},{"rbp",5,8},{"rsi",6,8},{"rdi",7,8},
    {"r8",8,8},{"r9",9,8},{"r10",10,8},{"r11",11,8},{"r12",12,8},{"r13",13,8},{"r14",14,8},{"r15",15,8},
    {"eax",0,4},{"ecx",1,4},{"edx",2,4},{"ebx",3,4},{"esp",4,4},{"ebp",5,4},{"esi",6,4},{"edi",7,4},
    {"r8d",8,4},{"r9d",9,4},{"r10d",10,4},{"r11d",11,4},{"r12d",12,4},{"r13d",13,4},{"r14d",14,4},{"r15d",15,4},
    {"ax",0,2},{"cx",1,2},{"dx",2,2},{"bx",3,2},{"sp",4,2},{"bp",5,2},{"si",6,2},{"di",7,2},
    {"r8w",8,2},{"r9w",9,2},{"r10w",10,2},{"r11w",11,2},{"r12w",12,2},{"r13w",13,2},{"r14w",14,2},{"r15w",15,2},
    {"al",0,1},{"cl",1,1},{"dl",2,1},{"bl",3,1},{"spl",4,1},{"bpl",5,1},{"sil",6,1},{"dil",7,1},
    {"r8b",8,1},{"r9b",9,1},{"r10b",10,1},{"r11b",11,1},{"r12b",12,1},{"r13b",13,1},{"r14b",14,1},{"r15b",15,1},
    {"r8l",8,1},{"r9l",9,1},{"r10l",10,1},{"r11l",11,1},{"r12l",12,1},{"r13l",13,1},{"r14l",14,1},{"r15l",15,1}
};

bool lookupReg(const std::string& lname, int& num, int& size)
{
    for (size_t i = 0; i < sizeof(kRegs) / sizeof(kRegs[0]); ++i)
        if (lname == kRegs[i].name) { num = kRegs[i].num; size = kRegs[i].size; return true; }
    return false;
}

struct Operand {
    enum Kind { None, Reg, Xmm, Imm, Mem, Sym };
    Kind        kind;
    int         reg;      // Reg/Xmm number 0..15
    int         size;     // Reg: 1/2/4/8, Xmm: 16, Mem: 1/2/4/8/16 or 0 when not given
    long        imm;
    int         base, index, scale;   // Mem; -1 when absent
    long        disp;
    std::string symbol;   // Mem: RIP-relative symbol; Sym: the name
    Operand() : kind(None), reg(-1), size(0), imm(0), base(-1), index(-1), scale(1), disp(0) {}
    bool isReg() const { return kind == Reg; }
    bool isMem() const { return kind == Mem; }
    bool isRm()  const { return kind == Reg || kind == Mem; }
};

bool parseMemory(const std::string& innerText, Operand& op, std::string& err)
{
    // pieces separated by + or -, each a register, register*scale, number or symbol
    std::string inner = trimWs(innerText);
    size_t i = 0;
    bool first = true;
    while (i < inner.size()) {
        int sign = 1;
        while (i < inner.size() && isspace(static_cast<unsigned char>(inner[i]))) ++i;
        if (i < inner.size() && (inner[i] == '+' || inner[i] == '-')) { sign = inner[i] == '-' ? -1 : 1; ++i; }
        else if (!first) { err = "bad memory operand [" + inner + "]"; return false; }
        while (i < inner.size() && isspace(static_cast<unsigned char>(inner[i]))) ++i;
        size_t s = i;
        while (i < inner.size() && inner[i] != '+' && inner[i] != '-') ++i;
        std::string piece = trimWs(inner.substr(s, i - s));
        first = false;
        if (piece.empty()) { err = "bad memory operand [" + inner + "]"; return false; }
        std::string lp = lowerText(piece);
        size_t star = lp.find('*');
        int num, size;
        if (star != std::string::npos) {
            std::string r = trimWs(lp.substr(0, star));
            long sc = 0;
            if (!lookupReg(r, num, size) || size != 8 || !parseLong(lp.substr(star + 1), sc) || (sc != 1 && sc != 2 && sc != 4 && sc != 8) || op.index >= 0) {
                err = "bad index in [" + inner + "]"; return false;
            }
            op.index = num; op.scale = static_cast<int>(sc);
        } else if (lookupReg(lp, num, size)) {
            if (size != 8) { err = "memory operand needs 64-bit registers: [" + inner + "]"; return false; }
            if (op.base < 0) op.base = num;
            else if (op.index < 0) { op.index = num; op.scale = 1; }
            else { err = "too many registers in [" + inner + "]"; return false; }
        } else if (isNumberText(lp)) {
            long v;
            if (!parseLong(lp, v)) { err = "bad displacement in [" + inner + "]"; return false; }
            op.disp += sign * v;
        } else if (isIdentStart(piece[0])) {
            if (!op.symbol.empty()) { err = "two symbols in [" + inner + "]"; return false; }
            op.symbol = piece;
        } else { err = "bad memory operand [" + inner + "]"; return false; }
    }
    if (!op.symbol.empty() && (op.base >= 0 || op.index >= 0)) { err = "symbol with registers is not RIP-addressable: [" + inner + "]"; return false; }
    if (op.index == 4) { err = "rsp cannot be an index register: [" + inner + "]"; return false; }
    return true;
}

bool parseOperand(const std::string& text, Operand& op, std::string& err)
{
    std::string t = trimWs(text);
    std::string l = lowerText(t);
    int num, size;
    if (lookupReg(l, num, size)) { op.kind = Operand::Reg; op.reg = num; op.size = size; return true; }
    if (l.size() >= 4 && l.compare(0, 3, "xmm") == 0 && isdigit(static_cast<unsigned char>(l[3]))) {
        long n = atol(l.c_str() + 3);
        if (n < 0 || n > 15) { err = "bad register " + t; return false; }
        op.kind = Operand::Xmm; op.reg = static_cast<int>(n); op.size = 16; return true;
    }
    if (isNumberText(l)) {
        if (!parseLong(l, op.imm)) { err = "immediate does not fit in 32 bits: " + t; return false; }
        op.kind = Operand::Imm; return true;
    }
    size_t br = l.find('[');
    if (br != std::string::npos) {
        if (l[l.size() - 1] != ']') { err = "bad operand " + t; return false; }
        std::string pre = trimWs(l.substr(0, br));
        op.kind = Operand::Mem;
        if (!pre.empty()) {
            if (pre == "byte ptr") op.size = 1;
            else if (pre == "word ptr") op.size = 2;
            else if (pre == "dword ptr" || pre == "real4 ptr") op.size = 4;
            else if (pre == "qword ptr" || pre == "real8 ptr") op.size = 8;
            else if (pre == "xmmword ptr") op.size = 16;
            else { err = "bad size prefix " + t; return false; }
        }
        return parseMemory(t.substr(br + 1, t.size() - br - 2), op, err);
    }
    if (l.compare(0, 7, "offset ") == 0) t = trimWs(t.substr(7));
    if (!t.empty() && isIdentStart(t[0])) {
        for (size_t i = 0; i < t.size(); ++i) if (!isIdentChar(t[i])) { err = "bad operand " + t; return false; }
        op.kind = Operand::Sym; op.symbol = t; return true;
    }
    err = "bad operand " + t;
    return false;
}


enum { RelAddr64 = 0x0001, RelAddr32Nb = 0x0003, RelRel32 = 0x0004 };   // ADDR32NB: image-relative (.pdata entries)

struct Reloc {
    unsigned long  offset;    // within the section (chunk-relative until layout)
    std::string    symbol;
    unsigned short type;
};

struct Fixup {                // rel32 to a label of the same function
    unsigned long at;         // offset of the displacement field (4 bytes, or 1 byte once shortened)
    std::string   label;
    int           line;
    bool          jump;       // jmp/jcc (may be shortened) rather than call
    bool          cond;       // jcc
    bool          shortForm;
    unsigned long opcodeAt;   // offset of the first opcode byte (jumps only)
};

// one prologue step of a PROC FRAME, from its unwind directive: where the instruction before it ends, and what it did
struct UnwindStep {
    enum Op { Push, Alloc, SaveXmm };
    Op            op;
    int           reg;        // Push: register number; SaveXmm: xmm number
    unsigned long value;      // Alloc: bytes; SaveXmm: offset from rsp after the prologue
    unsigned long offset;     // code offset just after the instruction
};

struct Chunk {
    std::vector<unsigned char>                bytes;
    std::vector<UnwindStep>                   unwind;     // .pushreg / .allocstack / .savexmm128, in prologue order
    long                                      prologEnd;  // .endprolog: code offset, -1 when absent
    std::vector<std::vector<std::string> >    ehRanges;   // .ehrange begin, end, landing pad (labels of this function)
    std::vector<Reloc>                        relocs;
    std::vector<Fixup>                        fixups;
    std::map<std::string, unsigned long>      labels;    // local labels
    std::vector<std::pair<std::string, unsigned long> > symbols;   // named definitions (PROC names, data labels)
    std::vector<unsigned long>                padStarts;  // where ALIGN padding begins: a data item ends there
    unsigned long                             maxAlign;   // the largest ALIGN (or COMDAT marker alignment): the section's alignment
    std::vector<std::string>                  errors;
    int                                       instructions;
    Chunk() : prologEnd(-1), maxAlign(1), instructions(0) {}
};


class Encoder {
public:
    explicit Encoder(Chunk& c) : m_c(c) {}

    bool codeLine(const std::string& text, int line);
    bool dataLine(const std::string& text, int line);
    void finish();   // resolves the local fixups; unresolved labels become REL32 relocations

private:
    unsigned long pos() const { return static_cast<unsigned long>(m_c.bytes.size()); }
    void byte(int b) { m_c.bytes.push_back(static_cast<unsigned char>(b & 0xFF)); }
    void word(long v) { byte(static_cast<int>(v)); byte(static_cast<int>(v >> 8)); }
    void dword(long v) { for (int i = 0; i < 4; ++i) byte(static_cast<int>(v >> (8 * i))); }
    void qword(const U64& v) { dword(static_cast<long>(v.lo)); dword(static_cast<long>(v.hi)); }
    void imm(long v, int bytes) { for (int i = 0; i < bytes; ++i) byte(static_cast<int>(v >> (8 * i))); }
    bool fail(const std::string& what, int line) { m_c.errors.push_back("line " + numText(line) + ": " + what); return false; }

    bool instruction(const std::string& mn, std::vector<Operand>& ops, int line);
    bool unwindDirective(const std::string& mn, const std::vector<std::string>& opText, int line);
    void emitRm(int prefix, const unsigned char* op, int opLen, int regField, const Operand& rm, bool w, int trailing, bool forceRex);
    void rel32(const std::string& label, int line);
    void jump(int opcode, const std::string& label, int line);   // long form now, shortened in finish() when possible
    void shrinkJumps();
    static bool fitsImm8(long v) { return v >= -128 && v <= 127; }
    static bool byteRegsNeedRex(const std::vector<Operand>& ops)
    {
        for (size_t i = 0; i < ops.size(); ++i) if (ops[i].kind == Operand::Reg && ops[i].size == 1 && ops[i].reg >= 4 && ops[i].reg <= 7) return true;
        return false;
    }
    static int condCode(const std::string& cc);

    Chunk& m_c;
};

int Encoder::condCode(const std::string& cc)
{
    if (cc == "o") return 0;  if (cc == "no") return 1;
    if (cc == "b" || cc == "c" || cc == "nae") return 2;  if (cc == "ae" || cc == "nb" || cc == "nc") return 3;
    if (cc == "e" || cc == "z") return 4;  if (cc == "ne" || cc == "nz") return 5;
    if (cc == "be" || cc == "na") return 6;  if (cc == "a" || cc == "nbe") return 7;
    if (cc == "s") return 8;  if (cc == "ns") return 9;
    if (cc == "p" || cc == "pe") return 10;  if (cc == "np" || cc == "po") return 11;
    if (cc == "l" || cc == "nge") return 12;  if (cc == "ge" || cc == "nl") return 13;
    if (cc == "le" || cc == "ng") return 14;  if (cc == "g" || cc == "nle") return 15;
    return -1;
}

// prefix (0, 0x66, 0xF2, 0xF3), opcode bytes, ModRM.reg field, the r/m operand, REX.W, bytes of immediate that
// follow (for the relocation type of RIP-relative operands), REX forced for spl/bpl/sil/dil
void Encoder::emitRm(int prefix, const unsigned char* op, int opLen, int regField, const Operand& rm, bool w, int trailing, bool forceRex)
{
    int rex = 0;
    if (w) rex |= 8;
    if (regField >= 8) rex |= 4;
    if (rm.kind == Operand::Reg || rm.kind == Operand::Xmm) { if (rm.reg >= 8) rex |= 1; }
    else { if (rm.index >= 8) rex |= 2; if (rm.base >= 8) rex |= 1; }
    if (prefix) byte(prefix);
    if (rex || forceRex) byte(0x40 | rex);
    for (int i = 0; i < opLen; ++i) byte(op[i]);
    int r = regField & 7;
    if (rm.kind == Operand::Reg || rm.kind == Operand::Xmm) { byte(0xC0 | (r << 3) | (rm.reg & 7)); return; }

    if (!rm.symbol.empty()) {                       // RIP-relative
        byte((r << 3) | 5);
        Reloc rl; rl.offset = pos(); rl.symbol = rm.symbol; rl.type = static_cast<unsigned short>(RelRel32 + trailing);
        m_c.relocs.push_back(rl);
        dword(rm.disp);
        return;
    }
    int scaleBits = rm.scale == 8 ? 3 : rm.scale == 4 ? 2 : rm.scale == 2 ? 1 : 0;
    if (rm.base < 0 && rm.index < 0) {               // absolute disp32 (SIB with no base and no index)
        byte((r << 3) | 4); byte(0x25); dword(rm.disp); return;
    }
    if (rm.base < 0) {                               // index only: mod=00, base=101 means disp32 without base
        byte((r << 3) | 4); byte((scaleBits << 6) | ((rm.index & 7) << 3) | 5); dword(rm.disp); return;
    }
    int baseLow = rm.base & 7;
    bool needSib = rm.index >= 0 || baseLow == 4;    // rsp/r12 as base always need a SIB
    int mod;
    if (rm.disp == 0 && baseLow != 5) mod = 0;       // rbp/r13 need a displacement
    else if (fitsImm8(rm.disp)) mod = 1;
    else mod = 2;
    if (needSib) {
        byte((mod << 6) | (r << 3) | 4);
        byte((scaleBits << 6) | ((rm.index < 0 ? 4 : (rm.index & 7)) << 3) | baseLow);
    } else {
        byte((mod << 6) | (r << 3) | baseLow);
    }
    if (mod == 1) byte(static_cast<int>(rm.disp));
    else if (mod == 2) dword(rm.disp);
}

void Encoder::rel32(const std::string& label, int line)
{
    Fixup f; f.at = pos(); f.label = label; f.line = line; f.jump = false; f.cond = false; f.shortForm = false; f.opcodeAt = pos();
    m_c.fixups.push_back(f);
    dword(0);
}

// opcode: 0xE9 for jmp, 0x80+cc for jcc (emitted as 0F 8x)
void Encoder::jump(int opcode, const std::string& label, int line)
{
    Fixup f; f.opcodeAt = pos(); f.label = label; f.line = line; f.jump = true; f.cond = opcode != 0xE9; f.shortForm = false;
    if (f.cond) { byte(0x0F); byte(opcode); } else byte(0xE9);
    f.at = pos();
    m_c.fixups.push_back(f);
    dword(0);
}

// turns long jumps whose target is within -128..127 bytes into the 2-byte form (like ml64); shrinking only
// ever brings targets closer, so repeating until nothing changes converges
void Encoder::shrinkJumps()
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < m_c.fixups.size(); ++i) {
            Fixup& f = m_c.fixups[i];
            if (!f.jump || f.shortForm) continue;
            std::map<std::string, unsigned long>::const_iterator it = m_c.labels.find(f.label);
            if (it == m_c.labels.end()) continue;
            unsigned long end = f.at + 4;                       // end of the long instruction
            unsigned long removed = end - (f.opcodeAt + 2);     // 3 for jmp, 4 for jcc
            unsigned long target = it->second;
            unsigned long newTarget = target >= end ? target - removed : target;
            long rel = static_cast<long>(newTarget) - static_cast<long>(f.opcodeAt + 2);
            if (rel < -128 || rel > 127) continue;
            // rewrite the instruction and close the gap
            m_c.bytes[f.opcodeAt] = static_cast<unsigned char>(f.cond ? 0x70 + (m_c.bytes[f.opcodeAt + 1] - 0x80) : 0xEB);
            m_c.bytes[f.opcodeAt + 1] = 0;
            m_c.bytes.erase(m_c.bytes.begin() + static_cast<long>(f.opcodeAt + 2), m_c.bytes.begin() + static_cast<long>(end));
            for (std::map<std::string, unsigned long>::iterator l = m_c.labels.begin(); l != m_c.labels.end(); ++l)
                if (l->second >= end) l->second -= removed;
            for (size_t k = 0; k < m_c.fixups.size(); ++k) {
                if (k == i) continue;
                if (m_c.fixups[k].opcodeAt >= end) { m_c.fixups[k].opcodeAt -= removed; m_c.fixups[k].at -= removed; }
            }
            for (size_t r = 0; r < m_c.relocs.size(); ++r) if (m_c.relocs[r].offset >= end) m_c.relocs[r].offset -= removed;
            for (size_t u = 0; u < m_c.unwind.size(); ++u) if (m_c.unwind[u].offset >= end) m_c.unwind[u].offset -= removed;
            if (m_c.prologEnd >= static_cast<long>(end)) m_c.prologEnd -= static_cast<long>(removed);
            for (size_t s = 0; s < m_c.symbols.size(); ++s) if (m_c.symbols[s].second >= end) m_c.symbols[s].second -= removed;
            f.at = f.opcodeAt + 1;
            f.shortForm = true;
            changed = true;
        }
    }
}

void Encoder::finish()
{
    shrinkJumps();
    for (size_t i = 0; i < m_c.fixups.size(); ++i) {
        const Fixup& f = m_c.fixups[i];
        std::map<std::string, unsigned long>::const_iterator it = m_c.labels.find(f.label);
        if (it != m_c.labels.end()) {
            if (f.shortForm) {
                long rel = static_cast<long>(it->second) - static_cast<long>(f.at + 1);
                m_c.bytes[f.at] = static_cast<unsigned char>(rel & 0xFF);
            } else {
                long rel = static_cast<long>(it->second) - static_cast<long>(f.at + 4);
                for (int k = 0; k < 4; ++k) m_c.bytes[f.at + k] = static_cast<unsigned char>((rel >> (8 * k)) & 0xFF);
            }
        } else {                                      // a function or other external: let the linker resolve it
            Reloc rl; rl.offset = f.at; rl.symbol = f.label; rl.type = RelRel32;
            m_c.relocs.push_back(rl);
        }
    }
    m_c.fixups.clear();
}

bool Encoder::codeLine(const std::string& raw, int line)
{
    std::string text = trimWs(raw);
    if (text.empty()) return true;
    if (text[text.size() - 1] == ':' && text.find(' ') == std::string::npos) {   // label
        std::string name = text.substr(0, text.size() - 1);
        if (m_c.labels.count(name)) return fail("duplicate label " + name, line);
        m_c.labels[name] = pos();
        return true;
    }
    size_t sp = text.find_first_of(" \t");
    std::string mn = lowerText(sp == std::string::npos ? text : text.substr(0, sp));
    std::vector<std::string> opText;
    if (sp != std::string::npos) splitOperands(text.substr(sp + 1), opText);
    if (mn[0] == '.') return unwindDirective(mn, opText, line);
    std::vector<Operand> ops(opText.size());
    for (size_t i = 0; i < opText.size(); ++i) {
        std::string err;
        if (!parseOperand(opText[i], ops[i], err)) return fail(err, line);
    }
    size_t before = m_c.bytes.size();
    if (!instruction(mn, ops, line)) { m_c.bytes.resize(before); return false; }
    ++m_c.instructions;
    return true;
}

// the MASM prologue directives of a PROC FRAME (the subset X86.cpp writes): each one describes the instruction
// just before it, so it records the current code offset
bool Encoder::unwindDirective(const std::string& mn, const std::vector<std::string>& opText, int line)
{
    if (mn == ".ehrange") {
        // a code range of a try region and its landing pad, for the language-specific data after the handler
        if (opText.size() != 3) return fail(".ehrange needs a begin label, an end label and a landing pad", line);
        m_c.ehRanges.push_back(opText);
        return true;
    }
    if (m_c.prologEnd >= 0) return fail(mn + " after .endprolog", line);
    if (mn == ".endprolog") {
        if (!opText.empty()) return fail(".endprolog takes no operands", line);
        m_c.prologEnd = static_cast<long>(pos());
        return true;
    }
    UnwindStep s;
    s.offset = pos();
    s.reg = 0;
    s.value = 0;
    int num = 0, size = 0;
    long v = 0;
    if (mn == ".pushreg") {
        if (opText.size() != 1 || !lookupReg(lowerText(opText[0]), num, size) || size != 8) return fail(".pushreg needs a 64-bit register", line);
        s.op = UnwindStep::Push; s.reg = num;
    } else if (mn == ".allocstack") {
        if (opText.size() != 1 || !parseLong(opText[0], v) || v <= 0 || v % 8) return fail(".allocstack needs a positive multiple of 8", line);
        s.op = UnwindStep::Alloc; s.value = static_cast<unsigned long>(v);
    } else if (mn == ".savexmm128") {
        Operand x;
        std::string err;
        if (opText.size() != 2 || !parseOperand(opText[0], x, err) || x.kind != Operand::Xmm || !parseLong(opText[1], v) || v < 0 || v % 16)
            return fail(".savexmm128 needs an xmm register and an offset that is a multiple of 16", line);
        s.op = UnwindStep::SaveXmm; s.reg = x.reg; s.value = static_cast<unsigned long>(v);
    } else return fail("unsupported directive " + mn, line);
    m_c.unwind.push_back(s);
    return true;
}

const unsigned long kEhMagic = 0x58505043UL;   // "CPPX": the range table that follows the handler is cppparse's

// the UNWIND_INFO of a PROC FRAME (Windows x64 exception handling): version 1, the prologue size, the unwind codes in
// reverse prologue order (each code slot: code offset, then operation | info << 4; larger values in the slots after
// it), padded to an even number of slots. With a handler (PROC FRAME:handler): flag EHANDLER, the handler's address,
// then cppparse's range table: "CPPX", the count, and per range its begin, end and landing pad. Every address is
// image-relative (ADDR32NB); 'relocs' gets them, at offsets within 'out', against 'fn' or the handler
bool buildUnwindInfo(const Chunk& c, const std::string& fn, const std::string& handler, std::vector<unsigned char>& out,
                     std::vector<Reloc>& relocs, std::string& err)
{
    if (c.prologEnd < 0) { err = "PROC FRAME without .endprolog"; return false; }
    if (c.prologEnd > 255) { err = "prologue longer than 255 bytes"; return false; }
    std::vector<unsigned long> slots;   // 16-bit each
    for (size_t k = c.unwind.size(); k-- > 0;) {
        const UnwindStep& s = c.unwind[k];
        const unsigned long at = s.offset;
        if (s.op == UnwindStep::Push) slots.push_back(at | (static_cast<unsigned long>(s.reg) << 12));                      // UWOP_PUSH_NONVOL
        else if (s.op == UnwindStep::Alloc) {
            if (s.value <= 128) slots.push_back(at | (2UL << 8) | (((s.value - 8) / 8) << 12));                             // UWOP_ALLOC_SMALL
            else if (s.value <= 524280UL) { slots.push_back(at | (1UL << 8)); slots.push_back(s.value / 8); }               // UWOP_ALLOC_LARGE, size/8
            else { slots.push_back(at | (1UL << 8) | (1UL << 12)); slots.push_back(s.value & 0xFFFFUL); slots.push_back(s.value >> 16); }
        } else {
            if (s.value / 16 <= 0xFFFFUL) { slots.push_back(at | (8UL << 8) | (static_cast<unsigned long>(s.reg) << 12)); slots.push_back(s.value / 16); }   // UWOP_SAVE_XMM128
            else { slots.push_back(at | (9UL << 8) | (static_cast<unsigned long>(s.reg) << 12)); slots.push_back(s.value & 0xFFFFUL); slots.push_back(s.value >> 16); }
        }
    }
    if (slots.size() > 255) { err = "too many unwind codes"; return false; }
    out.clear();
    relocs.clear();
    out.push_back(static_cast<unsigned char>(handler.empty() ? 1 : 1 | (1 << 3)));   // version 1, UNW_FLAG_EHANDLER
    out.push_back(static_cast<unsigned char>(c.prologEnd));
    out.push_back(static_cast<unsigned char>(slots.size()));
    out.push_back(0);                                              // no frame register
    if (slots.size() % 2) slots.push_back(0);
    for (size_t k = 0; k < slots.size(); ++k) { out.push_back(static_cast<unsigned char>(slots[k] & 0xFF)); out.push_back(static_cast<unsigned char>((slots[k] >> 8) & 0xFF)); }
    if (handler.empty()) {
        if (!c.ehRanges.empty()) { err = ".ehrange in a PROC FRAME without a handler"; return false; }
        return true;
    }
    std::vector<std::pair<std::string, unsigned long> > fields;   // symbol (empty: a plain value) and value / addend
    fields.push_back(std::make_pair(handler, 0UL));
    fields.push_back(std::make_pair(std::string(), kEhMagic));
    fields.push_back(std::make_pair(std::string(), static_cast<unsigned long>(c.ehRanges.size())));
    for (size_t r = 0; r < c.ehRanges.size(); ++r)
        for (int k = 0; k < 3; ++k) {
            std::map<std::string, unsigned long>::const_iterator l = c.labels.find(c.ehRanges[r][static_cast<size_t>(k)]);
            if (l == c.labels.end()) { err = ".ehrange label " + c.ehRanges[r][static_cast<size_t>(k)] + " is not defined"; return false; }
            fields.push_back(std::make_pair(fn, l->second));
        }
    for (size_t f = 0; f < fields.size(); ++f) {
        if (!fields[f].first.empty()) { Reloc rl; rl.offset = static_cast<unsigned long>(out.size()); rl.symbol = fields[f].first; rl.type = RelAddr32Nb; relocs.push_back(rl); }
        for (int b = 0; b < 4; ++b) out.push_back(static_cast<unsigned char>((fields[f].second >> (8 * b)) & 0xFF));
    }
    return true;
}

struct SseOp { const char* name; int prefix; int opcode; int form; };   // form: 0 xmm,xmm/mem  1 store mem,xmm  2 xmm,r/m int  3 r,xmm/mem

const SseOp kSse[] = {
    {"movsd",0xF2,0x10,0},{"movss",0xF3,0x10,0},{"movapd",0x66,0x28,0},{"movaps",0x00,0x28,0},{"movupd",0x66,0x10,0},{"movups",0x00,0x10,0},
    {"movdqu",0xF3,0x6F,0},{"movdqa",0x66,0x6F,0},
    {"addsd",0xF2,0x58,0},{"mulsd",0xF2,0x59,0},{"subsd",0xF2,0x5C,0},{"divsd",0xF2,0x5E,0},{"minsd",0xF2,0x5D,0},{"maxsd",0xF2,0x5F,0},{"sqrtsd",0xF2,0x51,0},
    {"addss",0xF3,0x58,0},{"mulss",0xF3,0x59,0},{"subss",0xF3,0x5C,0},{"divss",0xF3,0x5E,0},{"minss",0xF3,0x5D,0},{"maxss",0xF3,0x5F,0},{"sqrtss",0xF3,0x51,0},
    {"addpd",0x66,0x58,0},{"mulpd",0x66,0x59,0},{"subpd",0x66,0x5C,0},{"divpd",0x66,0x5E,0},
    {"xorpd",0x66,0x57,0},{"xorps",0x00,0x57,0},{"andpd",0x66,0x54,0},{"andps",0x00,0x54,0},{"andnpd",0x66,0x55,0},{"orpd",0x66,0x56,0},{"orps",0x00,0x56,0},
    {"pxor",0x66,0xEF,0},{"pand",0x66,0xDB,0},{"por",0x66,0xEB,0},{"paddq",0x66,0xD4,0},{"psubq",0x66,0xFB,0},
    {"ucomisd",0x66,0x2E,0},{"comisd",0x66,0x2F,0},{"ucomiss",0x00,0x2E,0},{"comiss",0x00,0x2F,0},
    {"cvtsd2ss",0xF2,0x5A,0},{"cvtss2sd",0xF3,0x5A,0},
    {"cvtsi2sd",0xF2,0x2A,2},{"cvtsi2ss",0xF3,0x2A,2},
    {"cvttsd2si",0xF2,0x2C,3},{"cvttss2si",0xF3,0x2C,3},{"cvtsd2si",0xF2,0x2D,3},{"cvtss2si",0xF3,0x2D,3}
};

bool Encoder::instruction(const std::string& mn, std::vector<Operand>& ops, int line)
{
    size_t n = ops.size();
    bool forceRex = byteRegsNeedRex(ops);
    unsigned char op[3];

    if (n == 0) {
        if (mn == "ret") { byte(0xC3); return true; }
        if (mn == "leave") { byte(0xC9); return true; }
        if (mn == "cqo") { byte(0x48); byte(0x99); return true; }
        if (mn == "cdq") { byte(0x99); return true; }
        if (mn == "cdqe") { byte(0x48); byte(0x98); return true; }
        if (mn == "cwde") { byte(0x98); return true; }
        if (mn == "nop") { byte(0x90); return true; }
        if (mn == "int3") { byte(0xCC); return true; }
        if (mn == "ud2") { byte(0x0F); byte(0x0B); return true; }
        return fail("unknown instruction " + mn, line);
    }
    if (mn == "int" && n == 1 && ops[0].kind == Operand::Imm && ops[0].imm == 3) { byte(0xCC); return true; }   // ml64's spelling
    const Operand& a = ops[0];
    const Operand* b = n > 1 ? &ops[1] : 0;
    bool mixed = false;
    for (size_t i = 0; i < n; ++i) if (ops[i].kind == Operand::Xmm) mixed = true;

    for (size_t k = 0; k < sizeof(kSse) / sizeof(kSse[0]); ++k) {
        if (mn != kSse[k].name) continue;
        if (n != 2) return fail(mn + " needs two operands", line);
        const SseOp& s = kSse[k];
        if (s.form == 0 || s.form == 1) {
            if (a.kind == Operand::Xmm && (b->kind == Operand::Xmm || b->isMem())) {
                op[0] = 0x0F; op[1] = static_cast<unsigned char>(s.opcode);
                emitRm(s.prefix, op, 2, a.reg, *b, false, 0, false);
                return true;
            }
            if (a.isMem() && b->kind == Operand::Xmm) {
                int store = s.opcode == 0x10 ? 0x11 : s.opcode == 0x28 ? 0x29 : s.opcode == 0x6F ? 0x7F : -1;
                if (store < 0) return fail(mn + " cannot store to memory", line);
                op[0] = 0x0F; op[1] = static_cast<unsigned char>(store);
                emitRm(s.prefix, op, 2, b->reg, a, false, 0, false);
                return true;
            }
            return fail(mn + ": operands must be xmm registers or memory (float/integer mix?)", line);
        }
        if (s.form == 2) {   // cvtsi2sd xmm, r/m32|64
            if (a.kind != Operand::Xmm || !b->isRm()) return fail(mn + ": needs xmm, r/m", line);
            op[0] = 0x0F; op[1] = static_cast<unsigned char>(s.opcode);
            emitRm(s.prefix, op, 2, a.reg, *b, b->size == 8, 0, false);
            return true;
        }
        // form 3: cvttsd2si r32|64, xmm/m
        if (!a.isReg() || !(b->kind == Operand::Xmm || b->isMem())) return fail(mn + ": needs r, xmm/m", line);
        op[0] = 0x0F; op[1] = static_cast<unsigned char>(s.opcode);
        emitRm(s.prefix, op, 2, a.reg, *b, a.size == 8, 0, false);
        return true;
    }
    if (mn == "movq" || mn == "movd") {
        if (n != 2) return fail(mn + " needs two operands", line);
        bool w = mn == "movq";
        op[0] = 0x0F;
        if (a.kind == Operand::Xmm && b->isRm()) { op[1] = 0x6E; emitRm(0x66, op, 2, a.reg, *b, w, 0, false); return true; }
        if (a.isRm() && b->kind == Operand::Xmm) { op[1] = 0x7E; emitRm(0x66, op, 2, b->reg, a, w, 0, false); return true; }
        return fail(mn + ": bad operands", line);
    }
    if (mixed) return fail(mn + ": float register used with an integer operand", line);

    if (mn == "mov") {
        if (n != 2) return fail("mov needs two operands", line);
        if (a.isReg() && b->isReg()) {
            if (a.size != b->size) return fail("mov: register sizes differ", line);
            op[0] = static_cast<unsigned char>(a.size == 1 ? 0x88 : 0x89);
            emitRm(a.size == 2 ? 0x66 : 0, op, 1, b->reg, a, a.size == 8, 0, forceRex);
            return true;
        }
        if (a.isReg() && b->kind == Operand::Imm) {
            long v = b->imm;
            if (a.size == 8) { op[0] = 0xC7; emitRm(0, op, 1, 0, a, true, 4, false); dword(v); return true; }   // sign-extended imm32, as ml64
            if (a.size == 4) { if (a.reg >= 8) byte(0x41); byte(0xB8 + (a.reg & 7)); dword(v); return true; }
            if (a.size == 2) { byte(0x66); if (a.reg >= 8) byte(0x41); byte(0xB8 + (a.reg & 7)); word(v); return true; }
            if (a.reg >= 8 || forceRex) byte(0x40 | (a.reg >= 8 ? 1 : 0));
            byte(0xB0 + (a.reg & 7)); byte(static_cast<int>(v)); return true;
        }
        if (a.isReg() && b->isMem()) {
            if (b->size && b->size != a.size) return fail("mov: operand sizes differ", line);
            op[0] = static_cast<unsigned char>(a.size == 1 ? 0x8A : 0x8B);
            emitRm(a.size == 2 ? 0x66 : 0, op, 1, a.reg, *b, a.size == 8, 0, forceRex);
            return true;
        }
        if (a.isMem() && b->isReg()) {
            if (a.size && a.size != b->size) return fail("mov: operand sizes differ", line);
            op[0] = static_cast<unsigned char>(b->size == 1 ? 0x88 : 0x89);
            emitRm(b->size == 2 ? 0x66 : 0, op, 1, b->reg, a, b->size == 8, 0, forceRex);
            return true;
        }
        if (a.isMem() && b->kind == Operand::Imm) {
            int sz = a.size ? a.size : 8;
            op[0] = static_cast<unsigned char>(sz == 1 ? 0xC6 : 0xC7);
            int ib = sz == 1 ? 1 : sz == 2 ? 2 : 4;
            emitRm(sz == 2 ? 0x66 : 0, op, 1, 0, a, sz == 8, ib, false);
            imm(b->imm, ib);
            return true;
        }
        return fail("mov: bad operands", line);
    }

    if (mn == "lea") {
        if (n != 2 || !a.isReg()) return fail("lea: bad operands", line);
        Operand m = *b;
        if (m.kind == Operand::Sym) { m.kind = Operand::Mem; m.size = 0; }   // lea r, symbol  ->  [rip+symbol]
        if (!m.isMem()) return fail("lea: second operand must be memory", line);
        op[0] = 0x8D;
        emitRm(a.size == 2 ? 0x66 : 0, op, 1, a.reg, m, a.size == 8, 0, false);
        return true;
    }

    {
        static const char* const names[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
        int digit = -1;
        for (int k = 0; k < 8; ++k) if (mn == names[k]) digit = k;
        if (digit >= 0) {
            if (n != 2) return fail(mn + " needs two operands", line);
            int sz = a.isReg() ? a.size : a.size ? a.size : (b->isReg() ? b->size : 8);
            int pre = sz == 2 ? 0x66 : 0;
            if (a.isRm() && b->isReg()) {
                if (a.isReg() && a.size != b->size) return fail(mn + ": register sizes differ", line);
                op[0] = static_cast<unsigned char>(digit * 8 + (sz == 1 ? 0 : 1));
                emitRm(pre, op, 1, b->reg, a, sz == 8, 0, forceRex);
                return true;
            }
            if (a.isReg() && b->isMem()) {
                op[0] = static_cast<unsigned char>(digit * 8 + (sz == 1 ? 2 : 3));
                emitRm(pre, op, 1, a.reg, *b, sz == 8, 0, forceRex);
                return true;
            }
            if (a.isRm() && b->kind == Operand::Imm) {
                if (sz == 1) { op[0] = 0x80; emitRm(0, op, 1, digit, a, false, 1, forceRex); byte(static_cast<int>(b->imm)); return true; }
                if (fitsImm8(b->imm)) { op[0] = 0x83; emitRm(pre, op, 1, digit, a, sz == 8, 1, false); byte(static_cast<int>(b->imm)); return true; }
                int ib = sz == 2 ? 2 : 4;
                op[0] = 0x81; emitRm(pre, op, 1, digit, a, sz == 8, ib, false); imm(b->imm, ib); return true;
            }
            return fail(mn + ": bad operands", line);
        }
    }

    if (mn == "test") {
        if (n != 2) return fail("test needs two operands", line);
        if (a.isRm() && b->isReg()) {
            int sz = b->size;
            op[0] = static_cast<unsigned char>(sz == 1 ? 0x84 : 0x85);
            emitRm(sz == 2 ? 0x66 : 0, op, 1, b->reg, a, sz == 8, 0, forceRex);
            return true;
        }
        if (a.isRm() && b->kind == Operand::Imm) {
            int sz = a.size ? a.size : 8;
            int ib = sz == 1 ? 1 : sz == 2 ? 2 : 4;
            op[0] = static_cast<unsigned char>(sz == 1 ? 0xF6 : 0xF7);
            emitRm(sz == 2 ? 0x66 : 0, op, 1, 0, a, sz == 8, ib, forceRex);
            imm(b->imm, ib);
            return true;
        }
        return fail("test: bad operands", line);
    }

    if (mn == "imul") {
        if (n == 2 && a.isReg() && b->isRm()) { op[0] = 0x0F; op[1] = 0xAF; emitRm(a.size == 2 ? 0x66 : 0, op, 2, a.reg, *b, a.size == 8, 0, false); return true; }
        if (n == 3 && a.isReg() && b->isRm() && ops[2].kind == Operand::Imm) {
            long v = ops[2].imm;
            if (fitsImm8(v)) { op[0] = 0x6B; emitRm(a.size == 2 ? 0x66 : 0, op, 1, a.reg, *b, a.size == 8, 1, false); byte(static_cast<int>(v)); return true; }
            int ib = a.size == 2 ? 2 : 4;
            op[0] = 0x69; emitRm(a.size == 2 ? 0x66 : 0, op, 1, a.reg, *b, a.size == 8, ib, false); imm(v, ib); return true;
        }
        if (n != 1) return fail("imul: bad operands", line);
        // one-operand form falls through to the F7 group
    }

    {
        static const char* const names[] = { "", "", "not", "neg", "mul", "imul", "div", "idiv" };
        int digit = -1;
        for (int k = 2; k < 8; ++k) if (mn == names[k]) digit = k;
        if (digit >= 0) {
            if (n != 1 || !a.isRm()) return fail(mn + ": needs one register or memory operand", line);
            int sz = a.size ? a.size : 8;
            op[0] = static_cast<unsigned char>(sz == 1 ? 0xF6 : 0xF7);
            emitRm(sz == 2 ? 0x66 : 0, op, 1, digit, a, sz == 8, 0, forceRex);
            return true;
        }
    }

    if (mn == "inc" || mn == "dec") {
        if (n != 1 || !a.isRm()) return fail(mn + ": bad operand", line);
        int sz = a.size ? a.size : 8;
        op[0] = static_cast<unsigned char>(sz == 1 ? 0xFE : 0xFF);
        emitRm(sz == 2 ? 0x66 : 0, op, 1, mn == "inc" ? 0 : 1, a, sz == 8, 0, forceRex);
        return true;
    }

    {
        static const char* const names[] = { "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar" };
        int digit = -1;
        for (int k = 0; k < 8; ++k) if (mn == names[k]) digit = k == 6 ? 4 : k;
        if (digit >= 0) {
            if (n != 2 || !a.isRm()) return fail(mn + ": bad operands", line);
            int sz = a.size ? a.size : 8;
            int pre = sz == 2 ? 0x66 : 0;
            if (b->kind == Operand::Imm) {
                if (b->imm == 1) { op[0] = static_cast<unsigned char>(sz == 1 ? 0xD0 : 0xD1); emitRm(pre, op, 1, digit, a, sz == 8, 0, forceRex); return true; }
                op[0] = static_cast<unsigned char>(sz == 1 ? 0xC0 : 0xC1); emitRm(pre, op, 1, digit, a, sz == 8, 1, forceRex); byte(static_cast<int>(b->imm)); return true;
            }
            if (b->isReg() && b->reg == 1 && b->size == 1) { op[0] = static_cast<unsigned char>(sz == 1 ? 0xD2 : 0xD3); emitRm(pre, op, 1, digit, a, sz == 8, 0, forceRex); return true; }
            return fail(mn + ": count must be an immediate or cl", line);
        }
    }

    if (mn.size() > 3 && mn.compare(0, 3, "set") == 0) {
        int cc = condCode(mn.substr(3));
        if (cc < 0) return fail("unknown instruction " + mn, line);
        if (n != 1 || !a.isRm() || (a.isReg() && a.size != 1)) return fail(mn + ": needs an 8-bit operand", line);
        op[0] = 0x0F; op[1] = static_cast<unsigned char>(0x90 + cc);
        emitRm(0, op, 2, 0, a, false, 0, forceRex);
        return true;
    }

    if (mn == "movzx" || mn == "movsx" || mn == "movsxd") {
        if (n != 2 || !a.isReg() || !b->isRm()) return fail(mn + ": bad operands", line);
        int srcSize = b->isReg() ? b->size : b->size;
        if (mn == "movsxd") {
            if (srcSize && srcSize != 4) return fail("movsxd: source must be 32 bits", line);
            op[0] = 0x63; emitRm(0, op, 1, a.reg, *b, a.size == 8, 0, false); return true;
        }
        if (srcSize != 1 && srcSize != 2) return fail(mn + ": source must be 8 or 16 bits (give a size prefix)", line);
        op[0] = 0x0F; op[1] = static_cast<unsigned char>((mn == "movzx" ? 0xB6 : 0xBE) + (srcSize == 2 ? 1 : 0));
        emitRm(a.size == 2 ? 0x66 : 0, op, 2, a.reg, *b, a.size == 8, 0, forceRex);
        return true;
    }

    if (mn == "jmp") {
        if (n != 1) return fail("jmp: bad operand", line);
        if (a.kind == Operand::Sym) { jump(0xE9, a.symbol, line); return true; }
        if (a.isRm()) { op[0] = 0xFF; emitRm(0, op, 1, 4, a, false, 0, false); return true; }
        return fail("jmp: bad operand", line);
    }
    if (mn.size() > 1 && mn[0] == 'j' && condCode(mn.substr(1)) >= 0) {
        if (n != 1 || a.kind != Operand::Sym) return fail(mn + ": needs a label", line);
        jump(0x80 + condCode(mn.substr(1)), a.symbol, line); return true;
    }
    if (mn == "call") {
        if (n != 1) return fail("call: bad operand", line);
        if (a.kind == Operand::Sym) { byte(0xE8); rel32(a.symbol, line); return true; }
        if (a.isRm()) { op[0] = 0xFF; emitRm(0, op, 1, 2, a, false, 0, false); return true; }
        return fail("call: bad operand", line);
    }

    if (mn == "push" || mn == "pop") {
        if (n != 1) return fail(mn + ": bad operand", line);
        if (a.isReg() && a.size == 8) { if (a.reg >= 8) byte(0x41); byte((mn == "push" ? 0x50 : 0x58) + (a.reg & 7)); return true; }
        if (mn == "push" && a.kind == Operand::Imm) { byte(0x68); dword(a.imm); return true; }
        if (a.isMem()) { op[0] = static_cast<unsigned char>(mn == "push" ? 0xFF : 0x8F); emitRm(0, op, 1, mn == "push" ? 6 : 0, a, false, 0, false); return true; }
        return fail(mn + ": bad operand", line);
    }
    if (mn == "xchg" && n == 2 && a.isReg() && b->isReg()) { op[0] = 0x87; emitRm(a.size == 2 ? 0x66 : 0, op, 1, b->reg, a, a.size == 8, 0, forceRex); return true; }

    return fail("unknown instruction " + mn, line);
}

// data directives: [name] DB|DW|DD|DQ values  |  [name] REAL8|REAL4 value  |  name LABEL type  |  ALIGN n
bool Encoder::dataLine(const std::string& raw, int line)
{
    std::string text = trimWs(raw);
    if (text.empty()) return true;
    std::vector<std::string> words;
    {
        std::istringstream in(text);
        std::string w;
        while (in >> w) words.push_back(w);
    }
    static const char* const kDirs[] = { "DB", "DW", "DD", "DQ", "REAL4", "REAL8", "LABEL", "ALIGN" };
    size_t di = 0;
    std::string name;
    std::string u0 = upperText(words[0]);
    bool firstIsDir = false;
    for (int k = 0; k < 8; ++k) if (u0 == kDirs[k]) firstIsDir = true;
    if (!firstIsDir) {
        if (words.size() < 2) return fail("bad data line: " + text, line);
        name = words[0];
        di = 1;
    }
    std::string dir = upperText(words[di]);
    if (dir == "ALIGN") {
        long n = 0;
        if (di + 1 >= words.size() || !parseLong(words[di + 1], n) || n <= 0 || n > 8192 || (n & (n - 1)) != 0) return fail("bad ALIGN", line);
        if (static_cast<unsigned long>(n) > m_c.maxAlign) m_c.maxAlign = static_cast<unsigned long>(n);
        if (pos() % static_cast<unsigned long>(n)) m_c.padStarts.push_back(pos());
        while (pos() % static_cast<unsigned long>(n)) byte(0);
        if (!name.empty()) m_c.symbols.push_back(std::make_pair(name, pos()));
        return true;
    }
    if (!name.empty()) m_c.symbols.push_back(std::make_pair(name, pos()));
    if (dir == "LABEL") return true;
    // searched after the name: a decorated name may end in the directive's letters ("?p@@3QBQEBDB DB ...")
    size_t valuesAt = text.find(words[di], name.size()) + words[di].size();
    std::vector<std::string> values;
    splitOperands(text.substr(valuesAt), values);
    if (values.empty()) return fail("missing value after " + dir, line);
    int size = dir == "DB" ? 1 : dir == "DW" ? 2 : dir == "DD" || dir == "REAL4" ? 4 : 8;
    for (size_t v = 0; v < values.size(); ++v) {
        std::string val = values[v];
        std::string lv = lowerText(val);
        if (dir == "REAL8" || dir == "REAL4") {
            // MASM's encoded real: "03FD0000000000000r", the bits in hex (a folded constant, exactly)
            const size_t digits = dir == "REAL8" ? 16 : 8;
            if (lv.size() == digits + 2 && lv[0] == '0' && lv[lv.size() - 1] == 'r' && lv.find_first_not_of("0123456789abcdef", 1) == lv.size() - 1) {
                for (size_t i = 0; i < digits / 2; ++i)   // little-endian: the last pair of digits first
                    byte(static_cast<unsigned char>(std::strtoul(lv.substr(lv.size() - 3 - 2 * i, 2).c_str(), 0, 16)));
                continue;
            }
            double d = strtod(val.c_str(), 0);
            if (dir == "REAL8") { unsigned char buf[8]; std::memcpy(buf, &d, 8); for (int i = 0; i < 8; ++i) byte(buf[i]); }
            else { float f = static_cast<float>(d); unsigned char buf[4]; std::memcpy(buf, &f, 4); for (int i = 0; i < 4; ++i) byte(buf[i]); }
            continue;
        }
        if (val == "?") { for (int i = 0; i < size; ++i) byte(0); continue; }
        size_t dup = lv.find(" dup");
        if (dup != std::string::npos) {                      // "N DUP (v)"
            long count = 0;
            std::string inner = trimWs(val.substr(dup + 4));
            if (!parseLong(val.substr(0, dup), count) || count < 0 || inner.size() < 2 || inner[0] != '(' || inner[inner.size() - 1] != ')') return fail("bad DUP: " + val, line);
            U64 u;
            std::string v2 = trimWs(inner.substr(1, inner.size() - 2));
            if (v2 != "?" && !parseU64(v2, u)) return fail("bad DUP value: " + val, line);
            for (long k = 0; k < count; ++k) { if (size == 8) qword(u); else imm(static_cast<long>(u.lo), size); }
            continue;
        }
        if (lv.compare(0, 7, "offset ") == 0 || (!val.empty() && isIdentStart(val[0]) && !isNumberText(val))) {
            std::string sym = trimWs(lv.compare(0, 7, "offset ") == 0 ? val.substr(7) : val);
            if (size != 8) return fail("an address needs DQ: " + val, line);
            Reloc rl; rl.offset = pos(); rl.symbol = sym; rl.type = RelAddr64;
            m_c.relocs.push_back(rl);
            for (int i = 0; i < 8; ++i) byte(0);
            continue;
        }
        U64 u;
        if (!parseU64(val, u)) return fail("bad value " + val, line);
        if (size == 8) qword(u);
        else imm(static_cast<long>(u.lo), size);
    }
    return true;
}


struct CodeBlock {
    std::string name;
    std::vector<std::pair<int, std::string> > lines;
    Chunk chunk;
    bool  comdat;      // one COMDAT section of its own (inline functions, template instantiations)
    bool  frame;       // PROC FRAME: gets unwind info (.xdata) and a function table entry (.pdata)
    std::string handler;   // PROC FRAME:handler, empty without one
    std::vector<unsigned char> xdata;
    std::vector<Reloc> xdataRelocs;
    CodeBlock() : comdat(false), frame(false) {}
};

struct EncodeJob : public ParallelJob {
    std::vector<CodeBlock>* blocks;
    virtual void run(size_t i)
    {
        CodeBlock& b = (*blocks)[i];
        Encoder enc(b.chunk);
        b.chunk.symbols.push_back(std::make_pair(b.name, 0UL));
        for (size_t k = 0; k < b.lines.size(); ++k) enc.codeLine(b.lines[k].second, b.lines[k].first);
        enc.finish();
        std::string err;
        if (b.frame && !buildUnwindInfo(b.chunk, b.name, b.handler, b.xdata, b.xdataRelocs, err)) b.chunk.errors.push_back(b.name + ": " + err);
        if (!b.frame && (b.chunk.prologEnd >= 0 || !b.chunk.unwind.empty() || !b.chunk.ehRanges.empty())) b.chunk.errors.push_back(b.name + ": unwind directives in a PROC without FRAME");
    }
};


struct Section {
    std::string                name;
    unsigned long              characteristics;
    std::vector<unsigned char> bytes;
    std::vector<Reloc>         relocs;
    int                        selection;     // IMAGE_COMDAT_SELECT_* of a COMDAT section, 0 otherwise
    int                        associated;    // selection 5 (associative): the 1-based section it goes with
    bool                       isCode;
    unsigned long              align;         // the largest alignment its contents ask for (ALIGN, COMDAT marker)
    Section() : characteristics(0), selection(0), associated(0), isCode(false), align(1) {}
};

struct SymbolEntry {
    std::string    name;
    unsigned long  value;
    int            section;       // 1-based, 0 = undefined
    int            type;          // 0x20 for functions
    int            storageClass;  // 2 external, 3 static
    bool           isSection;     // has one auxiliary record
    int            selection;     // section symbols: the COMDAT selection
};

// data lines of one section, or of one COMDAT item (a vtable) that becomes a section of its own
struct DataPiece {
    Chunk    chunk;
    Encoder* enc;
    int      kind;       // 2 data, 3 rdata, 4 CRT initialisers
    bool     comdat;
    DataPiece(int k, bool c) : enc(0), kind(k), comdat(c) { enc = new Encoder(chunk); }
    ~DataPiece() { delete enc; }
};

void put8(std::string& o, int v) { o += static_cast<char>(v & 0xFF); }
void put16(std::string& o, unsigned long v) { put8(o, static_cast<int>(v)); put8(o, static_cast<int>(v >> 8)); }
void put32(std::string& o, unsigned long v) { for (int i = 0; i < 4; ++i) put8(o, static_cast<int>(v >> (8 * i))); }

const unsigned long kCodeFlags  = 0x60500020UL;   // code | execute | read | align 16
const unsigned long kDataFlags  = 0xC0500040UL;   // initialized | read | write | align 16
const unsigned long kRdataFlags = 0x40500040UL;   // initialized | read | align 16
const unsigned long kCrtFlags   = 0x40400040UL;   // initialized | read | align 8
const unsigned long kComdat     = 0x00001000UL;   // IMAGE_SCN_LNK_COMDAT
const int           kSelectAny  = 2;              // IMAGE_COMDAT_SELECT_ANY
const int           kSelectAssociative = 5;       // IMAGE_COMDAT_SELECT_ASSOCIATIVE: kept or dropped with another section
const unsigned long kUnwindFlags = 0x40300040UL;  // .xdata / .pdata: initialized | read | align 4

// bytes of the s-th definition of a chunk: up to the next definition, the start of ALIGN padding or the end
// of the chunk, whichever comes first
unsigned long symbolSize(const Chunk& c, size_t s)
{
    const unsigned long start = c.symbols[s].second;
    unsigned long end = static_cast<unsigned long>(c.bytes.size());
    for (size_t k = 0; k < c.symbols.size(); ++k)
        if (c.symbols[k].second > start && c.symbols[k].second < end) end = c.symbols[k].second;
    for (size_t k = 0; k < c.padStarts.size(); ++k)
        if (c.padStarts[k] >= start && c.padStarts[k] < end) end = c.padStarts[k];
    return end - start;
}

struct SymbolInfoOrder {
    bool operator()(const CoffSymbolInfo& a, const CoffSymbolInfo& b) const
    {
        if (a.section != b.section) return a.section < b.section;
        if (a.offset != b.offset) return a.offset < b.offset;
        return a.name < b.name;
    }
};

} // namespace

bool assembleCoff(const std::string& masmText, int threads, std::string& objectBytes, CoffResult& result)
{
    std::vector<CodeBlock> blocks;
    std::vector<DataPiece*> pieces;
    DataPiece* plain[5] = { 0, 0, 0, 0, 0 };     // the non-COMDAT piece of each data section kind
    DataPiece* currentPiece = 0;
    std::set<std::string> publics;
    std::vector<std::string> externs;
    std::set<std::string> externSet;
    int section = 0;            // 0 none, 1 text, 2 data, 3 rdata, 4 CRT initialisers
    CodeBlock* current = 0;
    bool comdatNext = false;    // ";@comdat any" was seen: the next PROC or data item is a COMDAT
    unsigned long comdatAlign = 0;   // ";@comdat any align N": the alignment of that item's section
    std::vector<std::string> globalErrors;

    size_t lineStart = 0;
    int lineNo = 0;
    while (lineStart <= masmText.size()) {
        size_t nl = masmText.find('\n', lineStart);
        std::string raw = masmText.substr(lineStart, nl == std::string::npos ? std::string::npos : nl - lineStart);
        lineStart = nl == std::string::npos ? masmText.size() + 1 : nl + 1;
        ++lineNo;
        std::string marker = trimWs(raw);
        if (marker.compare(0, 10, ";@ehrange ") == 0 && current) {   // a try range of the current PROC FRAME (a comment to ml64)
            current->lines.push_back(std::make_pair(lineNo, ".ehrange " + marker.substr(10)));
            continue;
        }
        if (marker.compare(0, 8, ";@comdat") == 0) {
            comdatNext = true;
            size_t al = marker.find(" align ");                  // ";@comdat any align 32": the item's section alignment
            comdatAlign = al == std::string::npos ? 0UL : static_cast<unsigned long>(atol(marker.c_str() + al + 7));
            continue;
        }
        size_t sc = raw.find(';');
        if (sc != std::string::npos) raw = raw.substr(0, sc);
        std::string text = trimWs(raw);
        if (text.empty()) continue;
        std::istringstream in(text);
        std::string w0, w1, w2;
        in >> w0 >> w1 >> w2;
        std::string u0 = upperText(w0), u1 = upperText(w1);
        // the prologue directives of a PROC FRAME belong to its code (the other '.' lines are section switches)
        if (current && section == 1 && (u0 == ".PUSHREG" || u0 == ".ALLOCSTACK" || u0 == ".SAVEXMM128" || u0 == ".ENDPROLOG")) {
            current->lines.push_back(std::make_pair(lineNo, text));
            continue;
        }
        if (u0 == ".CODE") { section = 1; currentPiece = 0; continue; }
        if (u0 == ".DATA") { section = 2; currentPiece = 0; continue; }
        if (u0 == ".CONST" || u0 == ".RDATA") { section = 3; currentPiece = 0; continue; }
        if (u1 == "SEGMENT") { section = u0 == "CRT$XCU" ? 4 : 0; currentPiece = 0; if (section == 0) globalErrors.push_back("line " + numText(lineNo) + ": unknown segment " + w0); continue; }
        if (u1 == "ENDS") { section = 0; currentPiece = 0; continue; }
        if (u0[0] == '.' || u0 == "OPTION" || u0 == "END" || u0 == "INCLUDE" || u0 == "INCLUDELIB") continue;
        if (u0 == "PUBLIC") { publics.insert(w1); if (currentPiece && currentPiece->comdat) currentPiece = 0; continue; }
        if (u0 == "EXTERN" || u0 == "EXTRN") {
            std::string name = w1.substr(0, w1.find(':'));
            if (externSet.insert(name).second) externs.push_back(name);
            continue;
        }
        if (u1 == "PROC") {
            blocks.push_back(CodeBlock());
            blocks.back().name = w0;
            blocks.back().comdat = comdatNext;
            blocks.back().frame = upperText(w2.substr(0, 5)) == "FRAME" && (w2.size() == 5 || w2[5] == ':');
            if (blocks.back().frame && w2.size() > 6) blocks.back().handler = w2.substr(6);
            if (!blocks.back().handler.empty() && externSet.insert(blocks.back().handler).second) externs.push_back(blocks.back().handler);
            comdatNext = false;
            comdatAlign = 0;
            current = &blocks.back();
            continue;
        }
        if (u1 == "ENDP") { current = 0; continue; }
        if (section == 1) {
            if (!current) { globalErrors.push_back("line " + numText(lineNo) + ": code outside PROC: " + text); continue; }
            current->lines.push_back(std::make_pair(lineNo, text));
        } else if (section >= 2 && section <= 4) {
            // a COMDAT data item ends where the next labelled definition begins: a symbol of internal linkage
            // has no PUBLIC line before it, and must not join the COMDAT section the linker may discard
            // (an ALIGN starts the next item too: it pads for the definition after it)
            const bool labelled = u0 != "DB" && u0 != "DW" && u0 != "DD" && u0 != "DQ" && u0 != "REAL4" && u0 != "REAL8" && u0 != "ALIGN";
            if (!comdatNext && (labelled || u0 == "ALIGN") && currentPiece && currentPiece->comdat) currentPiece = 0;
            if (comdatNext) {
                pieces.push_back(new DataPiece(section, true)); currentPiece = pieces.back(); comdatNext = false;
                if (comdatAlign > currentPiece->chunk.maxAlign) currentPiece->chunk.maxAlign = comdatAlign;
                comdatAlign = 0;
            }
            if (!currentPiece) {
                if (!plain[section]) { pieces.push_back(new DataPiece(section, false)); plain[section] = pieces.back(); }
                currentPiece = plain[section];
            }
            currentPiece->enc->dataLine(text, lineNo);
        } else {
            globalErrors.push_back("line " + numText(lineNo) + ": statement before any section: " + text);
        }
    }

    EncodeJob job;
    job.blocks = &blocks;
    runParallel(job, blocks.size(), threads);

    // sections: .text, .data, .rdata, [.CRT$XCU], then one per COMDAT function and per COMDAT data item
    std::vector<Section> secs(3);
    secs[0].name = ".text";  secs[0].characteristics = kCodeFlags;  secs[0].isCode = true;
    secs[1].name = ".data";  secs[1].characteristics = kDataFlags;
    secs[2].name = ".rdata"; secs[2].characteristics = kRdataFlags;
    if (plain[4]) { Section s; s.name = ".CRT$XCU"; s.characteristics = kCrtFlags; secs.push_back(s); }
    const size_t crtIndex = plain[4] ? 3 : 0;
    // the unwind info and function table of the functions in .text (a COMDAT function has its own)
    bool plainFrames = false;
    for (size_t i = 0; i < blocks.size(); ++i) if (blocks[i].frame && !blocks[i].comdat) plainFrames = true;
    size_t xdataIndex = 0, pdataIndex = 0;
    if (plainFrames) {
        Section x; x.name = ".xdata"; x.characteristics = kUnwindFlags; xdataIndex = secs.size(); secs.push_back(x);
        Section p; p.name = ".pdata"; p.characteristics = kUnwindFlags; pdataIndex = secs.size(); secs.push_back(p);
    }
    const size_t plainSections = secs.size();
    std::vector<SymbolEntry> symbols;
    std::map<std::string, size_t> symbolIndex;   // name -> index in 'symbols'
    // symbols of a COMDAT section follow its section symbol: collected per section, appended at the end
    std::vector<std::vector<SymbolEntry> > comdatSymbols;

    result.errors = globalErrors;
    std::vector<SymbolEntry> plainSymbols;
    unsigned long textBytes = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        Chunk& c = blocks[i].chunk;
        result.errors.insert(result.errors.end(), c.errors.begin(), c.errors.end());
        result.instructions += c.instructions;
        textBytes += static_cast<unsigned long>(c.bytes.size());
        Section* target;
        unsigned long base;
        int sectionNumber;
        if (blocks[i].comdat) {
            Section s; s.name = ".text$mn"; s.characteristics = kCodeFlags | kComdat; s.selection = kSelectAny; s.isCode = true;
            secs.push_back(s);
            comdatSymbols.push_back(std::vector<SymbolEntry>());
            target = &secs.back(); base = 0; sectionNumber = static_cast<int>(secs.size());
        } else {
            // every function starts on a 16-byte boundary, the gap filled with int 3, as cl lays out code for
            // speed (/Od, /O2): the section's own 16-byte alignment covers the first one and each COMDAT function
            while (secs[0].bytes.size() % 16) secs[0].bytes.push_back(0xCC);
            target = &secs[0]; base = static_cast<unsigned long>(secs[0].bytes.size()); sectionNumber = 1;
        }
        target->bytes.insert(target->bytes.end(), c.bytes.begin(), c.bytes.end());
        for (size_t r = 0; r < c.relocs.size(); ++r) { Reloc rl = c.relocs[r]; rl.offset += base; target->relocs.push_back(rl); }
        for (size_t s = 0; s < c.symbols.size(); ++s) {
            SymbolEntry f;
            f.name = c.symbols[s].first; f.value = base + c.symbols[s].second; f.section = sectionNumber; f.type = 0x20;
            f.storageClass = publics.count(f.name) ? 2 : 3; f.isSection = false; f.selection = 0;
            if (symbolIndex.count(f.name)) { result.errors.push_back("duplicate symbol " + f.name); continue; }
            symbolIndex[f.name] = 0;   // defined; the index is assigned when the table is laid out
            if (blocks[i].comdat) comdatSymbols.back().push_back(f); else plainSymbols.push_back(f);
            CoffSymbolInfo mi; mi.name = f.name; mi.section = sectionNumber; mi.offset = f.value; mi.size = symbolSize(c, s);
            mi.isFunction = true; mi.external = f.storageClass == 2;
            result.symbolList.push_back(mi);
        }
        if (!blocks[i].frame || blocks[i].xdata.empty()) continue;
        // UNWIND_INFO ($unwind$f) and RUNTIME_FUNCTION ($pdata$f: begin, end, unwind info as image-relative
        // addresses), named as cl names them; for a COMDAT function in sections that go with its code
        const std::string& fn = blocks[i].name;
        const std::string names[2] = { "$unwind$" + fn, "$pdata$" + fn };
        for (int part = 0; part < 2; ++part) {
            size_t si;
            if (blocks[i].comdat) {
                Section s; s.name = part ? ".pdata" : ".xdata"; s.characteristics = kUnwindFlags | kComdat;
                s.selection = kSelectAssociative; s.associated = sectionNumber;
                si = secs.size(); secs.push_back(s);
                comdatSymbols.push_back(std::vector<SymbolEntry>());
            } else si = part ? pdataIndex : xdataIndex;
            Section& sec = secs[si];
            const unsigned long at = static_cast<unsigned long>(sec.bytes.size());
            if (part == 0) {
                sec.bytes.insert(sec.bytes.end(), blocks[i].xdata.begin(), blocks[i].xdata.end());
                for (size_t r = 0; r < blocks[i].xdataRelocs.size(); ++r) { Reloc rl = blocks[i].xdataRelocs[r]; rl.offset += at; sec.relocs.push_back(rl); }
            }
            else {
                const unsigned long addend[3] = { 0, static_cast<unsigned long>(c.bytes.size()), 0 };
                const std::string relocTo[3] = { fn, fn, names[0] };
                for (int k = 0; k < 3; ++k) {
                    for (int b = 0; b < 4; ++b) sec.bytes.push_back(static_cast<unsigned char>((addend[k] >> (8 * b)) & 0xFF));
                    Reloc rl; rl.offset = at + 4 * static_cast<unsigned long>(k); rl.symbol = relocTo[k]; rl.type = RelAddr32Nb;
                    sec.relocs.push_back(rl);
                }
            }
            SymbolEntry u;
            u.name = names[part]; u.value = at; u.section = static_cast<int>(si + 1); u.type = 0; u.storageClass = 3; u.isSection = false; u.selection = 0;
            if (symbolIndex.count(u.name)) { result.errors.push_back("duplicate symbol " + u.name); continue; }
            symbolIndex[u.name] = 0;
            if (blocks[i].comdat) comdatSymbols.back().push_back(u); else plainSymbols.push_back(u);
            CoffSymbolInfo mi; mi.name = u.name; mi.section = u.section; mi.offset = at;
            mi.size = static_cast<unsigned long>(sec.bytes.size()) - at; mi.isFunction = false; mi.external = false;
            result.symbolList.push_back(mi);
        }
    }
    result.functions = static_cast<int>(blocks.size());
    for (size_t p = 0; p < pieces.size(); ++p) {
        DataPiece& dp = *pieces[p];
        Chunk& c = dp.chunk;
        result.errors.insert(result.errors.end(), c.errors.begin(), c.errors.end());
        Section* target;
        int sectionNumber;
        if (dp.comdat) {
            Section s; s.name = dp.kind == 2 ? ".data" : ".rdata"; s.characteristics = (dp.kind == 2 ? kDataFlags : kRdataFlags) | kComdat; s.selection = kSelectAny;
            secs.push_back(s);
            comdatSymbols.push_back(std::vector<SymbolEntry>());
            target = &secs.back(); sectionNumber = static_cast<int>(secs.size());
        } else if (dp.kind == 4) { target = &secs[crtIndex]; sectionNumber = static_cast<int>(crtIndex + 1); }
        else { target = &secs[static_cast<size_t>(dp.kind - 1)]; sectionNumber = dp.kind; }
        unsigned long base = static_cast<unsigned long>(target->bytes.size());
        if (c.maxAlign > target->align) target->align = c.maxAlign;
        target->bytes.insert(target->bytes.end(), c.bytes.begin(), c.bytes.end());
        for (size_t r = 0; r < c.relocs.size(); ++r) { Reloc rl = c.relocs[r]; rl.offset += base; target->relocs.push_back(rl); }
        for (size_t k = 0; k < c.symbols.size(); ++k) {
            SymbolEntry f;
            f.name = c.symbols[k].first; f.value = base + c.symbols[k].second; f.section = sectionNumber; f.type = 0;
            f.storageClass = publics.count(f.name) ? 2 : 3; f.isSection = false; f.selection = 0;
            if (symbolIndex.count(f.name)) { result.errors.push_back("duplicate symbol " + f.name); continue; }
            symbolIndex[f.name] = 0;
            if (dp.comdat) comdatSymbols.back().push_back(f); else plainSymbols.push_back(f);
            CoffSymbolInfo mi; mi.name = f.name; mi.section = sectionNumber; mi.offset = f.value; mi.size = symbolSize(c, k);
            mi.isFunction = false; mi.external = f.storageClass == 2;
            result.symbolList.push_back(mi);
        }
    }
    for (size_t p = 0; p < pieces.size(); ++p) delete pieces[p];
    pieces.clear();
    // a section whose contents need more than its default alignment (16, or 8 for the initialisers) says so in its
    // IMAGE_SCN_ALIGN_* bits, which the linker honours when it places the section (a 32- or 64-byte aligned variable)
    for (size_t s = 0; s < secs.size(); ++s) {
        const unsigned long bits = (secs[s].characteristics >> 20) & 0xF;
        const unsigned long declared = bits ? 1UL << (bits - 1) : 16UL;
        if (secs[s].align <= declared) continue;
        unsigned long k = 1;
        while ((1UL << (k - 1)) < secs[s].align && k < 14) ++k;
        secs[s].characteristics = (secs[s].characteristics & ~0x00F00000UL) | (k << 20);
    }
    for (size_t s = 0; s < secs.size(); ++s) {
        CoffSectionInfo si; si.number = static_cast<int>(s + 1); si.name = secs[s].name; si.size = static_cast<unsigned long>(secs[s].bytes.size());
        si.isCode = secs[s].isCode; si.comdat = (secs[s].characteristics & kComdat) != 0;
        result.sectionList.push_back(si);
    }
    std::stable_sort(result.symbolList.begin(), result.symbolList.end(), SymbolInfoOrder());

    // symbol table: the plain section symbols, the plain symbols, each COMDAT section symbol with its symbols, the externals
    for (size_t s = 0; s < plainSections; ++s) {
        SymbolEntry se; se.name = secs[s].name; se.value = 0; se.section = static_cast<int>(s + 1); se.type = 0; se.storageClass = 3; se.isSection = true; se.selection = 0;
        symbols.push_back(se);
    }
    for (size_t i = 0; i < plainSymbols.size(); ++i) { symbolIndex[plainSymbols[i].name] = symbols.size(); symbols.push_back(plainSymbols[i]); }
    for (size_t k = 0; k < comdatSymbols.size(); ++k) {
        size_t s = plainSections + k;
        SymbolEntry se; se.name = secs[s].name; se.value = 0; se.section = static_cast<int>(s + 1); se.type = 0; se.storageClass = 3; se.isSection = true; se.selection = secs[s].selection;
        symbols.push_back(se);
        for (size_t i = 0; i < comdatSymbols[k].size(); ++i) { symbolIndex[comdatSymbols[k][i].name] = symbols.size(); symbols.push_back(comdatSymbols[k][i]); }
    }
    // externals: the declared ones first, then anything referenced but never defined (sorted, for determinism)
    std::set<std::string> undefined;
    for (size_t s = 0; s < secs.size(); ++s)
        for (size_t r = 0; r < secs[s].relocs.size(); ++r)
            if (!symbolIndex.count(secs[s].relocs[r].symbol) && !externSet.count(secs[s].relocs[r].symbol)) undefined.insert(secs[s].relocs[r].symbol);
    for (size_t e = 0; e < externs.size(); ++e) {
        if (symbolIndex.count(externs[e])) continue;    // declared EXTERN but defined here as well
        SymbolEntry f; f.name = externs[e]; f.value = 0; f.section = 0; f.type = 0; f.storageClass = 2; f.isSection = false; f.selection = 0;
        symbolIndex[f.name] = symbols.size(); symbols.push_back(f);
    }
    for (std::set<std::string>::const_iterator u = undefined.begin(); u != undefined.end(); ++u) {
        SymbolEntry f; f.name = *u; f.value = 0; f.section = 0; f.type = 0; f.storageClass = 2; f.isSection = false; f.selection = 0;
        symbolIndex[f.name] = symbols.size(); symbols.push_back(f);
    }
    if (!result.errors.empty()) return false;

    // symbol table indices count auxiliary records
    std::vector<unsigned long> tableIndex(symbols.size());
    unsigned long idx = 0;
    for (size_t i = 0; i < symbols.size(); ++i) { tableIndex[i] = idx; idx += symbols[i].isSection ? 2 : 1; }
    unsigned long symbolCount = idx;

    std::string o;
    const size_t n = secs.size();
    unsigned long headerSize = 20 + static_cast<unsigned long>(n) * 40;
    unsigned long offset = headerSize;
    std::vector<unsigned long> rawOffset(n), relocOffset(n), relocCount(n);
    for (size_t s = 0; s < n; ++s) {
        rawOffset[s] = secs[s].bytes.empty() ? 0 : offset;
        offset += static_cast<unsigned long>(secs[s].bytes.size());
        relocCount[s] = static_cast<unsigned long>(secs[s].relocs.size());
        relocOffset[s] = relocCount[s] ? offset : 0;
        offset += (relocCount[s] + (relocCount[s] >= 0xFFFF ? 1 : 0)) * 10;
    }
    unsigned long symbolTableOffset = offset;

    put16(o, 0x8664); put16(o, static_cast<unsigned long>(n)); put32(o, 0); put32(o, symbolTableOffset); put32(o, symbolCount); put16(o, 0); put16(o, 0);
    for (size_t s = 0; s < n; ++s) {
        std::string nm = secs[s].name; nm.resize(8, '\0'); o += nm;
        put32(o, 0); put32(o, 0);                                   // VirtualSize, VirtualAddress
        put32(o, static_cast<unsigned long>(secs[s].bytes.size()));
        put32(o, rawOffset[s]); put32(o, relocOffset[s]); put32(o, 0);
        put16(o, relocCount[s] >= 0xFFFF ? 0xFFFF : relocCount[s]); put16(o, 0);
        put32(o, secs[s].characteristics | (relocCount[s] >= 0xFFFF ? 0x01000000UL : 0));   // LNK_NRELOC_OVFL
    }
    for (size_t s = 0; s < n; ++s) {
        o.append(reinterpret_cast<const char*>(secs[s].bytes.empty() ? 0 : &secs[s].bytes[0]), secs[s].bytes.size());
        if (relocCount[s] >= 0xFFFF) { put32(o, relocCount[s] + 1); put32(o, 0); put16(o, 0); }
        for (size_t r = 0; r < secs[s].relocs.size(); ++r) {
            const Reloc& rl = secs[s].relocs[r];
            put32(o, rl.offset);
            put32(o, tableIndex[symbolIndex[rl.symbol]]);
            put16(o, rl.type);
        }
    }
    std::string strings;
    put32(strings, 0);   // length, patched below
    for (size_t i = 0; i < symbols.size(); ++i) {
        const SymbolEntry& s = symbols[i];
        if (s.name.size() <= 8) { std::string nm = s.name; nm.resize(8, '\0'); o += nm; }
        else { put32(o, 0); put32(o, static_cast<unsigned long>(strings.size())); strings += s.name; strings += '\0'; }
        put32(o, s.value); put16(o, static_cast<unsigned long>(s.section)); put16(o, static_cast<unsigned long>(s.type));
        put8(o, s.storageClass); put8(o, s.isSection ? 1 : 0);
        if (s.isSection) {
            const Section& sec = secs[static_cast<size_t>(s.section - 1)];
            put32(o, static_cast<unsigned long>(sec.bytes.size()));
            put16(o, sec.relocs.size() >= 0xFFFF ? 0xFFFF : static_cast<unsigned long>(sec.relocs.size()));
            put16(o, 0); put32(o, 0); put16(o, static_cast<unsigned long>(sec.associated)); put8(o, s.selection); put8(o, 0); put8(o, 0); put8(o, 0);
        }
    }
    unsigned long sl = static_cast<unsigned long>(strings.size());
    for (int i = 0; i < 4; ++i) strings[i] = static_cast<char>((sl >> (8 * i)) & 0xFF);
    o += strings;

    unsigned long relocations = 0, dataBytes = 0, rdataBytes = 0;
    for (size_t s = 0; s < n; ++s) {
        relocations += static_cast<unsigned long>(secs[s].relocs.size());
        if (secs[s].isCode) continue;
        if (secs[s].name == ".data") dataBytes += static_cast<unsigned long>(secs[s].bytes.size()); else rdataBytes += static_cast<unsigned long>(secs[s].bytes.size());
    }
    result.relocations = static_cast<int>(relocations);
    result.symbols = static_cast<int>(symbols.size());
    result.textBytes = textBytes;
    result.dataBytes = dataBytes;
    result.rdataBytes = rdataBytes;
    result.fileBytes = static_cast<unsigned long>(o.size());
    objectBytes.swap(o);
    return true;
}

bool writeCoffObject(const std::string& masmText, const std::string& objPath, int threads, CoffResult& result)
{
    std::string bytes;
    if (!assembleCoff(masmText, threads, bytes, result)) return false;
    std::ofstream out(objPath.c_str(), std::ios::binary);
    if (!out) { result.errors.push_back("cannot write " + objPath); return false; }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

} // namespace cppparse
