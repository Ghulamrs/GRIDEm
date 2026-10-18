// Ssa.cpp - SSA construction, phi simplification, global value numbering, SSA destruction.
#include "Ssa.hpp"
#include "Optimizer.hpp"
#include "PathUtil.hpp"

#include <cstring>
#include <map>
#include <set>

namespace cppparse {

std::string operandKey(const IrOperand& o)
{
    if (o.kind != IrOperand::FImm) return o.str();
    unsigned char b[sizeof(double)];
    std::memcpy(b, &o.fval, sizeof b);
    std::string k = "#f";
    for (size_t i = 0; i < sizeof b; ++i) k += intToString(static_cast<int>(b[i])) + ".";
    return k;
}

namespace {

bool promotable(const IrFunction& f, int local)
{
    const IrLocal& l = f.locals[static_cast<size_t>(local)];
    if (l.addressTaken) return false;
    if (!l.type) return false;
    const Type* t = l.type;
    if (t->kind == Type::Class || t->kind == Type::Array || t->kind == Type::Unknown) return false;
    return true;
}

// dominance frontiers from the immediate dominators
void dominanceFrontiers(const IrFunction& f, const std::vector<int>& idom, std::vector<std::set<int> >& df)
{
    size_t n = f.blocks.size();
    df.assign(n, std::set<int>());
    for (size_t b = 0; b < n; ++b) {
        const std::vector<int>& preds = f.blocks[b].preds;
        if (preds.size() < 2) continue;
        for (size_t p = 0; p < preds.size(); ++p) {
            int runner = preds[p];
            if (idom[static_cast<size_t>(runner)] < 0) continue;
            int guard = 0;
            while (runner != idom[b] && runner >= 0 && guard++ < 100000) {
                df[static_cast<size_t>(runner)].insert(static_cast<int>(b));
                if (runner == 0) break;
                runner = idom[static_cast<size_t>(runner)];
            }
        }
    }
}

struct Renamer {
    IrFunction&                     f;
    const std::vector<int>&         idom;
    std::vector<std::vector<int> >  children;
    std::map<int, std::vector<int> > stacks;          // local -> stack of current temps
    std::map<int, int>              undefTemp;        // local -> temp holding "undefined" (const 0)
    std::map<int, int>              alias;            // load temp -> value temp
    std::map<int, int>              phiLocal;         // phi dst temp -> local
    std::vector<IrInst>             undefInsts;       // zero constants for reads without a reaching store

    Renamer(IrFunction& fn, const std::vector<int>& dom) : f(fn), idom(dom) {}

    int current(int local, int line)
    {
        std::vector<int>& st = stacks[local];
        if (!st.empty()) return st.back();
        // used before any store (uninitialised variable): a zero constant, inserted into the
        // entry block after the traversal (inserting now would invalidate the instruction being visited)
        std::map<int, int>::iterator u = undefTemp.find(local);
        if (u != undefTemp.end()) return u->second;
        const Type* t = f.locals[static_cast<size_t>(local)].type;
        IrInst c;
        c.op = t && t->isFloating() ? IrInst::FConst : IrInst::Const;
        c.type = t; c.line = line;
        c.dst = f.newTemp(t);
        c.sub = "undefined " + f.locals[static_cast<size_t>(local)].name;
        if (c.op == IrInst::FConst) c.args.push_back(IrOperand::fimm(0.0, t)); else c.args.push_back(IrOperand::imm(0, t));
        undefInsts.push_back(c);
        undefTemp[local] = c.dst;
        return c.dst;
    }

