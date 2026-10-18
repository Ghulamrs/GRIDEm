// Pdf.hpp - minimal PDF writer: paginated text report plus the class
// diagram drawn as vector graphics. Uses only the standard PDF base fonts,
// so no font files or libraries are needed.
// Strict C++98.
#ifndef CPPPARSE_PDF_HPP
#define CPPPARSE_PDF_HPP

#include "Diagram.hpp"

#include <string>

namespace cppparse {

std::string renderPdfReport(const CodeBase& cb, const DiagramLayout& layout, const std::string& textReport);

} // namespace cppparse

#endif
