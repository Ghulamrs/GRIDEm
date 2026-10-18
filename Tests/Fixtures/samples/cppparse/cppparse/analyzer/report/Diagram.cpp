// Diagram.cpp - class diagram layout, SVG and DOT output.
#include "Diagram.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cppparse {

namespace {

const double kCharW      = 6.6;   // monospace 11px
const double kLineH      = 15.0;
const double kTitleH     = 22.0;
const double kPadX       = 8.0;
const double kPadY       = 6.0;
const double kGapX       = 36.0;
const double kGapY       = 70.0;
const double kMargin     = 30.0;
const size_t kMaxFields  = 8;
const size_t kMaxMethods = 10;

char accessSymbol(Access a)
{
    switch (a) {
    case AccessPublic:    return '+';
    case AccessProtected: return '#';
    case AccessPrivate:   return '-';
    default:              return ' ';
    }
}

std::string shorten(const std::string& s, size_t max)
{
    if (s.size() <= max) return s;
    return s.substr(0, max - 3) + "...";
}

std::string fmt(double v)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(1);
    oss << v;
    return oss.str();
}

struct ByName {
    const std::vector<DiagramNode>* nodes;
    bool operator()(size_t a, size_t b) const { return (*nodes)[a].title < (*nodes)[b].title; }
};

struct ByKey {
    const std::vector<double>* keys;
    const std::vector<DiagramNode>* nodes;
    bool operator()(size_t a, size_t b) const {
        if ((*keys)[a] != (*keys)[b]) return (*keys)[a] < (*keys)[b];
        return (*nodes)[a].title < (*nodes)[b].title;
    }
};

} // namespace

DiagramNode::DiagramNode()
    : cls(0), x(0), y(0), w(0), h(0), titleHeight(kTitleH), depth(0), external(false), abstract(false)
{
}

DiagramEdge::DiagramEdge() : kind(Inherit), from(0), to(0), isVirtual(false), access(AccessPublic) {}

// ---- building -----------------------------------------------------------------

size_t DiagramLayout::externalNode(const std::string& name)
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].external && nodes[i].title == name) return i;
    DiagramNode n;
    n.external = true;
    n.title = name;
    nodes.push_back(n);
    return nodes.size() - 1;
}

