// IrBuilder.cpp - version 3 IR generation from the old body parser AST, and the class layouts of the old model.
#include "IrBuilder.hpp"
#include "Parallel.hpp"
#include "PathUtil.hpp"
#include "TypeCheck.hpp"
#include <cstdlib>
#include <sstream>

namespace cppparse {

int Layouts::sizeOf(const Type* t)
{
    if (!t) return 8;
    switch (t->kind) {
    case Type::Class: return classSize(t->cls);
    case Type::Array: return sizeOf(t->inner) * (t->count > 0 ? t->count : 1);
    case Type::Unknown: return 8;
    case Type::Enum: return 4;
    default: { int n = t->builtinSize(); return n > 0 ? n : 8; }
    }
}

int Layouts::alignOf(const Type* t)
{
    if (!t) return 8;
    if (t->kind == Type::Class) return t->cls ? layout(t->cls).align : 8;
    if (t->kind == Type::Array) return alignOf(t->inner);
    int n = sizeOf(t);
    return n >= 8 ? 8 : (n >= 4 ? 4 : (n >= 2 ? 2 : 1));
}

int Layouts::classSize(const ClassInfo* cls)
{
    if (!cls || !cls->isDefinition) return 8;
    Lock l(semanticMutex());
    return layout(cls).size;
}

bool Layouts::opaqueByValue(const Type* t)
{
    if (!t) return false;
    if (t->kind == Type::Array) return opaqueByValue(t->inner);
    if (t->kind == Type::Class) {
        if (!t->cls || !t->cls->isDefinition) return false;
        Lock l(semanticMutex());
        return layout(t->cls).opaque;
    }
    if (t->kind != Type::Unknown) return false;
    // unresolved names are mostly scalar typedefs (std::size_t, time_t) that the 8 bytes assumed for them
    // fit; a template instance or a known library class is an object of unknown size
    if (t->name.find('<') != std::string::npos) return true;
    static const char* const kClasses[] = { "string", "wstring", "basic_string", "vector", "list", "deque", "map", "multimap",
                                            "set", "multiset", "pair", "ostream", "istream", "iostream", "ofstream", "ifstream",
                                            "fstream", "ostringstream", "istringstream", "stringstream", "exception", 0 };
    std::string n = t->name;
    if (n.compare(0, 5, "std::") == 0) n = n.substr(5);
    for (int i = 0; kClasses[i]; ++i) if (n == kClasses[i]) return true;
    return false;
}

bool Layouts::hasVtable(const ClassInfo* cls)
{
    if (!cls || !cls->isDefinition) return false;
    Lock l(semanticMutex());
    return !layout(cls).vtable.empty();
}

namespace {

std::string methodKey(const FunctionInfo& fn)
{
    std::string k = fn.name + "(";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        std::string t;
        for (size_t c = 0; c < fn.params[i].type.size(); ++c) if (fn.params[i].type[c] != ' ') t += fn.params[i].type[c];
        k += t + ";";
    }
    k += ")";
    if (fn.isConst) k += "c";
    if (fn.isDestructor) k = "~dtor";
    return k;
}

} // namespace

const Layouts::Layout& Layouts::layout(const ClassInfo* cls)
{
    Lock l(semanticMutex());
    std::map<const ClassInfo*, Layout>::iterator it = m_cache.find(cls);
    if (it != m_cache.end()) return it->second;
    Layout& L = m_cache[cls];   // inserted first: recursion through pointers to self terminates
    int offset = 0;
    bool anyBaseVptr = false;
    // bases
    for (size_t b = 0; b < cls->bases.size(); ++b) {
        const ClassInfo* base = cls->bases[b].resolved;
        if (!base || !base->isDefinition || base == cls) continue;
        const Layout& BL = layout(base);
        if (BL.align > L.align) L.align = BL.align;
        offset = (offset + BL.align - 1) / BL.align * BL.align;
        L.bases[base] = offset;
        if (BL.opaque) L.opaque = true;
        L.order.push_back("+" + intToString(offset) + " base " + base->qualifiedName);
        if (b == 0) L.vtable = BL.vtable; else for (size_t v = 0; v < BL.vtable.size(); ++v) L.vtable.push_back(base->name + "::" + BL.vtable[v]);
        if (BL.vptr) anyBaseVptr = true;
        offset += BL.size;
    }
    // own virtuals
    bool ownVirtual = false;
    for (size_t m = 0; m < cls->methods.size(); ++m) if (cls->methods[m].isVirtual || cls->methods[m].isPureVirtual) ownVirtual = true;
    for (size_t m = 0; m < cls->methods.size(); ++m) {
        const FunctionInfo& fn = cls->methods[m];
        std::string key = methodKey(fn);
        bool inherited = false;
        for (size_t v = 0; v < L.vtable.size(); ++v) if (L.vtable[v] == key) { inherited = true; break; }
        if (inherited) continue;
        if (fn.isVirtual || fn.isPureVirtual) L.vtable.push_back(key);
    }
    if ((ownVirtual || !L.vtable.empty()) && !anyBaseVptr) {
        // vptr goes first: shift bases and fields by 8
        std::map<const ClassInfo*, int> shifted;
        for (std::map<const ClassInfo*, int>::iterator bi = L.bases.begin(); bi != L.bases.end(); ++bi) shifted[bi->first] = bi->second + 8;
        L.bases = shifted;
        for (size_t o = 0; o < L.order.size(); ++o) {
            size_t sp = L.order[o].find(' ');
            int old = std::atoi(L.order[o].c_str() + 1);
            L.order[o] = "+" + intToString(old + 8) + L.order[o].substr(sp);
        }
        L.vptr = true;
        L.order.insert(L.order.begin(), "+0 vptr");
        offset += 8;
        if (L.align < 8) L.align = 8;
    }
    // fields
    for (size_t f = 0; f < cls->fields.size(); ++f) {
        const VariableInfo& v = cls->fields[f];
        Field fld;
        fld.type = m_types.parse(v.type, cls->qualifiedName, cls);
        if (v.isStatic) { fld.isStatic = true; L.fields[v.name] = fld; continue; }
        if (opaqueByValue(fld.type)) L.opaque = true;
        int size = sizeOf(fld.type), align = alignOf(fld.type);
        if (!v.bitField.empty()) { size = 4; align = 4; }
        if (align > L.align) L.align = align;
        offset = (offset + align - 1) / align * align;
        fld.offset = offset;
        L.fields[v.name] = fld;
        L.order.push_back("+" + intToString(offset) + " " + (fld.type ? fld.type->str() : v.type) + " " + v.name);
        offset += size;
    }
    if (offset == 0) offset = 1;
    L.size = (offset + L.align - 1) / L.align * L.align;
    return L;
}

bool Layouts::field(const ClassInfo* cls, const std::string& name, Field& out)
{
    if (!cls || !cls->isDefinition) return false;
    Lock l(semanticMutex());
    const Layout& L = layout(cls);
    std::map<std::string, Field>::const_iterator it = L.fields.find(name);
    if (it != L.fields.end()) { out = it->second; return true; }
    for (std::map<const ClassInfo*, int>::const_iterator b = L.bases.begin(); b != L.bases.end(); ++b) {
        Field f;
        if (field(b->first, name, f)) { f.offset += b->second; out = f; return true; }
    }
    return false;
}

int Layouts::baseOffset(const ClassInfo* cls, const ClassInfo* base)
{
    if (!cls || !base) return -1;
    if (cls == base) return 0;
    if (!cls->isDefinition) return -1;
    Lock l(semanticMutex());
    const Layout& L = layout(cls);
    for (std::map<const ClassInfo*, int>::const_iterator b = L.bases.begin(); b != L.bases.end(); ++b) {
        int off = baseOffset(b->first, base);
        if (off >= 0) return b->second + off;
    }
    return -1;
}

int Layouts::vtableSlot(const ClassInfo* cls, const FunctionInfo* method)
{
    if (!cls || !method) return -1;
    Lock l(semanticMutex());
    const Layout& L = layout(cls);
    std::string key = methodKey(*method);
    for (size_t v = 0; v < L.vtable.size(); ++v) if (L.vtable[v] == key) return static_cast<int>(v);
    if (method->ownerClass && method->ownerClass != cls) return vtableSlot(method->ownerClass, method);
    return -1;
}

std::string Layouts::describe(const ClassInfo* cls)
{
    if (!cls || !cls->isDefinition) return "";
    Lock l(semanticMutex());
    const Layout& L = layout(cls);
    std::string s = std::string(ClassInfo::kindName(cls->kind)) + " " + cls->qualifiedName + ": size " + intToString(L.size) + ", align " + intToString(L.align) + " {";
    for (size_t o = 0; o < L.order.size(); ++o) s += (o ? "; " : " ") + L.order[o];
    s += " }";
    if (!L.vtable.empty()) {
        s += "  vtable:";
        for (size_t v = 0; v < L.vtable.size(); ++v) s += " [" + intToString(static_cast<int>(v)) + "] " + L.vtable[v];
    }
    return s;
}


IrBuilder::IrBuilder(const CodeBase& cb, TypeTable& types, Layouts& layouts)
    : m_cb(cb), m_types(types), m_layouts(layouts), m_ast(0), m_out(0), m_block(0), m_catchDepth(0), m_tryCount(0)
{
}

IrInst IrBuilder::make(IrInst::Op op, const Type* type, int line)
{
    IrInst in;
    in.op = op;
    in.type = type;
    in.line = line;
    return in;
}

