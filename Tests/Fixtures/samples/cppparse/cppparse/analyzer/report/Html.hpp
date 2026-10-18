// Html.hpp - self-contained HTML report (inline CSS, inline SVG diagram).
// Strict C++98.
#ifndef CPPPARSE_HTML_HPP
#define CPPPARSE_HTML_HPP

#include "Diagram.hpp"
#include "Report.hpp"

#include <string>

namespace cppparse {

std::string renderHtmlReport(const CodeBase& cb, const DiagramLayout& layout, const ReportOptions& opt);

} // namespace cppparse

#endif
