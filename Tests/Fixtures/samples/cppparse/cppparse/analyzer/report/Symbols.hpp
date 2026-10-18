// Symbols.hpp - symbol table export.
//
// Every named entity the parser found (namespaces, classes, methods, fields,
// free functions, globals, parameters, locals, typedefs, enums, enumerators,
// macros) as one flat table with kind, qualified name, type or signature,
// scope, storage, access, location and the declaration/definition link.
//
//   <name>-symbols.json   for tools (one object per symbol)
//   <name>-symbols.txt    tab-separated, one line per symbol
//
// Strict C++98.
#ifndef CPPPARSE_SYMBOLS_HPP
#define CPPPARSE_SYMBOLS_HPP

#include "CodeBase.hpp"

#include <string>

namespace cppparse {

std::string renderSymbolsJson(const CodeBase& cb);
std::string renderSymbolsText(const CodeBase& cb);
// linker-style map file: modules, symbols per module with module:ordinal
// references, symbols sorted by name, cross reference (who uses what),
// unreferenced symbols, entry point
std::string renderSymbolsMap(const CodeBase& cb);
int         countSymbols(const CodeBase& cb);

} // namespace cppparse

#endif