void DiagramLayout::build(const CodeBase& cb)
{
    nodes.clear();
    edges.clear();
    const std::vector<ClassInfo*>& classes = cb.classes();

    // one node per class definition
    std::vector<ClassInfo*> defs;
    for (size_t i = 0; i < classes.size(); ++i) {
        ClassInfo* c = classes[i];
        if (!c->isDefinition) continue;
        bool dup = false;
        for (size_t k = 0; k < defs.size(); ++k)
            if (defs[k]->qualifiedName == c->qualifiedName && defs[k]->tmpl.specializationArgs == c->tmpl.specializationArgs) { dup = true; break; }
        if (!dup) defs.push_back(c);
    }
    for (size_t i = 0; i < defs.size(); ++i) {
        ClassInfo* c = defs[i];
        DiagramNode n;
        n.cls = c;
        n.abstract = c->isAbstract;
        n.title = std::string(ClassInfo::kindName(c->kind)) + " " + c->qualifiedName + c->tmpl.specializationArgs;
        if (c->tmpl.isTemplate) n.subtitle = shorten(c->tmpl.text(), 48);
        for (size_t f = 0; f < c->fields.size() && f < kMaxFields; ++f) {
            const VariableInfo& v = c->fields[f];
            std::string line = std::string(1, accessSymbol(v.access)) + " " + (v.isStatic ? "static " : "") + v.name + " : " + v.type;
            n.fields.push_back(shorten(line, 44));
        }
        if (c->fields.size() > kMaxFields) n.fields.push_back("  ... " + intToString(static_cast<int>(c->fields.size() - kMaxFields)) + " more");
        for (size_t m = 0; m < c->methods.size() && m < kMaxMethods; ++m) {
            const FunctionInfo& fn = c->methods[m];
            std::string line = std::string(1, accessSymbol(fn.access)) + " ";
            if (fn.isStatic) line += "static ";
            if (fn.isVirtual) line += "virtual ";
            line += fn.name + "(" + shorten(fn.paramText, 20) + ")";
            if (!fn.returnType.empty()) line += " : " + fn.returnType;
            if (fn.isPureVirtual) line += " = 0";
            n.methods.push_back(shorten(line, 44));
        }
        if (c->methods.size() > kMaxMethods) n.methods.push_back("  ... " + intToString(static_cast<int>(c->methods.size() - kMaxMethods)) + " more");
        nodes.push_back(n);
    }

    // edges
    for (size_t i = 0; i < defs.size(); ++i) {
        ClassInfo* c = defs[i];
        for (size_t b = 0; b < c->bases.size(); ++b) {
            DiagramEdge e;
            e.kind = DiagramEdge::Inherit;
            e.from = i;
            e.isVirtual = c->bases[b].isVirtual;
            e.access = c->bases[b].access;
            ClassInfo* base = c->bases[b].resolved;
            size_t to = std::string::npos;
            if (base) for (size_t k = 0; k < defs.size(); ++k) if (defs[k] == base) { to = k; break; }
            if (to == std::string::npos) to = externalNode(stripTemplateArgs(c->bases[b].name));
            e.to = to;
            if (e.to != e.from) edges.push_back(e);
        }
        for (size_t a = 0; a < c->associations.size(); ++a) {
            size_t to = std::string::npos;
            for (size_t k = 0; k < defs.size(); ++k) if (defs[k] == c->associations[a]) { to = k; break; }
            if (to == std::string::npos || to == i) continue;
            DiagramEdge e;
            e.kind = DiagramEdge::Association;
            e.from = i; e.to = to;
            edges.push_back(e);
        }
        if (c->owner) {
            size_t to = std::string::npos;
            for (size_t k = 0; k < defs.size(); ++k) if (defs[k] == c->owner) { to = k; break; }
            if (to != std::string::npos && to != i) {
                DiagramEdge e;
                e.kind = DiagramEdge::Nested;
                e.from = i; e.to = to;
                edges.push_back(e);
            }
        }
    }

    // box sizes
    for (size_t i = 0; i < nodes.size(); ++i) {
        DiagramNode& n = nodes[i];
        size_t longest = n.title.size() + 2;
        if (n.subtitle.size() > longest) longest = n.subtitle.size();
        for (size_t k = 0; k < n.fields.size(); ++k) if (n.fields[k].size() > longest) longest = n.fields[k].size();
        for (size_t k = 0; k < n.methods.size(); ++k) if (n.methods[k].size() > longest) longest = n.methods[k].size();
        n.w = static_cast<double>(longest) * kCharW + 2 * kPadX;
        if (n.w < 120) n.w = 120;
        n.titleHeight = kTitleH + (n.subtitle.empty() ? 0 : kLineH);
        double body = 0;
        if (!n.external) {
            body += kPadY + static_cast<double>(n.fields.empty() ? 1 : n.fields.size()) * kLineH + kPadY;
            body += kPadY + static_cast<double>(n.methods.empty() ? 1 : n.methods.size()) * kLineH + kPadY;
        }
        n.h = n.titleHeight + body;
    }

    computeDepths();
    placeNodes();
    routeEdges();
}

void DiagramLayout::computeDepths()
{
    for (size_t i = 0; i < nodes.size(); ++i) nodes[i].depth = 0;
    // relax: derived below base, nested below owner; bounded to avoid cycles
    for (size_t pass = 0; pass < nodes.size() + 1; ++pass) {
        bool changed = false;
        for (size_t e = 0; e < edges.size(); ++e) {
            const DiagramEdge& ed = edges[e];
            if (ed.kind == DiagramEdge::Association) continue;
            int want = nodes[ed.to].depth + 1;
            if (nodes[ed.from].depth < want && want < 64) { nodes[ed.from].depth = want; changed = true; }
        }
        if (!changed) break;
    }
}

