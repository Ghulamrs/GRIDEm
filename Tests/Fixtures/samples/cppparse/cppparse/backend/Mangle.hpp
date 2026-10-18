// Mangle.hpp - version 5: Microsoft Visual C++ compatible name mangling (x64).
//
// Produces the decorated names cl.exe would give the same declarations, so
// that objects written by cppparse link against code compiled by MSVC with
// ordinary C++ declarations (no extern "C" needed):
//
//   int square(int)                   ?square@@YAHH@Z
//   int Point::sum() const            ?sum@Point@@QEBAHXZ
//   Point::Point(int, int)            ??0Point@@QEAA@HH@Z
//   void Buffer<int,4>::add(const int&)  ?add@?$Buffer@H$03@@QEAAXAEBH@Z
//   int maxOf<int>(const int&, const int&)  ??$maxOf@H@@YAHAEBH0@Z
//   int g_shapeVersion                ?g_shapeVersion@@3HA
//   vtable of geom::Circle            ??_7Circle@geom@@6B@
//
// Covered: namespaces and nested classes, access and static/virtual kinds,
// const methods, constructors/destructors/operators, the fundamental types,
// pointers/references with cv-qualifiers, class/struct/union/enum types,
// template instances with type and integer arguments, name and type
// back-references; the version 6 mangler (MangleSema.cpp) also covers
// pointers to members and vcall thunks. Not covered: function pointer
// parameters beyond the simple case, varargs.
//
// Strict C++98.
#ifndef CPPPARSE_MANGLE_HPP
#define CPPPARSE_MANGLE_HPP

#include "Types.hpp"

#include <string>
#include <vector>

namespace cppparse {
namespace sema { struct Symbol; }

std::string msvcMangleFunction(const FunctionInfo& f, TypeTable& types);
std::string msvcMangleVariable(const VariableInfo& v, TypeTable& types);   // globals and static data members
std::string msvcMangleVtable(const std::string& qualifiedClassName);

// the same decoration from the version 6 symbols (MangleSema.cpp)
std::string msvcMangleSymbol(const sema::Symbol& fn);
// the id that distinguishes this translation unit's anonymous namespace ("?A0x<id>@"), set per unit
void        setMangleUnitId(unsigned long id);
extern unsigned long g_mangleUnitId;
std::string msvcMangleVariableSymbol(const sema::Symbol& v);
// a static local: "?n@?1??counterA@@YAHXZ@4HA", the variable in block <discriminator> (2: the function's body)
std::string msvcMangleStaticLocal(const sema::Symbol& v, const sema::Symbol& fn, long discriminator);
// a vftable; forBase names one of several (??_7D@@6BPB@@@: D's vftable for PB), 0 for a class with one
std::string msvcMangleVtableSymbol(const sema::Symbol& c, const sema::Symbol* forBase = 0);
// a vbtable, named by the bases of its path (??_8D@@7B@, ??_8VX@@7BVL@@@, ??_8R@@7BB@@P@@@)
std::string msvcMangleVbtableSymbol(const sema::Symbol& c, const std::vector<sema::Symbol*>& path);
// an adjustor thunk of a virtual function: ?f@D@@WBA@EAAHXZ, D::f`adjustor{16}'
std::string msvcMangleThunkSymbol(const sema::Symbol& fn, long adjust);
// cl's vcall thunk of the virtual function at slotOffset (8 per slot) of a class's vftable: ??_9P@@$B7AA, P::`vcall'{8}'
std::string msvcMangleVcallThunk(const sema::Symbol& c, long slotOffset);
// cl's return-adjusting thunk of the covariant override fn in the slot of root: fn as a non-virtual function with
// root's return type, ?self@CD@@QEAAPEAUSB@@XZ
std::string msvcMangleReturnThunk(const sema::Symbol& fn, const sema::Symbol& root);
// cl's vtordisp thunk of an overrider in the vftable inside a virtual base: ?f@VL@@$4PPPPPPPM@7EBAHXZ (the vtordisp
// at -4 before the base, then the adjustment 8)
std::string msvcMangleVtordispThunk(const sema::Symbol& fn, long adjust);
// cl's thread-safe-static guard of a function's index-th guarded static local: "?$TSS0@?1??f@@YAAEAUCnt@@XZ@4HA"
std::string msvcMangleStaticGuard(const sema::Symbol& fn, long discriminator, int index);

} // namespace cppparse

#endif