bool IrBuilder::terminated() const
{
    const IrBlock& b = m_out->blocks[static_cast<size_t>(m_block)];
    return !b.insts.empty() && b.insts.back().isTerminator();
}

int IrBuilder::emit(IrInst inst)
{
    if (terminated()) {
        // code after a terminator: give it a dead block so the IR stays well formed
        setBlock(m_out->newBlock("dead"));
    }
    bool producesValue = inst.op == IrInst::Const || inst.op == IrInst::FConst || inst.op == IrInst::StrConst || inst.op == IrInst::Copy ||
                         inst.op == IrInst::LoadLocal || inst.op == IrInst::AddrLocal || inst.op == IrInst::LoadGlobal || inst.op == IrInst::AddrGlobal ||
                         inst.op == IrInst::LoadMem || inst.op == IrInst::AddrField || inst.op == IrInst::AddrIndex || inst.op == IrInst::Bin ||
                         inst.op == IrInst::Un || inst.op == IrInst::Cmp || inst.op == IrInst::Conv || inst.op == IrInst::CatchParam ||
                         ((inst.op == IrInst::Call || inst.op == IrInst::VCall) && inst.type && !inst.type->isVoid());
    if (producesValue && inst.dst < 0) inst.dst = m_out->newTemp(inst.type);
    m_out->blocks[static_cast<size_t>(m_block)].insts.push_back(inst);
    return inst.dst;
}

void IrBuilder::br(int target, int line)
{
    if (terminated()) return;
    IrInst in = make(IrInst::Br, 0, line);
    in.args.push_back(IrOperand::block(target));
    emit(in);
}

void IrBuilder::condbr(int cond, int t, int f, int line)
{
    IrInst in = make(IrInst::CondBr, 0, line);
    in.args.push_back(IrOperand::temp(cond, m_types.boolType()));
    in.args.push_back(IrOperand::block(t));
    in.args.push_back(IrOperand::block(f));
    emit(in);
}

int IrBuilder::constant(long v, const Type* t, int line)
{
    IrInst in = make(IrInst::Const, t, line);
    in.args.push_back(IrOperand::imm(v, t));
    return emit(in);
}

int IrBuilder::thisTemp(int line)
{
    IrInst in = make(IrInst::LoadLocal, m_out->locals[0].type, line);
    in.args.push_back(IrOperand::local(0, m_out->locals[0].type));
    return emit(in);
}

int IrBuilder::localIndexFor(const Expr* e) const
{
    if (e->sym == Expr::SymLocal) return e->index >= 0 && static_cast<size_t>(e->index) < m_localMap.size() ? m_localMap[static_cast<size_t>(e->index)] : -1;
    if (e->sym == Expr::SymParam) return e->index + (m_out->hasThis ? 1 : 0);
    return -1;
}

int IrBuilder::toBool(int temp, const Type* t, int line)
{
    t = m_types.decay(t);
    if (!t || t->kind == Type::Bool) return temp;
    IrInst in = make(IrInst::Cmp, m_types.boolType(), line);
    if (t->isFloating()) { in.sub = "fne"; in.args.push_back(IrOperand::temp(temp, t)); in.args.push_back(IrOperand::fimm(0.0, t)); }
    else { in.sub = "ne"; in.args.push_back(IrOperand::temp(temp, t)); in.args.push_back(IrOperand::imm(0, t)); }
    return emit(in);
}

int IrBuilder::convert(int temp, const Type* from, const Type* to, int line)
{
    from = m_types.decay(from);
    to = m_types.unref(to);
    if (!from || !to || from == to) return temp;
    if (to->kind == Type::Array || to->kind == Type::Class || from->kind == Type::Class || to->isUnknown() || from->isUnknown()) {
        // derived -> base pointer adjustment
        if (from->kind == Type::Pointer && to->kind == Type::Pointer && from->inner->kind == Type::Class && to->inner->kind == Type::Class && from->inner->cls != to->inner->cls) {
            int off = m_layouts.baseOffset(from->inner->cls, to->inner->cls);
            if (off > 0) {
                IrInst in = make(IrInst::AddrField, to, line);
                in.sub = "base " + to->inner->cls->name;
                in.extra = off;
                in.args.push_back(IrOperand::temp(temp, from));
                return emit(in);
            }
        }
        return temp;
    }
    if (to->kind == Type::Bool) return toBool(temp, from, line);
    std::string sub;
    if (from->isIntegral() && to->isIntegral()) {
        int fs = from->builtinSize(), ts = to->builtinSize();
        if (fs == ts) return temp;
        sub = ts < fs ? "trunc" : (from->isUnsigned || from->kind == Type::Bool ? "zext" : "sext");
    } else if (from->isIntegral() && to->isFloating()) sub = from->isUnsigned ? "uitofp" : "sitofp";
    else if (from->isFloating() && to->isIntegral()) sub = "fptosi";
    else if (from->isFloating() && to->isFloating()) sub = to->builtinSize() > from->builtinSize() ? "fpext" : "fptrunc";
    else if (from->isPointer() && to->isIntegral()) sub = "ptrtoint";
    else if (from->isIntegral() && to->isPointer()) sub = "inttoptr";
    else if (from->kind == Type::Pointer && to->kind == Type::Pointer) {
        if (from->inner->kind == Type::Class && to->inner->kind == Type::Class && from->inner->cls != to->inner->cls) {
            int off = m_layouts.baseOffset(from->inner->cls, to->inner->cls);
            if (off > 0) {
                IrInst in = make(IrInst::AddrField, to, line);
                in.sub = "base " + to->inner->cls->name;
                in.extra = off;
                in.args.push_back(IrOperand::temp(temp, from));
                return emit(in);
            }
        }
        return temp;
    }
    else return temp;
    IrInst in = make(IrInst::Conv, to, line);
    in.sub = sub;
    in.args.push_back(IrOperand::temp(temp, from));
    return emit(in);
}

int IrBuilder::load(const LValue& lv, const Type* t, int line)
{
    const Type* u = m_types.unref(t);
    if (u && (u->kind == Type::Class || u->kind == Type::Array)) {
        // objects and arrays are handled by address
        if (lv.kind == LValue::Local) {
            m_out->locals[static_cast<size_t>(lv.local)].addressTaken = true;
            IrInst in = make(IrInst::AddrLocal, m_types.pointer(u), line);
            in.args.push_back(IrOperand::local(lv.local, u));
            return emit(in);
        }
        if (lv.kind == LValue::Global) { IrInst in = make(IrInst::AddrGlobal, m_types.pointer(u), line); in.args.push_back(IrOperand::global(lv.global, u)); return emit(in); }
        return lv.addr;
    }
    if (lv.kind == LValue::Local) { IrInst in = make(IrInst::LoadLocal, u, line); in.args.push_back(IrOperand::local(lv.local, u)); return emit(in); }
    if (lv.kind == LValue::Global) { IrInst in = make(IrInst::LoadGlobal, u, line); in.args.push_back(IrOperand::global(lv.global, u)); return emit(in); }
    if (lv.kind == LValue::Mem) { IrInst in = make(IrInst::LoadMem, u, line); in.args.push_back(IrOperand::temp(lv.addr, m_types.pointer(u))); return emit(in); }
    return constant(0, u, line);
}

void IrBuilder::store(const LValue& lv, int temp, const Type* t, int line)
{
    const Type* u = m_types.unref(t);
    if (u && (u->kind == Type::Class || u->kind == Type::Array)) {
        // memberwise copy of an object: __copy(dst, src, size)
        LValue dst = lv;
        int dstAddr = -1;
        if (dst.kind == LValue::Local) { m_out->locals[static_cast<size_t>(dst.local)].addressTaken = true; IrInst a = make(IrInst::AddrLocal, m_types.pointer(u), line); a.args.push_back(IrOperand::local(dst.local, u)); dstAddr = emit(a); }
        else if (dst.kind == LValue::Global) { IrInst a = make(IrInst::AddrGlobal, m_types.pointer(u), line); a.args.push_back(IrOperand::global(dst.global, u)); dstAddr = emit(a); }
        else dstAddr = dst.addr;
        IrInst c = make(IrInst::Call, m_types.voidType(), line);
        c.sub = "__copy";
        c.args.push_back(IrOperand::temp(dstAddr, m_types.pointer(u)));
        c.args.push_back(IrOperand::temp(temp, m_types.pointer(u)));
        c.args.push_back(IrOperand::imm(m_layouts.sizeOf(u), m_types.intType()));
        emit(c);
        return;
    }
    if (lv.kind == LValue::Local) { IrInst in = make(IrInst::StoreLocal, u, line); in.args.push_back(IrOperand::local(lv.local, u)); in.args.push_back(IrOperand::temp(temp, u)); emit(in); return; }
    if (lv.kind == LValue::Global) { IrInst in = make(IrInst::StoreGlobal, u, line); in.args.push_back(IrOperand::global(lv.global, u)); in.args.push_back(IrOperand::temp(temp, u)); emit(in); return; }
    if (lv.kind == LValue::Mem) { IrInst in = make(IrInst::StoreMem, u, line); in.args.push_back(IrOperand::temp(lv.addr, m_types.pointer(u))); in.args.push_back(IrOperand::temp(temp, u)); emit(in); return; }
    m_out->notes.push_back("L" + intToString(line) + ": store to a non-lvalue was dropped");
}