    void visit(int b)
    {
        std::vector<std::pair<int, size_t> > pushed;   // local, count
        IrBlock& blk = f.blocks[static_cast<size_t>(b)];
        std::vector<IrInst> kept;
        for (size_t i = 0; i < blk.insts.size(); ++i) {
            IrInst& in = blk.insts[i];
            if (in.op == IrInst::Phi) {
                std::map<int, int>::iterator pl = phiLocal.find(in.dst);
                if (pl != phiLocal.end()) { stacks[pl->second].push_back(in.dst); pushed.push_back(std::make_pair(pl->second, static_cast<size_t>(1))); }
                kept.push_back(in);
                continue;
            }
            // the parameter-entry loads inserted by toSsa define the incoming values: keep them
            if (in.op == IrInst::LoadLocal && in.sub == "param") { kept.push_back(in); continue; }
            // uses: loads of promoted locals become aliases of the current value
            if (in.op == IrInst::LoadLocal && in.args.size() == 1 && promotable(f, in.args[0].id)) {
                alias[in.dst] = current(in.args[0].id, in.line);
                continue;   // dropped
            }
            if (in.op == IrInst::StoreLocal && in.args.size() == 2 && promotable(f, in.args[0].id)) {
                int local = in.args[0].id;
                int value;
                if (in.args[1].kind == IrOperand::Temp) {
                    value = in.args[1].id;
                    std::map<int, int>::iterator a = alias.find(value);
                    if (a != alias.end()) value = a->second;
                } else {
                    // constant store: materialise as a const definition so the value has a temp
                    IrInst c;
                    c.op = in.args[1].kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const;
                    c.type = in.type; c.line = in.line;
                    c.dst = f.newTemp(in.type);
                    c.args.push_back(in.args[1]);
                    kept.push_back(c);
                    value = c.dst;
                }
                stacks[local].push_back(value);
                pushed.push_back(std::make_pair(local, static_cast<size_t>(1)));
                continue;   // store dropped
            }
            kept.push_back(in);
        }
        blk.insts.swap(kept);
        // phi operands of successors
        for (size_t s = 0; s < blk.succs.size(); ++s) {
            IrBlock& succ = f.blocks[static_cast<size_t>(blk.succs[s])];
            for (size_t i = 0; i < succ.insts.size(); ++i) {
                IrInst& phi = succ.insts[i];
                if (phi.op != IrInst::Phi) break;
                std::map<int, int>::iterator pl = phiLocal.find(phi.dst);
                if (pl == phiLocal.end()) continue;
                int v = current(pl->second, phi.line);
                phi.args.push_back(IrOperand::temp(v, phi.type));
                phi.args.push_back(IrOperand::block(b));
            }
        }
        for (size_t c = 0; c < children[static_cast<size_t>(b)].size(); ++c) visit(children[static_cast<size_t>(b)][c]);
        for (size_t p = 0; p < pushed.size(); ++p) stacks[pushed[p].first].pop_back();
    }
};

void rewriteAliases(IrFunction& f, const std::map<int, int>& alias)
{
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            for (size_t a = 0; a < f.blocks[b].insts[i].args.size(); ++a) {
                IrOperand& o = f.blocks[b].insts[i].args[a];
                if (o.kind != IrOperand::Temp) continue;
                int guard = 0;
                std::map<int, int>::const_iterator it = alias.find(o.id);
                while (it != alias.end() && guard++ < 1000) { o.id = it->second; it = alias.find(o.id); }
            }
}

} // namespace

bool isSsa(const IrFunction& f)
{
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i)
            if (f.blocks[b].insts[i].op == IrInst::Phi) return true;
    return false;
}

void toSsa(IrFunction& f, SsaStats& stats)
{
    f.computeCfg();
    std::vector<int> idom;
    Optimizer::dominators(f, idom);
    std::vector<std::set<int> > df;
    dominanceFrontiers(f, idom, df);

    // phi placement per promotable local
    Renamer R(f, idom);
    R.children.assign(f.blocks.size(), std::vector<int>());
    for (size_t b = 1; b < f.blocks.size(); ++b) if (idom[b] >= 0 && idom[b] != static_cast<int>(b)) R.children[static_cast<size_t>(idom[b])].push_back(static_cast<int>(b));

    for (size_t l = 0; l < f.locals.size(); ++l) {
        if (!promotable(f, static_cast<int>(l))) continue;
        std::set<int> defBlocks;
        for (size_t b = 0; b < f.blocks.size(); ++b)
            for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
                const IrInst& in = f.blocks[b].insts[i];
                if (in.op == IrInst::StoreLocal && in.args.size() == 2 && in.args[0].id == static_cast<int>(l)) defBlocks.insert(static_cast<int>(b));
            }
        bool param = f.locals[l].isParam;
        if (param) defBlocks.insert(0);
        if (defBlocks.empty()) continue;
        ++stats.promotedLocals;
        std::set<int> hasPhi;
        std::vector<int> work(defBlocks.begin(), defBlocks.end());
        while (!work.empty()) {
            int x = work.back(); work.pop_back();
            for (std::set<int>::iterator y = df[static_cast<size_t>(x)].begin(); y != df[static_cast<size_t>(x)].end(); ++y) {
                if (hasPhi.count(*y)) continue;
                hasPhi.insert(*y);
                IrInst phi;
                phi.op = IrInst::Phi;
                phi.type = f.locals[l].type;
                phi.dst = f.newTemp(phi.type);
                phi.sub = f.locals[l].name;
                phi.line = f.blocks[static_cast<size_t>(*y)].insts.empty() ? 0 : f.blocks[static_cast<size_t>(*y)].insts[0].line;
                f.blocks[static_cast<size_t>(*y)].insts.insert(f.blocks[static_cast<size_t>(*y)].insts.begin(), phi);
                R.phiLocal[phi.dst] = static_cast<int>(l);
                ++stats.phisInserted;
                if (!defBlocks.count(*y)) { defBlocks.insert(*y); work.push_back(*y); }
            }
        }
        if (param) {
            // the incoming parameter value: a load at the very start of the entry block
            IrInst ld;
            ld.op = IrInst::LoadLocal;
            ld.type = f.locals[l].type;
            ld.line = 0;
            ld.dst = f.newTemp(ld.type);
            ld.sub = "param";
            ld.args.push_back(IrOperand::local(static_cast<int>(l), ld.type));
            f.blocks[0].insts.insert(f.blocks[0].insts.begin(), ld);
            R.stacks[static_cast<int>(l)].push_back(ld.dst);
        }
    }
    // the parameter loads were inserted after any phi of the entry block: move phis first
    {
        std::vector<IrInst>& e = f.blocks[0].insts;
        std::vector<IrInst> phis, rest;
        for (size_t i = 0; i < e.size(); ++i) (e[i].op == IrInst::Phi ? phis : rest).push_back(e[i]);
        phis.insert(phis.end(), rest.begin(), rest.end());
        e.swap(phis);
    }
    R.visit(0);
    f.blocks[0].insts.insert(f.blocks[0].insts.begin(), R.undefInsts.begin(), R.undefInsts.end());
    rewriteAliases(f, R.alias);
    // parameter loads marked "param" are real loads from the incoming slots; drop the marker
    for (size_t i = 0; i < f.blocks[0].insts.size(); ++i) if (f.blocks[0].insts[i].sub == "param") f.blocks[0].insts[i].sub.clear();
    f.computeCfg();
}

