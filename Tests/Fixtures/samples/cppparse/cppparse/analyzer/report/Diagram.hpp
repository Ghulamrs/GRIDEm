// Diagram.hpp - class diagram layout plus SVG and Graphviz DOT renderers.
//
// The layout places every class definition in a box; boxes are arranged in
// rows by inheritance depth (roots at the top). Edges:
//   solid line, hollow triangle   inheritance (derived -> base)
//   dashed line, open arrow       association (class has a field of that type)
//   dotted line                   nesting (inner class -> enclosing class)
// Base classes that are not defined in the parsed files (std::exception,
// CObject, ...) are drawn as grey "external" boxes.
//
// The same geometry is used by the SVG, HTML and PDF outputs.
// Strict C++98.
#ifndef CPPPARSE_DIAGRAM_HPP
#define CPPPARSE_DIAGRAM_HPP

#include "CodeBase.hpp"

#include <string>
#include <vector>

namespace cppparse {

struct DiagramPoint { double x, y; DiagramPoint() : x(0), y(0) {} DiagramPoint(double px, double py) : x(px), y(py) {} };

struct DiagramNode {
    ClassInfo*               cls;        // NULL for an external (undefined) class
    std::string              title;      // "class geom::Circle"
    std::string              subtitle;   // "template <typename T>" or ""
    std::vector<std::string> fields;     // "- m_radius : double"
    std::vector<std::string> methods;    // "+ area() : double"
    double                   x, y, w, h;
    double                   titleHeight;
    int                      depth;
    bool                     external;
    bool                     abstract;
    DiagramNode();
};

struct DiagramEdge {
    enum Kind { Inherit, Association, Nested };
    Kind                      kind;
    size_t                    from, to;   // node indices
    bool                      isVirtual;
    Access                    access;
    std::vector<DiagramPoint> points;     // polyline from 'from' to 'to'
    DiagramPoint              arrow[3];   // arrow head triangle at the 'to' end
    DiagramEdge();
};

struct DiagramLayout {
    std::vector<DiagramNode> nodes;
    std::vector<DiagramEdge> edges;
    double width, height;
    DiagramLayout() : width(0), height(0) {}
    void build(const CodeBase& cb);
private:
    size_t externalNode(const std::string& name);
    void   computeDepths();
    void   placeNodes();
    void   routeEdges();
};

std::string renderSvg(const DiagramLayout& layout, const std::string& title, bool standalone);
std::string renderDot(const CodeBase& cb);

} // namespace cppparse

#endif
