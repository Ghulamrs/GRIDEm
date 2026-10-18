// Instantiate.cpp - template instantiation by substitution.
#include "Instantiate.hpp"
#include "Parser.hpp"
#include "Parallel.hpp"
#include "PathUtil.hpp"

#include <cctype>
#include <cstdlib>

namespace cppparse {

Instantiator::Instantiator(CodeBase& cb) : m_cb(cb), m_functionCount(0) {}

Instantiator::~Instantiator()
{
    for (size_t i = 0; i < m_ownedClasses.size(); ++i) delete m_ownedClasses[i];
    for (size_t i = 0; i < m_ownedFunctions.size(); ++i) delete m_ownedFunctions[i];
    for (size_t i = 0; i < m_ownedTokens.size(); ++i) delete m_ownedTokens[i];
}

// ---- helpers ------------------------------------------------------------------------

void Instantiator::parameterNames(const TemplateInfo& t, std::vector<std::string>& names, std::vector<std::string>& defaults)
{
    for (size_t i = 0; i < t.params.size(); ++i) {
        std::string p = t.params[i];
        std::string def;
        size_t eq = p.find('=');
        if (eq != std::string::npos) { def = trim(p.substr(eq + 1)); p = trim(p.substr(0, eq)); }
        // the name is the last identifier: "typename T", "class U", "int N", "template <typename> class C"
        std::string name;
        for (size_t k = p.size(); k > 0; --k) {
            char c = p[k - 1];
            if (isalnum(static_cast<unsigned char>(c)) || c == '_') name.insert(name.begin(), c);
            else if (!name.empty()) break;
        }
        names.push_back(name);
        defaults.push_back(def);
    }
}

void Instantiator::splitArgs(const std::string& argsText, std::vector<std::string>& out)
{
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i <= argsText.size(); ++i) {
        char c = i < argsText.size() ? argsText[i] : ',';
        if (c == '<' || c == '(' || c == '[') ++depth;
        else if (c == '>' || c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) { std::string t = trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); continue; }
        cur += c;
    }
}

namespace {

// tiny constant evaluator for integral template arguments: numbers, + - * / %, parentheses
struct ConstEval {
    const std::vector<Token>& t;
    size_t pos;
    bool ok;
    ConstEval(const std::vector<Token>& toks) : t(toks), pos(0), ok(true) {}
    long primary()
    {
        if (pos < t.size() && t[pos].type == TokNumber) return std::strtol(t[pos++].text.c_str(), 0, 0);
        if (pos < t.size() && t[pos].text == "(") { ++pos; long v = expr(); if (pos < t.size() && t[pos].text == ")") ++pos; else ok = false; return v; }
        if (pos < t.size() && t[pos].text == "-") { ++pos; return -primary(); }
        if (pos < t.size() && t[pos].text == "+") { ++pos; return primary(); }
        ok = false;
        return 0;
    }
    long term()
    {
        long v = primary();
        while (ok && pos < t.size() && (t[pos].text == "*" || t[pos].text == "/" || t[pos].text == "%")) {
            std::string op = t[pos++].text;
            long r = primary();
            if (op == "*") v *= r; else if (r == 0) ok = false; else if (op == "/") v /= r; else v %= r;
        }
        return v;
    }
    long expr()
    {
        long v = term();
        while (ok && pos < t.size() && (t[pos].text == "+" || t[pos].text == "-")) {
            std::string op = t[pos++].text;
            long r = term();
            v = op == "+" ? v + r : v - r;
        }
        return v;
    }
};

} // namespace

std::string Instantiator::normalizeArgs(const std::string& argsText)
{
    std::vector<std::string> args;
    splitArgs(argsText, args);
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        Lexer lx(args[i]);
        std::vector<Token> toks;
        lx.run(toks);
        toks.pop_back();   // TokEnd
        if (i) out += ", ";
        // integral constant expressions fold to their value: "3 - 1" -> "2"
        bool numeric = !toks.empty();
        for (size_t k = 0; k < toks.size(); ++k) if (toks[k].type == TokIdentifier || toks[k].type == TokString || toks[k].type == TokChar) numeric = false;
        if (numeric) {
            ConstEval ev(toks);
            long v = ev.expr();
            if (ev.ok && ev.pos == toks.size()) { out += intToString(static_cast<int>(v)); continue; }
        }
        out += joinTokens(toks);
    }
    return out;
}

