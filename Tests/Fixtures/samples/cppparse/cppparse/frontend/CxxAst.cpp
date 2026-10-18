// CxxAst.cpp - version 6: syntax tree helpers, text rendering, dump and counts.
#include "CxxAst.hpp"

#include <sstream>

namespace cppparse {
namespace cxx {


std::string QualifiedName::text() const
{
    std::string s = global ? "::" : "";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) s += "::";
        const NameComponent& c = parts[i];
        if (c.convType) { s += "operator " + c.convType->text(); continue; }
        s += c.ident;
        if (c.hasTemplateArgs) {
            s += "<";
            for (size_t a = 0; a < c.templateArgs.size(); ++a) {
                if (a) s += ", ";
                if (c.templateArgs[a].type) s += c.templateArgs[a].type->text();
                else s += "(expr)";
            }
            s += ">";
        }
    }
    return s;
}

DeclSpec::DeclSpec()
    : isTypedef(false), isStatic(false), isExtern(false), isMutable(false), isRegister(false), isAuto(false), isFriend(false),
      isVirtual(false), isExplicit(false), isInline(false), isConst(false), isVolatile(false), kind(None),
      builtinChar(0), builtinInt(0), builtinLong(0), builtinShort(0), builtinSigned(0), builtinUnsigned(0), builtinFloat(0),
      builtinDouble(0), builtinVoid(0), builtinBool(0), builtinWchar(0), classDef(0), enumDef(0), align(0) {}

std::string DeclSpec::text() const
{
    std::string s;
    if (isConst) s += "const ";
    if (isVolatile) s += "volatile ";
    switch (kind) {
    case None: break;
    case Builtin:
        if (builtinUnsigned) s += "unsigned ";
        if (builtinSigned) s += "signed ";
        if (builtinShort) s += "short ";
        for (int i = 0; i < builtinLong; ++i) s += "long ";
        if (builtinChar) s += "char ";
        else if (builtinBool) s += "bool ";
        else if (builtinVoid) s += "void ";
        else if (builtinFloat) s += "float ";
        else if (builtinDouble) s += "double ";
        else if (builtinWchar) s += "wchar_t ";
        else if (builtinInt || (!builtinShort && !builtinLong && !builtinSigned && !builtinUnsigned)) s += "int ";
        else s += "int ";
        break;
    case Named:
        if (!elaborated.empty()) s += elaborated + " ";
        s += name.text() + " ";
        break;
    case Class: s += (classDef ? classDef->key + " " + classDef->name.text() : std::string("class")) + " "; break;
    case Enum: s += "enum " + (enumDef ? enumDef->name : std::string("")) + " "; break;
    }
    while (!s.empty() && s[s.size() - 1] == ' ') s.erase(s.size() - 1);
    return s;
}

bool Declarator::isFunction() const
{
    // the part applied last decides what the declared entity is: parts are stored in application order,
    // so the entity is a function when the last part is Function
    return !parts.empty() && parts.back().kind == DeclaratorPart::Function;
}

const DeclaratorPart* Declarator::functionPart() const
{
    return isFunction() ? &parts.back() : 0;
}

std::string Declarator::text() const
{
    // renders "name" with the parts after it in a readable, not source-exact, form
    std::string s = name.text();
    for (size_t i = parts.size(); i > 0; --i) {
        const DeclaratorPart& p = parts[i - 1];
        switch (p.kind) {
        case DeclaratorPart::Pointer: s = "*" + std::string(p.isConst ? "const " : "") + s; break;
        case DeclaratorPart::Reference: s = "&" + s; break;
        case DeclaratorPart::MemberPointer: s = p.memberClass.text() + "::*" + s; break;
        case DeclaratorPart::Array: s += "[]"; break;
        case DeclaratorPart::Paren: s = "(" + s + ")"; break;
        case DeclaratorPart::Function: {
            s += "(";
            for (size_t k = 0; k < p.params.size(); ++k) {
                if (k) s += ", ";
                s += p.params[k]->spec.text();
                if (p.params[k]->decl) { std::string d = p.params[k]->decl->text(); if (!d.empty()) s += " " + d; }
            }
            if (p.variadic) s += p.params.empty() ? "..." : ", ...";
            s += ")";
            if (p.isConst) s += " const";
            break;
        }
        }
    }
    return s;
}

