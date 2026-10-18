// IrGen.hpp - version 6 stage 4: IR generation from the semantically analysed syntax tree.
//
// Walks every function with a body (including template instantiations and the
// implicit special members that are used) and produces IrFunctions for the
// shared back end. The IR still carries the old front end's Type objects, so
// sema types are converted through an adapter; class types get a synthesised
// ClassInfo that only names the class and its bases. Conventions are those of
// the old builder: locals are memory slots, class values are addresses, member
// access is AddrField with the sema offset, virtual calls carry the vtable slot,
// exceptions use the try-region ids. Functions returning a class by value take a
// hidden result pointer as their first parameter after 'this'.
//
// Object lifetimes: every constructed local, by-value parameter (destroyed by
// the callee, as MSVC does) and class temporary is recorded as a Cleanup with a
// "constructed" flag byte. Block scopes destroy their objects in reverse order
// on normal exit, break/continue/return unwind the scopes they leave, and
// temporaries die at the end of the full expression. A function that owns such
// objects gets a catch-all try region around its body whose handler destroys the
// objects whose flag is still set and rethrows, so destructors also run while an
// exception unwinds through the function.
//
// Strict C++98.
#ifndef CPPPARSE_IRGEN_HPP
#define CPPPARSE_IRGEN_HPP

#include "CodeBase.hpp"
#include "Ir.hpp"
#include "Sema.hpp"
#include "Types.hpp"
#include "X86.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cppparse {

class IrGen {
public:
    IrGen(sema::Program& program, sema::TypeTable& semaTypes);
    ~IrGen();

    // generates every function of the program; the IrFunctions are owned by the caller
    void generate(std::vector<IrFunction*>& out);
    // vtables, globals and (when msvc) decorated names for the x86 module
    void registerMetadata(X86Module& x86, bool msvc);
    const std::vector<std::string>& notes() const { return m_notes; }
    static std::string irName(const sema::Symbol* fn);
    int functionsGenerated() const { return m_generated; }
    // builds a local that every return statement names directly in the caller's result object (cl /O2's NRVO)
    void setNamedReturnElision(bool on) { m_nrvoEnabled = on; }
    // members of class template instantiations whose bodies had errors (Symbol::instantiationFailed) that the
    // generated code needs all the same, e.g. through an implicit destructor call: the unit cannot be compiled
    const std::vector<sema::Symbol*>& failedMembersUsed() const { return m_failedUsed; }
    // constructs with virtual bases --compile6 cannot generate correctly (S132): "line: message"
    const std::vector<std::string>& unsupported() const { return m_unsupported; }

private:
    struct LValue {
        enum Kind { None, Local, Global, Mem } kind;
        int         local;
        std::string global;
        int         addr;
        const sema::Type* type;      // the object's sema type (unreferenced)
        int         bitWidth;        // a bit-field (Mem only): its width (0: not a bit-field) and first bit in the unit at addr
        int         bitOffset;
        LValue() : kind(None), local(-1), addr(-1), type(0), bitWidth(0), bitOffset(0) {}
    };
    struct LoopTargets { int breakBlock, continueBlock; size_t tryDepth, cleanupDepth; };
    struct Cleanup {
        int               slot;        // the object's local, or the local holding its address when viaPointer
        const sema::Type* type;
        bool              viaPointer;
        int               flag;        // local byte: 1 while the object is constructed
        bool              guarded;     // the normal-path destruction must test the flag (temporary of a conditional arm)
        bool              unwindOnly;  // a member built by a constructor: destroyed only when the body throws
        bool              baseSubobject;   // a base part: destroyed without its virtual bases
        std::string       abortGuard;  // a static local's initializer: its guard global, handed to _Init_thread_abort
        int               restIndex;   // >= 0: the elements [0, index) of the array at slot (viaPointer), this local the index
        int               freeKind;    // 1: frees the object whose address slot holds, 2: the array whose cookie it holds
        sema::Symbol*     freeCls;     // the class whose operator delete frees it (0: the global one)
        int               countSlot;   // freeKind 2: the local holding the element count
        bool              noCookie;    // freeKind 2: an array without a cookie (a sized operator delete[] gets count * size)
        int               freeSize;    // freeKind: the size of the object (of an element), for the sized global operators
        Cleanup() : slot(-1), type(0), viaPointer(false), flag(-1), guarded(false), unwindOnly(false), baseSubobject(false), restIndex(-1), freeKind(0), freeCls(0), countSlot(-1), noCookie(false), freeSize(1) {}
    };
    struct ExitThunk {                 // destroys a global or static local at exit; registered with atexit once it is built
        sema::Symbol*     fn;
        std::string       global;      // the object's IR name
        const sema::Type* type;
    };

