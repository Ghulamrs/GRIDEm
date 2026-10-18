// Ast.hpp - version 3: abstract syntax tree of function bodies.
//
// The body parser (BodyParser.cpp) builds one FunctionAst per function
// definition; the type checker annotates every Expr with its type, value
// category and the symbol it resolved to; the IR builder consumes the
// annotated tree.
//
// Strict C++98.
#ifndef CPPPARSE_AST_HPP
#define CPPPARSE_AST_HPP

#include "Types.hpp"

#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace cppparse {

struct Expr {
    enum Kind { IntLit, FloatLit, CharLit, StringLit, BoolLit, Name, Member, Index, Call, Unary, Postfix,
                Binary, Assign, Cond, Cast, SizeOf, New, Delete, This, Comma, Throw, Error };
    enum SymKind { SymNone, SymLocal, SymParam, SymField, SymGlobal, SymFunction, SymMethod,
                   SymEnumerator, SymType, SymUnknown };

    Kind               kind;
    std::string        text;        // operator, name (possibly qualified), literal text
    std::vector<Expr*> kids;        // operands; Call: kids[0] callee then arguments
    int                line;
    bool               arrow;       // Member: obj->name
    bool               newArray;    // New: new T[kids[0]]
    std::string        typeText;    // Cast / SizeOf / New: the written type

    // filled by the type checker
    const Type*        type;
    bool               lvalue;
    SymKind            sym;
    int                index;       // local index / parameter index / field index
    const ClassInfo*   cls;         // class owning the member, or the named class
    const FunctionInfo* func;       // resolved callee
    const VariableInfo* var;        // resolved field or global
    bool               isVirtualCall;
    bool               isConstant;
    long               cval;
    double             fval;

    Expr();
};

struct Stmt {
    enum Kind { Block, Decl, ExprStmt, If, While, DoWhile, For, Switch, Case, Default, Break, Continue,
                Return, Try, Catch, Empty, Unsupported };

    Kind               kind;
    int                line;
    std::vector<Stmt*> kids;        // Block: statements; If: then [else]; loops: body; Try: body, catches
    Expr*              cond;        // If / While / DoWhile / For / Switch
    Expr*              expr;        // ExprStmt / Return / Case / Decl initializer
    Stmt*              init;        // For
    Expr*              step;        // For
    std::string        typeText;    // Decl / Catch
    std::string        name;        // Decl / Catch
    std::vector<Expr*> args;        // Decl: "T x(a, b)"
    bool               ctorStyle;   // Decl written with parentheses
    int                localIndex;  // Decl / Catch: index into FunctionAst::locals
    std::string        text;        // Unsupported: what was skipped

    Stmt();
};

struct AstLocal {
    std::string name;
    std::string typeText;
    const Type* type;
    int         line;
    bool        addressTaken;
    AstLocal() : type(0), line(0), addressTaken(false) {}
};

class AstArena {
public:
    AstArena() {}
    ~AstArena();
    Expr* expr(Expr::Kind k, int line);
    Stmt* stmt(Stmt::Kind k, int line);
private:
    AstArena(const AstArena&);
    AstArena& operator=(const AstArena&);
    std::vector<Expr*> m_exprs;
    std::vector<Stmt*> m_stmts;
};

// one entry of a constructor initializer list: name(args)
struct InitEntry {
    std::string        name;      // member or base name as written
    std::vector<Expr*> args;
    int                line;
    InitEntry() : line(0) {}
};

struct FunctionAst {
    const FunctionInfo*       fn;
    Stmt*                     body;
    std::vector<InitEntry>    inits;         // constructor initializer list
    std::vector<AstLocal>     locals;
    std::vector<std::pair<std::string, std::string> > localTypedefs;   // alias -> target type text
    std::vector<std::pair<std::string, long> >        localEnumerators; // enumerators of enums declared in the body
    std::vector<const Type*>  paramTypes;
    const Type*               returnType;
    bool                      parsed;        // body parsed without errors
    bool                      checked;       // type checked without errors
    std::vector<std::string>  errors;        // "L12: ..." parse problems
    FunctionAst() : fn(0), body(0), returnType(0), parsed(false), checked(false) {}
};

void dumpExpr(const Expr* e, std::ostream& os, int indent);
void dumpStmt(const Stmt* s, std::ostream& os, int indent);
void dumpFunctionAst(const FunctionAst& ast, std::ostream& os);
const char* exprKindName(Expr::Kind k);
const char* stmtKindName(Stmt::Kind k);

} // namespace cppparse

#endif