IrBuilder::LValue IrBuilder::lvalue(Expr* e)
{
    LValue lv;
    lv.type = m_types.unref(e->type);
    const int line = e->line;
    switch (e->kind) {
    case Expr::Name: {
        if (e->sym == Expr::SymLocal || e->sym == Expr::SymParam) {
            int idx = localIndexFor(e);
            if (idx < 0) break;
            const Type* declared = m_out->locals[static_cast<size_t>(idx)].type;
            if (declared && declared->kind == Type::Reference) {
                IrInst in = make(IrInst::LoadLocal, m_types.pointer(declared->inner), line);
                in.args.push_back(IrOperand::local(idx, declared));
                lv.kind = LValue::Mem; lv.addr = emit(in); lv.type = declared->inner;
                return lv;
            }
            lv.kind = LValue::Local; lv.local = idx;
            return lv;
        }
        if (e->sym == Expr::SymField && e->var) {
            if (e->var->isStatic) { lv.kind = LValue::Global; lv.global = e->cls->qualifiedName + "::" + e->var->name; return lv; }
            if (!m_out->hasThis) break;
            Layouts::Field f;
            if (!m_layouts.field(m_out->locals[0].type->inner->cls, e->var->name, f)) {
                // field of a class this method's class does not derive from (unusual): take the owner's layout
                m_layouts.field(e->cls, e->var->name, f);
            }
            int self = thisTemp(line);
            IrInst in = make(IrInst::AddrField, m_types.pointer(lv.type), line);
            in.sub = e->var->name;
            in.extra = f.offset;
            in.args.push_back(IrOperand::temp(self, m_out->locals[0].type));
            lv.kind = LValue::Mem; lv.addr = emit(in);
            return lv;
        }
        if (e->sym == Expr::SymGlobal && e->var) { lv.kind = LValue::Global; lv.global = e->var->fullName(); return lv; }
        break;
    }
    case Expr::Member: {
        if (e->sym == Expr::SymField && e->var && e->var->isStatic) { lv.kind = LValue::Global; lv.global = e->cls->qualifiedName + "::" + e->var->name; return lv; }
        if (e->sym != Expr::SymField || !e->var) break;
        Expr* obj = e->kids[0];
        int base;
        const Type* objT = m_types.unref(obj->type);
        const ClassInfo* objCls = 0;
        if (e->arrow) {
            base = value(obj);
            if (objT && objT->kind == Type::Pointer && objT->inner->kind == Type::Class) objCls = objT->inner->cls;
        } else {
            LValue ol = lvalue(obj);
            if (ol.kind == LValue::None) base = value(obj);
            else base = load(ol, objT, line);   // objects load as their address
            if (objT && objT->kind == Type::Class) objCls = objT->cls;
        }
        Layouts::Field f;
        if (!m_layouts.field(objCls ? objCls : e->cls, e->var->name, f)) m_layouts.field(e->cls, e->var->name, f);
        IrInst in = make(IrInst::AddrField, m_types.pointer(lv.type), line);
        in.sub = e->var->name;
        in.extra = f.offset;
        in.args.push_back(IrOperand::temp(base, m_types.pointer(m_types.classType(objCls ? objCls : e->cls))));
        lv.kind = LValue::Mem; lv.addr = emit(in);
        return lv;
    }
    case Expr::Index: {
        if (e->func) break;   // operator[] handled as a call
        Expr* base = e->kids[0];
        const Type* bt = m_types.unref(base->type);
        int basePtr;
        if (bt && bt->kind == Type::Array) { LValue bl = lvalue(base); basePtr = bl.kind == LValue::None ? value(base) : load(bl, bt, line); }
        else basePtr = value(base);
        int idx = convert(value(e->kids[1]), e->kids[1]->type, m_types.builtin(Type::Long), line);
        IrInst in = make(IrInst::AddrIndex, m_types.pointer(lv.type), line);
        in.extra = m_layouts.sizeOf(lv.type);
        in.args.push_back(IrOperand::temp(basePtr, m_types.pointer(lv.type)));
        in.args.push_back(IrOperand::temp(idx, m_types.builtin(Type::Long)));
        lv.kind = LValue::Mem; lv.addr = emit(in);
        return lv;
    }
    case Expr::Unary:
        if (e->text == "*" && !e->func) { lv.kind = LValue::Mem; lv.addr = value(e->kids[0]); return lv; }
        if ((e->text == "++" || e->text == "--") && !e->func) { value(e); return lvalue(e->kids[0]); }
        break;
    case Expr::Cast:
        if (!e->kids.empty() && e->lvalue) return lvalue(e->kids[0]);
        break;
    case Expr::Call:
        if (e->lvalue) { lv.kind = LValue::Mem; lv.addr = value(e); return lv; }
        break;
    case Expr::Assign:
        value(e);
        return lvalue(e->kids[0]);
    case Expr::Comma:
        value(e->kids[0]);
        return lvalue(e->kids[1]);
    default:
        break;
    }
    return lv;   // None
}

int IrBuilder::binary(const std::string& op, int a, const Type* ta, int b, const Type* tb, const Type* result, int line)
{
    ta = m_types.decay(ta); tb = m_types.decay(tb); result = m_types.decay(result);
    bool cmp = op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
    // pointer arithmetic
    if (!cmp && ta->kind == Type::Pointer && tb->isIntegral() && (op == "+" || op == "-")) {
        int idx = convert(b, tb, m_types.builtin(Type::Long), line);
        if (op == "-") { IrInst n = make(IrInst::Un, m_types.builtin(Type::Long), line); n.sub = "neg"; n.args.push_back(IrOperand::temp(idx, m_types.builtin(Type::Long))); idx = emit(n); }
        IrInst in = make(IrInst::AddrIndex, ta, line);
        in.extra = m_layouts.sizeOf(ta->inner);
        in.args.push_back(IrOperand::temp(a, ta));
        in.args.push_back(IrOperand::temp(idx, m_types.builtin(Type::Long)));
        return emit(in);
    }
    if (!cmp && op == "+" && ta->isIntegral() && tb->kind == Type::Pointer) return binary(op, b, tb, a, ta, result, line);
    if (!cmp && op == "-" && ta->kind == Type::Pointer && tb->kind == Type::Pointer) {
        const Type* lng = m_types.builtin(Type::Long);
        IrInst d = make(IrInst::Bin, lng, line); d.sub = "sub"; d.args.push_back(IrOperand::temp(a, lng)); d.args.push_back(IrOperand::temp(b, lng));
        int diff = emit(d);
        IrInst q = make(IrInst::Bin, lng, line); q.sub = "sdiv"; q.args.push_back(IrOperand::temp(diff, lng)); q.args.push_back(IrOperand::imm(m_layouts.sizeOf(ta->inner) ? m_layouts.sizeOf(ta->inner) : 1, lng));
        return emit(q);
    }
    // common type
    const Type* common = result;
    if (cmp) {
        if (ta->isArithmetic() && tb->isArithmetic()) {
            const Type* pa = ta->rank() < 3 ? m_types.intType() : ta;
            const Type* pb = tb->rank() < 3 ? m_types.intType() : tb;
            if (pa->isFloating() || pb->isFloating()) common = (pa->kind == Type::Double || pb->kind == Type::Double) ? m_types.doubleType() : m_types.builtin(Type::Float);
            else common = pa->rank() >= pb->rank() ? m_types.builtin(pa->kind, pa->isUnsigned || (pb->rank() == pa->rank() && pb->isUnsigned)) : m_types.builtin(pb->kind, pb->isUnsigned);
        } else common = ta->isPointer() ? ta : tb;
    }
    if (common->isUnknown()) common = ta->isUnknown() ? tb : ta;
    a = convert(a, ta, common, line);
    b = convert(b, tb, common, line);
    bool fl = common->isFloating();
    bool uns = common->isUnsigned || common->isPointer();
    std::string sub;
    if (cmp) {
        if (op == "==") sub = fl ? "feq" : "eq";
        else if (op == "!=") sub = fl ? "fne" : "ne";
        else if (op == "<") sub = fl ? "flt" : (uns ? "ult" : "slt");
        else if (op == "<=") sub = fl ? "fle" : (uns ? "ule" : "sle");
        else if (op == ">") sub = fl ? "fgt" : (uns ? "ugt" : "sgt");
        else sub = fl ? "fge" : (uns ? "uge" : "sge");
        IrInst in = make(IrInst::Cmp, m_types.boolType(), line);
        in.sub = sub;
        in.args.push_back(IrOperand::temp(a, common));
        in.args.push_back(IrOperand::temp(b, common));
        return emit(in);
    }
    if (op == "+") sub = fl ? "fadd" : "add";
    else if (op == "-") sub = fl ? "fsub" : "sub";
    else if (op == "*") sub = fl ? "fmul" : "mul";
    else if (op == "/") sub = fl ? "fdiv" : (uns ? "udiv" : "sdiv");
    else if (op == "%") sub = uns ? "urem" : "srem";
    else if (op == "<<") sub = "shl";
    else if (op == ">>") sub = uns ? "lshr" : "ashr";
    else if (op == "&") sub = "and";
    else if (op == "|") sub = "or";
    else if (op == "^") sub = "xor";
    else sub = "add";
    IrInst in = make(IrInst::Bin, common, line);
    in.sub = sub;
    in.args.push_back(IrOperand::temp(a, common));
    in.args.push_back(IrOperand::temp(b, common));
    int r = emit(in);
    return convert(r, common, result, line);
}