    // types
    const Type* oldType(const sema::Type* t);
    const ClassInfo* classInfo(sema::Symbol* cls);
    int  sizeOf(const sema::Type* t) const { return m_semaTypes.sizeOf(t); }
    // the bytes a trivial copy moves: none for an empty class, whose byte may belong to a member of the
    // derived object when it is a base subobject (MSVC places empty bases over other members)
    int  copySize(const sema::Type* t) const { return t && t->kind == sema::Type::Class && t->decl && t->decl->layoutDone && t->decl->nvSize == 0 ? 0 : sizeOf(t); }
    int  alignOf(const sema::Type* t) const { return m_semaTypes.alignOf(t); }
    bool isClassType(const sema::Type* t) const { t = m_semaTypes.unref(t); return t && t->kind == sema::Type::Class; }
    bool baseOffset(sema::Symbol* derived, sema::Symbol* base, int& offset, int depth = 0) const;   // not through virtual bases
    // MSVC's virtual bases: base is reached through the virtual base at entry index of derived's vbtable (the
    // one at its vbptrOffset), then rest bytes further; toBase converts a non-null address either way
    bool virtualBasePath(sema::Symbol* derived, sema::Symbol* base, int& index, int& rest) const;
    bool isBaseOf(sema::Symbol* derived, sema::Symbol* base) const { int o = 0, i = 0, r = 0; return baseOffset(derived, base, o) || virtualBasePath(derived, base, i, r); }
    static bool hasVbases(const sema::Symbol* cls) { return cls && !cls->vbases.empty(); }
    int  toBase(int addr, sema::Symbol* derived, sema::Symbol* base, int line);
    static std::string vbtableName(const sema::Symbol* cls, size_t t);