void DiagramLayout::placeNodes()
{
    int maxDepth = 0;
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].depth > maxDepth) maxDepth = nodes[i].depth;

    std::vector<std::vector<size_t> > rows(static_cast<size_t>(maxDepth) + 1);
    for (size_t i = 0; i < nodes.size(); ++i) rows[static_cast<size_t>(nodes[i].depth)].push_back(i);

    double y = kMargin;
    double maxWidth = 0;
    std::vector<double> rowWidth(rows.size(), 0.0);
    for (size_t r = 0; r < rows.size(); ++r) {
        std::vector<size_t>& row = rows[r];
        if (r == 0) {
            ByName cmp; cmp.nodes = &nodes;
            std::sort(row.begin(), row.end(), cmp);
        } else {
            // barycenter of the parents (bases / owner) already placed above
            std::vector<double> keys(nodes.size(), 0.0);
            for (size_t k = 0; k < row.size(); ++k) {
                double sum = 0; int cnt = 0;
                for (size_t e = 0; e < edges.size(); ++e) {
                    if (edges[e].from != row[k] || edges[e].kind == DiagramEdge::Association) continue;
                    const DiagramNode& p = nodes[edges[e].to];
                    if (p.depth < static_cast<int>(r)) { sum += p.x + p.w / 2; ++cnt; }
                }
                keys[row[k]] = cnt ? sum / cnt : 1e9;
            }
            ByKey cmp; cmp.keys = &keys; cmp.nodes = &nodes;
            std::sort(row.begin(), row.end(), cmp);
        }
        double x = kMargin;
        double rowH = 0;
        for (size_t k = 0; k < row.size(); ++k) {
            DiagramNode& n = nodes[row[k]];
            n.x = x; n.y = y;
            x += n.w + kGapX;
            if (n.h > rowH) rowH = n.h;
        }
        rowWidth[r] = x - kGapX + kMargin;
        if (rowWidth[r] > maxWidth) maxWidth = rowWidth[r];
        y += rowH + kGapY;
    }
    // centre each row
    for (size_t r = 0; r < rows.size(); ++r) {
        double shift = (maxWidth - rowWidth[r]) / 2;
        for (size_t k = 0; k < rows[r].size(); ++k) nodes[rows[r][k]].x += shift;
    }
    width = maxWidth;
    height = y - kGapY + kMargin;
    if (nodes.empty()) { width = 300; height = 80; }
}

namespace {

// point where the segment from the box centre towards 'toward' leaves the box
DiagramPoint borderPoint(const DiagramNode& n, const DiagramPoint& toward)
{
    double cx = n.x + n.w / 2, cy = n.y + n.h / 2;
    double dx = toward.x - cx, dy = toward.y - cy;
    double t = 1.0;
    if (dx != 0) t = std::min(t, (n.w / 2) / std::fabs(dx));
    if (dy != 0) t = std::min(t, (n.h / 2) / std::fabs(dy));
    if (dx == 0 && dy == 0) t = 0;
    return DiagramPoint(cx + dx * t, cy + dy * t);
}

void arrowHead(DiagramEdge& e, double size)
{
    size_t n = e.points.size();
    if (n < 2) return;
    DiagramPoint tip = e.points[n - 1], prev = e.points[n - 2];
    double dx = tip.x - prev.x, dy = tip.y - prev.y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) { dx = 0; dy = -1; len = 1; }
    dx /= len; dy /= len;
    double bx = tip.x - dx * size, by = tip.y - dy * size;
    double px = -dy, py = dx;
    e.arrow[0] = tip;
    e.arrow[1] = DiagramPoint(bx + px * size * 0.55, by + py * size * 0.55);
    e.arrow[2] = DiagramPoint(bx - px * size * 0.55, by - py * size * 0.55);
    // shorten the line so it ends at the base of the head
    e.points[n - 1] = DiagramPoint(bx, by);
}

} // namespace

