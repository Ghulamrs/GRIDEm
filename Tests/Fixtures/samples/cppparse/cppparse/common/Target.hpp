// Target.hpp - the platform cppparse compiles for: predefined macros, type sizes, calling convention, name
// mangling and object format. Every stage reads these from the selected target instead of assuming one.
#ifndef CPPPARSE_TARGET_HPP
#define CPPPARSE_TARGET_HPP

#include <string>
#include <vector>

namespace cppparse {

struct Target {
    enum Os { Windows, Linux };
    enum CallingConvention { MicrosoftX64, SystemV };    // argument registers, shadow space, variadic rules
    enum Mangling { MangleMsvc, MangleItanium };          // MSVC decoration or the Itanium C++ ABI of g++/clang
    enum ObjectFormat { Coff, Elf };

    std::string              name;              // "x86_64-windows"
    Os                       os;
    CallingConvention        convention;
    Mangling                 mangling;
    ObjectFormat             objectFormat;
    int                      pointerSize;
    int                      longSize;          // 4 on Windows (LLP64), 8 on Linux (LP64)
    int                      wcharSize;
    int                      longDoubleSize;
    int                      maxScalarAlign;    // the largest alignment a fundamental type gets
    bool                     sizeTypeIsLong;    // size_t is unsigned long (LP64); else unsigned long long
    std::vector<std::string> predefines;        // "NAME=value", besides the language's own macros
    bool                     hasBackEnd;        // --compile6 can produce objects for it
};

const Target&            defaultTarget();                    // x86_64-windows
const Target*            findTarget(const std::string& name); // 0 when unknown
std::vector<std::string> targetNames();

} // namespace cppparse

#endif