std::string TypeId::text() const
{
    std::string s = spec.text();
    if (decl) { std::string d = decl->text(); if (!d.empty()) s += " " + d; }
    return s;
}


CxxArena::~CxxArena()
{
    for (size_t i = 0; i < m_exprs.size(); ++i) delete m_exprs[i];
    for (size_t i = 0; i < m_stmts.size(); ++i) delete m_stmts[i];
    for (size_t i = 0; i < m_decls.size(); ++i) delete m_decls[i];
    for (size_t i = 0; i < m_declarators.size(); ++i) delete m_declarators[i];
    for (size_t i = 0; i < m_typeIds.size(); ++i) delete m_typeIds[i];
    for (size_t i = 0; i < m_params.size(); ++i) delete m_params[i];
    for (size_t i = 0; i < m_classes.size(); ++i) delete m_classes[i];
    for (size_t i = 0; i < m_enums.size(); ++i) delete m_enums[i];
    for (size_t i = 0; i < m_tparams.size(); ++i) delete m_tparams[i];
}


namespace {

int g_exprDepth = 0;

struct DepthGuard { DepthGuard() { ++g_exprDepth; } ~DepthGuard() { --g_exprDepth; } };

std::string exprText(const Expr* e)
{
    if (!e) return "";
    DepthGuard guard;
    if (g_exprDepth > 100) return "<cycle>";   // a malformed tree must not overflow the stack
    switch (e->kind) {
    case Expr::IntLit: case Expr::FloatLit: case Expr::CharLit: case Expr::StringLit: case Expr::BoolLit: return e->text;
    case Expr::This: return "this";
    case Expr::Name: return e->name.text();
    case Expr::Paren: return "(" + exprText(e->kids[0]) + ")";
    case Expr::Unary: return e->text + exprText(e->kids[0]);
    case Expr::PostIncDec: return exprText(e->kids[0]) + e->text;
    case Expr::Binary: case Expr::Assign: return exprText(e->kids[0]) + " " + e->text + " " + exprText(e->kids[1]);
    case Expr::Cond: return exprText(e->kids[0]) + " ? " + exprText(e->kids[1]) + " : " + exprText(e->kids[2]);
    case Expr::Comma: return exprText(e->kids[0]) + ", " + exprText(e->kids[1]);
    case Expr::Call: {
        std::string s = exprText(e->kids[0]) + "(";
        for (size_t i = 1; i < e->kids.size(); ++i) { if (i > 1) s += ", "; s += exprText(e->kids[i]); }
        return s + ")";
    }
    case Expr::Index: return exprText(e->kids[0]) + "[" + exprText(e->kids[1]) + "]";
    case Expr::Member: return exprText(e->kids[0]) + (e->arrowMember ? "->" : ".") + e->name.text();
    case Expr::Cast: return "(" + (e->type ? e->type->text() : std::string("?")) + ")" + exprText(e->kids[0]);
    case Expr::NamedCast: return e->text + "<" + (e->type ? e->type->text() : std::string("?")) + ">(" + exprText(e->kids[0]) + ")";
    case Expr::FunctionalCast: {
        std::string s = (e->type ? e->type->text() : std::string("?")) + "(";
        for (size_t i = 0; i < e->kids.size(); ++i) { if (i) s += ", "; s += exprText(e->kids[i]); }
        return s + ")";
    }
    case Expr::SizeofExpr: return "sizeof " + exprText(e->kids[0]);
    case Expr::SizeofType: return "sizeof(" + (e->type ? e->type->text() : std::string("?")) + ")";
    case Expr::New: {
        std::string s = "new " + (e->type ? e->type->text() : std::string("?"));
        if (e->hasInit) { s += "("; for (size_t i = 0; i < e->kids.size(); ++i) { if (i) s += ", "; s += exprText(e->kids[i]); } s += ")"; }
        return s;
    }
    case Expr::Delete: return std::string(e->arrayDelete ? "delete[] " : "delete ") + exprText(e->kids[0]);
    case Expr::Throw: return e->kids.empty() ? "throw" : "throw " + exprText(e->kids[0]);
    case Expr::Typeid: return "typeid(" + (e->type ? e->type->text() : exprText(e->kids.empty() ? 0 : e->kids[0])) + ")";
    case Expr::InitList: {
        std::string s = "{";
        for (size_t i = 0; i < e->kids.size(); ++i) { if (i) s += ", "; s += exprText(e->kids[i]); }
        return s + "}";
    }
    case Expr::Error: return "<error>";
    }
    return "?";
}

void dumpStmt(const Stmt* s, int depth, std::ostringstream& out);
void dumpDecl(const Decl* d, int depth, std::ostringstream& out);

std::string pad(int depth) { return std::string(static_cast<size_t>(depth * 2), ' '); }

void dumpStmt(const Stmt* s, int depth, std::ostringstream& out)
{
    if (!s) return;
    std::string p = pad(depth);
    switch (s->kind) {
    case Stmt::Compound: out << p << "{\n"; for (size_t i = 0; i < s->kids.size(); ++i) dumpStmt(s->kids[i], depth + 1, out); out << p << "}\n"; break;
    case Stmt::ExprStmt: out << p << "expr " << exprText(s->expr) << "\n"; break;
    case Stmt::DeclStmt: dumpDecl(s->decl, depth, out); break;
    case Stmt::If:
        out << p << "if " << (s->decl ? "(decl)" : exprText(s->expr)) << "\n";
        dumpStmt(s->kids[0], depth + 1, out);
        if (s->kids.size() > 1) { out << p << "else\n"; dumpStmt(s->kids[1], depth + 1, out); }
        break;
    case Stmt::Switch: out << p << "switch " << exprText(s->expr) << "\n"; dumpStmt(s->kids[0], depth + 1, out); break;
    case Stmt::While: out << p << "while " << exprText(s->expr) << "\n"; dumpStmt(s->kids[0], depth + 1, out); break;
    case Stmt::DoWhile: out << p << "do\n"; dumpStmt(s->kids[0], depth + 1, out); out << p << "while " << exprText(s->expr) << "\n"; break;
    case Stmt::For:
        out << p << "for\n";
        if (s->init) dumpStmt(s->init, depth + 1, out);
        out << p << "  cond " << exprText(s->expr) << "; step " << exprText(s->expr2) << "\n";
        dumpStmt(s->kids[0], depth + 1, out);
        break;
    case Stmt::Break: out << p << "break\n"; break;
    case Stmt::Continue: out << p << "continue\n"; break;
    case Stmt::Return: out << p << "return " << exprText(s->expr) << "\n"; break;
    case Stmt::Goto: out << p << "goto " << s->label << "\n"; break;
    case Stmt::Label: out << p << s->label << ":\n"; dumpStmt(s->kids[0], depth, out); break;
    case Stmt::Case: out << p << "case " << exprText(s->expr) << ":\n"; dumpStmt(s->kids[0], depth + 1, out); break;
    case Stmt::Default: out << p << "default:\n"; dumpStmt(s->kids[0], depth + 1, out); break;
    case Stmt::Try: out << p << "try\n"; for (size_t i = 0; i < s->kids.size(); ++i) dumpStmt(s->kids[i], depth + 1, out); break;
    case Stmt::Handler:
        out << p << "catch " << (s->catchAll ? "(...)" : "(decl)") << "\n";
        if (s->decl) dumpDecl(s->decl, depth + 1, out);
        dumpStmt(s->kids[0], depth + 1, out);
        break;
    case Stmt::Empty: out << p << ";\n"; break;
    case Stmt::Asm: out << p << "asm\n"; break;
    case Stmt::Error: out << p << "<error>\n"; break;
    }
}

void dumpClass(const ClassDecl* c, int depth, std::ostringstream& out)
{
    std::string p = pad(depth);
    out << p << c->key << " " << c->name.text();
    for (size_t b = 0; b < c->bases.size(); ++b) {
        out << (b ? ", " : " : ") << (c->bases[b].isVirtual ? "virtual " : "")
            << (c->bases[b].access == AccessPublic ? "public " : c->bases[b].access == AccessProtected ? "protected " : c->bases[b].access == AccessPrivate ? "private " : "")
            << c->bases[b].name.text();
    }
    out << (c->isDefinition ? " {\n" : ";\n");
    if (!c->isDefinition) return;
    for (size_t m = 0; m < c->members.size(); ++m) dumpDecl(c->members[m], depth + 1, out);
    out << p << "}\n";
}

void dumpDecl(const Decl* d, int depth, std::ostringstream& out)
{
    if (!d) return;
    std::string p = pad(depth);
    std::string acc = d->access == AccessPublic ? "public " : d->access == AccessProtected ? "protected " : d->access == AccessPrivate ? "private " : "";
    switch (d->kind) {
    case Decl::Simple: {
        if (d->spec.kind == DeclSpec::Class && d->spec.classDef) { dumpClass(d->spec.classDef, depth, out); if (d->declarators.empty()) break; }
        if (d->spec.kind == DeclSpec::Enum && d->spec.enumDef) {
            out << p << "enum " << d->spec.enumDef->name << " {";
            for (size_t e = 0; e < d->spec.enumDef->enumerators.size(); ++e) out << (e ? ", " : " ") << d->spec.enumDef->enumerators[e].name;
            out << " }\n";
            if (d->declarators.empty()) break;
        }
        out << p << acc;
        if (d->spec.isTypedef) out << "typedef ";
        if (d->spec.isStatic) out << "static ";
        if (d->spec.isExtern) out << "extern ";
        if (d->spec.isVirtual) out << "virtual ";
        if (d->spec.isFriend) out << "friend ";
        out << d->spec.text();
        for (size_t i = 0; i < d->declarators.size(); ++i) {
            const InitDeclarator& id = d->declarators[i];
            out << (i ? ", " : " ") << (id.decl ? id.decl->text() : "");
            if (id.pureVirtual) out << " = 0";
            else if (id.init.kind == Initializer::Assign && !id.init.args.empty()) out << " = " << exprText(id.init.args[0]);
            else if (id.init.kind == Initializer::Parens) { out << "("; for (size_t a = 0; a < id.init.args.size(); ++a) out << (a ? ", " : "") << exprText(id.init.args[a]); out << ")"; }
            else if (id.init.kind == Initializer::Brace) out << " = {...}";
        }
        out << ";\n";
        break;
    }
    case Decl::Function:
        out << p << acc << "function ";
        if (d->spec.isStatic) out << "static ";
        if (d->spec.isVirtual) out << "virtual ";
        if (d->spec.isInline) out << "inline ";
        out << d->spec.text() << " " << (d->declarator ? d->declarator->text() : "");
        if (!d->memInits.empty()) { out << " :"; for (size_t m = 0; m < d->memInits.size(); ++m) out << (m ? ", " : " ") << d->memInits[m].name.text() << "(...)"; }
        out << "\n";
        dumpStmt(d->body, depth + 1, out);
        break;
    case Decl::Class: dumpClass(d->classDef, depth, out); break;
    case Decl::Enum:
        out << p << "enum " << (d->enumDef ? d->enumDef->name : std::string("")) << " {";
        if (d->enumDef) for (size_t e = 0; e < d->enumDef->enumerators.size(); ++e) out << (e ? ", " : " ") << d->enumDef->enumerators[e].name;
        out << " }\n";
        break;
    case Decl::Namespace:
        out << p << "namespace " << (d->name.empty() ? "(anonymous)" : d->name) << " {\n";
        for (size_t i = 0; i < d->decls.size(); ++i) dumpDecl(d->decls[i], depth + 1, out);
        out << p << "}\n";
        break;
    case Decl::NamespaceAlias: out << p << "namespace " << d->name << " = " << d->target.text() << ";\n"; break;
    case Decl::UsingDirective: out << p << "using namespace " << d->target.text() << ";\n"; break;
    case Decl::UsingDecl: out << p << "using " << d->target.text() << ";\n"; break;
    case Decl::Template:
        out << p << "template <";
        for (size_t i = 0; i < d->templateParams.size(); ++i) {
            const TemplateParam* t = d->templateParams[i];
            out << (i ? ", " : "") << (t->kind == TemplateParam::Type ? "typename " : t->kind == TemplateParam::Template ? "template class " : "")
                << (t->kind == TemplateParam::NonType && t->nonType ? t->nonType->spec.text() + " " : "") << t->name;
        }
        out << ">\n";
        for (size_t i = 0; i < d->decls.size(); ++i) dumpDecl(d->decls[i], depth + 1, out);
        break;
    case Decl::ExplicitInstantiation: out << p << "template instantiation\n"; for (size_t i = 0; i < d->decls.size(); ++i) dumpDecl(d->decls[i], depth + 1, out); break;
    case Decl::LinkageSpec:
        out << p << "extern \"" << d->name << "\" {\n";
        for (size_t i = 0; i < d->decls.size(); ++i) dumpDecl(d->decls[i], depth + 1, out);
        out << p << "}\n";
        break;
    case Decl::AccessLabel: out << p << acc << ":\n"; break;
    case Decl::Friend: out << p << "friend " << d->spec.text() << ";\n"; break;
    case Decl::Empty: break;
    case Decl::Error: out << p << "<error>\n"; break;
    }
}

void countStmt(const Stmt* s, Counts& c);
void countDecl(const Decl* d, Counts& c);

void countExpr(const Expr* e, Counts& c)
{
    if (!e) return;
    ++c.expressions;
    for (size_t i = 0; i < e->kids.size(); ++i) countExpr(e->kids[i], c);
    for (size_t i = 0; i < e->placement.size(); ++i) countExpr(e->placement[i], c);
}

void countStmt(const Stmt* s, Counts& c)
{
    if (!s) return;
    ++c.statements;
    countExpr(s->expr, c);
    countExpr(s->expr2, c);
    if (s->decl) countDecl(s->decl, c);
    countStmt(s->init, c);
    for (size_t i = 0; i < s->kids.size(); ++i) countStmt(s->kids[i], c);
}

void countClass(const ClassDecl* cls, Counts& c)
{
    if (!cls || !cls->isDefinition) return;
    ++c.classes;
    for (size_t i = 0; i < cls->members.size(); ++i) countDecl(cls->members[i], c);
}

void countDecl(const Decl* d, Counts& c)
{
    if (!d) return;
    switch (d->kind) {
    case Decl::Simple:
        if (d->spec.kind == DeclSpec::Class) countClass(d->spec.classDef, c);
        if (d->spec.kind == DeclSpec::Enum && d->spec.enumDef && d->spec.enumDef->isDefinition) ++c.enums;
        for (size_t i = 0; i < d->declarators.size(); ++i) {
            const InitDeclarator& id = d->declarators[i];
            if (d->spec.isTypedef) ++c.typedefs;
            else if (id.decl && id.decl->isFunction()) ++c.functionsDeclared;
            else ++c.variables;
            for (size_t a = 0; a < id.init.args.size(); ++a) countExpr(id.init.args[a], c);
        }
        break;
    case Decl::Function:
        ++c.functionsDefined;
        for (size_t m = 0; m < d->memInits.size(); ++m) for (size_t a = 0; a < d->memInits[m].args.size(); ++a) countExpr(d->memInits[m].args[a], c);
        countStmt(d->body, c);
        break;
    case Decl::Class: countClass(d->classDef, c); break;
    case Decl::Enum: if (d->enumDef && d->enumDef->isDefinition) ++c.enums; break;
    case Decl::Namespace: ++c.namespaces; for (size_t i = 0; i < d->decls.size(); ++i) countDecl(d->decls[i], c); break;
    case Decl::Template: ++c.templates; for (size_t i = 0; i < d->decls.size(); ++i) countDecl(d->decls[i], c); break;
    case Decl::ExplicitInstantiation: case Decl::LinkageSpec: for (size_t i = 0; i < d->decls.size(); ++i) countDecl(d->decls[i], c); break;
    case Decl::UsingDirective: case Decl::UsingDecl: case Decl::NamespaceAlias: ++c.usings; break;
    default: break;
    }
}

} // namespace

