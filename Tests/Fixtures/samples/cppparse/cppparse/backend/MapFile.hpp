// MapFile.hpp - the object map: the layout of the assembled object in the form of a Visual Studio
// linker map (link /MAP), with the real size of every symbol.
//
//   time stamp         when the map was written, as link /MAP prints it
//   section table      Start (section:offset), Length, Name, Class
//   Publics by Value   Address, Size, Flags (link's: f function, i inline), Type, Symbol (as written to the
//                      object), readable name
//   Static symbols     the same for internal linkage
//   entry point
//   Symbols by Name    every symbol again, sorted by readable name, with its linkage (public / static)
//
// Addresses are section:offset inside the object (a linker map adds the image base after linking);
// sizes are the bytes the encoder produced for each definition, without alignment padding.
//
// Strict C++98.
#ifndef CPPPARSE_MAPFILE_HPP
#define CPPPARSE_MAPFILE_HPP

#include "Coff.hpp"

#include <map>
#include <string>

namespace cppparse {

// title: the module name on the first line; object: what the layout belongs to ("Sample.obj", or a note
// that the object was assembled in memory only); names: symbol -> readable name
std::string renderObjectMap(const std::string& title, const std::string& object, const CoffResult& layout,
                            const std::map<std::string, std::string>& names);

} // namespace cppparse

#endif