void IrBuilder::condition(Expr* e, int trueBlock, int falseBlock)
{
    if (e->kind == Expr::Binary && e->text == "&&" && !e->func) {
        int mid = m_out->newBlock("and.rhs");
        condition(e->kids[0], mid, falseBlock);
        setBlock(mid);
        condition(e->kids[1], trueBlock, falseBlock);
        return;
    }
    if (e->kind == Expr::Binary && e->text == "||" && !e->func) {
        int mid = m_out->newBlock("or.rhs");
        condition(e->kids[0], trueBlock, mid);
        setBlock(mid);
        condition(e->kids[1], trueBlock, falseBlock);
        return;
    }
    if (e->kind == Expr::Unary && e->text == "!" && !e->func) { condition(e->kids[0], falseBlock, trueBlock); return; }
    int v = toBool(value(e), e->type, e->line);
    condbr(v, trueBlock, falseBlock, e->line);
}

namespace {

} // namespace

// "Derived;Base;GrandBase": the thrown class and every transitive base, matched by name by the
// exception runtime against the catch clauses; scalars give their type text
std::string throwDescriptor(const Type* t)
{
    if (!t) return "unknown";
    if (t->kind != Type::Class || !t->cls) return t->str();
    std::string d = t->cls->qualifiedName;
    std::vector<const ClassInfo*> todo;
    todo.push_back(t->cls);
    for (size_t i = 0; i < todo.size() && i < 64; ++i)
        for (size_t b = 0; b < todo[i]->bases.size(); ++b) {
            const ClassInfo* base = todo[i]->bases[b].resolved;
            if (base) { d += ";" + base->qualifiedName; todo.push_back(base); }
        }
    return d;
}

std::string irFunctionName(const FunctionInfo* f)
{
    // extern "C" functions keep their plain name so that generated code links against C symbols
    if (f->isExtern && !f->ownerClass && f->qualifier.empty() && f->scope.empty()) return f->name;
    return f->displayName() + "(" + f->paramText + ")" + (f->isConst ? " const" : "");
}

namespace {

std::string funcName(const FunctionInfo* f) { return irFunctionName(f); }

} // namespace

int IrBuilder::call(Expr* e)
{
    const int line = e->line;
    const FunctionInfo* f = e->func;
    const Type* rt = m_types.unref(e->type);
    IrInst in = make(f && f->isConstructor ? IrInst::Call : IrInst::Call, rt, line);

    // callee kinds
    Expr* callee = e->kids.empty() ? 0 : e->kids[0];   // new T has no kids
    size_t firstArg = 1;
    int thisPtr = -1;

    if (e->kind == Expr::New) firstArg = 0;

    if (f && f->isConstructor) {
        // object construction: temporary (functional cast) or new
        in.type = m_types.voidType();
        int addr;
        if (e->kind == Expr::New) {
            const Type* ct = m_types.classType(e->cls);
            IrInst n = make(IrInst::Call, m_types.pointer(ct), line);
            n.sub = "__new";
            n.args.push_back(IrOperand::imm(m_layouts.sizeOf(ct), m_types.intType()));
            addr = emit(n);
        } else {
            IrLocal tmp;
            tmp.name = "tmp." + e->cls->name;
            tmp.type = m_types.classType(e->cls);
            tmp.size = m_layouts.sizeOf(tmp.type);
            tmp.align = m_layouts.alignOf(tmp.type);
            tmp.addressTaken = true;
            m_out->locals.push_back(tmp);
            IrInst a = make(IrInst::AddrLocal, m_types.pointer(tmp.type), line);
            a.args.push_back(IrOperand::local(static_cast<int>(m_out->locals.size() - 1), tmp.type));
            addr = emit(a);
        }
        in.sub = funcName(f);
        in.args.push_back(IrOperand::temp(addr, m_types.pointer(m_types.classType(e->cls))));
        for (size_t a = firstArg; a < e->kids.size(); ++a) {
            size_t p = a - firstArg;
            const Type* pt = p < f->params.size() ? m_types.parse(f->params[p].type, f->ownerClass ? f->ownerClass->qualifiedName : f->scope, f->ownerClass) : 0;
            int v = value(e->kids[a]);
            if (pt) v = convert(v, e->kids[a]->type, pt, line);
            in.args.push_back(IrOperand::temp(v, pt ? m_types.decay(pt) : e->kids[a]->type));
        }
        emit(in);
        return addr;
    }

    if (callee && callee->kind == Expr::Member && f && f->ownerClass) {
        Expr* obj = callee->kids[0];
        if (callee->arrow) thisPtr = value(obj);
        else { LValue ol = lvalue(obj); thisPtr = ol.kind == LValue::None ? value(obj) : load(ol, obj->type, line); }
        const Type* objT = m_types.unref(obj->type);
        if (callee->arrow && objT && objT->kind == Type::Pointer) objT = objT->inner;
        if (objT && objT->kind == Type::Class && objT->cls != f->ownerClass) {
            int off = m_layouts.baseOffset(objT->cls, f->ownerClass);
            if (off > 0) { IrInst adj = make(IrInst::AddrField, m_types.pointer(m_types.classType(f->ownerClass)), line); adj.sub = "base " + f->ownerClass->name; adj.extra = off; adj.args.push_back(IrOperand::temp(thisPtr, m_types.pointer(objT))); thisPtr = emit(adj); }
        }
    } else if (callee && callee->kind == Expr::Name && f && f->ownerClass && !f->isStatic && m_out->hasThis) {
        thisPtr = thisTemp(line);
        const ClassInfo* self = m_out->locals[0].type->inner->cls;
        int off = m_layouts.baseOffset(self, f->ownerClass);
        if (off > 0) { IrInst adj = make(IrInst::AddrField, m_types.pointer(m_types.classType(f->ownerClass)), line); adj.sub = "base " + f->ownerClass->name; adj.extra = off; adj.args.push_back(IrOperand::temp(thisPtr, m_out->locals[0].type)); thisPtr = emit(adj); }
    }

    if (!f) {
        // unknown callee (library function, function pointer): opaque call
        std::string name = callee ? (callee->kind == Expr::Name ? callee->text : callee->kind == Expr::Member ? callee->text : "(indirect)") : "?";
        if (callee && callee->kind == Expr::Member) {
            Expr* obj = callee->kids[0];
            int objv;
            if (callee->arrow) objv = value(obj); else { LValue ol = lvalue(obj); objv = ol.kind == LValue::None ? value(obj) : load(ol, obj->type, line); }
            in.args.push_back(IrOperand::temp(objv, obj->type));
        } else if (callee && callee->kind == Expr::Name && (callee->sym == Expr::SymLocal || callee->sym == Expr::SymParam || callee->sym == Expr::SymGlobal || callee->sym == Expr::SymField)) {
            int fp = value(callee);
            in.args.push_back(IrOperand::temp(fp, callee->type));
            name = "(indirect)";
        }
        in.sub = name + (name == "(indirect)" ? "" : "?");
        const bool math = callee && callee->kind == Expr::Name && isDoubleMathFunction(name);   // arguments are doubles (xmm0..)
        for (size_t a = firstArg; a < e->kids.size(); ++a) {
            int v = value(e->kids[a]);
            const Type* at = m_types.decay(e->kids[a]->type);
            if (math && at && at->isArithmetic() && at->kind != Type::Double) { v = convert(v, at, m_types.doubleType(), line); at = m_types.doubleType(); }
            in.args.push_back(IrOperand::temp(v, at));
        }
        if (!rt || rt->isUnknown()) in.type = m_types.builtin(Type::Long);
        return emit(in);
    }

    in.sub = funcName(f);
    if (thisPtr >= 0) in.args.push_back(IrOperand::temp(thisPtr, m_types.pointer(m_types.classType(f->ownerClass))));
    for (size_t a = firstArg; a < e->kids.size(); ++a) {
        size_t p = a - firstArg;
        const Type* pt = p < f->params.size() && f->params[p].type != "..." ? m_types.parse(f->params[p].type, f->ownerClass ? f->ownerClass->qualifiedName : f->scope, f->ownerClass) : 0;
        int v;
        if (pt && pt->kind == Type::Reference) {
            LValue al = lvalue(e->kids[a]);
            if (al.kind == LValue::None) {
                // bind a temporary: store the value into a scratch local
                int tv = value(e->kids[a]);
                IrLocal tmp; tmp.name = "tmp.arg"; tmp.type = m_types.unref(pt->inner); tmp.size = m_layouts.sizeOf(tmp.type); tmp.align = m_layouts.alignOf(tmp.type); tmp.addressTaken = true;
                m_out->locals.push_back(tmp);
                LValue tl; tl.kind = LValue::Local; tl.local = static_cast<int>(m_out->locals.size() - 1); tl.type = tmp.type;
                if (tmp.type->kind != Type::Class) store(tl, convert(tv, e->kids[a]->type, tmp.type, line), tmp.type, line);
                IrInst ad = make(IrInst::AddrLocal, m_types.pointer(tmp.type), line); ad.args.push_back(IrOperand::local(tl.local, tmp.type));
                v = tmp.type->kind == Type::Class ? tv : emit(ad);
            } else {
                v = load(al, m_types.pointer(al.type), line);   // address: objects/arrays give their address
                if (al.type && al.type->kind != Type::Class && al.type->kind != Type::Array) {
                    if (al.kind == LValue::Local) { m_out->locals[static_cast<size_t>(al.local)].addressTaken = true; IrInst ad = make(IrInst::AddrLocal, m_types.pointer(al.type), line); ad.args.push_back(IrOperand::local(al.local, al.type)); v = emit(ad); }
                    else if (al.kind == LValue::Global) { IrInst ad = make(IrInst::AddrGlobal, m_types.pointer(al.type), line); ad.args.push_back(IrOperand::global(al.global, al.type)); v = emit(ad); }
                    else v = al.addr;
                }
            }
            in.args.push_back(IrOperand::temp(v, m_types.pointer(pt->inner)));
            continue;
        }
        v = value(e->kids[a]);
        if (pt) v = convert(v, e->kids[a]->type, pt, line);
        in.args.push_back(IrOperand::temp(v, pt ? m_types.decay(pt) : m_types.decay(e->kids[a]->type)));
    }
    if (e->isVirtualCall && thisPtr >= 0) {
        in.op = IrInst::VCall;
        in.extra = m_layouts.vtableSlot(f->ownerClass, f);
        if (in.extra < 0) in.op = IrInst::Call;
    }
    if (rt && rt->kind == Type::Reference) in.type = m_types.pointer(rt->inner);
    return emit(in);
}