void DiagramLayout::routeEdges()
{
    // spread multiple inheritance edges leaving the same node a little
    std::vector<int> outCount(nodes.size(), 0), outSeen(nodes.size(), 0);
    for (size_t e = 0; e < edges.size(); ++e) if (edges[e].kind == DiagramEdge::Inherit) outCount[edges[e].from]++;

    for (size_t e = 0; e < edges.size(); ++e) {
        DiagramEdge& ed = edges[e];
        const DiagramNode& a = nodes[ed.from];
        const DiagramNode& b = nodes[ed.to];
        ed.points.clear();
        if (ed.kind == DiagramEdge::Inherit && b.y + b.h <= a.y) {
            int k = outSeen[ed.from]++;
            double spread = outCount[ed.from] > 1 ? (static_cast<double>(k) - (outCount[ed.from] - 1) / 2.0) * 14.0 : 0.0;
            double x1 = a.x + a.w / 2 + spread, y1 = a.y;
            double x2 = b.x + b.w / 2, y2 = b.y + b.h;
            double ym = y2 + (y1 - y2) / 2 + (static_cast<double>(e % 5) - 2) * 3.0;
            ed.points.push_back(DiagramPoint(x1, y1));
            ed.points.push_back(DiagramPoint(x1, ym));
            ed.points.push_back(DiagramPoint(x2, ym));
            ed.points.push_back(DiagramPoint(x2, y2));
            arrowHead(ed, 12.0);
        } else {
            DiagramPoint ca(a.x + a.w / 2, a.y + a.h / 2), cb(b.x + b.w / 2, b.y + b.h / 2);
            ed.points.push_back(borderPoint(a, cb));
            ed.points.push_back(borderPoint(b, ca));
            arrowHead(ed, ed.kind == DiagramEdge::Inherit ? 12.0 : 9.0);
        }
    }
}

// ---- SVG ------------------------------------------------------------------------

