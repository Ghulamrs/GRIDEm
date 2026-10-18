// TypeCheck.cpp - version 3 type checker.
#include "TypeCheck.hpp"
#include "Instantiate.hpp"
#include "PathUtil.hpp"

#include <cstdlib>

namespace cppparse {

TypeChecker::TypeChecker(const CodeBase& cb, TypeTable& types, std::vector<Finding>& findings, const std::set<std::string>& macros)
    : m_cb(cb), m_types(types), m_findings(findings), m_ast(0), m_fn(0), m_owner(0), m_loopDepth(0), m_switchDepth(0), m_errors(0),
      m_macroNames(macros), m_calleeContext(false), m_receiverConst(false)
{
}

void TypeChecker::collectMacroNames(const CodeBase& cb, std::set<std::string>& out)
{
    const std::vector<SourceFile*>& files = cb.files();
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t d = 0; d < files[f]->directives.size(); ++d)
            if (files[f]->directives[d].kind == Directive::Define) out.insert(files[f]->directives[d].argument);
}

void TypeChecker::add(Finding::Severity s, const char* code, int line, const std::string& msg, const std::string& detail)
{
    Finding f;
    f.severity = s;
    f.code = code;
    f.file = m_fn ? m_fn->file : "";
    f.line = line;
    f.message = msg + (m_fn ? "  [in " + m_fn->fullName() + "]" : "");
    f.detail = detail;
    m_findings.push_back(f);
    if (s == Finding::Error) ++m_errors;
}

void TypeChecker::declare(const std::string& name, int localIndex)
{
    if (m_scopes.empty()) push();
    m_scopes.back().locals[name] = localIndex;
}

int TypeChecker::lookupLocal(const std::string& name) const
{
    for (size_t i = m_scopes.size(); i > 0; --i) {
        std::map<std::string, int>::const_iterator it = m_scopes[i - 1].locals.find(name);
        if (it != m_scopes[i - 1].locals.end()) return it->second;
    }
    return -1;
}

// ---- class helpers -----------------------------------------------------------------

bool TypeChecker::findField(const ClassInfo* cls, const std::string& name, const ClassInfo*& owner, int& index, int depth)
{
    if (!cls || depth > 32) return false;
    for (size_t i = 0; i < cls->fields.size(); ++i)
        if (cls->fields[i].name == name) { owner = cls; index = static_cast<int>(i); return true; }
    for (size_t b = 0; b < cls->bases.size(); ++b)
        if (findField(cls->bases[b].resolved, name, owner, index, depth + 1)) return true;
    return false;
}

void TypeChecker::findMethods(const ClassInfo* cls, const std::string& name, std::vector<const FunctionInfo*>& out, int depth)
{
    if (!cls || depth > 32) return;
    for (size_t i = 0; i < cls->methods.size(); ++i) if (cls->methods[i].name == name) out.push_back(&cls->methods[i]);
    if (!out.empty()) return;   // derived declarations hide base ones
    for (size_t b = 0; b < cls->bases.size(); ++b) findMethods(cls->bases[b].resolved, name, out, depth + 1);
}

bool TypeChecker::derivesFrom(const ClassInfo* cls, const ClassInfo* base, int depth)
{
    if (!cls || !base || depth > 32) return false;
    if (cls == base) return true;
    // an instantiation and its template are the same class seen from inside the template
    // (injected class name), and so are two instantiations of the same template written differently
    if (!cls->tmpl.specializationArgs.empty() && base->tmpl.isTemplate && startsWith(cls->qualifiedName, base->qualifiedName + "<")) return true;
    if (!base->tmpl.specializationArgs.empty() && cls->tmpl.isTemplate && startsWith(base->qualifiedName, cls->qualifiedName + "<")) return true;
    for (size_t b = 0; b < cls->bases.size(); ++b) if (derivesFrom(cls->bases[b].resolved, base, depth + 1)) return true;
    return false;
}

bool TypeChecker::accessible(const ClassInfo* memberOwner, Access access, const std::string& name, int line)
{
    if (!memberOwner || access == AccessPublic || access == AccessNone) return true;
    if (!memberOwner->friends.empty()) return true;   // friends are not tracked precisely
    if (m_owner && (m_owner == memberOwner || derivesFrom(m_owner, memberOwner) || derivesFrom(memberOwner, m_owner))) {
        if (access == AccessProtected) return true;
        for (const ClassInfo* c = m_owner; c; c = c->owner) if (c == memberOwner) return true;
        if (m_owner == memberOwner) return true;
    }
    if (m_owner) for (const ClassInfo* c = m_owner->owner; c; c = c->owner) if (c == memberOwner) return true;
    add(Finding::Error, "T119", line, std::string(accessName(access)) + " member " + memberOwner->qualifiedName + "::" + name + " is not accessible here");
    return false;
}

const Type* TypeChecker::fieldType(const VariableInfo& v, const ClassInfo* owner)
{
    return m_types.parse(v.type, owner ? owner->qualifiedName : v.scope, owner);
}

void TypeChecker::collectFreeFunctions(const std::string& name, std::vector<const FunctionInfo*>& out) const
{
    std::string qualifier, simple = name;
    size_t pos = name.rfind("::");
    if (pos != std::string::npos) { qualifier = name.substr(0, pos); simple = name.substr(pos + 2); }
    if (qualifier.compare(0, 2, "::") == 0) qualifier = qualifier.substr(2);
    const std::vector<SourceFile*>& files = m_cb.files();
    std::vector<std::string> seenKeys;
    for (size_t f = 0; f < files.size(); ++f) {
        for (size_t i = 0; i < files[f]->functions.size(); ++i) {
            const FunctionInfo& g = files[f]->functions[i];
            if (g.name != simple || g.ownerClass) continue;
            std::string full = g.fullName();
            bool ok = false;
            if (!qualifier.empty()) ok = (full == qualifier + "::" + simple) || endsWith(full, "::" + qualifier + "::" + simple);
            else {
                // visible from the current scope: enclosing namespaces, using-directives, anonymous namespace of this file
                std::string s = m_scopeName;
                for (;;) {
                    if (full == (s.empty() ? simple : s + "::" + simple)) { ok = true; break; }
                    if (s.empty()) break;
                    size_t p = s.rfind("::");
                    s = p == std::string::npos ? "" : s.substr(0, p);
                }
                for (size_t u = 0; u < m_usingNamespaces.size() && !ok; ++u) if (full == m_usingNamespaces[u] + "::" + simple) ok = true;
                if (!ok && g.scope.find("(anonymous)") != std::string::npos && m_fn && samePath(g.file, m_fn->file)) ok = true;
            }
            if (!ok) continue;
            // one entry per distinct signature; prefer the declaration (it carries default arguments)
            std::string key;
            for (size_t p = 0; p < g.params.size(); ++p) key += g.params[p].type + ";";
            bool dup = false;
            for (size_t k = 0; k < out.size(); ++k) {
                std::string k2;
                for (size_t p = 0; p < out[k]->params.size(); ++p) k2 += out[k]->params[p].type + ";";
                if (k2 == key && out[k]->fullName() == full) {
                    dup = true;
                    bool outHasDefault = false, gHasDefault = false;
                    for (size_t p = 0; p < out[k]->params.size(); ++p) if (!out[k]->params[p].defaultValue.empty()) outHasDefault = true;
                    for (size_t p = 0; p < g.params.size(); ++p) if (!g.params[p].defaultValue.empty()) gHasDefault = true;
                    if (gHasDefault && !outHasDefault) out[k] = &g;
                    break;
                }
            }
            if (!dup) out.push_back(&g);
        }
    }
}

bool TypeChecker::findGlobal(const std::string& name, const VariableInfo*& out) const
{
    std::string qualifier, simple = name;
    size_t pos = name.rfind("::");
    if (pos != std::string::npos) { qualifier = name.substr(0, pos); simple = name.substr(pos + 2); }
    const std::vector<SourceFile*>& files = m_cb.files();
    const VariableInfo* best = 0;
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v) {
            const VariableInfo& g = files[f]->variables[v];
            if (g.name != simple || !g.qualifier.empty()) continue;
            std::string full = g.fullName();
            bool ok = false;
            if (!qualifier.empty()) ok = full == qualifier + "::" + simple || endsWith(full, "::" + qualifier + "::" + simple);
            else {
                std::string s = m_scopeName;
                for (;;) {
                    if (full == (s.empty() ? simple : s + "::" + simple)) { ok = true; break; }
                    if (s.empty()) break;
                    size_t p = s.rfind("::");
                    s = p == std::string::npos ? "" : s.substr(0, p);
                }
                for (size_t u = 0; u < m_usingNamespaces.size() && !ok; ++u) if (full == m_usingNamespaces[u] + "::" + simple) ok = true;
                if (!ok && g.scope.find("(anonymous)") != std::string::npos && m_fn && samePath(g.file, m_fn->file)) ok = true;
            }
            if (!ok) continue;
            if (!best || (best->isExtern && !g.isExtern)) best = &g;
        }
    out = best;
    return best != 0;
}

// ---- conversions ---------------------------------------------------------------------

const Type* TypeChecker::promote(const Type* t)
{
    t = m_types.decay(t);
    if (t->kind == Type::Bool || t->kind == Type::Char || t->kind == Type::Short || t->kind == Type::Enum) return m_types.intType();
    return t;
}

const Type* TypeChecker::arithResult(const Type* a, const Type* b)
{
    a = promote(a); b = promote(b);
    if (a->isUnknown()) return a;
    if (b->isUnknown()) return b;
    if (a->kind == Type::Double || b->kind == Type::Double) return m_types.doubleType();
    if (a->kind == Type::Float || b->kind == Type::Float) return m_types.builtin(Type::Float);
    const Type* r = a->rank() >= b->rank() ? a : b;
    bool uns = (a->rank() == b->rank()) ? (a->isUnsigned || b->isUnsigned) : r->isUnsigned;
    return m_types.builtin(r->kind, uns);
}