bool Instantiator::argumentsDependent(const std::string& argsText, TypeTable& types) const
{
    std::vector<std::string> args;
    splitArgs(argsText, args);
    for (size_t i = 0; i < args.size(); ++i) {
        Lexer lx(args[i]);
        std::vector<Token> toks;
        lx.run(toks);
        toks.pop_back();
        if (toks.empty()) return true;
        bool anyIdent = false, anyOperator = false;
        for (size_t k = 0; k < toks.size(); ++k) {
            if (toks[k].type == TokIdentifier) anyIdent = true;
            else if (toks[k].type == TokPunct && toks[k].text != "::" && toks[k].text != "*" && toks[k].text != "&" && toks[k].text != "<" && toks[k].text != ">" && toks[k].text != "," && toks[k].text != "[" && toks[k].text != "]" && toks[k].text != "(" && toks[k].text != ")") anyOperator = true;
        }
        if (!anyIdent) continue;                                   // a number or numeric expression
        // a non-type argument with a name in it ("C - 1") is still a parameter's value even when a class of that
        // name exists elsewhere in the folder; instantiating it would nest without end (Matrix<T, C - 1 - 1, ...>)
        if (anyOperator) return true;
        std::string stripped = stripTemplateArgs(args[i]);
        std::string base = stripped;
        size_t cut = base.find_first_of("*&[");
        if (cut != std::string::npos) base = trim(base.substr(0, cut));
        std::string word;
        std::vector<std::string> words;
        for (size_t k = 0; k <= base.size(); ++k) {
            char c = k < base.size() ? base[k] : ' ';
            if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') { word += c; continue; }
            if (!word.empty()) words.push_back(word);
            word.clear();
        }
        bool typeLike = false;
        for (size_t w = 0; w < words.size(); ++w) {
            if (words[w] == "const" || words[w] == "unsigned" || words[w] == "signed" || words[w] == "struct" || words[w] == "class" || words[w] == "typename") continue;
            if (types.isTypeName(words[w], "", 0) || startsWith(words[w], "std::")) { typeLike = true; break; }
            std::string en; long value;
            if (types.findEnumerator(words[w], 0, en, value)) { typeLike = true; break; }
        }
        if (!typeLike) return true;                                // "T", "N", "K": still a template parameter
    }
    return false;
}

std::string Instantiator::substitute(const std::string& text, const Substitution& subst)
{
    if (subst.empty() || text.empty()) return text;
    Lexer lx(text);
    std::vector<Token> toks;
    lx.run(toks);
    toks.pop_back();
    std::vector<Token> out;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].type == TokIdentifier) {
            Substitution::const_iterator s = subst.find(toks[i].text);
            if (s != subst.end() && !(i > 0 && toks[i - 1].text == "::")) {
                Lexer la(s->second);
                std::vector<Token> at;
                la.run(at);
                at.pop_back();
                for (size_t k = 0; k < at.size(); ++k) { at[k].line = toks[i].line; out.push_back(at[k]); }
                continue;
            }
        }
        out.push_back(toks[i]);
    }
    return joinTokens(out);
}

std::vector<Token>* Instantiator::substituteBody(const FunctionInfo& fn, const Substitution& subst, size_t& bodyBegin, size_t& bodyEnd)
{
    const std::vector<Token>* src = tokensFor(&fn);
    size_t begin = fn.bodyBegin, end = fn.bodyEnd;
    if (!src) {
        const SourceFile* sf = m_cb.fileByPath(fn.file);
        if (!sf || fn.bodyEnd <= fn.bodyBegin || fn.bodyEnd > sf->tokens.size()) return 0;
        src = &sf->tokens;
        // include a constructor initializer list: from the ':' after the parameter list
        if (fn.isConstructor && fn.hasInitializerList) {
            size_t k = begin;
            int depth = 0;
            while (k > 0) {
                --k;
                const Token& t = (*src)[k];
                if (t.type != TokPunct) continue;
                if (t.text == ")") ++depth;
                else if (t.text == "(") { if (depth == 0) break; --depth; }
                else if (t.text == ":" && depth == 0) { begin = k; break; }
            }
        }
    }
    std::vector<Token>* out = new std::vector<Token>;
    bodyBegin = 0;
    bool haveBodyBegin = false;
    for (size_t i = begin; i < end; ++i) {
        const Token& t = (*src)[i];
        if (!haveBodyBegin && i == fn.bodyBegin) { bodyBegin = out->size(); haveBodyBegin = true; }
        if (t.type == TokIdentifier) {
            Substitution::const_iterator s = subst.find(t.text);
            if (s != subst.end() && !(i > begin && (*src)[i - 1].text == "::")) {
                Lexer la(s->second);
                std::vector<Token> at;
                la.run(at);
                at.pop_back();
                for (size_t k = 0; k < at.size(); ++k) { at[k].line = t.line; out->push_back(at[k]); }
                continue;
            }
        }
        out->push_back(t);
    }
    Token endTok; endTok.type = TokEnd; endTok.line = fn.endLine;
    out->push_back(endTok);
    bodyEnd = out->size() - 1;
    m_ownedTokens.push_back(out);
    return out;
}