std::string expressionText(const Expr* e) { return exprText(e); }

std::string dumpTranslationUnit(const TranslationUnit& tu)
{
    std::ostringstream out;
    out << "translation unit " << tu.path << "\n";
    for (size_t i = 0; i < tu.decls.size(); ++i) dumpDecl(tu.decls[i], 0, out);
    return out.str();
}

void countTranslationUnit(const TranslationUnit& tu, Counts& out)
{
    for (size_t i = 0; i < tu.decls.size(); ++i) countDecl(tu.decls[i], out);
}

// ---- deep copies ------------------------------------------------------------------------------------------

namespace {

struct Cloner {
    CxxArena& a;
    explicit Cloner(CxxArena& arena) : a(arena) {}

    void name(QualifiedName& n)
    {
        for (size_t i = 0; i < n.parts.size(); ++i) {
            NameComponent& c = n.parts[i];
            if (c.convType) c.convType = typeId(c.convType);
            for (size_t k = 0; k < c.templateArgs.size(); ++k) {
                if (c.templateArgs[k].type) c.templateArgs[k].type = typeId(c.templateArgs[k].type);
                if (c.templateArgs[k].expr) c.templateArgs[k].expr = expr(c.templateArgs[k].expr);
            }
        }
    }
    void declSpec(DeclSpec& s)
    {
        name(s.name);
        if (s.classDef) s.classDef = classDecl(s.classDef);
        if (s.enumDef) s.enumDef = enumDecl(s.enumDef);
    }
    void init(Initializer& i)
    {
        for (size_t k = 0; k < i.args.size(); ++k) i.args[k] = expr(i.args[k]);
    }
    Expr* expr(const Expr* e)
    {
        if (!e) return 0;
        Expr* c = a.expr(e->kind, e->loc);
        *c = *e;
        name(c->name);
        for (size_t i = 0; i < c->kids.size(); ++i) c->kids[i] = expr(c->kids[i]);
        for (size_t i = 0; i < c->placement.size(); ++i) c->placement[i] = expr(c->placement[i]);
        if (c->type) c->type = typeId(c->type);
        c->semaType = 0; c->semaSym = 0; c->semaLvalue = false; c->semaConst = false; c->semaValue = 0;
        return c;
    }
    Stmt* stmt(const Stmt* s)
    {
        if (!s) return 0;
        Stmt* c = a.stmt(s->kind, s->loc);
        *c = *s;
        c->expr = expr(c->expr);
        c->expr2 = expr(c->expr2);
        c->decl = decl(c->decl);
        c->init = stmt(c->init);
        for (size_t i = 0; i < c->kids.size(); ++i) c->kids[i] = stmt(c->kids[i]);
        return c;
    }
    TypeId* typeId(const TypeId* t)
    {
        if (!t) return 0;
        TypeId* c = a.typeId();
        *c = *t;
        declSpec(c->spec);
        c->decl = declarator(c->decl);
        return c;
    }
    Param* param(const Param* p)
    {
        if (!p) return 0;
        Param* c = a.param();
        *c = *p;
        declSpec(c->spec);
        c->decl = declarator(c->decl);
        c->defaultValue = expr(c->defaultValue);
        return c;
    }
    Declarator* declarator(const Declarator* d)
    {
        if (!d) return 0;
        Declarator* c = a.declarator();
        *c = *d;
        name(c->name);
        for (size_t i = 0; i < c->parts.size(); ++i) {
            DeclaratorPart& p = c->parts[i];
            p.arraySize = expr(p.arraySize);
            for (size_t k = 0; k < p.params.size(); ++k) p.params[k] = param(p.params[k]);
            for (size_t k = 0; k < p.throwSpec.size(); ++k) p.throwSpec[k] = typeId(p.throwSpec[k]);
            name(p.memberClass);
        }
        c->bitfield = expr(c->bitfield);
        c->semaSym = 0;
        return c;
    }
    TemplateParam* templateParam(const TemplateParam* t)
    {
        if (!t) return 0;
        TemplateParam* c = a.templateParam();
        *c = *t;
        c->defaultType = typeId(c->defaultType);
        c->nonType = param(c->nonType);
        for (size_t i = 0; i < c->params.size(); ++i) c->params[i] = templateParam(c->params[i]);
        return c;
    }
    EnumDecl* enumDecl(const EnumDecl* e)
    {
        if (!e) return 0;
        EnumDecl* c = a.enumDecl();
        *c = *e;
        for (size_t i = 0; i < c->enumerators.size(); ++i) c->enumerators[i].value = expr(c->enumerators[i].value);
        return c;
    }
    ClassDecl* classDecl(const ClassDecl* k)
    {
        if (!k) return 0;
        ClassDecl* c = a.classDecl();
        *c = *k;
        name(c->name);
        for (size_t i = 0; i < c->bases.size(); ++i) name(c->bases[i].name);
        for (size_t i = 0; i < c->members.size(); ++i) c->members[i] = decl(c->members[i]);
        return c;
    }
    Decl* decl(const Decl* d)
    {
        if (!d) return 0;
        Decl* c = a.decl(d->kind, d->loc);
        *c = *d;
        declSpec(c->spec);
        for (size_t i = 0; i < c->declarators.size(); ++i) { c->declarators[i].decl = declarator(c->declarators[i].decl); init(c->declarators[i].init); }
        c->declarator = declarator(c->declarator);
        for (size_t i = 0; i < c->memInits.size(); ++i) { c->memInits[i].semaSym = 0; name(c->memInits[i].name); for (size_t k = 0; k < c->memInits[i].args.size(); ++k) c->memInits[i].args[k] = expr(c->memInits[i].args[k]); }
        c->body = stmt(c->body);
        c->classDef = classDecl(c->classDef);
        c->enumDef = enumDecl(c->enumDef);
        name(c->target);
        for (size_t i = 0; i < c->decls.size(); ++i) c->decls[i] = decl(c->decls[i]);
        for (size_t i = 0; i < c->templateParams.size(); ++i) c->templateParams[i] = templateParam(c->templateParams[i]);
        c->semaSym = 0;
        return c;
    }
};

} // namespace

Decl* cloneDecl(const Decl* d, CxxArena& arena)
{
    Cloner c(arena);
    return c.decl(d);
}

ClassDecl* cloneClassDecl(const ClassDecl* k, CxxArena& arena)
{
    Cloner c(arena);
    return c.classDecl(k);
}

} // namespace cxx
} // namespace cppparse
