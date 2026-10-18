// Target.cpp - the targets cppparse knows. x86_64-windows has the back end (COFF, MSVC ABI); x86_64-linux is
// described for the front end (macros and LP64 sizes for -E, --parse6, --sema6) until its back end exists.
#include "Target.hpp"

namespace cppparse {

namespace {

Target makeWindows()
{
    Target t;
    t.name = "x86_64-windows";
    t.os = Target::Windows;
    t.convention = Target::MicrosoftX64;
    t.mangling = Target::MangleMsvc;
    t.objectFormat = Target::Coff;
    t.pointerSize = 8;
    t.longSize = 4;
    t.wcharSize = 2;
    t.longDoubleSize = 8;
    t.maxScalarAlign = 8;
    t.sizeTypeIsLong = false;
    t.predefines.push_back("_WIN32=1");
    t.predefines.push_back("_M_X64=100");
    t.predefines.push_back("_WIN64=1");
    // what cl defines for the way cppparse builds programs: the static multithreaded CRT (libcmt, as /MT) and
    // C++ exceptions (as /EHsc); not _MSC_VER, which would promise MSVC's extensions
    t.predefines.push_back("_MT=1");
    t.predefines.push_back("_CPPUNWIND=1");
    t.hasBackEnd = true;
    return t;
}

Target makeLinux()
{
    Target t;
    t.name = "x86_64-linux";
    t.os = Target::Linux;
    t.convention = Target::SystemV;
    t.mangling = Target::MangleItanium;
    t.objectFormat = Target::Elf;
    t.pointerSize = 8;
    t.longSize = 8;
    t.wcharSize = 4;
    t.longDoubleSize = 16;
    t.maxScalarAlign = 16;
    t.sizeTypeIsLong = true;
    t.predefines.push_back("__linux__=1");
    t.predefines.push_back("__linux=1");
    t.predefines.push_back("__unix__=1");
    t.predefines.push_back("__unix=1");
    t.predefines.push_back("__x86_64__=1");
    t.predefines.push_back("__x86_64=1");
    t.predefines.push_back("__LP64__=1");
    t.predefines.push_back("_LP64=1");
    t.predefines.push_back("__ELF__=1");
    t.hasBackEnd = false;
    return t;
}

const std::vector<Target>& targets()
{
    static std::vector<Target> all;
    if (all.empty()) {
        all.push_back(makeWindows());
        all.push_back(makeLinux());
    }
    return all;
}

} // namespace

const Target& defaultTarget()
{
    return targets()[0];
}

const Target* findTarget(const std::string& name)
{
    const std::vector<Target>& all = targets();
    for (size_t i = 0; i < all.size(); ++i) if (all[i].name == name) return &all[i];
    return 0;
}

std::vector<std::string> targetNames()
{
    std::vector<std::string> names;
    const std::vector<Target>& all = targets();
    for (size_t i = 0; i < all.size(); ++i) names.push_back(all[i].name);
    return names;
}

} // namespace cppparse