int IrBuilder::value(Expr* e)
{
    if (!e) return -1;
    const int line = e->line;
    const Type* t = m_types.unref(e->type);
    if (!t) t = m_types.builtin(Type::Long);

    if (e->isConstant && e->kind != Expr::Call && e->kind != Expr::Assign && (t->isIntegral() || t->isFloating() || t->kind == Type::Null)) {
        if (t->isFloating()) { IrInst in = make(IrInst::FConst, t, line); in.args.push_back(IrOperand::fimm(e->fval, t)); return emit(in); }
        return constant(e->cval, t->kind == Type::Null ? m_types.pointer(m_types.voidType()) : t, line);
    }

    switch (e->kind) {
    case Expr::IntLit: case Expr::CharLit: case Expr::BoolLit: return constant(e->cval, t, line);
    case Expr::FloatLit: { IrInst in = make(IrInst::FConst, t, line); in.args.push_back(IrOperand::fimm(e->fval, t)); return emit(in); }
    case Expr::StringLit: { IrInst in = make(IrInst::StrConst, m_types.charPtr(), line); in.args.push_back(IrOperand::str(e->text, m_types.charPtr())); return emit(in); }
    case Expr::This: return m_out->hasThis ? thisTemp(line) : constant(0, t, line);
    case Expr::Name: {
        if (e->sym == Expr::SymFunction || e->sym == Expr::SymMethod) { IrInst in = make(IrInst::AddrGlobal, m_types.function(t), line); in.args.push_back(IrOperand::global(e->func ? funcName(e->func) : e->text, t)); return emit(in); }
        if (e->sym == Expr::SymEnumerator) return constant(e->cval, t, line);
        LValue lv = lvalue(e);
        if (lv.kind == LValue::None) { m_out->notes.push_back("L" + intToString(line) + ": unresolved name " + e->text + " read as 0"); return constant(0, t, line); }
        return load(lv, e->type, line);
    }
    case Expr::Member: {
        if (e->sym == Expr::SymEnumerator) return constant(e->cval, t, line);
        LValue lv = lvalue(e);
        if (lv.kind == LValue::None) { if (!e->kids.empty()) value(e->kids[0]); m_out->notes.push_back("L" + intToString(line) + ": unresolved member " + e->text + " read as 0"); return constant(0, t, line); }
        return load(lv, e->type, line);
    }
    case Expr::Index: {
        if (e->func) return call(e);   // operator[]: not reached (func set on Index means class operator) -> approximate
        LValue lv = lvalue(e);
        if (lv.kind == LValue::None) return constant(0, t, line);
        return load(lv, e->type, line);
    }
    case Expr::Call: return call(e);
    case Expr::New: {
        if (e->func) return call(e);
        const Type* elem = m_types.unref(e->type)->inner;
        IrInst n = make(IrInst::Call, e->type, line);
        n.sub = e->newArray ? "__new_array" : "__new";
        if (e->newArray) {
            int count = convert(value(e->kids[0]), e->kids[0]->type, m_types.builtin(Type::Long), line);
            n.args.push_back(IrOperand::imm(m_layouts.sizeOf(elem), m_types.intType()));
            n.args.push_back(IrOperand::temp(count, m_types.builtin(Type::Long)));
            return emit(n);
        }
        n.args.push_back(IrOperand::imm(m_layouts.sizeOf(elem), m_types.intType()));
        int p = emit(n);
        if (!e->kids.empty()) { LValue lv; lv.kind = LValue::Mem; lv.addr = p; lv.type = elem; store(lv, convert(value(e->kids[0]), e->kids[0]->type, elem, line), elem, line); }
        return p;
    }
    case Expr::Delete: {
        int p = value(e->kids[0]);
        const Type* pt = m_types.decay(e->kids[0]->type);
        if (pt && pt->kind == Type::Pointer && pt->inner->kind == Type::Class && pt->inner->cls) {
            const ClassInfo* cls = pt->inner->cls;
            for (size_t m = 0; m < cls->methods.size(); ++m)
                if (cls->methods[m].isDestructor) {
                    IrInst d = make(IrInst::Call, m_types.voidType(), line);
                    d.sub = funcName(&cls->methods[m]);
                    d.args.push_back(IrOperand::temp(p, pt));
                    int slot = (cls->methods[m].isVirtual) ? m_layouts.vtableSlot(cls, &cls->methods[m]) : -1;
                    if (slot >= 0) { d.op = IrInst::VCall; d.extra = slot; }
                    emit(d);
                    break;
                }
        }
        IrInst in = make(IrInst::Call, m_types.voidType(), line);
        in.sub = e->newArray ? "__delete_array" : "__delete";
        in.args.push_back(IrOperand::temp(p, pt));
        emit(in);
        return constant(0, m_types.intType(), line);
    }
    case Expr::SizeOf: {
        const Type* of = e->kids.empty() ? m_types.parse(e->typeText, m_ast->fn->ownerClass ? m_ast->fn->ownerClass->qualifiedName : m_ast->fn->scope, m_ast->fn->ownerClass) : m_types.decay(e->kids[0]->type);
        return constant(m_layouts.sizeOf(of), t, line);
    }
    case Expr::Cast: {
        if (e->kids.empty()) return constant(0, t, line);
        int v = value(e->kids[0]);
        return convert(v, e->kids[0]->type, t, line);
    }
    case Expr::Comma: value(e->kids[0]); return value(e->kids[1]);
    case Expr::Throw: {
        IrInst in = make(IrInst::Throw, m_types.voidType(), line);
        if (!e->kids.empty()) {
            int v = value(e->kids[0]);
            const Type* tt = m_types.decay(e->kids[0]->type);
            in.args.push_back(IrOperand::temp(v, tt));
            in.sub = throwDescriptor(tt);
            in.extra = tt && tt->kind == Type::Class ? m_layouts.sizeOf(tt) : 8;
        }
        emit(in);
        return constant(0, m_types.intType(), line);
    }
    case Expr::Unary: {
        if (e->func) {
            // overloaded operator: this = operand, no arguments
            Expr tmpCallee; tmpCallee.kind = Expr::Member; tmpCallee.kids.push_back(e->kids[0]); tmpCallee.sym = Expr::SymMethod; tmpCallee.type = e->kids[0]->type;
            Expr tmp; tmp.kind = Expr::Call; tmp.line = line; tmp.func = e->func; tmp.type = e->type; tmp.cls = e->cls; tmp.kids.push_back(&tmpCallee);
            if (!e->func->ownerClass) { tmp.kids[0] = 0; tmp.kids.push_back(e->kids[0]); tmp.kids[0] = &tmpCallee; }
            return call(&tmp);
        }
        const std::string& op = e->text;
        if (op == "&") {
            LValue lv = lvalue(e->kids[0]);
            if (lv.kind == LValue::Local) { m_out->locals[static_cast<size_t>(lv.local)].addressTaken = true; IrInst in = make(IrInst::AddrLocal, t, line); in.args.push_back(IrOperand::local(lv.local, lv.type)); return emit(in); }
            if (lv.kind == LValue::Global) { IrInst in = make(IrInst::AddrGlobal, t, line); in.args.push_back(IrOperand::global(lv.global, lv.type)); return emit(in); }
            if (lv.kind == LValue::Mem) return lv.addr;
            return value(e->kids[0]);
        }
        if (op == "*") { LValue lv; lv.kind = LValue::Mem; lv.addr = value(e->kids[0]); lv.type = t; return load(lv, t, line); }
        if (op == "++" || op == "--") {
            LValue lv = lvalue(e->kids[0]);
            if (lv.kind == LValue::None) return value(e->kids[0]);
            int old = load(lv, lv.type, line);
            int one = constant(1, m_types.intType(), line);
            int nv = binary(op == "++" ? "+" : "-", old, lv.type, one, m_types.intType(), lv.type, line);
            store(lv, nv, lv.type, line);
            return nv;
        }
        int v = value(e->kids[0]);
        const Type* kt = m_types.decay(e->kids[0]->type);
        if (op == "!") { int b = toBool(v, kt, line); IrInst in = make(IrInst::Cmp, m_types.boolType(), line); in.sub = "eq"; in.args.push_back(IrOperand::temp(b, m_types.boolType())); in.args.push_back(IrOperand::imm(0, m_types.boolType())); return emit(in); }
        if (op == "-") { v = convert(v, kt, t, line); IrInst in = make(IrInst::Un, t, line); in.sub = t->isFloating() ? "fneg" : "neg"; in.args.push_back(IrOperand::temp(v, t)); return emit(in); }
        if (op == "~") { v = convert(v, kt, t, line); IrInst in = make(IrInst::Un, t, line); in.sub = "not"; in.args.push_back(IrOperand::temp(v, t)); return emit(in); }
        return convert(v, kt, t, line);   // unary plus
    }
    case Expr::Postfix: {
        if (e->func) { Expr tmpCallee; tmpCallee.kind = Expr::Member; tmpCallee.kids.push_back(e->kids[0]); tmpCallee.sym = Expr::SymMethod; tmpCallee.type = e->kids[0]->type; Expr tmp; tmp.kind = Expr::Call; tmp.line = line; tmp.func = e->func; tmp.type = e->type; tmp.cls = e->cls; tmp.kids.push_back(&tmpCallee); return call(&tmp); }
        LValue lv = lvalue(e->kids[0]);
        if (lv.kind == LValue::None) return value(e->kids[0]);
        int old = load(lv, lv.type, line);
        int one = constant(1, m_types.intType(), line);
        int nv = binary(e->text == "++" ? "+" : "-", old, lv.type, one, m_types.intType(), lv.type, line);
        store(lv, nv, lv.type, line);
        return old;
    }
    case Expr::Binary: {
        if (e->func) {
            Expr tmpCallee; tmpCallee.kind = Expr::Member; tmpCallee.kids.push_back(e->kids[0]); tmpCallee.sym = Expr::SymMethod; tmpCallee.type = e->kids[0]->type;
            Expr tmp; tmp.kind = Expr::Call; tmp.line = line; tmp.func = e->func; tmp.type = e->type; tmp.cls = e->cls;
            if (e->func->ownerClass) { tmp.kids.push_back(&tmpCallee); tmp.kids.push_back(e->kids[1]); }
            else { Expr nm; nm.kind = Expr::Name; nm.sym = Expr::SymFunction; nm.func = e->func; nm.text = e->func->name; tmp.kids.push_back(&nm); tmp.kids.push_back(e->kids[0]); tmp.kids.push_back(e->kids[1]); return call(&tmp); }
            return call(&tmp);
        }
        if (e->text == "&&" || e->text == "||") {
            IrLocal tmp; tmp.name = "tmp.logic"; tmp.type = m_types.boolType(); tmp.size = 1; tmp.align = 1;
            m_out->locals.push_back(tmp);
            int slot = static_cast<int>(m_out->locals.size() - 1);
            int tb = m_out->newBlock("logic.true"), fb = m_out->newBlock("logic.false"), join = m_out->newBlock("logic.join");
            condition(e, tb, fb);
            LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = tmp.type;
            setBlock(tb); store(lv, constant(1, tmp.type, line), tmp.type, line); br(join, line);
            setBlock(fb); store(lv, constant(0, tmp.type, line), tmp.type, line); br(join, line);
            setBlock(join);
            return load(lv, tmp.type, line);
        }
        int a = value(e->kids[0]);
        int b = value(e->kids[1]);
        return binary(e->text, a, e->kids[0]->type, b, e->kids[1]->type, e->type, line);
    }
    case Expr::Assign: {
        if (e->func) {
            Expr tmpCallee; tmpCallee.kind = Expr::Member; tmpCallee.kids.push_back(e->kids[0]); tmpCallee.sym = Expr::SymMethod; tmpCallee.type = e->kids[0]->type;
            Expr tmp; tmp.kind = Expr::Call; tmp.line = line; tmp.func = e->func; tmp.type = e->type; tmp.cls = e->cls; tmp.kids.push_back(&tmpCallee); tmp.kids.push_back(e->kids[1]);
            return call(&tmp);
        }
        LValue lv = lvalue(e->kids[0]);
        if (lv.kind == LValue::None) { value(e->kids[1]); m_out->notes.push_back("L" + intToString(line) + ": assignment to an unresolved target was dropped"); return constant(0, t, line); }
        int rhs = value(e->kids[1]);
        if (e->text == "=") {
            int v = convert(rhs, e->kids[1]->type, lv.type, line);
            store(lv, v, lv.type, line);
            return v;
        }
        std::string op = e->text.substr(0, e->text.size() - 1);
        int old = load(lv, lv.type, line);
        int nv = binary(op, old, lv.type, rhs, e->kids[1]->type, lv.type, line);
        store(lv, nv, lv.type, line);
        return nv;
    }
    case Expr::Cond: {
        IrLocal tmp; tmp.name = "tmp.cond"; tmp.type = t; tmp.size = m_layouts.sizeOf(t); tmp.align = m_layouts.alignOf(t);
        m_out->locals.push_back(tmp);
        int slot = static_cast<int>(m_out->locals.size() - 1);
        int tb = m_out->newBlock("cond.true"), fb = m_out->newBlock("cond.false"), join = m_out->newBlock("cond.join");
        condition(e->kids[0], tb, fb);
        LValue lv; lv.kind = LValue::Local; lv.local = slot; lv.type = t;
        setBlock(tb); { int v = convert(value(e->kids[1]), e->kids[1]->type, t, line); store(lv, v, t, line); } br(join, line);
        setBlock(fb); { int v = convert(value(e->kids[2]), e->kids[2]->type, t, line); store(lv, v, t, line); } br(join, line);
        setBlock(join);
        return load(lv, t, line);
    }
    default:
        return constant(0, t, line);
    }
}

