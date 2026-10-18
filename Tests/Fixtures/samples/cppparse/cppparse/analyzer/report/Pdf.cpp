// Pdf.cpp - minimal PDF 1.4 writer for the report and the class diagram.
#include "Pdf.hpp"
#include "PathUtil.hpp"
#include "Report.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace cppparse {

namespace {

const double kPageW = 595.0;   // A4 portrait, points
const double kPageH = 842.0;
const double kMargin = 40.0;
const double kTextSize = 8.0;
const double kLeading = 10.0;
const size_t kColumns = 107;   // 515pt / (0.6 * 8pt)

std::string num(double v)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << v;
    return oss.str();
}

std::string pdfEscape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '(' || c == ')' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32 || c > 126) out += '?';
        else out += static_cast<char>(c);
    }
    return out;
}

std::string textAt(const char* font, double size, double x, double y, const std::string& s)
{
    return "BT /" + std::string(font) + " " + num(size) + " Tf 1 0 0 1 " + num(x) + " " + num(y) + " Tm (" + pdfEscape(s) + ") Tj ET\n";
}

class PdfBuilder {
public:
    void addPage(const std::string& content) { m_pages.push_back(content); }

    std::string build() const
    {
        std::vector<std::string> objs;
        objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");
        std::string kids = "[";
        for (size_t i = 0; i < m_pages.size(); ++i) kids += intToString(static_cast<int>(7 + 2 * i)) + " 0 R ";
        kids += "]";
        objs.push_back("<< /Type /Pages /Kids " + kids + " /Count " + intToString(static_cast<int>(m_pages.size())) + " >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Bold >>");
        for (size_t i = 0; i < m_pages.size(); ++i) {
            objs.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(kPageW) + " " + num(kPageH) +
                           "] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents " +
                           intToString(static_cast<int>(8 + 2 * i)) + " 0 R >>");
            objs.push_back("<< /Length " + intToString(static_cast<int>(m_pages[i].size())) + " >>\nstream\n" + m_pages[i] + "\nendstream");
        }

        std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<size_t> offsets;
        for (size_t i = 0; i < objs.size(); ++i) {
            offsets.push_back(out.size());
            out += intToString(static_cast<int>(i + 1)) + " 0 obj\n" + objs[i] + "\nendobj\n";
        }
        size_t xref = out.size();
        out += "xref\n0 " + intToString(static_cast<int>(objs.size() + 1)) + "\n0000000000 65535 f \n";
        for (size_t i = 0; i < offsets.size(); ++i) {
            std::string digits = intToString(static_cast<int>(offsets[i]));
            if (digits.size() < 10) digits = std::string(10 - digits.size(), '0') + digits;
            out += digits + " 00000 n \n";
        }
        out += "trailer\n<< /Size " + intToString(static_cast<int>(objs.size() + 1)) + " /Root 1 0 R >>\nstartxref\n" +
               intToString(static_cast<int>(xref)) + "\n%%EOF\n";
        return out;
    }

private:
    std::vector<std::string> m_pages;
};

std::string footer(const std::string& title, int page, int total)
{
    return textAt("F2", 8, kMargin, 25, title + "  -  page " + intToString(page) + " of " + intToString(total)) +
           "0.6 G " + num(kMargin) + " 34 m " + num(kPageW - kMargin) + " 34 l S\n";
}

void wrapLines(const std::string& text, std::vector<std::string>& out)
{
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        // expand tabs
        std::string e;
        for (size_t i = 0; i < line.size(); ++i) { if (line[i] == '\t') e += "    "; else e += line[i]; }
        if (e.size() <= kColumns) { out.push_back(e); continue; }
        size_t indent = 0;
        while (indent < e.size() && e[indent] == ' ') ++indent;
        std::string pad(indent + 2 < 20 ? indent + 2 : 20, ' ');
        out.push_back(e.substr(0, kColumns));
        size_t pos = kColumns;
        while (pos < e.size()) {
            std::string chunk = pad + e.substr(pos, kColumns - pad.size());
            out.push_back(chunk);
            pos += kColumns - pad.size();
        }
    }
}