bool TypeChecker::isScalarish(const Type* t) const
{
    t = m_types.unref(t);
    if (!t) return false;
    if (t->kind == Type::Array) return true;
    return t->isScalar();
}

// 0 exact, 1 promotion/qualification, 2 standard conversion, 3 user-defined, -1 impossible
int TypeChecker::convRank(const Type* from, bool fromLvalue, const Type* to, std::string& why)
{
    if (!from || !to) return 1;
    if (from == to) return 0;
    if (from->isUnknown() || to->isUnknown()) return 1;

    // reference binding
    if (to->kind == Type::Reference) {
        const Type* target = to->inner;
        const Type* src = m_types.unref(from);
        if (!target->isConst) {
            if (!fromLvalue && !src->isUnknown()) { why = "a non-const reference cannot bind to a temporary"; return -1; }
            if (src->isConst && !target->isConst && !src->isUnknown()) { why = "discards const"; return -1; }
        }
        const Type* s = m_types.withConst(src, false);
        const Type* t = m_types.withConst(target, false);
        if (s == t) return src->isConst == target->isConst ? 0 : 1;
        if (s->kind == Type::Class && t->kind == Type::Class && derivesFrom(s->cls, t->cls)) return 2;
        if (s->kind == Type::Array && t->kind == Type::Array && s->inner == t->inner) return 0;
        if (target->isConst) {
            std::string w2;
            int r = convRank(src, true, m_types.withConst(target, false), w2);
            if (r >= 0) return r < 2 ? 2 : r;
            why = w2;
            return -1;
        }
        why = "cannot bind " + from->str() + " to " + to->str();
        return -1;
    }

    const Type* f = m_types.decay(from);
    const Type* t = to;
    if (f == t) return 0;
    const Type* fc = m_types.withConst(f, false);
    const Type* tc = m_types.withConst(t, false);
    if (fc == tc) return 1;

    if (f->isArithmetic() && t->isArithmetic()) {
        if (f->kind == Type::Enum && t->kind == Type::Enum) { why = "different enums"; return -1; }
        if (t->kind == Type::Bool) return 2;
        if (f->rank() < Type().rank() ) {}
        if (promote(f) == tc) return 1;
        return 2;
    }
    if (t->kind == Type::Bool && (f->isPointer() || f->kind == Type::Function)) return 2;
    if (t->isPointer() && f->kind == Type::Null) return 2;
    if (t->kind == Type::Pointer && f->kind == Type::Pointer) {
        const Type* fi = f->inner, *ti = t->inner;
        if (fi->isConst && !ti->isConst && !ti->isUnknown()) { why = "discards const"; return -1; }
        const Type* fib = m_types.withConst(fi, false), *tib = m_types.withConst(ti, false);
        if (fib == tib) return 1;
        if (tib->kind == Type::Void) return 2;
        if (fib->isUnknown() || tib->isUnknown()) return 1;
        if (fib->kind == Type::Class && tib->kind == Type::Class && derivesFrom(fib->cls, tib->cls)) return 2;
        why = "unrelated pointer types";
        return -1;
    }
    if (t->kind == Type::Pointer && f->kind == Type::Function) return 2;
    if (t->kind == Type::Function && f->kind == Type::Pointer) return 1;
    if (t->kind == Type::Function && f->kind == Type::Function) return 1;   // parameter lists are not compared
    if (t->kind == Type::Function && f->kind == Type::Null) return 2;
    if (t->kind == Type::Class && f->kind == Type::Class) {
        if (derivesFrom(f->cls, t->cls)) return 2;   // slicing copy
        // converting constructor of the target taking the source class
        if (t->cls && t->cls->isDefinition)
            for (size_t m = 0; m < t->cls->methods.size(); ++m) {
                const FunctionInfo& c = t->cls->methods[m];
                if (!c.isConstructor || c.isExplicit || c.params.size() < 1) continue;
                size_t required = 0;
                for (size_t p = 0; p < c.params.size(); ++p) if (c.params[p].defaultValue.empty()) ++required;
                if (required > 1) continue;
                const Type* pt = m_types.unref(m_types.parse(c.params[0].type, t->cls->qualifiedName, t->cls));
                if (m_types.withConst(pt, false) == fc) return 3;
            }
        why = "no conversion between " + f->str() + " and " + t->str();
        return -1;
    }
    if (t->kind == Type::Class && t->cls && t->cls->isDefinition) {
        // converting constructor from a scalar
        for (size_t m = 0; m < t->cls->methods.size(); ++m) {
            const FunctionInfo& c = t->cls->methods[m];
            if (!c.isConstructor || c.isExplicit || c.params.empty()) continue;
            size_t required = 0;
            for (size_t p = 0; p < c.params.size(); ++p) if (c.params[p].defaultValue.empty()) ++required;
            if (required > 1) continue;
            const Type* pt = m_types.unref(m_types.parse(c.params[0].type, t->cls->qualifiedName, t->cls));
            if (m_types.withConst(pt, false) == tc) continue;   // the copy constructor converts nothing (and would recurse)
            std::string w2;
            if (pt->isUnknown() || convRank(f, fromLvalue, m_types.withConst(pt, false), w2) >= 0) return 3;
        }
        why = "no constructor of " + t->str() + " accepts " + f->str();
        return -1;
    }
    if (f->kind == Type::Class && f->cls && f->cls->isDefinition) {
        // conversion operator
        for (size_t m = 0; m < f->cls->methods.size(); ++m) {
            const FunctionInfo& c = f->cls->methods[m];
            if (!c.isConversion) continue;
            std::string target = c.name.substr(9);   // after "operator "
            const Type* ct = m_types.parse(target, f->cls->qualifiedName, f->cls);
            std::string w2;
            if (ct->isUnknown() || convRank(ct, false, t, w2) >= 0) return 3;
        }
        why = f->str() + " has no conversion to " + t->str();
        return -1;
    }
    why = "cannot convert " + from->str() + " to " + to->str();
    return -1;
}

int TypeChecker::rankExpr(const Expr* e, const Type* to, std::string& why)
{
    if (!e || !e->type) return 1;
    int r = convRank(e->type, e->lvalue, to, why);
    // the integer literal 0 is a null pointer constant
    if (r < 0 && e->isConstant && e->cval == 0 && m_types.decay(e->type)->isIntegral() && m_types.unref(to)->isPointer()) { why.clear(); r = 2; }
    return r;
}

bool TypeChecker::checkConvert(Expr* e, const Type* to, const char* code, const std::string& context)
{
    if (!e || !e->type || !to) return true;
    std::string why;
    int r = rankExpr(e, to, why);
    if (r < 0) {
        add(Finding::Error, code, e->line, context + ": cannot convert " + e->type->str() + " to " + to->str(), why);
        return false;
    }
    const Type* f = m_types.decay(e->type);
    const Type* t = m_types.unref(to);
    if (f->isFloating() && t->isIntegral() && !e->isConstant)
        add(Finding::Warning, "T116", e->line, context + ": implicit conversion from " + f->str() + " to " + t->str() + " loses the fraction");
    else if (f->kind == Type::Double && t->kind == Type::Float && !e->isConstant)
        add(Finding::Warning, "T116", e->line, context + ": implicit narrowing from double to float");
    return true;
}

bool TypeChecker::requireLvalue(Expr* e, const std::string& what)
{
    if (!e->type || e->type->isUnknown() || e->lvalue) return true;
    add(Finding::Error, "T121", e->line, what + " requires an lvalue (a variable, member or dereferenced pointer)");
    return false;
}

// ---- constant evaluation -------------------------------------------------------------