int simplifyPhis(IrFunction& f)
{
    int changes = 0;
    for (size_t b = 0; b < f.blocks.size(); ++b)
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            IrInst& in = f.blocks[b].insts[i];
            if (in.op != IrInst::Phi) continue;
            // operands other than the phi itself
            IrOperand only;
            bool same = true, any = false;
            for (size_t a = 0; a + 1 < in.args.size(); a += 2) {
                const IrOperand& o = in.args[a];
                if (o.kind == IrOperand::Temp && o.id == in.dst) continue;
                if (!any) { only = o; any = true; continue; }
                bool eq = o.kind == only.kind && ((o.kind == IrOperand::Temp && o.id == only.id) || (o.kind == IrOperand::Imm && o.ival == only.ival) || (o.kind == IrOperand::FImm && o.str() == only.str()));   // exact: -0.0 is not 0.0
                if (!eq) { same = false; break; }
            }
            if (!any || !same) continue;
            IrInst n; n.dst = in.dst; n.type = in.type; n.line = in.line;
            if (only.kind == IrOperand::Temp) { n.op = IrInst::Copy; n.args.push_back(only); }
            else { n.op = only.kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const; n.args.push_back(only); }
            in = n;
            ++changes;
        }
    return changes;
}

namespace {

struct GvnState {
    IrFunction& f;
    std::vector<std::vector<int> > children;
    std::map<int, int> replaced;    // temp -> representative temp
    int changes;
    GvnState(IrFunction& fn) : f(fn), changes(0) {}

    std::string key(const IrInst& in)
    {
        bool addr = in.op == IrInst::AddrField || in.op == IrInst::AddrIndex || in.op == IrInst::AddrLocal || in.op == IrInst::AddrGlobal;
        std::string k = intToString(static_cast<int>(in.op)) + "|" + in.sub + "|" + intToString(static_cast<int>(in.extra)) + "|" + (in.type && !addr ? in.type->str() : "") + "|";
        for (size_t a = 0; a < in.args.size(); ++a) {
            IrOperand o = in.args[a];
            if (o.kind == IrOperand::Temp) { std::map<int, int>::iterator r = replaced.find(o.id); if (r != replaced.end()) o.id = r->second; }
            k += operandKey(o) + ",";
        }
        return k;
    }

    void visit(int b, std::map<std::string, int> table)
    {
        IrBlock& blk = f.blocks[static_cast<size_t>(b)];
        for (size_t i = 0; i < blk.insts.size(); ++i) {
            IrInst& in = blk.insts[i];
            if (!in.pure() || in.dst < 0 || in.op == IrInst::Copy || in.op == IrInst::Phi || in.op == IrInst::Const || in.op == IrInst::FConst || in.op == IrInst::StrConst) continue;
            std::string k = key(in);
            std::map<std::string, int>::iterator it = table.find(k);
            if (it == table.end()) { table[k] = in.dst; continue; }
            IrInst n; n.op = IrInst::Copy; n.dst = in.dst; n.type = in.type; n.line = in.line;
            n.args.push_back(IrOperand::temp(it->second, in.type));
            in = n;
            replaced[in.dst] = it->second;
            ++changes;
        }
        for (size_t c = 0; c < children[static_cast<size_t>(b)].size(); ++c) visit(children[static_cast<size_t>(b)][c], table);
    }
};

} // namespace