void Instantiator::substituteFunction(FunctionInfo& fn, const Substitution& subst)
{
    fn.returnType = substitute(fn.returnType, subst);
    fn.paramText = substitute(fn.paramText, subst);
    for (size_t p = 0; p < fn.params.size(); ++p) {
        fn.params[p].type = substitute(fn.params[p].type, subst);
        fn.params[p].defaultValue = substitute(fn.params[p].defaultValue, subst);
    }
    fn.initializerList = substitute(fn.initializerList, subst);
    fn.exceptionSpec = substitute(fn.exceptionSpec, subst);
    fn.tmpl = TemplateInfo();
    fn.typeChecked = false;
    fn.locals.clear();
    fn.calls.clear();
    fn.notes.clear();
    fn.bodyIdents.clear();
}

const std::vector<Token>* Instantiator::tokensFor(const FunctionInfo* fn) const
{
    Lock l(semanticMutex());
    std::map<const FunctionInfo*, std::vector<Token>*>::const_iterator it = m_bodies.find(fn);
    return it == m_bodies.end() ? 0 : it->second;
}

void Instantiator::takePending(std::vector<FunctionInfo*>& out)
{
    Lock l(semanticMutex());
    out = m_pending;
    m_pending.clear();
}

// ---- classes ------------------------------------------------------------------------

ClassInfo* Instantiator::instantiateClass(const ClassInfo* tmpl, const std::string& argsText, TypeTable& types)
{
    Lock l(semanticMutex());
    if (!tmpl || !tmpl->tmpl.isTemplate || !tmpl->isDefinition) return 0;
    if (argumentsDependent(argsText, types)) return 0;          // Vector<T, N> inside another template
    if (m_ownedClasses.size() > 400) return 0;                    // runaway guard
    std::string args = normalizeArgs(argsText);
    if (args.size() > 512) return 0;                               // arguments that keep growing: the same runaway
    std::string key = tmpl->qualifiedName + "<" + args + ">";
    std::map<std::string, ClassInfo*>::iterator it = m_classes.find(key);
    if (it != m_classes.end()) return it->second;

    // a user-written explicit specialization wins
    const std::vector<ClassInfo*>& all = m_cb.classes();
    for (size_t i = 0; i < all.size(); ++i) {
        const ClassInfo* c = all[i];
        if (c == tmpl || !c->isDefinition || c->qualifiedName != tmpl->qualifiedName || c->tmpl.specializationArgs.empty()) continue;
        std::string sa = c->tmpl.specializationArgs;
        if (sa.size() >= 2 && sa[0] == '<') sa = sa.substr(1, sa.size() - 2);
        if (normalizeArgs(sa) == args && c->tmpl.headers.empty()) { m_classes[key] = const_cast<ClassInfo*>(c); return const_cast<ClassInfo*>(c); }
    }

    std::vector<std::string> names, defaults, given;
    parameterNames(tmpl->tmpl, names, defaults);
    splitArgs(args, given);
    if (given.size() > names.size()) return 0;
    Substitution subst;
    for (size_t i = 0; i < names.size(); ++i) {
        std::string value = i < given.size() ? given[i] : defaults[i];
        if (value.empty()) return 0;   // not enough arguments and no default
        subst[names[i]] = value;
    }
    // defaults may mention earlier parameters
    for (Substitution::iterator s = subst.begin(); s != subst.end(); ++s) s->second = substitute(s->second, subst);
    if (given.size() < names.size()) {
        args.clear();
        for (size_t i = 0; i < names.size(); ++i) { if (i) args += ", "; args += normalizeArgs(subst[names[i]]); }
        key = tmpl->qualifiedName + "<" + args + ">";
        it = m_classes.find(key);
        if (it != m_classes.end()) return it->second;
    }

    ClassInfo* c = new ClassInfo(*tmpl);
    m_ownedClasses.push_back(c);
    m_classes[key] = c;
    c->name = tmpl->name + "<" + args + ">";
    c->qualifiedName = key;
    c->tmpl = TemplateInfo();
    c->tmpl.specializationArgs = "<" + args + ">";
    c->derived.clear();
    c->associations.clear();
    for (size_t f = 0; f < c->fields.size(); ++f) {
        c->fields[f].type = substitute(c->fields[f].type, subst);
        c->fields[f].initializer = substitute(c->fields[f].initializer, subst);
    }
    for (size_t t = 0; t < c->typedefs.size(); ++t) {
        c->typedefs[t].text = substitute(c->typedefs[t].text, subst);
        std::string text = c->typedefs[t].text;
        if (startsWith(text, "typedef ")) text = text.substr(8);
        size_t pos = text.rfind(c->typedefs[t].name);
        if (pos != std::string::npos) text = trim(text.substr(0, pos));
        types.addTypedef(c->qualifiedName + "::" + c->typedefs[t].name, text);
    }
    for (size_t b = 0; b < c->bases.size(); ++b) {
        c->bases[b].name = substitute(c->bases[b].name, subst);
        const Type* bt = types.parse(c->bases[b].name, tmpl->scope, 0);
        c->bases[b].resolved = (bt && bt->kind == Type::Class) ? const_cast<ClassInfo*>(bt->cls) : 0;
    }
    for (size_t m = 0; m < c->methods.size(); ++m) {
        FunctionInfo& fn = c->methods[m];
        const FunctionInfo& original = tmpl->methods[m];
        const FunctionInfo* originalDef = original.definition;
        bool memberTemplate = original.tmpl.isTemplate;
        substituteFunction(fn, subst);
        fn.ownerClass = c;
        fn.definition = 0;
        fn.definedAt.clear();
        if (memberTemplate) {
            // member templates keep their own parameters: not compiled (they would need deduction per call)
            fn.tmpl = original.tmpl;
            fn.hasBody = false;
            continue;
        }
        if (original.hasBody) {
            // in-class body
            size_t bb, be;
            std::vector<Token>* toks = substituteBody(original, subst, bb, be);
            if (toks) { fn.bodyBegin = bb; fn.bodyEnd = be; m_bodies[&fn] = toks; fn.definition = &fn; m_pending.push_back(&fn); ++m_functionCount; }
        } else if (originalDef && originalDef->hasBody) {
            // out-of-line "template <...> T FixedStack<T, N>::pop() { ... }"
            FunctionInfo* def = new FunctionInfo(*originalDef);
            m_ownedFunctions.push_back(def);
            substituteFunction(*def, subst);
            def->ownerClass = c;
            def->qualifierText = c->name;
            def->qualifier = c->name;
            size_t bb, be;
            std::vector<Token>* toks = substituteBody(*originalDef, subst, bb, be);
            if (toks) {
                def->bodyBegin = bb; def->bodyEnd = be;
                def->definition = def;
                def->declaredAt = m_cb.displayPath(fn.file) + ":" + intToString(fn.line);
                m_bodies[def] = toks;
                fn.definition = def;
                fn.definedAt = m_cb.displayPath(def->file) + ":" + intToString(def->line);
                m_pending.push_back(def);
                ++m_functionCount;
            }
        }
    }
    return c;
}