void TypeChecker::evalConst(Expr* e)
{
    switch (e->kind) {
    case Expr::IntLit: {
        e->isConstant = true;
        e->cval = std::strtol(e->text.c_str(), 0, 0);
        return;
    }
    case Expr::FloatLit: e->isConstant = true; e->fval = std::atof(e->text.c_str()); e->cval = static_cast<long>(e->fval); return;
    case Expr::CharLit: {
        e->isConstant = true;
        std::string s = e->text;
        size_t q = s.find('\'');
        if (q != std::string::npos && q + 1 < s.size()) {
            char c = s[q + 1];
            if (c == '\\' && q + 2 < s.size()) {
                char x = s[q + 2];
                c = x == 'n' ? '\n' : x == 't' ? '\t' : x == '0' ? '\0' : x == 'r' ? '\r' : x;
            }
            e->cval = static_cast<unsigned char>(c);
        }
        return;
    }
    case Expr::BoolLit: e->isConstant = true; e->cval = e->text == "true" ? 1 : 0; return;
    case Expr::Unary: {
        Expr* k = e->kids[0];
        if (!k->isConstant) return;
        bool fl = k->type && k->type->isFloating();
        if (e->text == "-") { e->isConstant = true; e->cval = -k->cval; e->fval = -k->fval; }
        else if (e->text == "+") { e->isConstant = true; e->cval = k->cval; e->fval = k->fval; }
        else if (e->text == "!") { e->isConstant = true; e->cval = fl ? (k->fval == 0.0) : (k->cval == 0); }
        else if (e->text == "~" && !fl) { e->isConstant = true; e->cval = ~k->cval; }
        return;
    }
    case Expr::Binary: {
        Expr* a = e->kids[0]; Expr* b = e->kids[1];
        if (!a->isConstant || !b->isConstant) return;
        bool fl = (e->type && e->type->isFloating()) || (a->type && a->type->isFloating()) || (b->type && b->type->isFloating());
        const std::string& op = e->text;
        if (fl) {
            double x = a->type && a->type->isFloating() ? a->fval : static_cast<double>(a->cval);
            double y = b->type && b->type->isFloating() ? b->fval : static_cast<double>(b->cval);
            double r = 0; bool ok = true; long c = 0; bool cmp = false;
            if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y;
            else if (op == "/") { if (y == 0.0) ok = false; else r = x / y; }
            else if (op == "<") { cmp = true; c = x < y; } else if (op == ">") { cmp = true; c = x > y; }
            else if (op == "<=") { cmp = true; c = x <= y; } else if (op == ">=") { cmp = true; c = x >= y; }
            else if (op == "==") { cmp = true; c = x == y; } else if (op == "!=") { cmp = true; c = x != y; }
            else ok = false;
            if (ok) { e->isConstant = true; if (cmp) { e->cval = c; e->fval = static_cast<double>(c); } else { e->fval = r; e->cval = static_cast<long>(r); } }
            return;
        }
        long x = a->cval, y = b->cval, r = 0; bool ok = true;
        if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y;
        else if (op == "/") { if (y == 0) ok = false; else r = x / y; }
        else if (op == "%") { if (y == 0) ok = false; else r = x % y; }
        else if (op == "<<") r = (y >= 0 && y < 32) ? (x << y) : 0;
        else if (op == ">>") r = (y >= 0 && y < 32) ? (x >> y) : 0;
        else if (op == "&") r = x & y; else if (op == "|") r = x | y; else if (op == "^") r = x ^ y;
        else if (op == "<") r = x < y; else if (op == ">") r = x > y; else if (op == "<=") r = x <= y; else if (op == ">=") r = x >= y;
        else if (op == "==") r = x == y; else if (op == "!=") r = x != y;
        else if (op == "&&") r = (x != 0) && (y != 0); else if (op == "||") r = (x != 0) || (y != 0);
        else ok = false;
        if (ok) { e->isConstant = true; e->cval = r; e->fval = static_cast<double>(r); }
        return;
    }
    case Expr::Cond: {
        if (e->kids[0]->isConstant && e->kids[1]->isConstant && e->kids[2]->isConstant) {
            Expr* pick = e->kids[0]->cval ? e->kids[1] : e->kids[2];
            e->isConstant = true; e->cval = pick->cval; e->fval = pick->fval;
        }
        return;
    }
    case Expr::Cast: {
        if (e->kids.empty() || !e->kids[0]->isConstant || !e->type) return;
        Expr* k = e->kids[0];
        e->isConstant = true;
        if (e->type->isFloating()) { e->fval = k->type && k->type->isFloating() ? k->fval : static_cast<double>(k->cval); e->cval = static_cast<long>(e->fval); }
        else if (e->type->isIntegral()) { e->cval = k->type && k->type->isFloating() ? static_cast<long>(k->fval) : k->cval; e->fval = static_cast<double>(e->cval); }
        else e->isConstant = false;
        return;
    }
    default: return;
    }
}

// ---- expressions -------------------------------------------------------------------------

const Type* TypeChecker::expr(Expr* e)
{
    if (!e) return 0;
    const Type* t = 0;
    switch (e->kind) {
    case Expr::IntLit: {
        std::string s = e->text;
        bool uns = false, lng = false;
        while (!s.empty() && (s[s.size() - 1] == 'u' || s[s.size() - 1] == 'U' || s[s.size() - 1] == 'l' || s[s.size() - 1] == 'L')) {
            char c = s[s.size() - 1];
            if (c == 'u' || c == 'U') uns = true; else lng = true;
            s.erase(s.size() - 1);
        }
        t = m_types.builtin(lng ? Type::Long : Type::Int, uns);
        break;
    }
    case Expr::FloatLit: t = m_types.builtin((e->text[e->text.size() - 1] == 'f' || e->text[e->text.size() - 1] == 'F') ? Type::Float : Type::Double); break;
    case Expr::CharLit: t = m_types.builtin(Type::Char); break;
    case Expr::StringLit: t = m_types.charPtr(); break;
    case Expr::BoolLit: t = m_types.boolType(); break;
    case Expr::This: {
        if (!m_owner) { add(Finding::Error, "T100", e->line, "'this' used outside a member function"); t = m_types.unknown("this"); break; }
        t = m_types.pointer(m_types.classType(m_owner, m_fn && m_fn->isConst));
        break;
    }
    case Expr::Name: t = checkName(e); break;
    case Expr::Member: t = checkMember(e); break;
    case Expr::Index: t = checkIndex(e); break;
    case Expr::Call: t = checkCall(e); break;
    case Expr::Unary: t = checkUnary(e); break;
    case Expr::Postfix: {
        const Type* k = expr(e->kids[0]);
        requireLvalue(e->kids[0], e->text);
        const Type* u = m_types.unref(k);
        if (u->isConst && !u->isUnknown()) add(Finding::Error, "T107", e->line, "cannot modify a const value with " + e->text);
        if (!isScalarish(k) && !u->isUnknown()) {
            std::vector<Expr*> ops; ops.push_back(e->kids[0]);
            t = classOperator(e, e->text == "++" ? "operator++" : "operator--", ops);
            if (t) break;
            add(Finding::Error, "T105", e->line, "operator " + e->text + " cannot be applied to " + k->str());
        }
        t = m_types.decay(k);
        break;
    }
    case Expr::Binary: t = checkBinary(e); break;
    case Expr::Assign: t = checkAssign(e); break;
    case Expr::Cond: t = checkCond(e); break;
    case Expr::Cast: t = checkCast(e); break;
    case Expr::SizeOf: {
        if (!e->kids.empty()) expr(e->kids[0]);
        t = m_types.builtin(Type::Long, true);
        const Type* of = e->kids.empty() ? parseType(e->typeText) : m_types.decay(e->kids[0]->type);
        if (of && of->builtinSize() > 0) { e->isConstant = true; e->cval = of->builtinSize(); }
        break;
    }
    case Expr::New: t = checkNew(e); break;
    case Expr::Delete: {
        const Type* k = m_types.decay(expr(e->kids[0]));
        if (k && !k->isPointer() && !k->isUnknown()) add(Finding::Error, "T112", e->line, "delete requires a pointer, got " + k->str());
        t = m_types.voidType();
        break;
    }
    case Expr::Comma: expr(e->kids[0]); t = expr(e->kids[1]); e->lvalue = e->kids[1]->lvalue; break;
    case Expr::Throw: if (!e->kids.empty()) expr(e->kids[0]); t = m_types.voidType(); break;
    default: t = m_types.unknown("error"); break;
    }
    e->type = t;
    evalConst(e);
    return t;
}

