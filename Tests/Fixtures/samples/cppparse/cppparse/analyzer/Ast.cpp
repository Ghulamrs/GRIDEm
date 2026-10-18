// Ast.cpp - AST node construction and dumping.
#include "Ast.hpp"
#include "PathUtil.hpp"

#include <ostream>

namespace cppparse {

Expr::Expr()
    : kind(Error), line(0), arrow(false), newArray(false), type(0), lvalue(false), sym(SymNone), index(-1),
      cls(0), func(0), var(0), isVirtualCall(false), isConstant(false), cval(0), fval(0)
{
}

Stmt::Stmt() : kind(Empty), line(0), cond(0), expr(0), init(0), step(0), ctorStyle(false), localIndex(-1) {}

AstArena::~AstArena()
{
    for (size_t i = 0; i < m_exprs.size(); ++i) delete m_exprs[i];
    for (size_t i = 0; i < m_stmts.size(); ++i) delete m_stmts[i];
}

Expr* AstArena::expr(Expr::Kind k, int line)
{
    Expr* e = new Expr;
    e->kind = k;
    e->line = line;
    m_exprs.push_back(e);
    return e;
}

Stmt* AstArena::stmt(Stmt::Kind k, int line)
{
    Stmt* s = new Stmt;
    s->kind = k;
    s->line = line;
    m_stmts.push_back(s);
    return s;
}

const char* exprKindName(Expr::Kind k)
{
    switch (k) {
    case Expr::IntLit: return "int";
    case Expr::FloatLit: return "float";
    case Expr::CharLit: return "char";
    case Expr::StringLit: return "string";
    case Expr::BoolLit: return "bool";
    case Expr::Name: return "name";
    case Expr::Member: return "member";
    case Expr::Index: return "index";
    case Expr::Call: return "call";
    case Expr::Unary: return "unary";
    case Expr::Postfix: return "postfix";
    case Expr::Binary: return "binary";
    case Expr::Assign: return "assign";
    case Expr::Cond: return "cond";
    case Expr::Cast: return "cast";
    case Expr::SizeOf: return "sizeof";
    case Expr::New: return "new";
    case Expr::Delete: return "delete";
    case Expr::This: return "this";
    case Expr::Comma: return "comma";
    case Expr::Throw: return "throw";
    default: return "error";
    }
}

const char* stmtKindName(Stmt::Kind k)
{
    switch (k) {
    case Stmt::Block: return "block";
    case Stmt::Decl: return "decl";
    case Stmt::ExprStmt: return "expr";
    case Stmt::If: return "if";
    case Stmt::While: return "while";
    case Stmt::DoWhile: return "do-while";
    case Stmt::For: return "for";
    case Stmt::Switch: return "switch";
    case Stmt::Case: return "case";
    case Stmt::Default: return "default";
    case Stmt::Break: return "break";
    case Stmt::Continue: return "continue";
    case Stmt::Return: return "return";
    case Stmt::Try: return "try";
    case Stmt::Catch: return "catch";
    case Stmt::Empty: return "empty";
    default: return "unsupported";
    }
}

namespace {

const char* symName(Expr::SymKind s)
{
    switch (s) {
    case Expr::SymLocal: return "local";
    case Expr::SymParam: return "param";
    case Expr::SymField: return "field";
    case Expr::SymGlobal: return "global";
    case Expr::SymFunction: return "function";
    case Expr::SymMethod: return "method";
    case Expr::SymEnumerator: return "enumerator";
    case Expr::SymType: return "type";
    case Expr::SymUnknown: return "unknown";
    default: return "";
    }
}

std::string pad(int n) { return std::string(static_cast<size_t>(n * 2), ' '); }

} // namespace

void dumpExpr(const Expr* e, std::ostream& os, int indent)
{
    if (!e) { os << pad(indent) << "(null)\n"; return; }
    os << pad(indent) << exprKindName(e->kind);
    if (!e->text.empty()) os << " '" << e->text << "'";
    if (!e->typeText.empty()) os << " <" << e->typeText << ">";
    if (e->arrow) os << " ->";
    if (e->type) os << "  : " << e->type->str() << (e->lvalue ? " lvalue" : "");
    if (e->sym != Expr::SymNone) {
        os << "  [" << symName(e->sym);
        if (e->index >= 0) os << " #" << e->index;
        if (e->func) os << " " << e->func->fullName() << "(" << e->func->paramText << ")";
        else if (e->cls && e->sym != Expr::SymFunction) os << " " << e->cls->qualifiedName;
        if (e->isVirtualCall) os << " virtual";
        os << "]";
    }
    if (e->isConstant) { os << "  = "; if (e->type && e->type->isFloating()) os << e->fval; else os << e->cval; }
    os << "\n";
    for (size_t i = 0; i < e->kids.size(); ++i) dumpExpr(e->kids[i], os, indent + 1);
}

void dumpStmt(const Stmt* s, std::ostream& os, int indent)
{
    if (!s) { os << pad(indent) << "(null)\n"; return; }
    os << pad(indent) << stmtKindName(s->kind) << " L" << s->line;
    if (s->kind == Stmt::Decl) os << " " << s->typeText << " " << s->name << (s->localIndex >= 0 ? " #" + intToString(s->localIndex) : "");
    if (s->kind == Stmt::Catch) os << " (" << s->typeText << (s->name.empty() ? "" : " " + s->name) << ")";
    if (s->kind == Stmt::Unsupported) os << " " << s->text;
    os << "\n";
    if (s->init) { os << pad(indent + 1) << "init:\n"; dumpStmt(s->init, os, indent + 2); }
    if (s->cond) { os << pad(indent + 1) << "cond:\n"; dumpExpr(s->cond, os, indent + 2); }
    if (s->step) { os << pad(indent + 1) << "step:\n"; dumpExpr(s->step, os, indent + 2); }
    if (s->expr) { os << pad(indent + 1) << (s->kind == Stmt::Decl ? "init:" : "expr:") << "\n"; dumpExpr(s->expr, os, indent + 2); }
    for (size_t i = 0; i < s->args.size(); ++i) { os << pad(indent + 1) << "arg:\n"; dumpExpr(s->args[i], os, indent + 2); }
    for (size_t i = 0; i < s->kids.size(); ++i) dumpStmt(s->kids[i], os, indent + 1);
}

void dumpFunctionAst(const FunctionAst& ast, std::ostream& os)
{
    os << "function " << (ast.fn ? ast.fn->displayName() + "(" + ast.fn->paramText + ")" : "?")
       << (ast.returnType ? "  -> " + ast.returnType->str() : "") << "\n";
    os << "  status: " << (ast.parsed ? "parsed" : "parse errors") << ", " << (ast.checked ? "type checked" : "type errors or unchecked") << "\n";
    for (size_t i = 0; i < ast.errors.size(); ++i) os << "  error: " << ast.errors[i] << "\n";
    for (size_t i = 0; i < ast.locals.size(); ++i)
        os << "  local #" << i << ": " << (ast.locals[i].type ? ast.locals[i].type->str() : ast.locals[i].typeText) << " " << ast.locals[i].name
           << " (L" << ast.locals[i].line << ")" << (ast.locals[i].addressTaken ? " address-taken" : "") << "\n";
    dumpStmt(ast.body, os, 1);
}

} // namespace cppparse
