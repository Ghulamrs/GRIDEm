// Compiler.cpp - version 3 driver, version 5: the per-function phases run on the worker pool.
//
// Phases (each one runs the functions in parallel unless noted):
//   1. body parse, type check, IR build        per function; shared structures are locked
//   2. template instantiation rounds           the same, for bodies requested while type checking
//   3. local optimisation (-O1 passes, LICM)   per function
//   4. inlining and unrolling                  sequential: reads the IR of other functions
//   5. SSA passes and final clean-up           per function
//   6. VM64 lowering and x86-64 code           per function; the module pools are locked
// Results are merged in a fixed order, so the outputs do not depend on thread timing.
#include "Compiler.hpp"
#include "BackEnd.hpp"
#include "BodyParser.hpp"
#include "Instantiate.hpp"
#include "IrBuilder.hpp"
#include "Lower.hpp"
#include "Mangle.hpp"
#include "Optimizer.hpp"
#include "Parallel.hpp"
#include "PathUtil.hpp"
#include "TypeCheck.hpp"
#include "Types.hpp"
#include "X86.hpp"

#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace cppparse {

Compiler::Compiler(CodeBase& cb, const CompileOptions& opt) : m_cb(cb), m_opt(opt) {}

Compiler::~Compiler() {}

namespace {

struct Unit {
    FunctionInfo*             fn;
    const std::vector<Token>* tokens;
    bool                      instantiated;
};

void collectUnits(CodeBase& cb, std::vector<Unit>& out)
{
    const std::vector<SourceFile*>& files = cb.files();
    for (size_t f = 0; f < files.size(); ++f) {
        SourceFile* sf = files[f];
        for (size_t k = 0; k < sf->functions.size(); ++k)
            if (sf->functions[k].hasBody) { Unit u; u.fn = &sf->functions[k]; u.tokens = &sf->tokens; u.instantiated = false; out.push_back(u); }
        for (size_t c = 0; c < sf->allClasses.size(); ++c)
            for (size_t m = 0; m < sf->allClasses[c]->methods.size(); ++m)
                if (sf->allClasses[c]->methods[m].hasBody) { Unit u; u.fn = &sf->allClasses[c]->methods[m]; u.tokens = &sf->tokens; u.instantiated = false; out.push_back(u); }
    }
}

// everything one function produces in phase 1; merged into the compiler state in unit order
struct UnitResult {
    std::vector<Finding> findings;
    std::string          astText;
    IrFunction*          ir;
    bool                 parseFailed, typeFailed, opaqueSkipped;
    UnitResult() : ir(0), parseFailed(false), typeFailed(false), opaqueSkipped(false) {}
};

// the first use by value of a standard-library class this pipeline does not know (see Layouts::opaqueByValue):
// such code would be laid out and copied with guessed sizes, so the function is left out instead
bool findOpaqueUse(const IrFunction& f, Layouts& layouts, std::string& what)
{
    for (size_t i = 0; i < f.locals.size(); ++i)
        if (layouts.opaqueByValue(f.locals[i].type)) { what = (f.locals[i].isParam ? "parameter " : "local ") + f.locals[i].name + " of type " + f.locals[i].type->str(); return true; }
    if (layouts.opaqueByValue(f.retType)) { what = "returns " + f.retType->str() + " by value"; return true; }
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t k = 0; k < f.blocks[b].insts.size(); ++k) {
            const IrInst& in = f.blocks[b].insts[k];
            if (layouts.opaqueByValue(in.type)) { what = "uses " + in.type->str() + " by value"; return true; }
            if (in.op == IrInst::AddrField && !in.args.empty() && in.args[0].type && in.args[0].type->kind == Type::Pointer &&
                layouts.opaqueByValue(in.args[0].type->inner)) { what = "accesses a field of " + in.args[0].type->inner->str(); return true; }
        }
    return false;
}