const Type* TypeChecker::checkName(Expr* e)
{
    const std::string& name = e->text;
    bool qualified = name.find("::") != std::string::npos;

    if (!qualified) {
        int li = lookupLocal(name);
        if (li >= 0) {
            e->sym = Expr::SymLocal; e->index = li; e->lvalue = true;
            const Type* t = m_ast->locals[static_cast<size_t>(li)].type;
            if (!t) t = m_types.unknown(name);
            return m_types.unref(t);
        }
        if (m_fn) for (size_t p = 0; p < m_fn->params.size(); ++p)
            if (m_fn->params[p].name == name) {
                e->sym = Expr::SymParam; e->index = static_cast<int>(p); e->lvalue = true;
                return m_types.unref(m_ast->paramTypes[p]);
            }
        if (m_owner) {
            // the injected class name: "Vector(a, b)" inside Vector<double, 3> is a construction
            for (const ClassInfo* c = m_owner; c; c = c->owner)
                if (name == c->name || name == stripTemplateArgs(c->name)) { e->sym = Expr::SymType; e->cls = c; return m_types.classType(c); }
            const ClassInfo* owner = 0; int idx = -1;
            if (findField(m_owner, name, owner, idx)) {
                const VariableInfo& v = owner->fields[static_cast<size_t>(idx)];
                e->sym = Expr::SymField; e->index = idx; e->cls = owner; e->var = &v; e->lvalue = true;
                accessible(owner, v.access, name, e->line);
                const Type* declared = fieldType(v, owner);
                const Type* t = m_types.unref(declared);
                if (m_fn && m_fn->isConst && !v.isStatic && !v.isMutable && declared->kind != Type::Reference) t = m_types.withConst(t, true);
                return t;
            }
            std::vector<const FunctionInfo*> methods;
            findMethods(m_owner, name, methods);
            if (!methods.empty()) { e->sym = Expr::SymMethod; e->cls = methods[0]->ownerClass; return m_types.unknown("method " + name); }
        }
        std::string enumName; long value;
        if (m_types.findEnumerator(name, m_owner, enumName, value)) {
            e->sym = Expr::SymEnumerator; e->isConstant = true; e->cval = value;
            return m_types.enumType(enumName);
        }
    }

    // qualified or not found locally: globals, functions, static members, enumerators, types
    std::string qualifier, simple = name;
    size_t pos = name.rfind("::");
    if (pos != std::string::npos) { qualifier = name.substr(0, pos); simple = name.substr(pos + 2); }
    if (qualifier.compare(0, 2, "::") == 0) qualifier = qualifier.substr(2);

    if (!qualifier.empty()) {
        const ClassInfo* cls = m_cb.findClass(m_scopeName, qualifier);
        if (cls) {
            const ClassInfo* owner = 0; int idx = -1;
            if (findField(cls, simple, owner, idx)) {
                const VariableInfo& v = owner->fields[static_cast<size_t>(idx)];
                e->sym = Expr::SymField; e->index = idx; e->cls = owner; e->var = &v; e->lvalue = true;
                accessible(owner, v.access, simple, e->line);
                return m_types.unref(fieldType(v, owner));
            }
            std::vector<const FunctionInfo*> methods;
            findMethods(cls, simple, methods);
            if (!methods.empty()) { e->sym = Expr::SymMethod; e->cls = methods[0]->ownerClass; return m_types.unknown("method " + name); }
            std::string enumName; long value;
            if (m_types.findEnumerator(cls->qualifiedName + "::" + simple, 0, enumName, value) || m_types.findEnumerator(simple, cls, enumName, value)) {
                e->sym = Expr::SymEnumerator; e->isConstant = true; e->cval = value; return m_types.enumType(enumName);
            }
            if (m_types.isTypeName(simple, cls->qualifiedName, cls) || m_cb.findClass(cls->qualifiedName, simple)) { e->sym = Expr::SymType; return parseType(name); }
            add(Finding::Error, "T101", e->line, cls->qualifiedName + " has no member named " + simple);
            return m_types.unknown(name);
        }
    }

    const VariableInfo* g = 0;
    if (findGlobal(name, g)) {
        e->sym = Expr::SymGlobal; e->var = g; e->lvalue = true;
        return m_types.unref(m_types.parse(g->type, g->scope, 0));
    }
    std::vector<const FunctionInfo*> fns;
    collectFreeFunctions(name, fns);
    if (!fns.empty()) { e->sym = Expr::SymFunction; e->func = fns[0]; return m_types.function(m_types.parse(fns[0]->returnType, fns[0]->scope, 0)); }
    std::string enumName; long value;
    if (m_types.findEnumerator(qualified ? simple : name, 0, enumName, value)) {
        e->sym = Expr::SymEnumerator; e->isConstant = true; e->cval = value; return m_types.enumType(enumName);
    }
    if (m_types.isTypeName(name, m_scopeName, m_owner) || m_cb.findClass(m_scopeName, name)) {
        e->sym = Expr::SymType;
        const Type* t = parseType(name + (e->typeText.empty() ? "" : e->typeText));
        if (t->kind == Type::Class) e->cls = t->cls;
        return t;
    }
    if (name == "NULL") { e->isConstant = true; e->cval = 0; e->sym = Expr::SymEnumerator; return m_types.nullType(); }
    e->sym = Expr::SymUnknown;
    bool quiet = m_macroNames.count(name) || isMacroLike(name) || qualified || startsWith(name, "std") || (m_calleeContext && isKnownLibraryName(name));
    if (!quiet && m_calleeContext) {
        // an unknown function name: usually a library function or a macro from a system header
        if (m_undeclaredCalls.insert(name).second)
            add(Finding::Info, "T146", e->line, "call to " + name + "() which is not declared in the parsed files",
                "library function or macro from a header that was not parsed; the call is not type checked");
    } else if (!quiet) {
        add(Finding::Error, "T100", e->line, "'" + name + "' was not declared in this scope",
            "not a local, parameter, member, global, function, enumerator or type known to the tool");
    }
    return m_types.unknown(name);
}

const Type* TypeChecker::checkMember(Expr* e)
{
    const Type* objT = m_types.unref(expr(e->kids[0]));
    const Type* obj = objT;
    if (e->arrow) {
        if (obj->kind == Type::Pointer) obj = obj->inner;
        else if (obj->kind == Type::Class && obj->cls) {
            // smart pointer with operator->
            std::vector<const FunctionInfo*> ops;
            findMethods(obj->cls, "operator->", ops);
            if (!ops.empty()) obj = m_types.unref(m_types.parse(ops[0]->returnType, obj->cls->qualifiedName, obj->cls));
            else { add(Finding::Error, "T117", e->line, "'->' applied to an object of class " + obj->str() + "; use '.'"); return m_types.unknown(e->text); }
            if (obj->kind == Type::Pointer) obj = obj->inner;
        }
        else if (!obj->isUnknown()) { add(Finding::Error, "T117", e->line, "'->' applied to a non-pointer of type " + obj->str()); return m_types.unknown(e->text); }
    } else if (obj->kind == Type::Pointer) {
        add(Finding::Error, "T117", e->line, "'.' applied to a pointer of type " + obj->str() + "; use '->'");
        obj = obj->inner;
    }
    obj = m_types.unref(obj);
    if (obj->isUnknown() || (obj->kind == Type::Class && (!obj->cls || !obj->cls->isDefinition))) { e->sym = Expr::SymUnknown; return m_types.unknown(e->text); }
    if (obj->kind != Type::Class) {
        add(Finding::Error, "T110", e->line, "member access '" + e->text + "' on a value of non-class type " + obj->str());
        return m_types.unknown(e->text);
    }
    const ClassInfo* cls = obj->cls;
    const ClassInfo* owner = 0; int idx = -1;
    if (findField(cls, e->text, owner, idx)) {
        const VariableInfo& v = owner->fields[static_cast<size_t>(idx)];
        e->sym = Expr::SymField; e->index = idx; e->cls = owner; e->var = &v; e->lvalue = true;
        accessible(owner, v.access, e->text, e->line);
        const Type* declared = fieldType(v, owner);
        const Type* t = m_types.unref(declared);
        if (obj->isConst && !v.isMutable && !v.isStatic && declared->kind != Type::Reference) t = m_types.withConst(t, true);
        return t;
    }
    std::vector<const FunctionInfo*> methods;
    findMethods(cls, e->text, methods);
    if (!methods.empty()) { e->sym = Expr::SymMethod; e->cls = methods[0]->ownerClass; return m_types.unknown("method " + e->text); }
    std::string enumName; long value;
    if (m_types.findEnumerator(e->text, cls, enumName, value)) { e->sym = Expr::SymEnumerator; e->isConstant = true; e->cval = value; return m_types.enumType(enumName); }
    bool unresolvedBase = false;
    for (size_t b = 0; b < cls->bases.size(); ++b) if (!cls->bases[b].resolved || !cls->bases[b].resolved->isDefinition) unresolvedBase = true;
    if (unresolvedBase || cls->tmpl.isTemplate) { e->sym = Expr::SymUnknown; return m_types.unknown(e->text); }
    add(Finding::Error, "T101", e->line, cls->qualifiedName + " has no member named " + e->text);
    return m_types.unknown(e->text);
}

const Type* TypeChecker::constructClass(Expr* e, const ClassInfo* cls, size_t firstArg, int line)
{
    std::vector<const FunctionInfo*> ctors;
    for (size_t m = 0; m < cls->methods.size(); ++m) if (cls->methods[m].isConstructor) ctors.push_back(&cls->methods[m]);
    size_t nargs = e->kids.size() - firstArg;
    if (cls->isAbstract) add(Finding::Error, "T102", line, "cannot create an object of abstract class " + cls->qualifiedName);
    if (nargs == 1 && e->kids[firstArg]->type) {
        // implicit copy constructor: same class or derived, unless the class declares a matching constructor itself
        const Type* at = m_types.decay(e->kids[firstArg]->type);
        if (at->kind == Type::Class && at->cls && (at->cls == cls || derivesFrom(at->cls, cls))) {
            bool userCopy = false;
            for (size_t c = 0; c < ctors.size(); ++c) if (ctors[c]->params.size() == 1 && stripTemplateArgs(ctors[c]->params[0].type).find(cls->name) != std::string::npos) userCopy = true;
            if (!userCopy) return m_types.classType(cls);
        }
    }
    if (ctors.empty()) {
        if (nargs > 1) add(Finding::Error, "T102", line, cls->qualifiedName + " has only the implicit constructors (default and copy); " + intToString(static_cast<int>(nargs)) + " arguments given");
        else if (nargs == 1) checkConvert(e->kids[firstArg], m_types.classType(cls), "T104", "copy construction of " + cls->qualifiedName);
        return m_types.classType(cls);
    }
    callFunction(e, ctors, firstArg, "constructor " + cls->qualifiedName + "::" + cls->name);
    return m_types.classType(cls);
}

