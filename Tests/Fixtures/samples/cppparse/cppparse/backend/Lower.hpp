// Lower.hpp - version 3: lowering of the optimised IR to a virtual target.
//
// Target "VM64": 64-bit, registers R0..R7 (integer/pointer) and F0..F7
// (floating point), frame pointer FP and stack pointer SP. R6/R7 and F6/F7
// are scratch registers for spills and immediates; R0..R5 / F0..F5 are
// allocated to temporaries by a linear-scan allocator with spilling.
// Calling convention: integer arguments in R0..R3, floating arguments in
// F0..F3, the rest pushed right to left; result in R0 / F0; every register is
// caller-saved (temporaries live across a call are kept in the frame).
// Locals, spills and temporaries that live across calls get frame slots.
//
// Strict C++98.
#ifndef CPPPARSE_LOWER_HPP
#define CPPPARSE_LOWER_HPP

#include "Ir.hpp"

#include <string>
#include <vector>

namespace cppparse {

struct LowerStats {
    int instructions;
    int spills;
    int frameSize;
    LowerStats() : instructions(0), spills(0), frameSize(0) {}
};

std::string lowerFunction(const IrFunction& f, LowerStats& stats);
std::string mangle(const std::string& name);   // "geom::Circle::area() const" -> "geom__Circle__area_c"

} // namespace cppparse

#endif