std::string renderSvg(const DiagramLayout& L, const std::string& title, bool standalone)
{
    std::ostringstream s;
    double legendH = 44;
    double W = L.width, H = L.height + legendH;
    if (standalone) s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << fmt(W) << "\" height=\"" << fmt(H)
      << "\" viewBox=\"0 0 " << fmt(W) << " " << fmt(H) << "\" font-family=\"Consolas, 'Courier New', monospace\" font-size=\"11\">\n";
    s << "<title>" << xmlEscape(title) << "</title>\n";
    s << "<rect x=\"0\" y=\"0\" width=\"" << fmt(W) << "\" height=\"" << fmt(H) << "\" fill=\"#ffffff\"/>\n";

    // legend
    s << "<g transform=\"translate(" << fmt(kMargin) << ",8)\" font-size=\"11\">\n";
    s << "<text x=\"0\" y=\"12\" font-weight=\"bold\" font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"14\">" << xmlEscape(title) << "</text>\n";
    s << "<line x1=\"0\" y1=\"30\" x2=\"40\" y2=\"30\" stroke=\"#222\" stroke-width=\"1.5\"/><polygon points=\"40,30 30,25 30,35\" fill=\"#fff\" stroke=\"#222\"/><text x=\"48\" y=\"34\">inherits</text>\n";
    s << "<line x1=\"120\" y1=\"30\" x2=\"160\" y2=\"30\" stroke=\"#1a6fb5\" stroke-dasharray=\"5,3\"/><polyline points=\"150,25 160,30 150,35\" fill=\"none\" stroke=\"#1a6fb5\"/><text x=\"168\" y=\"34\">has member of type</text>\n";
    s << "<line x1=\"310\" y1=\"30\" x2=\"350\" y2=\"30\" stroke=\"#888\" stroke-dasharray=\"2,3\"/><text x=\"358\" y=\"34\">nested in</text>\n";
    s << "<rect x=\"440\" y=\"23\" width=\"14\" height=\"14\" fill=\"#fff6d5\" stroke=\"#333\"/><text x=\"460\" y=\"34\">abstract</text>\n";
    s << "<rect x=\"540\" y=\"23\" width=\"14\" height=\"14\" fill=\"#eeeeee\" stroke=\"#999\" stroke-dasharray=\"3,2\"/><text x=\"560\" y=\"34\">not defined in project</text>\n";
    s << "</g>\n";

    s << "<g transform=\"translate(0," << fmt(legendH) << ")\">\n";
    // edges first
    for (size_t e = 0; e < L.edges.size(); ++e) {
        const DiagramEdge& ed = L.edges[e];
        std::string stroke = "#222", dash;
        if (ed.kind == DiagramEdge::Association) { stroke = "#1a6fb5"; dash = " stroke-dasharray=\"5,3\""; }
        if (ed.kind == DiagramEdge::Nested) { stroke = "#888"; dash = " stroke-dasharray=\"2,3\""; }
        if (ed.kind == DiagramEdge::Inherit && ed.access == AccessPrivate) stroke = "#b03030";
        if (ed.kind == DiagramEdge::Inherit && ed.access == AccessProtected) stroke = "#c07a00";
        s << "<polyline fill=\"none\" stroke=\"" << stroke << "\" stroke-width=\"" << (ed.kind == DiagramEdge::Inherit ? "1.5" : "1") << "\"" << dash << " points=\"";
        for (size_t p = 0; p < ed.points.size(); ++p) s << (p ? " " : "") << fmt(ed.points[p].x) << "," << fmt(ed.points[p].y);
        s << "\"/>\n";
        if (ed.kind == DiagramEdge::Inherit) {
            s << "<polygon fill=\"#fff\" stroke=\"" << stroke << "\" stroke-width=\"1.5\" points=\""
              << fmt(ed.arrow[0].x) << "," << fmt(ed.arrow[0].y) << " " << fmt(ed.arrow[1].x) << "," << fmt(ed.arrow[1].y) << " "
              << fmt(ed.arrow[2].x) << "," << fmt(ed.arrow[2].y) << "\"/>\n";
            if (ed.isVirtual && ed.points.size() >= 2) {
                const DiagramPoint& p = ed.points[ed.points.size() / 2];
                s << "<text x=\"" << fmt(p.x + 4) << "\" y=\"" << fmt(p.y - 3) << "\" font-size=\"9\" fill=\"" << stroke << "\">virtual</text>\n";
            }
        } else if (ed.kind == DiagramEdge::Association) {
            s << "<polyline fill=\"none\" stroke=\"" << stroke << "\" points=\""
              << fmt(ed.arrow[1].x) << "," << fmt(ed.arrow[1].y) << " " << fmt(ed.arrow[0].x) << "," << fmt(ed.arrow[0].y) << " "
              << fmt(ed.arrow[2].x) << "," << fmt(ed.arrow[2].y) << "\"/>\n";
        }
    }
    // nodes
    for (size_t i = 0; i < L.nodes.size(); ++i) {
        const DiagramNode& n = L.nodes[i];
        std::string fill = n.external ? "#eeeeee" : (n.abstract ? "#fff6d5" : "#f4f8ff");
        std::string stroke = n.external ? "#999" : "#333";
        s << "<g>\n<title>" << xmlEscape(n.title) << "</title>\n";
        s << "<rect x=\"" << fmt(n.x) << "\" y=\"" << fmt(n.y) << "\" width=\"" << fmt(n.w) << "\" height=\"" << fmt(n.h)
          << "\" rx=\"3\" fill=\"" << fill << "\" stroke=\"" << stroke << "\"" << (n.external ? " stroke-dasharray=\"3,2\"" : "") << "/>\n";
        s << "<text x=\"" << fmt(n.x + n.w / 2) << "\" y=\"" << fmt(n.y + 15) << "\" text-anchor=\"middle\" font-weight=\"bold\" font-size=\"12\""
          << (n.abstract ? " font-style=\"italic\"" : "") << ">" << xmlEscape(n.title) << "</text>\n";
        if (!n.subtitle.empty())
            s << "<text x=\"" << fmt(n.x + n.w / 2) << "\" y=\"" << fmt(n.y + 15 + kLineH) << "\" text-anchor=\"middle\" font-size=\"10\" fill=\"#555\">" << xmlEscape(n.subtitle) << "</text>\n";
        if (!n.external) {
            double y = n.y + n.titleHeight;
            s << "<line x1=\"" << fmt(n.x) << "\" y1=\"" << fmt(y) << "\" x2=\"" << fmt(n.x + n.w) << "\" y2=\"" << fmt(y) << "\" stroke=\"" << stroke << "\"/>\n";
            y += kPadY;
            if (n.fields.empty()) y += kLineH;
            for (size_t k = 0; k < n.fields.size(); ++k) {
                y += kLineH;
                s << "<text x=\"" << fmt(n.x + kPadX) << "\" y=\"" << fmt(y - 4) << "\">" << xmlEscape(n.fields[k]) << "</text>\n";
            }
            y += kPadY;
            s << "<line x1=\"" << fmt(n.x) << "\" y1=\"" << fmt(y) << "\" x2=\"" << fmt(n.x + n.w) << "\" y2=\"" << fmt(y) << "\" stroke=\"" << stroke << "\"/>\n";
            y += kPadY;
            if (n.methods.empty()) y += kLineH;
            for (size_t k = 0; k < n.methods.size(); ++k) {
                y += kLineH;
                s << "<text x=\"" << fmt(n.x + kPadX) << "\" y=\"" << fmt(y - 4) << "\">" << xmlEscape(n.methods[k]) << "</text>\n";
            }
        }
        s << "</g>\n";
    }
    s << "</g>\n</svg>\n";
    return s.str();
}