    // pointers to members as cl represents them (the sizes are TypeTable::sizeOf's): a data member is an int
    // offset, null -1 (kind 1); with virtual bases an int offset and the vbtable offset of the base's entry, null
    // {0, -1}; in the unspecified model (the class incomplete when a size was first needed) the offset, the vbptr's
    // offset and the vbtable offset, null {0, 0, -1}. A member function is a code pointer (a vcall thunk for a
    // virtual one), null 0 (kind 2); under multiple inheritance a code pointer and an int this adjustment; with
    // virtual bases the vbtable offset follows; in the unspecified model the vbptr's offset comes before it (null:
    // all zero, the vbtable offset -1 in the unspecified model). Every representation but the first two (kind 3)
    // lives in memory like a class object: values are addresses, copies move the bytes, and an 8-byte one travels
    // in a register as an integer, a larger one by the address of a copy and through the hidden result pointer
    struct MpLayout {
        bool function, multiple, virt, unspecified;
        int  size;
        int  adjAt, vbptrAt, vbindexAt;   // byte offsets of the fields after the pointer / offset at 0, -1 when absent
        MpLayout() : function(false), multiple(false), virt(false), unspecified(false), size(0), adjAt(-1), vbptrAt(-1), vbindexAt(-1) {}
    };
    MpLayout mpLayout(const sema::Type* t) const;
    int  mpKind(const sema::Type* t) const;
    bool byAddress(const sema::Type* u) const { return u && (u->kind == sema::Type::Class || u->kind == sema::Type::Array || mpKind(u) == 3); }
    const Type* mpClassType(int size);
    int  mpLoad(int addr, int at, const sema::Type* ft, int line);
    void mpStore(int addr, int at, int v, const sema::Type* ft, int line);
    int  mpTemp(const sema::Type* t, int line);
    int  mpNull(const sema::Type* t, int line);
    int  mpAddressOf(sema::Symbol* m, const sema::Type* t, int line);
    int  mpConvert(int v, const sema::Type* from, const sema::Type* to, int line);
    int  mpCompare(int a, const sema::Type* ta, int b, const sema::Type* tb, bool ne, int line);
    int  mpToBool(int v, const sema::Type* t, int line);
    int  mpApply(cxx::Expr* e, int* code);
    int  mpVbaseAddress(int obj, sema::Symbol* cls, int vbindex, int line);
    int  memberPointerCall(cxx::Expr* e, cxx::Expr* callee, int sretAddr);
    bool memberPointerData(const sema::Type* t, cxx::Expr* x, std::vector<IrDataItem>& out);
    bool nullMemberPointerData(const sema::Type* t, std::vector<IrDataItem>& out, bool direct);
    void nullMemberPointers(const sema::Type* t, int addr, int line);
    std::string vcallThunk(sema::Symbol* fn);
    static int vbptrOrigin(const sema::Symbol* cls);
    bool mpPath(sema::Symbol* derived, sema::Symbol* base, int& off, int& index, int& rest) const;
    std::map<int, const Type*> m_mpTypes;
    int                        m_thisSlot;      // the local holding this (0, or 1 behind a hidden result pointer cl puts first)
    void storeVbptrs(sema::Symbol* cls, int objAddr, int line);
    sema::Symbol* namedResult(sema::Symbol* fn, cxx::Stmt* body);   // the local every return names (NRVO), or 0
    sema::Symbol* vbaseDestructor(sema::Symbol* cls);   // cl's ??_D: the destructor of a complete object
    void genVbaseDestructor(sema::Symbol* fn);
    void catchableBases(sema::Symbol* top, std::vector<sema::Symbol*>& bases, std::vector<int>& offsets) const;
    std::string throwDescriptorFor(const sema::Type* t) const;

    // special members
    sema::Symbol* findSpecial(sema::Symbol* cls, char which) const;   // 'd' default ctor, 'c' copy ctor, 'a' copy assignment, '~' destructor
    bool needsDtor(const sema::Type* t, int depth = 0) const;
    bool needsCopyCtor(const sema::Type* t, int depth = 0) const;
    bool needsCopyAssign(const sema::Type* t, int depth = 0) const;
    bool needsDefaultCtor(const sema::Type* t, int depth = 0) const;
    // MSVC x64: a class of 1, 2, 4 or 8 bytes without a non-trivial copy constructor travels in an integer
    // register; a POD-like one of those sizes is returned in rax by a non-member or static function
    const sema::Type* regParamType(const sema::Type* t) const;
    const sema::Type* regReturnType(sema::Symbol* fn, const sema::Type* rt) const;
    const sema::Type* argOperandType(const sema::Type* pt, cxx::Expr* a) const;   // the IR type an argument of a parameter of type pt travels as
    bool podForReturn(const sema::Type* t, int depth) const;
    bool copyAssignDeleted(const sema::Type* t, int depth) const;
    void callSpecial(sema::Symbol* fn, int thisPtr, int argPtr, int line);
    void copyBytes(int dst, int src, int size, int line);
    void bitField(LValue& lv, const sema::Symbol* field) const;   // marks lv as the bit-field field (when it is one)
    int  loadBits(const LValue& lv, const sema::Type* t, int line);
    void storeBits(const LValue& lv, int temp, const sema::Type* t, int line);
    int  bitConstant(unsigned long long v, const sema::Type* t, int line);
    void copyConstruct(const sema::Type* t, int dst, int src, int line);
    void copyAssign(const sema::Type* t, int dst, int src, int line);
    void constructDefault(const sema::Type* t, int addr, int line);
    // T() (C++03 8.5/5, as cl): zeroed unless a user-declared constructor builds it; complete false for a base
    // subobject (only its own bytes); ctor: the constructor sema chose, or 0
    void valueInit(sema::Symbol* ctor, const sema::Type* t, int addr, bool complete, int line);
    void zeroBytes(int addr, int bytes, int line);
    void forEachElement(const sema::Type* elem, int base, int count, bool construct, int line);
    int  materialize(cxx::Expr* e, const sema::Type* t, int dst, int line);
    void destroy(const sema::Type* t, int objAddr, int line, bool viaVtable, bool complete = true);   // complete: with its virtual bases