const Type* TypeChecker::callFunction(Expr* e, const std::vector<const FunctionInfo*>& cands, size_t firstArg, const std::string& what)
{
    std::vector<Expr*> args(e->kids.begin() + static_cast<long>(firstArg), e->kids.end());
    const FunctionInfo* best = 0;
    int bestScore = 1 << 20;
    int bestCount = 0;
    std::vector<std::string> reasons;
    bool anyUnknown = false;
    for (size_t c = 0; c < cands.size(); ++c) {
        const FunctionInfo* f = cands[c];
        const ClassInfo* owner = f->ownerClass;
        std::string scope = owner ? owner->qualifiedName : f->scope;
        size_t required = 0; bool variadic = false;
        for (size_t p = 0; p < f->params.size(); ++p) { if (f->params[p].type == "...") variadic = true; else if (f->params[p].defaultValue.empty()) ++required; }
        if (args.size() < required || (!variadic && args.size() > f->params.size())) {
            reasons.push_back(f->signature() + ": takes " + intToString(static_cast<int>(required)) + (f->params.size() != required ? " to " + intToString(static_cast<int>(f->params.size())) : "") + " arguments");
            continue;
        }
        int score = 0; bool ok = true;
        for (size_t a = 0; a < args.size() && a < f->params.size(); ++a) {
            if (f->params[a].type == "...") break;
            const Type* pt = m_types.parse(f->params[a].type, scope, owner);
            if (pt->isUnknown() || (pt->kind == Type::Reference && pt->inner->isUnknown())) { anyUnknown = true; score += 1; continue; }
            std::string why;
            int r = args[a]->type ? convRank(args[a]->type, args[a]->lvalue, pt, why) : 1;
            if (r < 0 && args[a]->isConstant && args[a]->cval == 0 && args[a]->type && m_types.decay(args[a]->type)->isIntegral() && m_types.unref(pt)->isPointer()) r = 2;
            if (r < 0) { ok = false; reasons.push_back(f->signature() + ": argument " + intToString(static_cast<int>(a + 1)) + " (" + args[a]->type->str() + " -> " + pt->str() + ") " + why); break; }
            score += r;
        }
        if (!ok) continue;
        if (score < bestScore) { bestScore = score; best = f; bestCount = 1; }
        else if (score == bestScore) {
            // same signature declared twice (declaration + definition) is not ambiguous; a const and a
            // non-const overload with the same parameters are chosen by the receiver's constness
            bool sameParams = best && best->paramText == f->paramText && best->fullName() == f->fullName();
            if (sameParams && best->isConst != f->isConst) {
                if (f->isConst == m_receiverConst) best = f;
            } else if (!sameParams) ++bestCount;
        }
    }
    if (!best) {
        if (!anyUnknown || !reasons.empty()) {
            std::string detail;
            for (size_t r = 0; r < reasons.size() && r < 4; ++r) { if (r) detail += "; "; detail += reasons[r]; }
            std::string argTypes;
            for (size_t a = 0; a < args.size(); ++a) { if (a) argTypes += ", "; argTypes += args[a]->type ? args[a]->type->str() : "?"; }
            add(Finding::Error, "T102", e->line, "no matching call for " + what + "(" + argTypes + ")", detail);
        }
        e->func = cands.empty() ? 0 : cands[0];
        return cands.empty() ? m_types.unknown(what) : m_types.parse(cands[0]->returnType, cands[0]->ownerClass ? cands[0]->ownerClass->qualifiedName : cands[0]->scope, cands[0]->ownerClass);
    }
    bool templateCandidates = false;
    for (size_t c = 0; c < cands.size(); ++c) if (cands[c]->tmpl.isTemplate) templateCandidates = true;
    if (bestCount > 1 && !templateCandidates) add(Finding::Error, "T103", e->line, "call to " + what + " is ambiguous between " + intToString(bestCount) + " overloads");
    e->func = best;
    // function template: deduce the parameters from the argument types and instantiate
    if (best->tmpl.isTemplate && !best->ownerClass && m_types.instantiator()) {
        std::vector<std::string> names, defaults;
        Instantiator::parameterNames(best->tmpl, names, defaults);
        Substitution subst;
        for (size_t a = 0; a < args.size() && a < best->params.size(); ++a) {
            // the core of "const T&" / "T*" / "T" is the first word that is not const
            std::string pt = best->params[a].type;
            int ptrDepth = 0;
            for (size_t k = 0; k < pt.size(); ++k) if (pt[k] == '*') ++ptrDepth;
            std::string core, word;
            for (size_t k = 0; k <= pt.size() && core.empty(); ++k) {
                char c = k < pt.size() ? pt[k] : ' ';
                if (isalnum(static_cast<unsigned char>(c)) || c == '_') { word += c; continue; }
                if (!word.empty() && word != "const" && word != "volatile") core = word;
                word.clear();
            }
            bool isParam = false;
            for (size_t n = 0; n < names.size(); ++n) if (names[n] == core) isParam = true;
            if (!isParam || !args[a]->type) continue;
            const Type* at = m_types.decay(args[a]->type);
            for (int d = 0; d < ptrDepth && at && at->kind == Type::Pointer; ++d) at = at->inner;
            if (!at || at->isUnknown()) continue;
            std::string deduced = m_types.withConst(at, false)->str();
            if (deduced.compare(0, 5, "enum ") == 0) deduced = deduced.substr(5);
            if (!subst.count(core)) subst[core] = deduced;
        }
        for (size_t n = 0; n < names.size(); ++n) if (!subst.count(names[n]) && !defaults[n].empty()) subst[names[n]] = defaults[n];
        bool complete = true;
        for (size_t n = 0; n < names.size(); ++n) if (!subst.count(names[n])) complete = false;
        if (complete) {
            FunctionInfo* inst = m_types.instantiator()->instantiateFunction(best, subst, m_types);
            if (inst) { e->func = inst; best = inst; }
        }
    }
    // typed argument conversions (narrowing warnings)
    const ClassInfo* owner = best->ownerClass;
    for (size_t a = 0; a < args.size() && a < best->params.size(); ++a) {
        if (best->params[a].type == "...") break;
        const Type* pt = m_types.parse(best->params[a].type, owner ? owner->qualifiedName : best->scope, owner);
        checkConvert(args[a], pt, "T104", "argument " + intToString(static_cast<int>(a + 1)) + " of " + what);
    }
    if (best->isConstructor || best->isDestructor) return m_types.voidType();
    if (best->isConversion) return m_types.parse(best->name.substr(9), owner ? owner->qualifiedName : best->scope, owner);
    return m_types.parse(best->returnType, owner ? owner->qualifiedName : best->scope, owner);
}

const Type* TypeChecker::checkCall(Expr* e)
{
    Expr* callee = e->kids[0];
    for (size_t a = 1; a < e->kids.size(); ++a) expr(e->kids[a]);

    if (callee->kind == Expr::Member) {
        const Type* mt = expr(callee);
        if (callee->sym == Expr::SymMethod && callee->cls) {
            const Type* objT = m_types.unref(callee->kids[0]->type);
            if (callee->arrow && objT->kind == Type::Pointer) objT = objT->inner;
            objT = m_types.unref(objT);
            const ClassInfo* cls = objT->kind == Type::Class ? objT->cls : callee->cls;
            std::vector<const FunctionInfo*> methods;
            findMethods(cls, callee->text, methods);
            if (methods.empty()) findMethods(callee->cls, callee->text, methods);
            m_receiverConst = objT->isConst;
            const Type* r = callFunction(e, methods, 1, cls->qualifiedName + "::" + callee->text);
            m_receiverConst = false;
            if (e->func) {
                accessible(e->func->ownerClass, e->func->access, callee->text, e->line);
                if (objT->isConst && !e->func->isConst && !e->func->isStatic)
                    add(Finding::Error, "T106", e->line, "non-const method " + e->func->fullName() + "() called on a const object");
                bool viaPointer = callee->arrow || callee->kids[0]->kind != Expr::Name || (callee->kids[0]->type && callee->kids[0]->type->kind == Type::Reference);
                e->isVirtualCall = (e->func->isVirtual || e->func->isPureVirtual) && viaPointer;
                e->cls = e->func->ownerClass;
            }
            e->lvalue = r && r->kind == Type::Reference;
            return m_types.unref(r);
        }
        if (callee->sym == Expr::SymField && mt && mt->kind == Type::Function) return m_types.unref(mt->inner);   // field of function pointer type
        if (mt && mt->kind == Type::Class && mt->cls) {   // functor
            std::vector<Expr*> ops; ops.push_back(callee);
            const Type* r = classOperator(e, "operator()", ops);
            if (r) return r;
        }
        return m_types.unknown("call");
    }

    if (callee->kind == Expr::Name) {
        m_calleeContext = true;
        const Type* ct = expr(callee);
        m_calleeContext = false;
        if (callee->sym == Expr::SymLocal || callee->sym == Expr::SymParam || callee->sym == Expr::SymGlobal || callee->sym == Expr::SymField) {
            const Type* d = m_types.decay(ct);
            if (d->kind == Type::Function) return m_types.unref(d->inner);
            if (d->kind == Type::Pointer && d->inner->kind == Type::Function) return m_types.unref(d->inner->inner);
            if (d->kind == Type::Class && d->cls) { std::vector<Expr*> ops; ops.push_back(callee); const Type* r = classOperator(e, "operator()", ops); if (r) return r; }
            if (!d->isUnknown()) add(Finding::Error, "T102", e->line, "'" + callee->text + "' is a " + d->str() + ", not a function");
            return m_types.unknown("call");
        }
        if (callee->sym == Expr::SymMethod) {
            std::vector<const FunctionInfo*> methods;
            const ClassInfo* cls = callee->cls ? callee->cls : m_owner;
            std::string simple = lastComponent(callee->text);
            findMethods(cls, simple, methods);
            if (methods.empty() && m_owner) findMethods(m_owner, simple, methods);
            m_receiverConst = m_fn && m_fn->isConst && callee->text.find("::") == std::string::npos;
            const Type* r = callFunction(e, methods, 1, cls->qualifiedName + "::" + simple);
            m_receiverConst = false;
            if (e->func) {
                accessible(e->func->ownerClass, e->func->access, simple, e->line);
                if (m_fn && m_fn->isConst && !e->func->isConst && !e->func->isStatic && m_owner && callee->text.find("::") == std::string::npos)
                    add(Finding::Error, "T106", e->line, "non-const method " + e->func->fullName() + "() called from a const method");
                e->isVirtualCall = (e->func->isVirtual || e->func->isPureVirtual) && callee->text.find("::") == std::string::npos;
                e->cls = e->func->ownerClass;
            }
            e->lvalue = r && r->kind == Type::Reference;
            return m_types.unref(r);
        }
        if (callee->sym == Expr::SymFunction) {
            std::vector<const FunctionInfo*> fns;
            collectFreeFunctions(callee->text, fns);
            const Type* r = callFunction(e, fns, 1, callee->text);
            e->lvalue = r && r->kind == Type::Reference;
            return m_types.unref(r);
        }
        if (callee->sym == Expr::SymType) {
            const Type* t = ct;
            if (t->kind == Type::Class && t->cls && t->cls->isDefinition) { e->cls = t->cls; return constructClass(e, t->cls, 1, e->line); }
            if (e->kids.size() == 2) checkConvert(e->kids[1], t, "T104", "functional cast to " + t->str());
            return t;
        }
        // a <cmath> function from a header that was not parsed: its result is a double (returned in xmm0)
        if (callee->kind == Expr::Name && isDoubleMathFunction(callee->text)) return m_types.doubleType();
        return m_types.unknown("call");   // unknown callee: no cascade
    }
    expr(callee);
    return m_types.unknown("call");
}