// ---- DOT ------------------------------------------------------------------------

namespace {

std::string dotEscape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' || c == '\\' || c == '{' || c == '}' || c == '|' || c == '<' || c == '>') { out += '\\'; out += c; }
        else out += c;
    }
    return out;
}

std::string dotId(const ClassInfo* c)
{
    return "\"" + replaceAll(c->qualifiedName + c->tmpl.specializationArgs, "\"", "'") + "\"";
}

} // namespace

std::string renderDot(const CodeBase& cb)
{
    std::ostringstream s;
    s << "// Generated by cppparse. Render with: dot -Tpng -o diagram.png diagram.dot\n";
    s << "digraph \"" << dotEscape(cb.title()) << "\" {\n";
    s << "  rankdir=BT;\n  node [shape=record, fontname=\"Consolas\", fontsize=10];\n  edge [fontname=\"Consolas\", fontsize=9];\n";
    const std::vector<ClassInfo*>& classes = cb.classes();
    std::vector<std::string> externals;
    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo* c = classes[i];
        if (!c->isDefinition) continue;
        std::string label = "{" + dotEscape(std::string(ClassInfo::kindName(c->kind)) + " " + c->qualifiedName + c->tmpl.specializationArgs);
        if (c->tmpl.isTemplate) label += "\\n" + dotEscape(c->tmpl.text());
        label += "|";
        for (size_t f = 0; f < c->fields.size(); ++f)
            label += dotEscape(std::string(1, accessSymbol(c->fields[f].access)) + " " + c->fields[f].name + " : " + c->fields[f].type) + "\\l";
        label += "|";
        for (size_t m = 0; m < c->methods.size(); ++m) {
            const FunctionInfo& fn = c->methods[m];
            std::string line = std::string(1, accessSymbol(fn.access)) + " " + (fn.isVirtual ? "virtual " : "") + fn.name + "(" + fn.paramText + ")";
            if (!fn.returnType.empty()) line += " : " + fn.returnType;
            if (fn.isPureVirtual) line += " = 0";
            label += dotEscape(line) + "\\l";
        }
        label += "}";
        s << "  " << dotId(c) << " [label=\"" << label << "\"" << (c->isAbstract ? ", style=filled, fillcolor=\"#fff6d5\"" : "") << "];\n";
    }
    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo* c = classes[i];
        if (!c->isDefinition) continue;
        for (size_t b = 0; b < c->bases.size(); ++b) {
            std::string target;
            if (c->bases[b].resolved) target = dotId(c->bases[b].resolved);
            else {
                std::string nm = stripTemplateArgs(c->bases[b].name);
                target = "\"" + dotEscape(nm) + "\"";
                if (!contains(externals, nm)) {
                    externals.push_back(nm);
                    s << "  " << target << " [shape=box, style=\"dashed,filled\", fillcolor=\"#eeeeee\", label=\"" << dotEscape(nm) << "\"];\n";
                }
            }
            std::string color = c->bases[b].access == AccessPrivate ? "#b03030" : (c->bases[b].access == AccessProtected ? "#c07a00" : "#222222");
            s << "  " << dotId(c) << " -> " << target << " [arrowhead=empty, color=\"" << color << "\""
              << (c->bases[b].isVirtual ? ", label=\"virtual\"" : "") << "];\n";
        }
        for (size_t a = 0; a < c->associations.size(); ++a)
            s << "  " << dotId(c) << " -> " << dotId(c->associations[a]) << " [style=dashed, arrowhead=open, color=\"#1a6fb5\"];\n";
        if (c->owner && c->owner->isDefinition)
            s << "  " << dotId(c) << " -> " << dotId(c->owner) << " [style=dotted, arrowhead=none, color=\"#888888\", label=\"nested\"];\n";
    }
    s << "}\n";
    return s.str();
}

} // namespace cppparse