void IrBuilder::stmt(Stmt* s)
{
    if (!s) return;
    const int line = s->line;
    switch (s->kind) {
    case Stmt::Block: for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i]); return;
    case Stmt::Empty: return;
    case Stmt::Unsupported: m_out->notes.push_back("L" + intToString(line) + ": skipped " + s->text); return;
    case Stmt::Decl: {
        int idx = m_localMap[static_cast<size_t>(s->localIndex)];
        IrLocal& l = m_out->locals[static_cast<size_t>(idx)];
        const Type* t = l.type;
        LValue lv; lv.kind = LValue::Local; lv.local = idx; lv.type = m_types.unref(t);
        if (t && t->kind == Type::Reference) {
            // reference local: the slot holds the address of the initializer
            Expr* initE = s->expr ? s->expr : (s->args.empty() ? 0 : s->args[0]);
            if (initE) {
                LValue src = lvalue(initE);
                int addr;
                if (src.kind == LValue::Local) { m_out->locals[static_cast<size_t>(src.local)].addressTaken = true; IrInst a = make(IrInst::AddrLocal, m_types.pointer(src.type), line); a.args.push_back(IrOperand::local(src.local, src.type)); addr = emit(a); }
                else if (src.kind == LValue::Global) { IrInst a = make(IrInst::AddrGlobal, m_types.pointer(src.type), line); a.args.push_back(IrOperand::global(src.global, src.type)); addr = emit(a); }
                else if (src.kind == LValue::Mem) addr = src.addr;
                else addr = value(initE);
                IrInst st = make(IrInst::StoreLocal, t, line); st.args.push_back(IrOperand::local(idx, t)); st.args.push_back(IrOperand::temp(addr, m_types.pointer(t->inner))); emit(st);
            }
            return;
        }
        const Type* u = lv.type;
        if (u && u->kind == Type::Class && u->cls && u->cls->isDefinition) {
            // constructor call on the local's address
            const FunctionInfo* ctor = 0;
            size_t nargs = s->args.size();
            for (size_t m = 0; m < u->cls->methods.size() && !ctor; ++m) {
                const FunctionInfo& c = u->cls->methods[m];
                if (!c.isConstructor) continue;
                size_t required = 0;
                for (size_t p = 0; p < c.params.size(); ++p) if (c.params[p].defaultValue.empty()) ++required;
                if (nargs >= required && nargs <= c.params.size()) ctor = &c;
            }
            if (s->expr) {
                int src = value(s->expr);
                store(lv, src, u, line);   // __copy
                return;
            }
            if (ctor) {
                l.addressTaken = true;
                IrInst a = make(IrInst::AddrLocal, m_types.pointer(u), line); a.args.push_back(IrOperand::local(idx, u));
                int addr = emit(a);
                IrInst c = make(IrInst::Call, m_types.voidType(), line);
                c.sub = funcName(ctor);
                c.args.push_back(IrOperand::temp(addr, m_types.pointer(u)));
                for (size_t i = 0; i < s->args.size(); ++i) {
                    const Type* pt = i < ctor->params.size() ? m_types.parse(ctor->params[i].type, u->cls->qualifiedName, u->cls) : 0;
                    int v = value(s->args[i]);
                    if (pt && pt->kind != Type::Reference) v = convert(v, s->args[i]->type, pt, line);
                    c.args.push_back(IrOperand::temp(v, pt ? m_types.decay(pt) : s->args[i]->type));
                }
                emit(c);
            } else if (!s->args.empty()) {
                m_out->notes.push_back("L" + intToString(line) + ": no constructor found for " + s->name + ", arguments evaluated only");
                for (size_t i = 0; i < s->args.size(); ++i) value(s->args[i]);
            }
            return;
        }
        if (u && u->kind == Type::Array) { if (!s->text.empty()) m_out->notes.push_back("L" + intToString(line) + ": aggregate initializer of " + s->name + " not lowered"); return; }
        Expr* initE = s->expr ? s->expr : (s->args.size() == 1 ? s->args[0] : 0);
        if (initE) {
            int v = convert(value(initE), initE->type, u, line);
            store(lv, v, u, line);
        } else if (u && u->isUnknown()) {
            for (size_t i = 0; i < s->args.size(); ++i) value(s->args[i]);
        }
        return;
    }
    case Stmt::ExprStmt: if (s->expr) value(s->expr); return;
    case Stmt::If: {
        int tb = m_out->newBlock("if.then"), fb = s->kids.size() > 1 ? m_out->newBlock("if.else") : -1, join = m_out->newBlock("if.end");
        condition(s->cond, tb, fb >= 0 ? fb : join);
        setBlock(tb); stmt(s->kids[0]); br(join, line);
        if (fb >= 0) { setBlock(fb); stmt(s->kids[1]); br(join, line); }
        setBlock(join);
        return;
    }
    case Stmt::While: {
        int cb = m_out->newBlock("while.cond"), bb = m_out->newBlock("while.body"), xb = m_out->newBlock("while.end");
        br(cb, line);
        setBlock(cb); condition(s->cond, bb, xb);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = cb; lt.tryDepth = m_tryStack.size(); m_loops.push_back(lt);
        setBlock(bb); stmt(s->kids[0]); br(cb, line);
        m_loops.pop_back();
        setBlock(xb);
        return;
    }
    case Stmt::DoWhile: {
        int bb = m_out->newBlock("do.body"), cb = m_out->newBlock("do.cond"), xb = m_out->newBlock("do.end");
        br(bb, line);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = cb; lt.tryDepth = m_tryStack.size(); m_loops.push_back(lt);
        setBlock(bb); stmt(s->kids[0]); br(cb, line);
        m_loops.pop_back();
        setBlock(cb); condition(s->cond, bb, xb);
        setBlock(xb);
        return;
    }
    case Stmt::For: {
        if (s->init) stmt(s->init);
        int cb = m_out->newBlock("for.cond"), bb = m_out->newBlock("for.body"), sb = m_out->newBlock("for.step"), xb = m_out->newBlock("for.end");
        br(cb, line);
        setBlock(cb);
        if (s->cond) condition(s->cond, bb, xb); else br(bb, line);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = sb; lt.tryDepth = m_tryStack.size(); m_loops.push_back(lt);
        setBlock(bb); stmt(s->kids[0]); br(sb, line);
        m_loops.pop_back();
        setBlock(sb); if (s->step) value(s->step); br(cb, line);
        setBlock(xb);
        return;
    }
    case Stmt::Switch: {
        int v = value(s->cond);
        const Type* vt = m_types.decay(s->cond->type);
        if (!vt->isIntegral()) vt = m_types.intType();
        Stmt* body = s->kids[0];
        std::vector<Stmt*> items;
        if (body->kind == Stmt::Block) items = body->kids; else items.push_back(body);
        int xb = m_out->newBlock("switch.end");
        std::map<const Stmt*, int> labelBlock;
        int defaultBlock = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i]->kind == Stmt::Case) labelBlock[items[i]] = m_out->newBlock("case");
            else if (items[i]->kind == Stmt::Default) { defaultBlock = m_out->newBlock("default"); labelBlock[items[i]] = defaultBlock; }
        }
        // dispatch chain
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i]->kind != Stmt::Case) continue;
            int cv = constant(items[i]->expr->cval, vt, items[i]->line);
            IrInst c = make(IrInst::Cmp, m_types.boolType(), items[i]->line); c.sub = "eq";
            c.args.push_back(IrOperand::temp(v, vt)); c.args.push_back(IrOperand::temp(cv, vt));
            int cond = emit(c);
            int next = m_out->newBlock("switch.test");
            condbr(cond, labelBlock[items[i]], next, items[i]->line);
            setBlock(next);
        }
        br(defaultBlock >= 0 ? defaultBlock : xb, line);
        LoopTargets lt; lt.breakBlock = xb; lt.continueBlock = -1; lt.tryDepth = m_tryStack.size(); m_loops.push_back(lt);
        bool inLabel = false;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i]->kind == Stmt::Case || items[i]->kind == Stmt::Default) {
                int b = labelBlock[items[i]];
                if (inLabel) br(b, items[i]->line);   // fall through
                setBlock(b);
                inLabel = true;
                continue;
            }
            if (!inLabel) { setBlock(m_out->newBlock("switch.dead")); inLabel = true; }
            stmt(items[i]);
        }
        br(xb, line);
        m_loops.pop_back();
        setBlock(xb);
        return;
    }
    case Stmt::Case: case Stmt::Default: return;
    case Stmt::Break: {
        if (!m_loops.empty()) { leaveTryRegions(m_loops.back().tryDepth, line); br(m_loops.back().breakBlock, line); }
        return;
    }
    case Stmt::Continue: {
        for (size_t i = m_loops.size(); i > 0; --i) if (m_loops[i - 1].continueBlock >= 0) { leaveTryRegions(m_loops[i - 1].tryDepth, line); br(m_loops[i - 1].continueBlock, line); return; }
        return;
    }
    case Stmt::Return: {
        IrInst in = make(IrInst::Ret, 0, line);
        if (s->expr) {
            int v = value(s->expr);
            const Type* rt = m_out->retType;
            if (rt && rt->kind == Type::Reference) {
                LValue lv = lvalue(s->expr);
                if (lv.kind == LValue::Local) { m_out->locals[static_cast<size_t>(lv.local)].addressTaken = true; IrInst a = make(IrInst::AddrLocal, m_types.pointer(lv.type), line); a.args.push_back(IrOperand::local(lv.local, lv.type)); v = emit(a); }
                else if (lv.kind == LValue::Global) { IrInst a = make(IrInst::AddrGlobal, m_types.pointer(lv.type), line); a.args.push_back(IrOperand::global(lv.global, lv.type)); v = emit(a); }
                else if (lv.kind == LValue::Mem) v = lv.addr;
                in.args.push_back(IrOperand::temp(v, m_types.pointer(rt->inner)));
            } else {
                if (rt && !rt->isVoid()) v = convert(v, s->expr->type, rt, line);
                in.args.push_back(IrOperand::temp(v, rt ? m_types.decay(rt) : s->expr->type));
            }
        }
        leaveTryRegions(0, line);          // a return inside try regions pops their frames first
        emit(in);
        return;
    }
    case Stmt::Try: {
        m_out->hasTry = true;
        int id = m_tryCount++;
        std::vector<int> handlers;
        for (size_t i = 1; i < s->kids.size(); ++i) handlers.push_back(m_out->newBlock("catch"));
        int join = m_out->newBlock("try.end");
        int body = m_out->newBlock("try.body");
        IrInst tb = make(IrInst::TryBegin, 0, line);
        tb.extra = id;
        for (size_t i = 0; i < handlers.size(); ++i) tb.args.push_back(IrOperand::block(handlers[i]));
        emit(tb);
        // the body is its own block, so nothing defined inside the region dominates the handlers
        // (after a longjmp the registers hold the values they had when the region was entered)
        br(body, line);
        setBlock(body);
        m_tryStack.push_back(id);
        ++m_catchDepth;
        stmt(s->kids[0]);
        --m_catchDepth;
        m_tryStack.pop_back();
        if (!terminated()) { IrInst te = make(IrInst::TryEnd, 0, line); te.extra = id; emit(te); }
        br(join, line);
        for (size_t i = 1; i < s->kids.size(); ++i) {
            Stmt* c = s->kids[i];
            setBlock(handlers[i - 1]);
            IrInst cp = make(IrInst::CatchParam, m_types.pointer(m_types.voidType()), c->line);
            // the catch type in canonical form: the qualified class name, or the scalar type text
            cp.sub = c->typeText;
            if (c->typeText != "...") {
                const Type* ct0 = m_types.unref(m_types.parse(c->typeText, m_ast->fn->ownerClass ? m_ast->fn->ownerClass->qualifiedName : m_ast->fn->scope, m_ast->fn->ownerClass));
                if (ct0 && ct0->kind == Type::Class && ct0->cls) cp.sub = ct0->cls->qualifiedName;
                else if (ct0 && !ct0->isUnknown()) cp.sub = ct0->str();
            }
            int ev = emit(cp);                                   // address of the thrown object (runtime copy)
            if (c->localIndex >= 0) {
                int idx = m_localMap[static_cast<size_t>(c->localIndex)];
                const Type* ct = m_out->locals[static_cast<size_t>(idx)].type;
                if (ct && ct->kind != Type::Class) {
                    IrInst ld = make(IrInst::LoadMem, ct, c->line); ld.args.push_back(IrOperand::temp(ev, m_types.pointer(ct)));
                    int v = emit(ld);
                    IrInst st = make(IrInst::StoreLocal, ct, c->line); st.args.push_back(IrOperand::local(idx, ct)); st.args.push_back(IrOperand::temp(v, ct)); emit(st);
                } else {
                    IrInst st = make(IrInst::StoreLocal, m_types.pointer(ct), c->line); st.args.push_back(IrOperand::local(idx, ct)); st.args.push_back(IrOperand::temp(ev, m_types.pointer(ct))); emit(st);
                    m_out->locals[static_cast<size_t>(idx)].type = m_types.reference(ct);
                }
            }
            m_tryStack.push_back(-1);                            // a return/break/continue inside the handler ends it
            stmt(c->kids[0]);
            m_tryStack.pop_back();
            if (!terminated()) emit(make(IrInst::CatchEnd, 0, c->line));
            br(join, c->line);
        }
        setBlock(join);
        return;
    }
    case Stmt::Catch: return;
    }
}