const Type* TypeChecker::classOperator(Expr* e, const char* opName, const std::vector<Expr*>& operands)
{
    const Type* lt = m_types.unref(operands[0]->type);
    if (lt->kind == Type::Class && lt->cls && lt->cls->isDefinition) {
        std::vector<const FunctionInfo*> methods;
        findMethods(lt->cls, opName, methods);
        if (!methods.empty()) {
            // reuse callFunction with a temporary argument layout: kids = [callee-placeholder, operands...]
            Expr tmp;
            tmp.kind = Expr::Call;
            tmp.line = e->line;
            tmp.kids.push_back(operands[0]);
            for (size_t i = 1; i < operands.size(); ++i) tmp.kids.push_back(operands[i]);
            m_receiverConst = lt->isConst;
            const Type* r = callFunction(&tmp, methods, 1, lt->cls->qualifiedName + "::" + opName);
            m_receiverConst = false;
            e->func = tmp.func;
            e->cls = lt->cls;
            if (tmp.func && lt->isConst && !tmp.func->isConst)
                add(Finding::Error, "T106", e->line, "non-const " + std::string(opName) + " called on a const object");
            e->lvalue = r && r->kind == Type::Reference;
            return m_types.unref(r);
        }
    }
    // free operator
    std::vector<const FunctionInfo*> fns;
    collectFreeFunctions(opName, fns);
    std::vector<const FunctionInfo*> cands;
    for (size_t i = 0; i < fns.size(); ++i) if (fns[i]->params.size() == operands.size()) cands.push_back(fns[i]);
    if (!cands.empty()) {
        Expr tmp;
        tmp.kind = Expr::Call;
        tmp.line = e->line;
        tmp.kids.push_back(0);
        for (size_t i = 0; i < operands.size(); ++i) tmp.kids.push_back(operands[i]);
        const Type* r = callFunction(&tmp, cands, 1, opName);
        e->func = tmp.func;
        e->lvalue = r && r->kind == Type::Reference;
        return m_types.unref(r);
    }
    return 0;
}

const Type* TypeChecker::checkUnary(Expr* e)
{
    const Type* k = expr(e->kids[0]);
    const Type* d = m_types.decay(k);
    const std::string& op = e->text;
    if (d->isUnknown()) return op == "&" ? m_types.pointer(d) : d;
    if (d->kind == Type::Class && op != "&") {
        std::vector<Expr*> ops; ops.push_back(e->kids[0]);
        const Type* r = classOperator(e, (std::string("operator") + op).c_str(), ops);
        if (r) return r;
        add(Finding::Error, "T105", e->line, "operator " + op + " cannot be applied to " + d->str(), "the class defines no operator" + op);
        return m_types.unknown("op");
    }
    if (op == "*") {
        if (d->kind == Type::Pointer) { e->lvalue = true; return d->inner; }
        add(Finding::Error, "T112", e->line, "cannot dereference a value of type " + d->str());
        return m_types.unknown("deref");
    }
    if (op == "&") {
        if (e->kids[0]->sym == Expr::SymMethod || e->kids[0]->sym == Expr::SymFunction) {
            const FunctionInfo* fnp = e->kids[0]->func;
            return m_types.function(fnp ? m_types.parse(fnp->returnType, fnp->ownerClass ? fnp->ownerClass->qualifiedName : fnp->scope, fnp->ownerClass) : m_types.unknown("fn"));
        }
        requireLvalue(e->kids[0], "address-of");
        if (e->kids[0]->sym == Expr::SymLocal && e->kids[0]->index >= 0) m_ast->locals[static_cast<size_t>(e->kids[0]->index)].addressTaken = true;
        return m_types.pointer(m_types.unref(k));
    }
    if (op == "!") {
        if (!isScalarish(k)) add(Finding::Error, "T105", e->line, "operator ! requires a scalar operand, got " + d->str());
        return m_types.boolType();
    }
    if (op == "~") {
        if (!d->isIntegral()) add(Finding::Error, "T105", e->line, "operator ~ requires an integer operand, got " + d->str());
        return promote(d);
    }
    if (op == "-" || op == "+") {
        if (!d->isArithmetic()) add(Finding::Error, "T105", e->line, "unary " + op + " requires an arithmetic operand, got " + d->str());
        return promote(d);
    }
    // ++ --
    requireLvalue(e->kids[0], op);
    const Type* u = m_types.unref(k);
    if (u->isConst) add(Finding::Error, "T107", e->line, "cannot modify a const value with " + op);
    if (!isScalarish(k)) add(Finding::Error, "T105", e->line, "operator " + op + " cannot be applied to " + d->str());
    e->lvalue = true;
    return u;
}

const Type* TypeChecker::checkBinary(Expr* e)
{
    const Type* a = expr(e->kids[0]);
    const Type* b = expr(e->kids[1]);
    const Type* da = m_types.decay(a);
    const Type* db = m_types.decay(b);
    const std::string& op = e->text;
    if (da->isUnknown() || db->isUnknown()) {
        if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=" || op == "&&" || op == "||") return m_types.boolType();
        return da->isUnknown() ? da : db;
    }
    if (op == "&&" || op == "||") {
        if (!isScalarish(a) && !(da->kind == Type::Class)) add(Finding::Error, "T105", e->line, "left operand of " + op + " is not convertible to bool: " + da->str());
        if (!isScalarish(b) && !(db->kind == Type::Class)) add(Finding::Error, "T105", e->line, "right operand of " + op + " is not convertible to bool: " + db->str());
        if (da->kind == Type::Class) { std::string why; if (convRank(da, false, m_types.boolType(), why) < 0) add(Finding::Error, "T109", e->line, da->str() + " is not convertible to bool"); }
        if (db->kind == Type::Class) { std::string why; if (convRank(db, false, m_types.boolType(), why) < 0) add(Finding::Error, "T109", e->line, db->str() + " is not convertible to bool"); }
        return m_types.boolType();
    }
    if (da->kind == Type::Class || db->kind == Type::Class) {
        std::vector<Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
        const Type* r = classOperator(e, (std::string("operator") + op).c_str(), ops);
        if (r) return r;
        // the other operand's class may define it as a free function taking (scalar, class): covered by collectFreeFunctions
        add(Finding::Error, "T105", e->line, "no operator" + op + " for operands " + da->str() + " and " + db->str());
        return m_types.unknown("op");
    }
    bool cmp = op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
    if (cmp) {
        if (da->isArithmetic() && db->isArithmetic()) {
            const Type* pa = promote(da); const Type* pb = promote(db);
            if (pa->isIntegral() && pb->isIntegral() && pa->isUnsigned != pb->isUnsigned && !e->kids[0]->isConstant && !e->kids[1]->isConstant)
                add(Finding::Warning, "T115", e->line, "comparison between signed (" + (pa->isUnsigned ? db : da)->str() + ") and unsigned (" + (pa->isUnsigned ? da : db)->str() + ") values");
            return m_types.boolType();
        }
        if ((da->isPointer() || da->kind == Type::Null) && (db->isPointer() || db->kind == Type::Null)) return m_types.boolType();
        if (da->isPointer() && e->kids[1]->isConstant && e->kids[1]->cval == 0) return m_types.boolType();
        if (db->isPointer() && e->kids[0]->isConstant && e->kids[0]->cval == 0) return m_types.boolType();
        add(Finding::Error, "T105", e->line, "cannot compare " + da->str() + " with " + db->str());
        return m_types.boolType();
    }
    if (op == "+" || op == "-") {
        if (da->isArithmetic() && db->isArithmetic()) return arithResult(da, db);
        if (da->kind == Type::Pointer && db->isIntegral()) return da;
        if (op == "+" && da->isIntegral() && db->kind == Type::Pointer) return db;
        if (op == "-" && da->kind == Type::Pointer && db->kind == Type::Pointer) return m_types.builtin(Type::Long);
        add(Finding::Error, "T105", e->line, "invalid operands for " + op + ": " + da->str() + " and " + db->str());
        return m_types.unknown("op");
    }
    if (op == "*" || op == "/") {
        if (da->isArithmetic() && db->isArithmetic()) {
            if (op == "/" && e->kids[1]->isConstant && !db->isFloating() && e->kids[1]->cval == 0) add(Finding::Warning, "T105", e->line, "division by constant zero");
            return arithResult(da, db);
        }
        add(Finding::Error, "T105", e->line, "invalid operands for " + op + ": " + da->str() + " and " + db->str());
        return m_types.unknown("op");
    }
    // % << >> & | ^
    if (da->isIntegral() && db->isIntegral()) return (op == "<<" || op == ">>") ? promote(da) : arithResult(da, db);
    add(Finding::Error, "T105", e->line, "operator " + op + " requires integer operands, got " + da->str() + " and " + db->str());
    return m_types.unknown("op");
}