// ---- functions ----------------------------------------------------------------------

FunctionInfo* Instantiator::instantiateFunction(const FunctionInfo* tmpl, const Substitution& subst, TypeTable& types)
{
    (void)types;
    Lock l(semanticMutex());
    if (!tmpl || !tmpl->tmpl.isTemplate) return 0;
    const FunctionInfo* def = tmpl->hasBody ? tmpl : tmpl->definition;
    std::string args;
    std::vector<std::string> names, defaults;
    parameterNames(tmpl->tmpl, names, defaults);
    for (size_t i = 0; i < names.size(); ++i) {
        Substitution::const_iterator s = subst.find(names[i]);
        if (s == subst.end()) return 0;
        if (i) args += ", ";
        args += normalizeArgs(s->second);
    }
    std::string key = tmpl->fullName() + "<" + args + ">(" + substitute(tmpl->paramText, subst) + ")";
    std::map<std::string, FunctionInfo*>::iterator it = m_functions.find(key);
    if (it != m_functions.end()) return it->second;

    FunctionInfo* fn = new FunctionInfo(def ? *def : *tmpl);
    m_ownedFunctions.push_back(fn);
    m_functions[key] = fn;
    substituteFunction(*fn, subst);
    fn->name = tmpl->name + "<" + args + ">";
    fn->definition = 0;
    fn->definedAt.clear();
    fn->declaredAt.clear();
    if (def && def->hasBody) {
        size_t bb, be;
        std::vector<Token>* toks = substituteBody(*def, subst, bb, be);
        if (toks) { fn->bodyBegin = bb; fn->bodyEnd = be; fn->hasBody = true; fn->definition = fn; m_bodies[fn] = toks; m_pending.push_back(fn); ++m_functionCount; }
    } else {
        fn->hasBody = false;
    }
    return fn;
}

} // namespace cppparse