// Draws the diagram scaled by 'scale' with the layout origin mapped to
// (offX, offY) in PDF coordinates (PDF y grows upwards, layout y downwards).
std::string drawDiagram(const DiagramLayout& L, double scale, double offX, double offY)
{
    std::ostringstream s;
    for (size_t e = 0; e < L.edges.size(); ++e) {
        const DiagramEdge& ed = L.edges[e];
        if (ed.points.size() < 2) continue;
        if (ed.kind == DiagramEdge::Association) s << "0.10 0.44 0.71 RG [4 3] 0 d 0.8 w\n";
        else if (ed.kind == DiagramEdge::Nested) s << "0.53 G [1.5 2.5] 0 d 0.8 w\n";
        else if (ed.access == AccessPrivate) s << "0.69 0.19 0.19 RG [] 0 d 1.2 w\n";
        else if (ed.access == AccessProtected) s << "0.75 0.48 0 RG [] 0 d 1.2 w\n";
        else s << "0.13 G [] 0 d 1.2 w\n";
        for (size_t p = 0; p < ed.points.size(); ++p)
            s << num(offX + ed.points[p].x * scale) << " " << num(offY - ed.points[p].y * scale) << (p ? " l" : " m") << "\n";
        s << "S\n[] 0 d\n";
        if (ed.kind == DiagramEdge::Inherit) {
            s << "1 g " << num(offX + ed.arrow[0].x * scale) << " " << num(offY - ed.arrow[0].y * scale) << " m "
              << num(offX + ed.arrow[1].x * scale) << " " << num(offY - ed.arrow[1].y * scale) << " l "
              << num(offX + ed.arrow[2].x * scale) << " " << num(offY - ed.arrow[2].y * scale) << " l h B\n";
            if (ed.isVirtual) {
                const DiagramPoint& p = ed.points[ed.points.size() / 2];
                s << "0.13 g " << textAt("F2", 7 * scale, offX + (p.x + 4) * scale, offY - (p.y - 3) * scale, "virtual");
            }
        } else if (ed.kind == DiagramEdge::Association) {
            s << num(offX + ed.arrow[1].x * scale) << " " << num(offY - ed.arrow[1].y * scale) << " m "
              << num(offX + ed.arrow[0].x * scale) << " " << num(offY - ed.arrow[0].y * scale) << " l "
              << num(offX + ed.arrow[2].x * scale) << " " << num(offY - ed.arrow[2].y * scale) << " l S\n";
        }
    }
    s << "[] 0 d 1 w\n";
    for (size_t i = 0; i < L.nodes.size(); ++i) {
        const DiagramNode& n = L.nodes[i];
        double x = offX + n.x * scale, yTop = offY - n.y * scale, w = n.w * scale, hgt = n.h * scale;
        if (n.external) s << "0.93 g 0.6 G [2 2] 0 d\n";
        else if (n.abstract) s << "1 0.965 0.835 rg 0.2 G [] 0 d\n";
        else s << "0.957 0.973 1 rg 0.2 G [] 0 d\n";
        s << num(x) << " " << num(yTop - hgt) << " " << num(w) << " " << num(hgt) << " re B\n[] 0 d 0 g\n";
        double titleSize = 11 * scale;
        double tw = static_cast<double>(n.title.size()) * 0.6 * titleSize;
        s << textAt("F4", titleSize, x + (w - tw) / 2, yTop - 14 * scale, n.title);
        if (!n.subtitle.empty()) {
            double sw = static_cast<double>(n.subtitle.size()) * 0.6 * 9 * scale;
            s << "0.33 g " << textAt("F1", 9 * scale, x + (w - sw) / 2, yTop - (14 + 15) * scale, n.subtitle) << "0 g\n";
        }
        if (n.external) continue;
        double y = n.y + n.titleHeight;
        s << "0.2 G " << num(x) << " " << num(offY - y * scale) << " m " << num(x + w) << " " << num(offY - y * scale) << " l S\n";
        y += 6;
        if (n.fields.empty()) y += 15;
        for (size_t k = 0; k < n.fields.size(); ++k) { y += 15; s << textAt("F1", 10 * scale, x + 8 * scale, offY - (y - 4) * scale, n.fields[k]); }
        y += 6;
        s << num(x) << " " << num(offY - y * scale) << " m " << num(x + w) << " " << num(offY - y * scale) << " l S\n";
        y += 6;
        if (n.methods.empty()) y += 15;
        for (size_t k = 0; k < n.methods.size(); ++k) { y += 15; s << textAt("F1", 10 * scale, x + 8 * scale, offY - (y - 4) * scale, n.methods[k]); }
    }
    return s.str();
}

} // namespace