// one vtable per polymorphic class, with the IR names of the most derived implementations
void collectVtable(const ClassInfo* c, Layouts& layouts, X86Module& x86)
{
    if (!c->isDefinition || c->tmpl.isTemplate || !layouts.hasVtable(c)) return;
    std::string desc = layouts.describe(c);
    size_t vt = desc.find("vtable:");
    if (vt == std::string::npos) return;
    std::vector<std::string> keys;
    std::string rest = desc.substr(vt + 7);
    size_t pos = 0;
    while ((pos = rest.find('[', pos)) != std::string::npos) {
        size_t close = rest.find(']', pos);
        size_t next = rest.find('[', close);
        keys.push_back(trim(rest.substr(close + 1, next == std::string::npos ? std::string::npos : next - close - 1)));
        pos = close + 1;
    }
    IrVtable v;
    v.className = c->qualifiedName;
    for (size_t k = 0; k < keys.size(); ++k) {
        const FunctionInfo* impl = 0;
        std::string key = keys[k];
        if (key.find("::") != std::string::npos) key = key.substr(key.find("::") + 2);
        for (const ClassInfo* walk = c; walk && !impl; ) {
            for (size_t m = 0; m < walk->methods.size(); ++m) {
                const FunctionInfo& fn = walk->methods[m];
                std::string mk = fn.isDestructor ? "~dtor" : fn.name + "(";
                if (!fn.isDestructor) { for (size_t p = 0; p < fn.params.size(); ++p) { std::string t; for (size_t ch = 0; ch < fn.params[p].type.size(); ++ch) if (fn.params[p].type[ch] != ' ') t += fn.params[p].type[ch]; mk += t + ";"; } mk += ")"; if (fn.isConst) mk += "c"; }
                if (mk == key && (fn.definition || fn.hasBody)) { impl = fn.definition ? fn.definition : &fn; break; }
            }
            walk = walk->bases.empty() ? 0 : walk->bases[0].resolved;
        }
        v.entries.push_back(impl ? irFunctionName(impl) : std::string(""));
    }
    x86.addVtable(v);
}

void collectGlobals(const CodeBase& cb, TypeTable& types, Layouts& layouts, X86Module& x86, bool msvc)
{
    const std::vector<SourceFile*>& files = cb.files();
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v) {
            const VariableInfo& g = files[f]->variables[v];
            if (msvc) x86.registerGlobalName(g.fullName(), msvcMangleVariable(g, types));
            if (g.isExtern) continue;
            const Type* t = types.parse(g.type, g.scope, 0);
            IrGlobal ig;
            ig.name = g.fullName();
            ig.size = layouts.sizeOf(t);
            ig.align = layouts.alignOf(t);
            ig.isFloat = t && t->isFloating();
            std::string init = trim(g.initializer);
            bool numeric = !init.empty() && (isdigit(static_cast<unsigned char>(init[0])) || init[0] == '-' || init[0] == '+') && init.find_first_of("(,") == std::string::npos;
            if (numeric) ig.init = init;
            x86.addGlobal(ig);
        }
}

// parses, type checks and lowers one function body to IR (runs on a worker thread)
void compileUnit(const CodeBase& cb, TypeTable& types, Layouts& layouts, const std::set<std::string>& macros, bool emitAst,
                 FunctionInfo& fn, const std::vector<Token>& tokens, UnitResult& r)
{
    AstArena arena;
    FunctionAst ast;
    BodyParser parser(cb, types, arena, fn, tokens);
    if (fn.isConstructor && fn.hasInitializerList && fn.bodyBegin > 0) {
        // the initializer list sits between the ':' after the parameter list and the body's '{'
        size_t k = fn.bodyBegin;
        int depth = 0;
        size_t colon = 0;
        bool found = false;
        while (k > 0) {
            --k;
            const Token& t = tokens[k];
            if (t.type != TokPunct) continue;
            if (t.text == ")") ++depth;
            else if (t.text == "(") { if (depth == 0) break; --depth; }
            else if (t.text == ":" && depth == 0) { colon = k; found = true; break; }
        }
        if (found) parser.parseInitializers(ast, colon + 1, fn.bodyBegin);
    }
    parser.parse(ast);
    if (!ast.parsed) {
        r.parseFailed = true;
        for (size_t e = 0; e < ast.errors.size() && e < 3; ++e) {
            Finding f;
            f.severity = Finding::Warning;
            f.code = "T191";
            f.file = fn.file;
            f.line = ast.errors[e].size() > 1 ? atoi(ast.errors[e].c_str() + 1) : fn.line;
            f.message = "body of " + fn.displayName() + " could not be parsed: " + ast.errors[e].substr(ast.errors[e].find(':') + 2);
            f.detail = "the function is not type checked or compiled";
            r.findings.push_back(f);
        }
    }
    TypeChecker checker(cb, types, r.findings, macros);
    bool checked = ast.parsed && checker.check(ast);
    if (ast.parsed && !checked) r.typeFailed = true;
    fn.typeChecked = ast.parsed;   // typed checks replace the count-based ones even when they found errors

    if (emitAst) {
        std::ostringstream s;
        dumpFunctionAst(ast, s);
        r.astText = s.str() + "\n";
    }
    if (!ast.parsed) return;

    IrBuilder builder(cb, types, layouts);
    r.ir = new IrFunction;
    builder.build(ast, *r.ir);
    std::string what;
    if (findOpaqueUse(*r.ir, layouts, what)) {
        Finding f;
        f.severity = Finding::Info;
        f.code = "T190";
        f.file = fn.file;
        f.line = fn.line;
        f.message = fn.displayName() + " is outside the version 3 subset and not compiled: " + what;
        f.detail = "standard-library classes are opaque to version 3; --compile6 compiles them with cppparse's own headers";
        r.findings.push_back(f);
        delete r.ir;
        r.ir = 0;
        r.opaqueSkipped = true;
    }
}