    // lifetimes
    int  registerCleanup(int slot, const sema::Type* t, bool viaPointer, int mode, int line);
    void runCleanup(const Cleanup& c, bool guarded, int line);
    void openScope();
    void closeScope(int line);
    void leaveScopes(size_t depth, int line);
    void endFullExpr(int line, size_t from = 0);
    void keepTemporaries(size_t from);
    void handlerCleanups(size_t mark, int line);
    int  beginTerminateRegion(int& term, int line);
    void endTerminateRegion(int tid, int term, int line);
    int  registerFree(int ptr, sema::Symbol* cls, bool array, int count, int elemSize, int line);
    void clearFlag(int index, int line);
    void returnSequence(int line);
    void finishFunction(int line);
    void scopedStmt(cxx::Stmt* s, int line);

    // functions
    void genFunction(sema::Symbol* fn);
    void genImplicit(sema::Symbol* fn);
    void genInitializer();
    bool staticInit(sema::Symbol* var, IrGlobal& g);
    bool staticData(const sema::Type* t, const cxx::Initializer& init, std::vector<IrDataItem>& out);
    bool constantData(const sema::Type* t, cxx::Expr* x, std::vector<IrDataItem>& out, int depth);
    bool foldArith(cxx::Expr* x, double& v, int depth);
    bool foldToFloating(cxx::Expr* x, double& v, int depth);
    bool foldInteger(cxx::Expr* x, const sema::Type* to, unsigned long long& bits, int depth);
    bool convertFolded(const sema::Type* t, double& v) const;
    bool foldableFloating(cxx::Expr* e) const;
    bool aggregateData(const sema::Type* t, const std::vector<cxx::Expr*>* elems, std::vector<IrDataItem>& out, int depth);
    bool readOnlyObject(const sema::Type* t) const;
    void registerAtExit(const std::string& global, const sema::Type* t, int line);
    void genExitThunks();
    void drainQueue();
    void prologue(sema::Symbol* fn);
    void constructorInit(sema::Symbol* fn, cxx::Decl* def);
    void destructorCleanup(sema::Symbol* fn);
    void destructorParts(sema::Symbol* fn);
    void storeVptr(sema::Symbol* cls, int objAddr, int line);
    int  vfptrThis(sema::Symbol* fn, int thisPtr, int line);
    static std::string vtableName(const sema::Symbol* cls, size_t t);
    // MSVC's vector deleting destructor of a virtual destructor (what its vtable slot holds): with bit 1 of its
    // flags clear it destroys the complete object, with bit 1 set the array whose count is in the cookie before
    // it, and frees it when bit 0 is set; made on first use, generated after the other functions
    std::string deletingDestructor(sema::Symbol* dtor, X86Module& x86, bool msvc);
    void genDeletingDestructors();
    sema::Symbol* classOperatorDelete(sema::Symbol* cls, int depth, const char* name = "operator delete", size_t maxParams = 2) const;
    bool freeObject(sema::Symbol* cls, int p, int line);
    void freeArray(sema::Symbol* cls, int cookie, int count, int elemSize, int line, bool hasCookie = true);
    void freeGlobal(int p, int size, int line);
    void need(sema::Symbol* fn);
    int  newLocal(const std::string& name, const sema::Type* t, bool addressTaken);
    int  subobject(int objAddr, const sema::Type* objType, const std::string& name, int offset, const sema::Type* partType, int line);