std::string renderPdfReport(const CodeBase& cb, const DiagramLayout& layout, const std::string& textReport)
{
    PdfBuilder pdf;
    std::vector<std::string> pages;
    const std::string title = cb.title() + " - cppparse report";

    // ---- title page
    {
        std::ostringstream s;
        s << textAt("F3", 20, kMargin, kPageH - 90, cb.title());
        s << textAt("F2", 12, kMargin, kPageH - 110, "C++ project report generated by cppparse");
        double y = kPageH - 150;
        for (size_t i = 0; i < cb.inputs().size(); ++i) { s << textAt("F1", 9, kMargin, y, "input: " + cb.inputs()[i]); y -= 12; }
        y -= 10;
        s << textAt("F3", 12, kMargin, y, "Totals"); y -= 18;
        Totals t; computeTotals(cb, t);
        std::vector<std::string> lines; totalsLines(t, lines);
        std::vector<std::string> wrapped;
        for (size_t i = 0; i < lines.size(); ++i) wrapLines(lines[i], wrapped);
        for (size_t i = 0; i < wrapped.size(); ++i) { s << textAt("F1", 8, kMargin, y, wrapped[i]); y -= 11; }
        y -= 10;
        s << textAt("F3", 12, kMargin, y, "Contents"); y -= 18;
        const char* toc[] = { "Class diagram (following pages)", "1. Project", "2. Files and preprocessor directives", "3. Classes: methods, fields, local variables",
                              "4. Inheritance tree", "5. Global definitions, macros, namespaces", "6. Exception handling", "7. Health findings", "8. Totals", 0 };
        for (int i = 0; toc[i]; ++i) { s << textAt("F2", 10, kMargin + 10, y, toc[i]); y -= 14; }
        pages.push_back(s.str());
    }

    // ---- diagram pages (tiled when larger than a page)
    {
        const double areaW = kPageW - 2 * kMargin;
        const double areaTop = kPageH - 70;
        const double areaH = areaTop - 45;
        double scale = areaW / (layout.width > 1 ? layout.width : 1);
        if (scale > 1.0) scale = 1.0;
        if (scale < 0.5) scale = 0.5;
        double totalH = (layout.height + 10) * scale, totalW = layout.width * scale;
        int tilesX = static_cast<int>(std::ceil(totalW / areaW));
        int tilesY = static_cast<int>(std::ceil(totalH / areaH));
        if (tilesX < 1) tilesX = 1;
        if (tilesY < 1) tilesY = 1;
        for (int ty = 0; ty < tilesY; ++ty) {
            for (int tx = 0; tx < tilesX; ++tx) {
                std::ostringstream s;
                std::string head = "Class diagram";
                if (tilesX * tilesY > 1) head += " (part " + intToString(ty + 1) + "/" + intToString(tilesY) + " down, " + intToString(tx + 1) + "/" + intToString(tilesX) + " across)";
                s << textAt("F3", 13, kMargin, kPageH - 50, head);
                s << textAt("F2", 8, kMargin, kPageH - 62, "solid + hollow triangle: inherits    dashed blue: has member of type    dotted grey: nested in    yellow box: abstract    grey box: not defined in project");
                s << "q " << num(kMargin) << " " << num(areaTop - areaH) << " " << num(areaW) << " " << num(areaH) << " re W n\n";
                s << drawDiagram(layout, scale, kMargin - tx * areaW, areaTop + ty * areaH);
                s << "Q\n";
                pages.push_back(s.str());
            }
        }
    }

    // ---- text pages
    {
        std::vector<std::string> lines;
        wrapLines(textReport, lines);
        const size_t perPage = static_cast<size_t>((kPageH - kMargin - 45) / kLeading);
        for (size_t start = 0; start < lines.size(); start += perPage) {
            std::ostringstream s;
            double y = kPageH - kMargin;
            for (size_t i = start; i < lines.size() && i < start + perPage; ++i) {
                if (!lines[i].empty()) s << textAt("F1", kTextSize, kMargin, y, lines[i]);
                y -= kLeading;
            }
            pages.push_back(s.str());
        }
        if (lines.empty()) pages.push_back("");
    }

    for (size_t i = 0; i < pages.size(); ++i)
        pdf.addPage(pages[i] + footer(title, static_cast<int>(i + 1), static_cast<int>(pages.size())));
    return pdf.build();
}

} // namespace cppparse
