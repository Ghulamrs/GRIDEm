// IrBuilder.hpp - version 3: IR generation from the old front end (FunctionAst, Model, Types) and the
// class layouts computed from the old model. Kept for the reports and the version 3 to 5 tests until the
// version 6 pipeline replaces them.
//
// Strict C++98.
#ifndef CPPPARSE_IRBUILDER_HPP
#define CPPPARSE_IRBUILDER_HPP

#include "Ast.hpp"
#include "Ir.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

// the name the IR uses for a function: "geom::Circle::area() const"; extern "C" functions keep their plain name
std::string irFunctionName(const FunctionInfo* f);
// the type descriptor a throw instruction carries in its 'sub' field (see the exception runtime)
std::string throwDescriptor(const Type* t);

// class layouts: sizes, field offsets, vtables
class Layouts {
public:
    struct Field { int offset; const Type* type; bool isStatic; Field() : offset(0), type(0), isStatic(false) {} };
    Layouts(TypeTable& types, const CodeBase& cb) : m_types(types), m_cb(cb) {}
    int  sizeOf(const Type* t);
    int  alignOf(const Type* t);
    bool field(const ClassInfo* cls, const std::string& name, Field& out);
    int  baseOffset(const ClassInfo* cls, const ClassInfo* base);
    bool hasVtable(const ClassInfo* cls);
    int  vtableSlot(const ClassInfo* cls, const FunctionInfo* method);   // -1 if not virtual
    int  classSize(const ClassInfo* cls);
    std::string describe(const ClassInfo* cls);   // "class X: size N { +0 vptr, +8 int a, ... }"
    // a standard-library class this pipeline does not know (std::vector<double>, std::string, a stream) held
    // by value, or a class whose layout contains one: its size and field offsets are guesses, so code that
    // copies, indexes or allocates it would be wrong. Pointers and references to it are fine.
    bool opaqueByValue(const Type* t);

private:
    struct Layout {
        int size, align; bool vptr;
        bool opaque;                       // a base or a field is opaque by value (see opaqueByValue)
        std::map<std::string, Field> fields;
        std::map<const ClassInfo*, int> bases;
        std::vector<std::string> vtable;   // "name(params)const" keys
        std::vector<std::string> order;    // display
        Layout() : size(0), align(1), vptr(false), opaque(false) {}
    };
    const Layout& layout(const ClassInfo* cls);
    std::map<const ClassInfo*, Layout> m_cache;
    TypeTable&      m_types;
    const CodeBase& m_cb;
};

class IrBuilder {
public:
    IrBuilder(const CodeBase& cb, TypeTable& types, Layouts& layouts);
    bool build(const FunctionAst& ast, IrFunction& out);

private:
    struct LValue {
        enum Kind { None, Local, Global, Mem } kind;
        int         local;
        std::string global;
        int         addr;      // temp holding the address
        const Type* type;      // type of the object
        LValue() : kind(None), local(-1), addr(-1), type(0) {}
    };
    struct LoopTargets { int breakBlock, continueBlock; size_t tryDepth; };   // tryDepth: try regions open at loop entry

    int    emit(IrInst inst);                       // returns dst
    void   leaveTryRegions(size_t depth, int line); // tryend for every region deeper than depth
    IrInst make(IrInst::Op op, const Type* type, int line);
    int    cur() const { return m_block; }
    void   setBlock(int b) { m_block = b; }
    bool   terminated() const;
    void   br(int target, int line);
    void   condbr(int cond, int t, int f, int line);

    void   stmt(Stmt* s);
    int    value(Expr* e);                          // rvalue temp
    LValue lvalue(Expr* e);                         // address of an lvalue
    int    load(const LValue& lv, const Type* t, int line);
    void   store(const LValue& lv, int temp, const Type* t, int line);
    int    convert(int temp, const Type* from, const Type* to, int line);
    int    toBool(int temp, const Type* t, int line);
    void   condition(Expr* e, int trueBlock, int falseBlock);
    int    call(Expr* e);
    int    binary(const std::string& op, int a, const Type* ta, int b, const Type* tb, const Type* result, int line);
    int    constant(long v, const Type* t, int line);
    int    thisTemp(int line);
    int    localIndexFor(const Expr* e) const;      // AST local -> IR local

    const CodeBase&     m_cb;
    TypeTable&          m_types;
    Layouts&            m_layouts;
    const FunctionAst*  m_ast;
    IrFunction*         m_out;
    int                 m_block;
    std::vector<LoopTargets> m_loops;
    std::vector<int>    m_switchBreaks;
    std::vector<int>    m_localMap;                  // ast local index -> ir local index
    int                 m_catchDepth;
    std::vector<int>    m_tryStack;                  // cleanup stack: try region id (>= 0) or -1 for a catch handler body
    int                 m_tryCount;                  // try regions in this function (ids 0..n-1, IrInst::extra)
};

} // namespace cppparse

#endif