const Type* TypeChecker::checkAssign(Expr* e)
{
    const Type* l = expr(e->kids[0]);
    const Type* r = expr(e->kids[1]);
    const Type* lu = m_types.unref(l);
    if (lu->isUnknown()) { e->lvalue = true; return lu; }
    requireLvalue(e->kids[0], "assignment");
    if (lu->isConst) add(Finding::Error, "T107", e->line, "assignment to a const value of type " + lu->str());
    if (lu->kind == Type::Array) add(Finding::Error, "T105", e->line, "arrays cannot be assigned");
    if (lu->kind == Type::Class) {
        std::vector<Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
        if (e->text == "=") {
            const Type* ru = m_types.decay(r);
            std::vector<const FunctionInfo*> ms;
            if (lu->cls) findMethods(lu->cls, "operator=", ms);
            if (!ms.empty()) { classOperator(e, "operator=", ops); e->lvalue = true; return lu; }
            if (ru->isUnknown() || (ru->kind == Type::Class && (ru->cls == lu->cls || derivesFrom(ru->cls, lu->cls)))) { e->lvalue = true; return lu; }
            std::string why;
            if (rankExpr(e->kids[1], m_types.withConst(lu, false), why) < 0)
                add(Finding::Error, "T104", e->line, "cannot assign " + ru->str() + " to " + lu->str(), why);
            e->lvalue = true;
            return lu;
        }
        std::string opName = "operator" + e->text;
        const Type* res = classOperator(e, opName.c_str(), ops);
        if (!res) add(Finding::Error, "T105", e->line, "no " + opName + " for " + lu->str());
        e->lvalue = true;
        return lu;
    }
    const Type* ru = m_types.decay(r);
    if (e->text == "=") {
        checkConvert(e->kids[1], m_types.withConst(lu, false), "T104", "assignment");
    } else if (ru->isUnknown()) {
    } else if (e->text == "+=" || e->text == "-=") {
        if (!((lu->isArithmetic() && ru->isArithmetic()) || (lu->kind == Type::Pointer && ru->isIntegral())))
            add(Finding::Error, "T105", e->line, "invalid operands for " + e->text + ": " + lu->str() + " and " + ru->str());
    } else if (e->text == "*=" || e->text == "/=") {
        if (!(lu->isArithmetic() && ru->isArithmetic())) add(Finding::Error, "T105", e->line, "invalid operands for " + e->text + ": " + lu->str() + " and " + ru->str());
    } else {
        if (!(lu->isIntegral() && ru->isIntegral())) add(Finding::Error, "T105", e->line, "operator " + e->text + " requires integer operands, got " + lu->str() + " and " + ru->str());
    }
    e->lvalue = true;
    return lu;
}

const Type* TypeChecker::checkIndex(Expr* e)
{
    const Type* base = expr(e->kids[0]);
    const Type* idx = expr(e->kids[1]);
    const Type* d = m_types.decay(base);
    if (d->isUnknown()) { e->lvalue = true; return d; }
    if (d->kind == Type::Class) {
        std::vector<Expr*> ops; ops.push_back(e->kids[0]); ops.push_back(e->kids[1]);
        const Type* r = classOperator(e, "operator[]", ops);
        if (r) return r;
        add(Finding::Error, "T111", e->line, "subscript on " + d->str() + " which has no operator[]");
        return m_types.unknown("index");
    }
    if (d->kind != Type::Pointer) {
        add(Finding::Error, "T111", e->line, "subscript applied to a non-pointer value of type " + d->str());
        return m_types.unknown("index");
    }
    const Type* di = m_types.decay(idx);
    if (!di->isIntegral() && !di->isUnknown()) add(Finding::Error, "T111", e->line, "array index must be an integer, got " + di->str());
    e->lvalue = true;
    return d->inner;
}

const Type* TypeChecker::checkCond(Expr* e)
{
    const Type* c = expr(e->kids[0]);
    const Type* a = expr(e->kids[1]);
    const Type* b = expr(e->kids[2]);
    if (!isScalarish(c) && !(m_types.decay(c)->kind == Type::Class)) add(Finding::Error, "T109", e->line, "condition of ?: is not convertible to bool: " + c->str());
    const Type* da = m_types.decay(a); const Type* db = m_types.decay(b);
    if (da == db) { e->lvalue = e->kids[1]->lvalue && e->kids[2]->lvalue; return da; }
    if (da->isUnknown()) return da;
    if (db->isUnknown()) return db;
    if (da->isArithmetic() && db->isArithmetic()) return arithResult(da, db);
    if (da->isPointer() && (db->kind == Type::Null || (e->kids[2]->isConstant && e->kids[2]->cval == 0))) return da;
    if (db->isPointer() && (da->kind == Type::Null || (e->kids[1]->isConstant && e->kids[1]->cval == 0))) return db;
    if (m_types.withConst(da, false) == m_types.withConst(db, false)) return m_types.withConst(da, true);
    if (da->kind == Type::Pointer && db->kind == Type::Pointer) {
        // composite pointer type: same pointee apart from const -> pointer to const
        const Type* ia = m_types.withConst(da->inner, false);
        const Type* ib = m_types.withConst(db->inner, false);
        if (ia == ib) return m_types.pointer(m_types.withConst(ia, true));
        if (ia->kind == Type::Class && ib->kind == Type::Class) { if (derivesFrom(ia->cls, ib->cls)) return db; if (derivesFrom(ib->cls, ia->cls)) return da; }
        if (ia->kind == Type::Void || ib->kind == Type::Void) return m_types.pointer(m_types.voidType());
    }
    if (da->kind == Type::Class && db->kind == Type::Class) { if (derivesFrom(da->cls, db->cls)) return db; if (derivesFrom(db->cls, da->cls)) return da; }
    add(Finding::Error, "T104", e->line, "operands of ?: have incompatible types " + da->str() + " and " + db->str());
    return da;
}

const Type* TypeChecker::checkCast(Expr* e)
{
    const Type* target = parseType(e->typeText);
    if (!e->kids.empty()) {
        const Type* from = expr(e->kids[0]);
        if (e->text == "static_cast" || e->text == "(functional)") {
            std::string why;
            const Type* df = m_types.decay(from);
            if (!df->isUnknown() && !target->isUnknown() && convRank(from, e->kids[0]->lvalue, target, why) < 0) {
                // static_cast also allows the reverse of an implicit conversion (downcasts, int<->enum, void*->T*)
                bool reverse = false;
                std::string w2;
                if (convRank(m_types.unref(target), true, df, w2) >= 0) reverse = true;
                if (target->kind == Type::Enum && df->isIntegral()) reverse = true;
                if (!reverse) add(Finding::Error, "T104", e->line, e->text + " cannot convert " + from->str() + " to " + target->str(), why);
            }
        }
    }
    if (target->kind == Type::Reference) { e->lvalue = true; return target->inner; }
    return target;
}

const Type* TypeChecker::checkNew(Expr* e)
{
    const Type* t = parseType(e->typeText);
    size_t first = 0;
    if (e->newArray) {
        const Type* n = expr(e->kids[0]);
        if (!m_types.decay(n)->isIntegral() && !m_types.decay(n)->isUnknown()) add(Finding::Error, "T104", e->line, "array size in new must be an integer, got " + n->str());
        return m_types.pointer(t);
    }
    for (size_t a = first; a < e->kids.size(); ++a) expr(e->kids[a]);
    if (t->kind == Type::Class && t->cls && t->cls->isDefinition) { e->cls = t->cls; constructClass(e, t->cls, 0, e->line); }
    else if (e->kids.size() == 1 && !t->isUnknown()) checkConvert(e->kids[0], t, "T104", "new " + t->str());
    return m_types.pointer(t);
}

// ---- statements ----------------------------------------------------------------------------

void TypeChecker::block(Stmt* s, bool newScope)
{
    if (newScope) push();
    for (size_t i = 0; i < s->kids.size(); ++i) stmt(s->kids[i]);
    if (newScope) pop();
}