struct FrontJob : public ParallelJob {
    const CodeBase*               cb;
    TypeTable*                    types;
    Layouts*                      layouts;
    const std::set<std::string>*  macros;
    bool                          emitAst;
    std::vector<Unit>             units;
    std::vector<UnitResult>       results;
    virtual void run(size_t i) { compileUnit(*cb, *types, *layouts, *macros, emitAst, *units[i].fn, *units[i].tokens, results[i]); }
};

} // namespace

void Compiler::run()
{
    long start = millisecondsNow();
    int threads = m_opt.threads < 1 ? 1 : m_opt.threads;
    TypeTable types(m_cb);
    Instantiator instantiator(m_cb);
    types.setInstantiator(&instantiator);
    Layouts layouts(types, m_cb);
    std::set<std::string> macros;
    TypeChecker::collectMacroNames(m_cb, macros);

    std::vector<Unit> all;
    collectUnits(m_cb, all);

    std::ostringstream astOut, irOut, logOut, asmOut, layoutOut;
    std::vector<IrFunction*> irFunctions;
    std::map<std::string, IrFunction*> byName;

    logOut << "cppparse version 3 optimisation log  (-O" << m_opt.optLevel << ", " << threads << " thread(s))\n\n";

    long tFront = millisecondsNow();
    // phase 1: the functions written in the sources
    FrontJob job;
    job.cb = &m_cb; job.types = &types; job.layouts = &layouts; job.macros = &macros; job.emitAst = m_opt.emitAst;
    for (size_t u = 0; u < all.size(); ++u) {
        FunctionInfo& fn = *all[u].fn;
        ++m_summary.functionsSeen;
        std::string title = fn.displayName() + "(" + fn.paramText + ")" + (fn.isConst ? " const" : "") + "   [" + m_cb.displayPath(fn.file) + ":" + intToString(fn.line) + "]";
        std::string skip;
        if (fn.tmpl.isTemplate || (fn.ownerClass && fn.ownerClass->tmpl.isTemplate)) skip = "template (compiled per instantiation, see the instantiated functions below)";
        else if (fn.exceptions.functionTryBlock) skip = "function-try-block";
        else if (fn.isFriend) skip = "friend function defined inside a class";
        else if (fn.bodyEnd <= fn.bodyBegin || fn.bodyEnd > all[u].tokens->size()) skip = "body tokens not available";
        if (!skip.empty()) {
            ++m_summary.functionsSkipped;
            astOut << "function " << title << "\n  skipped: " << skip << "\n\n";
            continue;
        }
        job.units.push_back(all[u]);
    }

    // phase 2 repeats phase 1 for template instantiations requested while type checking (they may request more)
    for (int round = 0; round < 9 && !job.units.empty(); ++round) {
        job.results.clear();
        job.results.resize(job.units.size());
        runParallel(job, job.units.size(), threads);
        for (size_t i = 0; i < job.units.size(); ++i) {
            UnitResult& r = job.results[i];
            m_findings.insert(m_findings.end(), r.findings.begin(), r.findings.end());
            if (job.units[i].instantiated) astOut << "; instantiated\n";
            astOut << r.astText;
            if (r.parseFailed) ++m_summary.parseFailures;
            if (r.typeFailed) ++m_summary.typeFailures;
            if (r.opaqueSkipped) ++m_summary.functionsSkipped;
            if (r.ir) { irFunctions.push_back(r.ir); byName[r.ir->name] = r.ir; ++m_summary.functionsCompiled; }
        }
        job.units.clear();
        if (round == 8) break;
        std::vector<FunctionInfo*> pending;
        instantiator.takePending(pending);
        for (size_t i = 0; i < pending.size(); ++i) {
            const std::vector<Token>* toks = instantiator.tokensFor(pending[i]);
            if (!toks) continue;
            ++m_summary.functionsSeen;
            Unit u; u.fn = pending[i]; u.tokens = toks; u.instantiated = true;
            job.units.push_back(u);
        }
    }
    tFront = millisecondsNow() - tFront;
    m_summary.classInstantiations = instantiator.classCount();
    m_summary.functionInstantiations = instantiator.functionCount();

    // class layouts
    const std::vector<ClassInfo*>& classes = m_cb.classes();
    for (size_t c = 0; c < classes.size(); ++c)
        if (classes[c]->isDefinition && !classes[c]->tmpl.isTemplate) {
            std::string d = layouts.describe(classes[c]);
            if (!d.empty()) layoutOut << d << "\n";
        }
    for (size_t c = 0; c < instantiator.classes().size(); ++c) {
        std::string d = layouts.describe(instantiator.classes()[c]);
        if (!d.empty()) layoutOut << d << "   (instantiated)\n";
    }

    // x86 metadata: vtables, globals and decorated names come from the version 3 model
    X86Module x86(m_opt.msvcNames);
    x86.setTableExceptions(m_opt.tableEh);
    for (size_t c = 0; c < classes.size(); ++c) collectVtable(classes[c], layouts, x86);
    for (size_t c = 0; c < instantiator.classes().size(); ++c) collectVtable(instantiator.classes()[c], layouts, x86);
    collectGlobals(m_cb, types, layouts, x86, m_opt.msvcNames);
    if (m_opt.msvcNames) {
        const std::vector<SourceFile*>& files = m_cb.files();
        for (size_t f = 0; f < files.size(); ++f) {
            const SourceFile* sf = files[f];
            for (size_t k = 0; k < sf->functions.size(); ++k) x86.registerSymbolName(irFunctionName(&sf->functions[k]), msvcMangleFunction(sf->functions[k], types));
            for (size_t c = 0; c < sf->allClasses.size(); ++c)
                for (size_t m = 0; m < sf->allClasses[c]->methods.size(); ++m) x86.registerSymbolName(irFunctionName(&sf->allClasses[c]->methods[m]), msvcMangleFunction(sf->allClasses[c]->methods[m], types));
        }
        for (size_t i = 0; i < irFunctions.size(); ++i) if (irFunctions[i]->fn) x86.registerSymbolName(irFunctions[i]->name, msvcMangleFunction(*irFunctions[i]->fn, types));
    }

    BackEndOptions bo;
    bo.optLevel = m_opt.optLevel; bo.threads = threads; bo.x86 = m_opt.x86;
    BackEndResult br;
    runBackEnd(irFunctions, &x86, bo, br);
    m_summary.irBefore = br.irBefore; m_summary.irAfter = br.irAfter; m_summary.asmInstructions = br.asmInstructions; m_summary.spills = br.spills;
    logOut << br.log;
    irOut << br.irText;
    asmOut << br.asmText;
    if (m_opt.x86) { m_x86 = br.x86Text; m_symbolNames.swap(br.symbolNames); }
    if (m_opt.optLevel == 0) logOut << "(no optimisation requested: use -O1, -O2 or -O3)\n";
    logOut << "\ntotal IR instructions: " << m_summary.irBefore << " -> " << m_summary.irAfter << "\n";
    logOut << "phase times (ms, " << threads << " thread(s)): front end " << tFront << ", local optimisation " << br.tLocal
           << ", inlining/unrolling " << br.tInter << ", SSA " << br.tSsa << ", lowering " << br.tLower << "\n";

    for (size_t i = 0; i < irFunctions.size(); ++i) delete irFunctions[i];

    m_ast = astOut.str();
    m_ir = irOut.str();
    m_log = logOut.str();
    m_asm = "; cppparse version 3 lowering - target VM64 (R0-R7 integer, F0-F7 float, FP, SP)\n"
            "; calling convention: R0-R3 / F0-F3 then stack, result R0 / F0, all registers caller-saved\n\n" + asmOut.str();
    m_layouts = layoutOut.str();
    m_summary.compileMs = millisecondsNow() - start;
}

void Compiler::summaryLines(std::vector<std::string>& out) const
{
    out.push_back("Version 3 compile (-O" + intToString(m_opt.optLevel) + "): functions seen " + intToString(m_summary.functionsSeen) +
                  ", compiled " + intToString(m_summary.functionsCompiled) + ", skipped " + intToString(m_summary.functionsSkipped) +
                  " (templates, function-try-blocks, std:: classes by value), parse failures " + intToString(m_summary.parseFailures) +
                  ", functions with type errors " + intToString(m_summary.typeFailures));
    out.push_back("IR instructions: " + intToString(m_summary.irBefore) + " -> " + intToString(m_summary.irAfter) + " after optimisation;  assembly instructions " +
                  intToString(m_summary.asmInstructions) + ", spilled temporaries " + intToString(m_summary.spills));
    out.push_back("Template instantiations: " + intToString(m_summary.classInstantiations) + " classes, " + intToString(m_summary.functionInstantiations) + " functions");
}

} // namespace cppparse
