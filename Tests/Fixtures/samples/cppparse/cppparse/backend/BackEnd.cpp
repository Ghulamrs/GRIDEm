// BackEnd.cpp - IR to assembly, see BackEnd.hpp.
#include "BackEnd.hpp"
#include "Optimizer.hpp"
#include "Parallel.hpp"
#include "PathUtil.hpp"

#include <sstream>

namespace cppparse {

namespace {

struct OptJob : public ParallelJob {
    Optimizer*                              opt;
    int                                     phase;      // 0 local, 1 interprocedural, 2 SSA
    std::vector<IrFunction*>*               functions;
    std::vector<std::vector<std::string> >* logs;
    std::vector<std::string>*               ssaTexts;
    virtual void run(size_t i)
    {
        IrFunction& f = *(*functions)[i];
        if (phase == 0) opt->runLocal(f, (*logs)[i]);
        else if (phase == 1) opt->runInterprocedural(f, (*logs)[i]);
        else opt->runSsa(f, (*logs)[i], &(*ssaTexts)[i]);
    }
};

struct LowerJob : public ParallelJob {
    std::vector<IrFunction*>* functions;
    X86Module*                x86;
    std::vector<std::string>  vmText, x86Text, afterText;
    std::vector<LowerStats>   vmStats;
    virtual void run(size_t i)
    {
        IrFunction& f = *(*functions)[i];
        afterText[i] = printIr(f);
        vmText[i] = lowerFunction(f, vmStats[i]);
        if (x86) { LowerStats xs; x86Text[i] = x86->codeFor(f, xs); }
    }
};

} // namespace

void runBackEnd(std::vector<IrFunction*>& functions, X86Module* x86, const BackEndOptions& opt, BackEndResult& out)
{
    size_t n = functions.size();
    int threads = opt.threads < 1 ? 1 : opt.threads;
    std::map<std::string, IrFunction*> byName;
    for (size_t i = 0; i < n; ++i) byName[functions[i]->name] = functions[i];
    std::vector<int> before(n, 0);
    std::vector<std::string> beforeText(n), ssaTexts(n);
    std::vector<std::vector<std::string> > logs(n);
    for (size_t i = 0; i < n; ++i) { before[i] = functions[i]->instCount(); out.irBefore += before[i]; beforeText[i] = printIr(*functions[i]); }
    Optimizer optimizer(opt.optLevel, byName);
    if (opt.optLevel > 0) {
        OptJob oj;
        oj.opt = &optimizer; oj.functions = &functions; oj.logs = &logs; oj.ssaTexts = &ssaTexts;
        long t0 = millisecondsNow();
        oj.phase = 0; runParallel(oj, n, threads);
        long t1 = millisecondsNow();
        oj.phase = 1; runParallel(oj, n, 1);          // inlining reads other functions: one thread
        long t2 = millisecondsNow();
        oj.phase = 2; runParallel(oj, n, threads);
        long t3 = millisecondsNow();
        out.tLocal = t1 - t0; out.tInter = t2 - t1; out.tSsa = t3 - t2;
    }
    LowerJob lj;
    lj.functions = &functions; lj.x86 = opt.x86 ? x86 : 0;
    lj.vmText.resize(n); lj.x86Text.resize(n); lj.afterText.resize(n); lj.vmStats.resize(n);
    long tLower = millisecondsNow();
    runParallel(lj, n, threads);
    out.tLower = millisecondsNow() - tLower;

    std::ostringstream irOut, logOut, asmOut;
    for (size_t i = 0; i < n; ++i) {
        IrFunction& f = *functions[i];
        for (size_t l = 0; l < logs[i].size(); ++l) logOut << logs[i][l] << "\n";
        out.irAfter += f.instCount();
        irOut << "================================================================================\n";
        if (opt.optLevel > 0) {
            irOut << "--- before optimisation (" << before[i] << " instructions) ---\n" << beforeText[i];
            if (!ssaTexts[i].empty()) irOut << "--- SSA form after the SSA passes ---\n" << ssaTexts[i];
            irOut << "--- after -O" << opt.optLevel << " (" << f.instCount() << " instructions) ---\n" << lj.afterText[i] << "\n";
        } else {
            irOut << beforeText[i] << "\n";
        }
        asmOut << lj.vmText[i];
        out.asmInstructions += lj.vmStats[i].instructions;
        out.spills += lj.vmStats[i].spills;
        if (opt.x86 && x86) x86->appendCode(lj.x86Text[i]);
    }
    if (opt.x86 && x86) { out.x86Text = x86->finish(); x86->describeSymbols(functions, out.symbolNames); }
    out.irText = irOut.str();
    out.log = logOut.str();
    out.asmText = "; cppparse lowering - target VM64 (R0-R7 integer, F0-F7 float, FP, SP)\n"
                  "; calling convention: R0-R3 / F0-F3 then stack, result R0 / F0, all registers caller-saved\n\n" + asmOut.str();
}

} // namespace cppparse