// pops the try frames and ends the catch handlers deeper than 'depth' (innermost first): used
// before a return, break or continue that leaves them
void IrBuilder::leaveTryRegions(size_t depth, int line)
{
    for (size_t k = m_tryStack.size(); k > depth; --k) {
        if (m_tryStack[k - 1] < 0) { emit(make(IrInst::CatchEnd, 0, line)); continue; }
        IrInst te = make(IrInst::TryEnd, 0, line);
        te.extra = m_tryStack[k - 1];
        emit(te);
    }
}

bool IrBuilder::build(const FunctionAst& ast, IrFunction& out)
{
    m_ast = &ast;
    m_out = &out;
    m_loops.clear();
    m_localMap.clear();
    m_catchDepth = 0;
    m_tryStack.clear();
    m_tryCount = 0;
    const FunctionInfo* fn = ast.fn;
    out.fn = fn;
    out.name = funcName(fn);
    out.retType = ast.returnType ? ast.returnType : m_types.voidType();
    out.hasThis = fn->ownerClass && !fn->isStatic;
    if (out.hasThis) {
        IrLocal t;
        t.name = "this";
        t.type = m_types.pointer(m_types.classType(fn->ownerClass, fn->isConst));
        t.isParam = true; t.isThis = true;
        out.locals.push_back(t);
    }
    for (size_t p = 0; p < fn->params.size(); ++p) {
        IrLocal l;
        l.name = fn->params[p].name.empty() ? "arg" + intToString(static_cast<int>(p)) : fn->params[p].name;
        l.type = p < ast.paramTypes.size() ? ast.paramTypes[p] : m_types.builtin(Type::Long);
        l.size = l.type->kind == Type::Reference ? 8 : m_layouts.sizeOf(l.type);
        l.align = l.type->kind == Type::Reference ? 8 : m_layouts.alignOf(l.type);
        l.isParam = true;
        out.locals.push_back(l);
    }
    out.paramCount = static_cast<int>(out.locals.size());
    for (size_t i = 0; i < ast.locals.size(); ++i) {
        IrLocal l;
        l.name = ast.locals[i].name;
        l.type = ast.locals[i].type ? ast.locals[i].type : m_types.unknown(ast.locals[i].typeText);
        l.size = l.type->kind == Type::Reference ? 8 : m_layouts.sizeOf(l.type);
        l.align = l.type->kind == Type::Reference ? 8 : m_layouts.alignOf(l.type);
        l.addressTaken = ast.locals[i].addressTaken;
        out.locals.push_back(l);
        m_localMap.push_back(static_cast<int>(out.locals.size() - 1));
    }
    out.newBlock("entry");
    m_block = 0;
    // constructor initializer list: base constructors and member initialisation
    if (fn->isConstructor && out.hasThis && fn->ownerClass) {
        const ClassInfo* cls = fn->ownerClass;
        // base constructors first (from the initializer list), then the vptr, then members
        if (m_layouts.hasVtable(cls)) {
            int self = thisTemp(fn->line);
            IrInst vt = make(IrInst::AddrGlobal, m_types.pointer(m_types.voidType()), fn->line);
            vt.args.push_back(IrOperand::global("vtable@" + cls->qualifiedName, m_types.voidType()));
            int vtp = emit(vt);
            IrInst st = make(IrInst::StoreMem, m_types.pointer(m_types.voidType()), fn->line);
            st.sub = "vptr";
            st.args.push_back(IrOperand::temp(self, out.locals[0].type));
            st.args.push_back(IrOperand::temp(vtp, m_types.pointer(m_types.voidType())));
            emit(st);
        }
        for (size_t i = 0; i < ast.inits.size(); ++i) {
            const InitEntry& in = ast.inits[i];
            const int line = in.line;
            // base class?
            const ClassInfo* base = 0;
            for (size_t b = 0; b < cls->bases.size(); ++b) {
                const ClassInfo* r = cls->bases[b].resolved;
                if (r && (r->name == lastComponent(in.name) || r->qualifiedName == in.name || stripTemplateArgs(cls->bases[b].name) == in.name)) { base = r; break; }
            }
            if (base) {
                int self = thisTemp(line);
                int off = m_layouts.baseOffset(cls, base);
                if (off > 0) { IrInst adj = make(IrInst::AddrField, m_types.pointer(m_types.classType(base)), line); adj.sub = "base " + base->name; adj.extra = off; adj.args.push_back(IrOperand::temp(self, out.locals[0].type)); self = emit(adj); }
                const FunctionInfo* ctor = 0;
                for (size_t m = 0; m < base->methods.size() && !ctor; ++m) {
                    const FunctionInfo& c = base->methods[m];
                    if (!c.isConstructor) continue;
                    size_t required = 0;
                    for (size_t p = 0; p < c.params.size(); ++p) if (c.params[p].defaultValue.empty()) ++required;
                    if (in.args.size() >= required && in.args.size() <= c.params.size()) ctor = &c;
                }
                if (ctor) {
                    IrInst c = make(IrInst::Call, m_types.voidType(), line);
                    c.sub = funcName(ctor);
                    c.args.push_back(IrOperand::temp(self, m_types.pointer(m_types.classType(base))));
                    for (size_t a = 0; a < in.args.size(); ++a) {
                        const Type* pt = a < ctor->params.size() ? m_types.parse(ctor->params[a].type, base->qualifiedName, base) : 0;
                        int v = value(in.args[a]);
                        if (pt && pt->kind != Type::Reference) v = convert(v, in.args[a]->type, pt, line);
                        c.args.push_back(IrOperand::temp(v, pt ? m_types.decay(pt) : in.args[a]->type));
                    }
                    emit(c);
                } else for (size_t a = 0; a < in.args.size(); ++a) value(in.args[a]);
                continue;
            }
            // member
            Layouts::Field f;
            if (!m_layouts.field(cls, in.name, f)) { out.notes.push_back("L" + intToString(line) + ": initializer " + in.name + " is neither a member nor a base"); for (size_t a = 0; a < in.args.size(); ++a) value(in.args[a]); continue; }
            const Type* ft = m_types.unref(f.type);
            LValue lv;
            if (f.isStatic) { lv.kind = LValue::Global; lv.global = cls->qualifiedName + "::" + in.name; }
            else {
                int self = thisTemp(line);
                IrInst ad = make(IrInst::AddrField, m_types.pointer(ft), line);
                ad.sub = in.name; ad.extra = f.offset;
                ad.args.push_back(IrOperand::temp(self, out.locals[0].type));
                lv.kind = LValue::Mem; lv.addr = emit(ad);
            }
            lv.type = ft;
            if (ft && ft->kind == Type::Class && ft->cls && ft->cls->isDefinition) {
                const FunctionInfo* ctor = 0;
                for (size_t m = 0; m < ft->cls->methods.size() && !ctor; ++m) {
                    const FunctionInfo& c = ft->cls->methods[m];
                    if (!c.isConstructor) continue;
                    size_t required = 0;
                    for (size_t p = 0; p < c.params.size(); ++p) if (c.params[p].defaultValue.empty()) ++required;
                    if (in.args.size() >= required && in.args.size() <= c.params.size()) ctor = &c;
                }
                if (ctor) {
                    IrInst c = make(IrInst::Call, m_types.voidType(), line);
                    c.sub = funcName(ctor);
                    c.args.push_back(IrOperand::temp(lv.addr, m_types.pointer(ft)));
                    for (size_t a = 0; a < in.args.size(); ++a) {
                        const Type* pt = a < ctor->params.size() ? m_types.parse(ctor->params[a].type, ft->cls->qualifiedName, ft->cls) : 0;
                        int v = value(in.args[a]);
                        if (pt && pt->kind != Type::Reference) v = convert(v, in.args[a]->type, pt, line);
                        c.args.push_back(IrOperand::temp(v, pt ? m_types.decay(pt) : in.args[a]->type));
                    }
                    emit(c);
                } else if (in.args.size() == 1) {
                    store(lv, value(in.args[0]), ft, line);   // copy
                }
                continue;
            }
            if (in.args.size() == 1 && ft && ft->kind != Type::Array) {
                int v = convert(value(in.args[0]), in.args[0]->type, ft, line);
                store(lv, v, ft, line);
            } else for (size_t a = 0; a < in.args.size(); ++a) value(in.args[a]);
        }
    }
    if (ast.body) stmt(ast.body);
    // every block needs a terminator
    for (size_t b = 0; b < out.blocks.size(); ++b) {
        IrBlock& blk = out.blocks[b];
        if (blk.insts.empty() || !blk.insts.back().isTerminator()) {
            IrInst r = make(IrInst::Ret, 0, fn->endLine);
            if (out.retType && !out.retType->isVoid() && !fn->isConstructor && !fn->isDestructor) {
                r.op = IrInst::Unreachable;
                r.sub = "missing return";
            }
            blk.insts.push_back(r);
        }
    }
    out.computeCfg();
    return true;
}

} // namespace cppparse