    // IR emission
    int    emit(IrInst inst);
    IrInst make(IrInst::Op op, const sema::Type* type, int line);
    bool   terminated() const;
    void   br(int target, int line);
    void   condbr(int cond, int t, int f, int line);
    int    constant(long v, const sema::Type* t, int line);
    int    thisTemp(int line);
    int    addressOf(const LValue& lv, int line);
    int    load(const LValue& lv, const sema::Type* t, int line);
    void   store(const LValue& lv, int temp, const sema::Type* t, int line);
    int    convert(int temp, const sema::Type* from, const sema::Type* to, int line);
    int    offsetAddress(int temp, const sema::Type* fromPtr, const sema::Type* toPtr, long delta, int line);
    int    toBool(int temp, const sema::Type* t, int line);
    int    binary(const std::string& op, int a, const sema::Type* ta, int b, const sema::Type* tb, const sema::Type* result, int line);
    void   leaveTryRegions(size_t depth, int line);

    // expressions and statements
    int    value(cxx::Expr* e);
    LValue lvalue(cxx::Expr* e);
    void   condition(cxx::Expr* e, int trueBlock, int falseBlock);
    int    conditional(cxx::Expr* e, bool byAddr);
    int    call(cxx::Expr* e, int sretAddr = -1);
    int    operatorCall(cxx::Expr* e);
    int    adjustThis(sema::Symbol* fn, int objAddr, const sema::Type* objType, int line);
    int    rvalue(int r, cxx::Expr* e, int line);
    int    callFunction(sema::Symbol* fn, int thisPtr, int sretAddr, const std::vector<cxx::Expr*>& args, size_t firstArg, bool virtualCall, int line);
    int    construct(sema::Symbol* ctor, int objAddr, const std::vector<cxx::Expr*>& args, size_t firstArg, int line);
    int    argument(cxx::Expr* a, const sema::Type* paramType, int line);
    void   stmt(cxx::Stmt* s);
    int    labelBlock(const std::string& name);
    void   localDecl(cxx::Decl* d);
    int    conditionDecl(cxx::Decl* d, const sema::Type*& t, int line);
    void   declLoop(cxx::Stmt* s, int cb, int next, int xb, int line);
    void   initObject(const LValue& lv, const sema::Type* t, sema::Symbol* var, cxx::InitDeclarator& id, int line);
    void   abiResult(IrInst& r, int line);
    void   initAggregate(const sema::Type* t, int addr, const std::vector<cxx::Expr*>& elems, bool zeroRest, int line);
    bool   coversAll(const sema::Type* t, const std::vector<cxx::Expr*>& elems) const;
    const sema::Type* exprType(cxx::Expr* e) const;

    sema::Program&         m_prog;
    sema::TypeTable&       m_semaTypes;
    CodeBase               m_emptyCb;
    TypeTable              m_types;
    std::map<sema::Symbol*, ClassInfo*> m_classInfos;
    std::vector<ClassInfo*> m_ownedClassInfos;
    std::map<const sema::Type*, const Type*> m_typeCache;

