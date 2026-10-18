// TypeCheck.hpp - version 3: type checker over the AST.
//
// Resolves every name (locals, parameters, members including inherited
// ones, globals, functions, enumerators, types), computes the type and
// value category of every expression, performs overload resolution by
// argument types with implicit conversions, checks assignments, returns,
// conditions, operators and member access, and annotates the AST for the
// IR builder. Findings use T codes:
//
//   T100 undeclared identifier           T101 no member X in class Y
//   T102 no viable overload (by types)   T103 ambiguous call
//   T104 cannot convert X to Y           T105 invalid operands for operator
//   T106 non-const method on const obj   T107 assignment to const
//   T108 return type mismatch            T109 condition not convertible to bool
//   T110 member access on non-class      T111 subscript on non-pointer
//   T112 dereference of non-pointer      T113 argument count (typed)
//   T114 unknown type in declaration     T115 signed/unsigned comparison (W)
//   T116 narrowing conversion (W)        T117 use of '.' on pointer / '->' on object
//   T118 expression result unused (W)    T119 private/protected member access
//   T120 (reserved)                      T121 operand is not an lvalue
//   T122 break/continue outside loop     T123 switch on non-integral
//   T124 case value not constant         T125 duplicate case value
//   T190 statement outside the subset (I)
//
// Strict C++98.
#ifndef CPPPARSE_TYPECHECK_HPP
#define CPPPARSE_TYPECHECK_HPP

#include "Ast.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cppparse {

class TypeChecker {
public:
    // 'macros' is the set of macro names defined in the parsed files (computed once, shared read-only)
    TypeChecker(const CodeBase& cb, TypeTable& types, std::vector<Finding>& findings, const std::set<std::string>& macros);
    static void collectMacroNames(const CodeBase& cb, std::set<std::string>& out);
    bool check(FunctionAst& ast);

    // shared with the IR builder
    static bool findField(const ClassInfo* cls, const std::string& name, const ClassInfo*& owner, int& index, int depth = 0);
    static void findMethods(const ClassInfo* cls, const std::string& name, std::vector<const FunctionInfo*>& out, int depth = 0);
    static bool derivesFrom(const ClassInfo* cls, const ClassInfo* base, int depth = 0);

private:
    struct Scope { std::map<std::string, int> locals; };

    void add(Finding::Severity s, const char* code, int line, const std::string& msg, const std::string& detail = "");
    void push() { m_scopes.push_back(Scope()); }
    void pop()  { m_scopes.pop_back(); }
    void declare(const std::string& name, int localIndex);
    int  lookupLocal(const std::string& name) const;

    void stmt(Stmt* s);
    void block(Stmt* s, bool newScope);
    const Type* expr(Expr* e);
    const Type* checkName(Expr* e);
    const Type* checkMember(Expr* e);
    const Type* checkCall(Expr* e);
    const Type* checkUnary(Expr* e);
    const Type* checkBinary(Expr* e);
    const Type* checkAssign(Expr* e);
    const Type* checkIndex(Expr* e);
    const Type* checkCond(Expr* e);
    const Type* checkNew(Expr* e);
    const Type* checkCast(Expr* e);
    const Type* classOperator(Expr* e, const char* opName, const std::vector<Expr*>& operands);
    const Type* callFunction(Expr* e, const std::vector<const FunctionInfo*>& cands, size_t firstArg, const std::string& what);
    const Type* constructClass(Expr* e, const ClassInfo* cls, size_t firstArg, int line);

    int  convRank(const Type* from, bool fromLvalue, const Type* to, std::string& why);
    int  rankExpr(const Expr* e, const Type* to, std::string& why);   // convRank plus the null pointer constant rule
    bool checkConvert(Expr* e, const Type* to, const char* code, const std::string& context);
    const Type* arithResult(const Type* a, const Type* b);
    const Type* promote(const Type* t);
    bool isScalarish(const Type* t) const;
    bool requireLvalue(Expr* e, const std::string& what);
    bool accessible(const ClassInfo* memberOwner, Access access, const std::string& name, int line);
    void collectFreeFunctions(const std::string& name, std::vector<const FunctionInfo*>& out) const;
    bool findGlobal(const std::string& name, const VariableInfo*& out) const;
    void evalConst(Expr* e);
    const Type* parseType(const std::string& text) { return m_types.parse(text, m_scopeName, m_owner); }
    const Type* fieldType(const VariableInfo& v, const ClassInfo* owner);

    const CodeBase&         m_cb;
    TypeTable&              m_types;
    std::vector<Finding>&   m_findings;
    FunctionAst*            m_ast;
    const FunctionInfo*     m_fn;
    const ClassInfo*        m_owner;
    std::string             m_scopeName;
    std::vector<Scope>      m_scopes;
    int                     m_loopDepth;
    int                     m_switchDepth;
    int                     m_errors;
    std::vector<std::string> m_usingNamespaces;
    const std::set<std::string>& m_macroNames;
    bool                    m_calleeContext;      // resolving the callee of a call
    std::set<std::string>   m_undeclaredCalls;    // reported once per function
    bool                    m_receiverConst;      // the object a member call is made on is const
};

} // namespace cppparse

#endif