void TypeChecker::stmt(Stmt* s)
{
    if (!s) return;
    switch (s->kind) {
    case Stmt::Block: block(s, s->text != "declgroup"); return;
    case Stmt::Empty: return;
    case Stmt::Unsupported:
        add(Finding::Info, "T190", s->line, "statement outside the version 3 subset was skipped: " + s->text);
        return;
    case Stmt::Decl: {
        const Type* t = parseType(s->typeText);
        AstLocal& l = m_ast->locals[static_cast<size_t>(s->localIndex)];
        l.type = t;
        if (t->isUnknown() && s->typeText.find("::") == std::string::npos && s->typeText.find('<') == std::string::npos && !isMacroLike(s->typeText) &&
            !startsWith(s->typeText, "struct ") && !startsWith(s->typeText, "class ") && !startsWith(s->typeText, "union ") && !startsWith(s->typeText, "enum ") &&
            !m_types.isTypeName(stripTemplateArgs(s->typeText.substr(0, s->typeText.find_first_of("*&["))), m_scopeName, m_owner))
            add(Finding::Error, "T114", s->line, "unknown type '" + s->typeText + "' in the declaration of " + s->name);
        if (s->expr) {
            expr(s->expr);
            if (t->kind == Type::Class && t->cls && t->cls->isDefinition) {
                // copy-initialization: same class, derived, or converting constructor
                std::string why;
                if (rankExpr(s->expr, m_types.withConst(t, false), why) < 0)
                    add(Finding::Error, "T104", s->line, "cannot initialise " + s->name + " of type " + t->str() + " from " + s->expr->type->str(), why);
            } else if (!t->isUnknown() && t->kind != Type::Array) {
                checkConvert(s->expr, t, "T104", "initialisation of " + s->name);
            }
        } else if (s->ctorStyle) {
            for (size_t a = 0; a < s->args.size(); ++a) expr(s->args[a]);
            const Type* u = m_types.unref(t);
            if (u->kind == Type::Class && u->cls && u->cls->isDefinition) {
                Expr tmp; tmp.kind = Expr::Call; tmp.line = s->line; tmp.kids.push_back(0);
                for (size_t a = 0; a < s->args.size(); ++a) tmp.kids.push_back(s->args[a]);
                constructClass(&tmp, u->cls, 1, s->line);
            } else if (s->args.size() == 1 && !u->isUnknown()) {
                checkConvert(s->args[0], t, "T104", "initialisation of " + s->name);
            } else if (s->args.size() > 1 && !u->isUnknown()) {
                add(Finding::Error, "T102", s->line, "too many initialisers for " + s->name + " of type " + t->str());
            }
        } else if (t->kind == Type::Class && t->cls && t->cls->isDefinition && t->cls->isAbstract) {
            add(Finding::Error, "T102", s->line, "cannot create " + s->name + ": " + t->cls->qualifiedName + " is abstract");
        } else if (t->kind == Type::Class && t->cls && t->cls->isDefinition && !t->cls->tmpl.isTemplate) {
            bool hasCtor = false, hasDefault = false;
            for (size_t m = 0; m < t->cls->methods.size(); ++m) {
                const FunctionInfo& c = t->cls->methods[m];
                if (!c.isConstructor) continue;
                hasCtor = true;
                size_t required = 0;
                for (size_t p = 0; p < c.params.size(); ++p) if (c.params[p].defaultValue.empty()) ++required;
                if (required == 0) hasDefault = true;
            }
            if (hasCtor && !hasDefault) add(Finding::Error, "T102", s->line, t->cls->qualifiedName + " has no default constructor for " + s->name);
        } else if (t->kind == Type::Reference) {
            add(Finding::Error, "T104", s->line, "reference " + s->name + " must be initialised");
        }
        declare(s->name, s->localIndex);
        return;
    }
    case Stmt::ExprStmt: {
        if (!s->expr) return;
        expr(s->expr);
        Expr::Kind k = s->expr->kind;
        bool noEffect = k == Expr::Binary || k == Expr::Name || k == Expr::IntLit || k == Expr::FloatLit || k == Expr::Member ||
                        k == Expr::Index || k == Expr::Cond || k == Expr::This || k == Expr::BoolLit || k == Expr::StringLit ||
                        (k == Expr::Unary && s->expr->text != "++" && s->expr->text != "--");
        if (noEffect && s->expr->type && !s->expr->type->isUnknown() && !(s->expr->func))
            add(Finding::Warning, "T118", s->line, "expression result unused" + std::string(k == Expr::Binary && s->expr->text == "==" ? " (did you mean '='?)" : ""));
        return;
    }
    case Stmt::If: case Stmt::While: case Stmt::DoWhile: {
        if (s->cond) {
            const Type* c = expr(s->cond);
            if (!isScalarish(c)) {
                std::string why;
                if (!(m_types.decay(c)->kind == Type::Class && convRank(c, s->cond->lvalue, m_types.boolType(), why) >= 0))
                    add(Finding::Error, "T109", s->line, "condition has type " + c->str() + " which is not convertible to bool");
            }
        }
        if (s->kind != Stmt::If) ++m_loopDepth;
        push();
        for (size_t i = 0; i < s->kids.size(); ++i) { push(); stmt(s->kids[i]); pop(); }
        pop();
        if (s->kind != Stmt::If) --m_loopDepth;
        return;
    }
    case Stmt::For: {
        push();
        if (s->init) stmt(s->init);
        if (s->cond) { const Type* c = expr(s->cond); if (!isScalarish(c) && m_types.decay(c)->kind != Type::Class) add(Finding::Error, "T109", s->line, "for condition has type " + c->str() + " which is not convertible to bool"); }
        if (s->step) expr(s->step);
        ++m_loopDepth;
        push(); stmt(s->kids[0]); pop();
        --m_loopDepth;
        pop();
        return;
    }
    case Stmt::Switch: {
        const Type* c = expr(s->cond);
        if (!m_types.decay(c)->isIntegral() && !m_types.decay(c)->isUnknown()) add(Finding::Error, "T123", s->line, "switch condition must be an integer or enum, got " + c->str());
        ++m_switchDepth;
        push();
        std::set<long> seen;
        Stmt* body = s->kids[0];
        if (body->kind == Stmt::Block) {
            for (size_t i = 0; i < body->kids.size(); ++i) {
                Stmt* k = body->kids[i];
                if (k->kind == Stmt::Case) {
                    expr(k->expr);
                    if (!k->expr->isConstant) add(Finding::Error, "T124", k->line, "case value is not a constant expression");
                    else if (!seen.insert(k->expr->cval).second) add(Finding::Error, "T125", k->line, "duplicate case value " + intToString(static_cast<int>(k->expr->cval)));
                } else stmt(k);
            }
        } else stmt(body);
        pop();
        --m_switchDepth;
        return;
    }
    case Stmt::Case: if (s->expr) expr(s->expr); return;
    case Stmt::Default: return;
    case Stmt::Break: if (m_loopDepth == 0 && m_switchDepth == 0) add(Finding::Error, "T122", s->line, "break outside a loop or switch"); return;
    case Stmt::Continue: if (m_loopDepth == 0) add(Finding::Error, "T122", s->line, "continue outside a loop"); return;
    case Stmt::Return: {
        const Type* rt = m_ast->returnType;
        if (s->expr) {
            const Type* t = expr(s->expr);
            if (rt && rt->isVoid()) { if (!t->isVoid()) add(Finding::Error, "T108", s->line, "void function returns a value of type " + t->str()); }
            else if (rt) {
                std::string why;
                if (!t->isUnknown() && rankExpr(s->expr, rt, why) < 0)
                    add(Finding::Error, "T108", s->line, "cannot return " + t->str() + " from a function returning " + rt->str(), why);
                else if (!t->isUnknown()) checkConvert(s->expr, rt, "T108", "return");
            }
        } else if (rt && !rt->isVoid() && !(m_fn && (m_fn->isConstructor || m_fn->isDestructor))) {
            add(Finding::Error, "T108", s->line, "return without a value in a function returning " + rt->str());
        }
        return;
    }
    case Stmt::Try: {
        push(); stmt(s->kids[0]); pop();
        for (size_t i = 1; i < s->kids.size(); ++i) stmt(s->kids[i]);
        return;
    }
    case Stmt::Catch: {
        push();
        if (s->localIndex >= 0) {
            AstLocal& l = m_ast->locals[static_cast<size_t>(s->localIndex)];
            l.type = m_types.unref(parseType(s->typeText));
            declare(s->name, s->localIndex);
        }
        stmt(s->kids[0]);
        pop();
        return;
    }
    }
}

bool TypeChecker::check(FunctionAst& ast)
{
    m_ast = &ast;
    m_fn = ast.fn;
    m_owner = m_fn->ownerClass;
    m_scopeName = m_owner ? m_owner->qualifiedName : m_fn->scope;
    m_scopes.clear();
    m_loopDepth = m_switchDepth = 0;
    m_errors = 0;
    m_calleeContext = false;
    m_undeclaredCalls.clear();
    m_usingNamespaces.clear();
    for (size_t t = 0; t < ast.localTypedefs.size(); ++t) m_types.addTypedef(ast.localTypedefs[t].first, ast.localTypedefs[t].second);
    for (size_t t = 0; t < ast.localEnumerators.size(); ++t) m_types.addEnumerator(ast.localEnumerators[t].first, "(local)", ast.localEnumerators[t].second);
    const SourceFile* sf = m_cb.fileByPath(m_fn->file);
    if (sf) for (size_t u = 0; u < sf->usings.size(); ++u)
        if (startsWith(sf->usings[u].text, "using namespace ")) m_usingNamespaces.push_back(trim(sf->usings[u].text.substr(16)));
    for (size_t f = 0; f < m_cb.files().size(); ++f) {
        const SourceFile* h = m_cb.files()[f];
        if (h == sf || !h->isHeader) continue;
        for (size_t u = 0; u < h->usings.size(); ++u)
            if (startsWith(h->usings[u].text, "using namespace ")) addUnique(m_usingNamespaces, trim(h->usings[u].text.substr(16)));
    }

    ast.paramTypes.clear();
    for (size_t p = 0; p < m_fn->params.size(); ++p) ast.paramTypes.push_back(parseType(m_fn->params[p].type));
    if (m_fn->isConstructor || m_fn->isDestructor) ast.returnType = m_types.voidType();
    else if (m_fn->isConversion) ast.returnType = parseType(m_fn->name.substr(9));
    else ast.returnType = parseType(m_fn->returnType);

    push();
    for (size_t i = 0; i < ast.locals.size(); ++i) if (!ast.locals[i].type) ast.locals[i].type = 0;
    // constructor initializer list: type the arguments against the member or base
    for (size_t i = 0; i < ast.inits.size(); ++i) {
        InitEntry& in = ast.inits[i];
        for (size_t a = 0; a < in.args.size(); ++a) expr(in.args[a]);
        if (!m_owner) continue;
        const ClassInfo* owner = 0; int idx = -1;
        if (findField(m_owner, in.name, owner, idx) && owner == m_owner) {
            const Type* ft = m_types.unref(fieldType(owner->fields[static_cast<size_t>(idx)], owner));
            if (ft->kind == Type::Class && ft->cls && ft->cls->isDefinition) {
                Expr tmp; tmp.kind = Expr::Call; tmp.line = in.line; tmp.kids.push_back(0);
                for (size_t a = 0; a < in.args.size(); ++a) tmp.kids.push_back(in.args[a]);
                constructClass(&tmp, ft->cls, 1, in.line);
            } else if (in.args.size() == 1 && !ft->isUnknown() && ft->kind != Type::Array) {
                checkConvert(in.args[0], ft, "T104", "initialisation of member " + in.name);
            }
        }
    }
    if (ast.body) block(ast.body, false);
    pop();
    ast.checked = (m_errors == 0);
    return ast.checked;
}

} // namespace cppparse