int gvn(IrFunction& f)
{
    f.computeCfg();
    std::vector<int> idom;
    Optimizer::dominators(f, idom);
    GvnState S(f);
    S.children.assign(f.blocks.size(), std::vector<int>());
    for (size_t b = 1; b < f.blocks.size(); ++b) if (idom[b] >= 0 && idom[b] != static_cast<int>(b)) S.children[static_cast<size_t>(idom[b])].push_back(static_cast<int>(b));
    std::map<std::string, int> table;
    S.visit(0, table);
    return S.changes;
}

void fromSsa(IrFunction& f, SsaStats& stats)
{
    f.computeCfg();
    // split critical edges into blocks that hold phis
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        if (f.blocks[b].insts.empty() || f.blocks[b].insts[0].op != IrInst::Phi) continue;
        if (f.blocks[b].preds.size() < 2) continue;
        std::vector<int> preds = f.blocks[b].preds;
        for (size_t p = 0; p < preds.size(); ++p) {
            int pr = preds[p];
            if (f.blocks[static_cast<size_t>(pr)].succs.size() < 2) continue;
            int nb = f.newBlock("edge " + intToString(pr) + "->" + intToString(static_cast<int>(b)));
            IrInst j; j.op = IrInst::Br; j.args.push_back(IrOperand::block(static_cast<int>(b)));
            f.blocks[static_cast<size_t>(nb)].insts.push_back(j);
            IrInst& term = f.blocks[static_cast<size_t>(pr)].insts.back();
            for (size_t a = 0; a < term.args.size(); ++a) if (term.args[a].kind == IrOperand::Block && term.args[a].id == static_cast<int>(b)) term.args[a].id = nb;
            for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) {
                IrInst& phi = f.blocks[b].insts[i];
                if (phi.op != IrInst::Phi) break;
                for (size_t a = 1; a < phi.args.size(); a += 2) if (phi.args[a].kind == IrOperand::Block && phi.args[a].id == pr) phi.args[a].id = nb;
            }
            ++stats.edgesSplit;
        }
    }
    f.computeCfg();
    // parallel copies on each incoming edge
    for (size_t b = 0; b < f.blocks.size(); ++b) {
        std::vector<IrInst> phis;
        std::vector<IrInst> rest;
        for (size_t i = 0; i < f.blocks[b].insts.size(); ++i) (f.blocks[b].insts[i].op == IrInst::Phi ? phis : rest).push_back(f.blocks[b].insts[i]);
        if (phis.empty()) continue;
        f.blocks[b].insts.swap(rest);
        std::map<int, std::vector<std::pair<IrOperand, int> > > perPred;   // pred -> (value, phi dst)
        for (size_t i = 0; i < phis.size(); ++i) {
            const IrInst& phi = phis[i];
            for (size_t a = 0; a + 1 < phi.args.size(); a += 2)
                perPred[phi.args[a + 1].id].push_back(std::make_pair(phi.args[a], phi.dst));
            ++stats.phisRemoved;
        }
        for (std::map<int, std::vector<std::pair<IrOperand, int> > >::iterator it = perPred.begin(); it != perPred.end(); ++it) {
            IrBlock& pred = f.blocks[static_cast<size_t>(it->first)];
            std::vector<IrInst> copies;
            // two phases: sources into fresh temporaries, then into the phi results (handles swaps)
            std::vector<int> tmps;
            for (size_t k = 0; k < it->second.size(); ++k) {
                const IrOperand& src = it->second[k].first;
                const Type* t = src.type ? src.type : f.typeOf(it->second[k].second);
                IrInst c; c.type = t; c.line = 0;
                if (src.kind == IrOperand::Temp) { c.op = IrInst::Copy; c.args.push_back(src); }
                else { c.op = src.kind == IrOperand::FImm ? IrInst::FConst : IrInst::Const; c.args.push_back(src); }
                c.dst = f.newTemp(t);
                tmps.push_back(c.dst);
                copies.push_back(c);
            }
            for (size_t k = 0; k < it->second.size(); ++k) {
                IrInst c; c.op = IrInst::Copy; c.dst = it->second[k].second; c.type = f.typeOf(c.dst); c.line = 0;
                c.args.push_back(IrOperand::temp(tmps[k], c.type));
                copies.push_back(c);
            }
            stats.copiesInserted += static_cast<int>(copies.size());
            size_t pos = pred.insts.empty() ? 0 : pred.insts.size() - 1;
            if (!pred.insts.empty() && !pred.insts.back().isTerminator()) pos = pred.insts.size();
            pred.insts.insert(pred.insts.begin() + static_cast<long>(pos), copies.begin(), copies.end());
        }
    }
    f.computeCfg();
}

} // namespace cppparse