    std::vector<IrFunction*>* m_outList;
    std::set<sema::Symbol*>   m_done;
    std::vector<sema::Symbol*> m_queue;
    std::vector<sema::Symbol*> m_failedUsed;
    std::vector<std::string>   m_notes;
    int                        m_generated;
    std::vector<sema::Symbol*> m_dynamicGlobals;   // globals built by the module's initialiser, in declaration order
    sema::Symbol*              m_initSymbol;
    std::vector<ExitThunk>     m_exitThunks;       // destructor thunks registered with atexit; generated after the functions
    size_t                     m_exitThunksDone;
    X86Module*                 m_x86;              // receives the globals that static locals become
    bool                       m_msvcNames;        // --msvc-mangling: static locals get cl's decorated names
    std::map<sema::Symbol*, std::string> m_staticNames;   // static local -> its global's IR name
    std::map<sema::Symbol*, long> m_staticBlocks;         // static local of the current function -> cl's block number
    // the constant convert() last folded from a floating one, while nothing has been emitted after it: converted
    // straight on to a floating type it is its untruncated value (raw), as cl ((double)(unsigned char)300.0 is 300)
    int                        m_rawTemp;
    const IrFunction*          m_rawFunction;
    int                        m_rawBlock;
    size_t                     m_rawEnd;
    double                     m_raw;
    void numberStaticBlocks(cxx::Stmt* s, long& next, long current);
    std::map<sema::Symbol*, sema::Symbol*> m_deleting;     // virtual destructor -> its deleting destructor
    std::vector<sema::Symbol*> m_deletingOrder;            // the virtual destructors, in the order their deleting ones were made
    size_t                     m_deletingDone;
    std::map<sema::Symbol*, sema::Symbol*> m_vbaseDtors;   // class -> its vbase destructor (??_D)
    std::map<sema::Symbol*, sema::Symbol*> m_vbaseDtorOf;  // vbase destructor -> its class
    int                        m_ctorFlag;      // the "most derived" flag the next constructor call passes (1, 0 for a base)
    int                        m_vbFlagSlot;
    std::vector<std::string>   m_unsupported;
    void unsupported(int line, const std::string& what);    // the constructor's own flag parameter, or -1
    int                        m_staticCount;
    int                        m_guardCount;    // guarded static locals so far in the function (cl's $TSS<n>)
    std::map<std::string, int> m_labels;           // goto labels of the current function -> block
    std::vector<std::set<std::string> > m_handlerLabels;   // the labels inside each catch handler being generated
    static void collectLabels(const cxx::Stmt* s, std::set<std::string>& out);

    IrFunction*                m_out;
    sema::Symbol*              m_fn;
    int                        m_block;
    int                        m_sretSlot;
    const sema::Type*          m_retReg;        // the integer type a small class result returns in, else 0
    bool                       m_nrvoEnabled;   // named return value optimisation (-O1 and up, as cl /O2)
    sema::Symbol*              m_nrvo;          // the local built in the hidden result object of this function, or 0
    std::map<sema::Symbol*, int> m_slots;
    std::vector<LoopTargets>   m_loops;
    std::vector<int>           m_tryStack;      // region ids, -1 inside a handler body
    int                        m_tryCount;
    std::vector<Cleanup>       m_cleanups;      // constructed objects of the open scopes, in construction order
    std::vector<size_t>        m_scopes;        // m_cleanups.size() at each open block scope
    std::vector<int>           m_scopeIds;      // a number for each open block scope (goto: the scopes it leaves)
    int                        m_scopeSerial;
    struct JumpState {                          // the scopes, objects and try regions at a goto or a label
        int                  block;             // goto: the block that leaves the scopes (forward jumps)
        int                  line;
        std::vector<int>     ids;
        std::vector<size_t>  scopes;
        std::vector<Cleanup> cleanups;
        std::vector<int>     tryStack;
    };
    std::map<std::string, JumpState>              m_labelStates;    // labels generated so far
    std::map<std::string, std::vector<JumpState> > m_pendingGotos;  // gotos to labels not generated yet
    JumpState jumpState(int block, int line) const;
    void      gotoExit(const JumpState& from, const JumpState& to, const std::string& label);
    std::vector<Cleanup>       m_temps;         // temporaries of the current full expression
    std::vector<Cleanup>       m_flagged;       // every cleanup of the function, for the unwind handlers
    std::vector<int>           m_dtorParts;     // a destructor: its members and bases (indexes into m_flagged)
    int                        m_inPad;         // > 0 while a landing pad is generated: no cleanups are registered
    int                        m_unwindId;      // the function's catch-all region
    int                        m_bodyBlock;
};

} // namespace cppparse

#endif
