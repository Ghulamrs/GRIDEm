// main.cpp - cppparse command line.
//
//   cppparse [options] <input>...
//
//   input    a Visual Studio solution (.sln), a C++ project (.vcxproj, .vcproj),
//            a folder (scanned recursively for headers and sources), or
//            individual header / source files
//
// Outputs, written next to the first input (or into the -o directory):
//   <name>-report.txt     complete text report
//   <name>-health.txt     version 2 health check findings
//   <name>-symbols.json   symbol table (also -symbols.txt, tab-separated)
//   <name>-symbols.map    linker-style map: modules, symbols, cross reference
//   <name>-ast.txt        version 3: abstract syntax tree of every function body
//   <name>-ir.txt         version 3: class layouts and the IR before/after -On
//   <name>-opt.log        version 3: what each optimisation pass changed
//   <name>-asm.txt        version 3: lowered assembly for the VM64 target
//   <name>.asm            version 4: x86-64 MASM (Windows x64 ABI); --assemble adds <name>.obj via ml64
//   <name>-report.html    self-contained HTML report with the class diagram
//   <name>-report.pdf     the same report as PDF (diagram + text)
//   <name>-diagram.svg    class diagram picture
//   <name>-diagram.dot    Graphviz source (dot -Tpng -o diagram.png <name>-diagram.dot)
//
// Strict C++98.
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif
#include "CodeBase.hpp"
#include "Coff.hpp"
#include "MapFile.hpp"
#include "Compiler.hpp"
#include "Diagram.hpp"
#include "Health.hpp"
#include "Html.hpp"
#include "Parallel.hpp"
#include "PathUtil.hpp"
#include "Pdf.hpp"
#include "Pp.hpp"
#include "CxxParser.hpp"
#include "Sema.hpp"
#include "IrGen.hpp"
#include "Mangle.hpp"
#include "BackEnd.hpp"
#include "Report.hpp"
#include "Symbols.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace cppparse;

namespace {

void usage()
{
    std::cout <<
        "cppparse - C++98 project reader and report generator\n"
        "\n"
        "usage: cppparse [options] <input>...\n"
        "\n"
        "  <input>          .sln | .vcxproj | .vcproj | folder | header/source files\n"
        "\n"
        "options:\n"
        "  -o <dir>         output directory (default: next to the first input)\n"
        "  -n <name>        base name of the output files (default: input name)\n"
        "  --no-follow      do not parse #include'd files that are not part of the input\n"
        "  --no-directives  omit the per-file preprocessor listing from the reports\n"
        "  --no-locals      omit local variables from the reports\n"
        "  --text-only      write only the .txt report (and the health report)\n"
        "  --stdout         print the text report to the console, write no files\n"
        "  --health         print the health report to the console, write no files\n"
        "  --strict         exit code 3 when the health check finds errors\n"
        "  -O0 .. -O3       version 3: optimisation level for the IR (default -O0)\n"
        "  --no-compile     skip version 3 (AST, type check, IR, lowering)\n"
        "  --no-x86         version 4: do not write the x86-64 MASM file (<name>.asm)\n"
        "  --assemble       version 4: run \"ml64 /c\" on <name>.asm to produce <name>.obj (ml64 must be on PATH)\n"
        "  --obj            version 5: write <name>.obj with cppparse's own COFF writer (no ml64 needed)\n"
        "  --msvc-mangling  version 5: MSVC-compatible decorated names (?square@@YAHH@Z) so MSVC code links with plain C++ declarations\n"
        "  --eh=table|sjlj  try regions as unwind range tables with landing pads, no code on the normal path (the\n"
        "                   default), or as setjmp frames (version 5's model)\n"
        "  -j <n>           worker threads (default: one per processor for parsing, at most 4 for compiling;\n"
        "                   never more than the number of files read)\n"
        "  -E              version 6: preprocess the input files only and print the result (like cl /EP)\n"
        "  --parse6         version 6: preprocess and parse with the new front end; print counts and syntax errors,\n"
        "                   and write <name>-ast6.txt when -o or -n is given\n"
        "  --sema6          version 6: --parse6 plus semantic analysis; prints counts and S-coded diagnostics,\n"
        "                   and writes <name>-sym6.txt (symbols, layouts, vtables, instantiations)\n"
        "  --compile6       version 6: --sema6 plus IR generation and the back end: <name>-ir6.txt, -opt6.log,\n"
        "                   -asm6.txt, <name>6.asm (and <name>6.obj with --obj); -O, --msvc-mangling apply\n"
        "  -I <dir>         version 6: include directory for the preprocessor (also -Idir)\n"
        "  -D NAME[=value]  version 6: predefine a macro (also -DNAME=value); -U NAME undefines one\n"
        "  --target NAME    version 6: x86_64-windows (default; COFF objects, MSVC ABI) or x86_64-linux\n"
        "                   (LP64 sizes and macros for -E, --parse6, --sema6; no back end yet)\n"
        "                   predefined always: __cplusplus=199711L, __cppparse__=6, __STDC__=1; for\n"
        "                   x86_64-windows also _WIN32=1, _WIN64=1, _M_X64=100, _MT=1, _CPPUNWIND=1 (not _MSC_VER, not _DEBUG);\n"
        "                   for x86_64-linux __linux__, __unix__, __x86_64__, __LP64__, __ELF__\n"
        "                   --compile6 stops at a missing include file (the other modes warn)\n"
        "  src\\*.cpp        wildcard patterns are expanded by cppparse itself\n"
        "  @list.txt        response file: one path, pattern or option per line\n"
        "  -q               quiet: print only errors\n"
        "  -h, --help       this text\n";
}

std::string stem(const std::string& path)
{
    std::string b = baseName(normalizePath(path));
    if (b.empty()) b = "project";
    size_t dot = b.rfind('.');
    std::string ext = extension(b);
    if (dot != std::string::npos && (ext == ".sln" || ext == ".vcxproj" || ext == ".vcproj" || isHeaderExtension(ext) || isSourceExtension(ext)))
        b = b.substr(0, dot);
    return b;
}

bool writeOut(const std::string& path, const std::string& content, bool quiet, std::vector<std::string>& written)
{
    if (!writeFile(path, content)) { std::cerr << "error: cannot write " << path << "\n"; return false; }
    written.push_back(path + "  (" + intToString(static_cast<int>(content.size() / 1024)) + " KB)");
    if (!quiet) std::cout << "  wrote " << path << "\n";
    return true;
}

// a virtual base, direct or inherited: --compile6 does not lay those out as cl does yet (no vbptr, no shared
// subobject), so it refuses the class rather than emit wrong code (S130)
bool hasVirtualBase(const sema::Symbol* cls, int depth)
{
    if (!cls || depth > 32) return false;
    for (size_t b = 0; b < cls->bases.size(); ++b)
        if (cls->bases[b].isVirtual || hasVirtualBase(cls->bases[b].cls, depth + 1)) return true;
    return false;
}

// what --compile6 cannot generate yet for a class with virtual bases (0: it can): an overrider that lies in another
// virtual base (S130)
const char* virtualBaseRefusal(const sema::Symbol* cls)
{
    for (size_t t = 0; t < cls->vftables.size(); ++t)
        if (cls->vftables[t].vtordispex) return "a virtual function overridden in another virtual base (cl's vtordispex thunk, which reads the vbtable)";
    return 0;
}

// a covariant override's slot (Sema::completeClass, VfTable::returnOf) that --compile6 cannot give cl's
// return-adjusting thunk (S131): one whose return conversion goes through a virtual base (cl's thunk reads the
// vbtable), one that also moves this (cl's $R thunks: a this and a return adjustment in one), or a variadic function
// (the thunk cannot stage the unnamed arguments again); 0 when the slot is fine
const char* returnThunkRefusal(const sema::VfTable& t, size_t s)
{
    if (s >= t.returnOf.size() || !t.returnOf[s]) return 0;
    if (t.returnAdjust[s] < 0) return "its return conversion goes through a virtual base";
    if (t.vbase) return "the overridden function's vftable lies in a virtual base";
    if (t.thunks[s] != 0) return "the slot needs a this adjustment as well as the return adjustment";
    const sema::Symbol* fn = t.fns[s];
    if (fn && fn->type && fn->type->kind == sema::Type::Function && fn->type->variadic) return "it is variadic";
    return 0;
}

// a unit that is not compiled leaves no object, listing or map: one of an earlier run would be taken for its own
void removeUnitOutputs(const std::string& base)
{
    std::remove((base + "6.obj").c_str());
    std::remove((base + "6.asm").c_str());
    std::remove((base + "6.map").c_str());
}

} // namespace

#if defined(_MSC_VER) && defined(_DEBUG)
// the call stack, symbolised from the .pdb next to the executable
void printStack()
{
    void* frames[62];
    const unsigned short n = CaptureStackBackTrace(0, 62, frames, 0);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (SymInitialize(process, 0, TRUE)) {
        char buffer[sizeof(SYMBOL_INFO) + 256];
        for (unsigned short i = 0; i < n; ++i) {
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            std::memset(buffer, 0, sizeof(buffer));
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (!SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol)) continue;
            IMAGEHLP_LINE64 line; std::memset(&line, 0, sizeof(line)); line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(frames[i]), &lineDisplacement, &line)) std::fprintf(stderr, "  %s  %s(%lu)\n", symbol->Name, line.FileName, static_cast<unsigned long>(line.LineNumber));
            else std::fprintf(stderr, "  %s\n", symbol->Name);
        }
    }
    std::fflush(stderr);
}

// a stack overflow prints the innermost frames (the recursion) too; the handler runs on the stack the thread
// reserved with SetThreadStackGuarantee
LONG CALLBACK stackOverflowToStderr(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(stderr, "debug runtime: stack overflow\n");
    printStack();
    _exit(4);
}

// a debug-runtime assertion (an iterator check, a heap check) is written to stderr and ends the process with
// exit code 3, so that a build script or a bisection sees it instead of a dialog waiting for a click
int reportToStderr(int reportType, char* message, int* returnValue)
{
    std::fprintf(stderr, "debug runtime: %s", message ? message : "");
    if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
        printStack();
        _exit(3);
    }
    if (returnValue) *returnValue = 0;
    return 1;
}
#endif

int main(int argc, char* argv[])
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportHook(reportToStderr);
    ULONG guarantee = 256 * 1024;
    SetThreadStackGuarantee(&guarantee);
    AddVectoredExceptionHandler(1, stackOverflowToStderr);
#endif
    std::vector<std::string> inputs;
    std::string outDir, outName;
    bool follow = true, textOnly = false, toStdout = false, quiet = false, healthOnly = false, strict = false, compile = true, assemble = false, obj = false;
    int threads = hardwareThreads();
    bool threadsGiven = false;
    bool ok0 = true;
    bool preprocessOnly = false, parse6 = false, sema6 = false, compile6 = false;
    PpOptions ppOpt;
    CompileOptions copt;
    ReportOptions opt;

    // response files (@list.txt) are expanded into the argument list first
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() > 1 && a[0] == '@') {
            std::vector<std::string> more;
            if (!readResponseFile(a.substr(1), more)) { std::cerr << "error: cannot read response file " << a.substr(1) << "\n"; return 2; }
            args.insert(args.end(), more.begin(), more.end());
        } else args.push_back(a);
    }
    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = args[i];
        if (a == "-h" || a == "--help" || a == "/?") { usage(); return 0; }
        else if (a == "-j" && i + 1 < args.size()) { threads = atoi(args[++i].c_str()); threadsGiven = true; }
        else if (a.size() > 2 && a[0] == '-' && a[1] == 'j' && isdigit(static_cast<unsigned char>(a[2]))) { threads = atoi(a.c_str() + 2); threadsGiven = true; }
        else if (a == "-o" && i + 1 < args.size()) outDir = args[++i];
        else if (a == "-n" && i + 1 < args.size()) outName = args[++i];
        else if (a == "--no-follow") follow = false;
        else if (a == "--no-directives") opt.directives = false;
        else if (a == "--no-locals") opt.locals = false;
        else if (a == "--text-only") textOnly = true;
        else if (a == "--stdout") toStdout = true;
        else if (a == "--health") healthOnly = true;
        else if (a == "--strict") strict = true;
        else if (a == "--no-compile") compile = false;
        else if (a == "--no-x86") copt.x86 = false;
        else if (a == "--assemble") assemble = true;
        else if (a == "--obj") obj = true;
        else if (a == "--msvc-mangling") copt.msvcNames = true;
        else if (a == "--eh=table") copt.tableEh = true;
        else if (a == "--eh=sjlj") copt.tableEh = false;
        else if (a == "-E" || a == "--preprocess") preprocessOnly = true;
        else if (a == "--parse6") parse6 = true;
        else if (a == "--sema6") { parse6 = true; sema6 = true; }
        else if (a == "--compile6") { parse6 = true; sema6 = true; compile6 = true; }
        else if (a == "-I" && i + 1 < args.size()) ppOpt.includeDirs.push_back(args[++i]);
        else if (a.size() > 2 && a[0] == '-' && a[1] == 'I') ppOpt.includeDirs.push_back(a.substr(2));
        else if (a == "-D" && i + 1 < args.size()) ppOpt.defines.push_back(args[++i]);
        else if (a.size() > 2 && a[0] == '-' && a[1] == 'D') ppOpt.defines.push_back(a.substr(2));
        else if (a == "-U" && i + 1 < args.size()) ppOpt.undefines.push_back(args[++i]);
        else if (a == "--target" && i + 1 < args.size()) {
            ppOpt.target = findTarget(args[++i]);
            if (!ppOpt.target) {
                std::cerr << "unknown target: " << args[i] << " (known:";
                const std::vector<std::string> names = targetNames();
                for (size_t k = 0; k < names.size(); ++k) std::cerr << " " << names[k];
                std::cerr << ")\n";
                return 2;
            }
        }
        else if (a.size() == 3 && a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3') copt.optLevel = a[2] - '0';
        else if (a == "-q") quiet = true;
        else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option: " << a << "\n"; usage(); return 2; }
        else {
            std::vector<std::string> expanded;
            if (expandWildcards(a, expanded) == 0) { std::cerr << "error: no file matches " << a << "\n"; ok0 = false; }
            inputs.insert(inputs.end(), expanded.begin(), expanded.end());
        }
    }
    if (inputs.empty()) { usage(); return 2; }
    const Target& target = ppOpt.target ? *ppOpt.target : defaultTarget();
    if (compile6 && !target.hasBackEnd) {
        std::cerr << "error: no back end for target " << target.name << " yet: -E, --parse6 and --sema6 work for it, --compile6 does not\n";
        return 2;
    }

    if (parse6) {
        // version 6: preprocess and parse every input with the new front end; print counts and syntax errors
        int totalErrors = 0;
        // calling-convention macros of windows.h, empty when the header is not available
        ppOpt.defines.push_back("WINAPI=");
        ppOpt.defines.push_back("CALLBACK=");
        ppOpt.defines.push_back("APIENTRY=");
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!fileExists(inputs[i])) { std::cerr << "error: cannot read " << inputs[i] << "\n"; ++totalErrors; continue; }
            Preprocessor pp(ppOpt);
            pp.run(inputs[i]);
            if (compile6 && !pp.missingIncludes().empty()) {
                // an object compiled without one of its headers would rest on undeclared names and guessed types
                for (size_t k = 0; k < pp.missingIncludes().size(); ++k)
                    std::cerr << inputs[i] << ": error: cannot open include file '" << pp.missingIncludes()[k] << "'\n";
                totalErrors += static_cast<int>(pp.missingIncludes().size());
                continue;
            }
            cxx::CxxArena arena;
            cxx::TranslationUnit tu;
            tu.path = inputs[i];
            cxx::Parser parser(pp.tokens(), arena);
            // typedefs of the C library (and of windows.h when it was not found) that sources use without
            // the headers being available: until cppparse ships its own library headers they are assumed
            static const char* const kCTypes[] = { "size_t", "ptrdiff_t", "FILE", "time_t", "clock_t", "va_list", "jmp_buf", "fpos_t", "div_t", "ldiv_t",
                                                   "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t", "uintptr_t", 0 };
            static const char* const kWinTypes[] = { "DWORD", "LONG", "ULONG", "LPVOID", "HANDLE", "BOOL", "BYTE", "WORD", "UINT", "INT", "LPCSTR", "LPSTR",
                                                     "SYSTEM_INFO", "CRITICAL_SECTION", "LPCRITICAL_SECTION", "LPTHREAD_START_ROUTINE", "SIZE_T", "HMODULE", "HWND", "LPARAM", "WPARAM", "LRESULT", 0 };
            for (int k = 0; kCTypes[k]; ++k) parser.assumeType(kCTypes[k]);
            bool winMissing = false;
            for (size_t k = 0; k < pp.missingIncludes().size(); ++k) if (toLower(pp.missingIncludes()[k]) == "windows.h") winMissing = true;
            if (winMissing) for (int k = 0; kWinTypes[k]; ++k) parser.assumeType(kWinTypes[k]);
            int errors = parser.parse(tu);
            totalErrors += errors;
            cxx::Counts c;
            cxx::countTranslationUnit(tu, c);
            std::cout << inputs[i] << ": " << pp.tokens().size() << " tokens, " << errors << " syntax error(s); namespaces " << c.namespaces
                      << ", classes " << c.classes << ", enums " << c.enums << ", functions defined " << c.functionsDefined << ", declared " << c.functionsDeclared
                      << ", variables " << c.variables << ", typedefs " << c.typedefs << ", templates " << c.templates << ", statements " << c.statements
                      << ", expressions " << c.expressions << "\n";
            const std::vector<cxx::ParseDiag>& d = parser.diagnostics();
            for (size_t k = 0; k < d.size() && k < 20; ++k)
                std::cout << "  " << (d[k].file < static_cast<int>(pp.files().size()) ? pp.files()[static_cast<size_t>(d[k].file)] : "?") << "(" << d[k].line << "): error: " << d[k].message << "\n";
            if (sema6) {
                sema::TypeTable types;
                types.setTarget(target);
                sema::Program program;
                sema::Sema sema(types, program);
                sema.useArena(arena);   // nodes sema adds must outlive it: the AST dump runs later
                sema.analyze(tu, pp.files());
                const sema::SemaStats& st = sema.stats();
                std::cout << "  sema: " << sema.errorCount() << " error(s); classes " << st.classes << ", functions analysed " << st.functionsDefined << ", variables " << st.variables
                          << ", expressions " << st.expressionsTyped << ", names resolved " << st.namesResolved << ", unresolved " << st.namesUnresolved
                          << ", calls resolved " << st.callsResolved << ", ambiguous " << st.callsAmbiguous << ", instantiations " << st.classInstantiations << " classes / " << st.functionInstantiations << " functions\n";
                const std::vector<sema::Diag>& sd = sema.diagnostics();
                for (size_t k = 0; k < sd.size() && k < 30; ++k)
                    std::cout << "  " << (sd[k].loc.file < static_cast<int>(pp.files().size()) ? pp.files()[static_cast<size_t>(sd[k].loc.file)] : "?") << "(" << sd[k].loc.line << "): "
                              << (sd[k].severity == sema::Diag::Error ? "error" : "warning") << " " << sd[k].code << ": " << sd[k].message << "\n";
                totalErrors += sema.errorCount();
                std::string base = joinPath(outDir.empty() ? "." : outDir, (outName.empty() ? baseName(inputs[i]) : outName) + (inputs.size() > 1 ? "-" + intToString(static_cast<int>(i)) : ""));
                std::vector<std::string> written;
                if (!outDir.empty() || !outName.empty()) writeOut(base + "-sym6.txt", sema.dumpSymbols(), quiet, written);
                int unsupported = 0;
                if (compile6)
                    for (size_t k = 0; k < program.symbols.size(); ++k) {
                        const sema::Symbol* s = program.symbols[k];
                        if (s->kind == sema::Symbol::Class && s->isDefinition && !s->isDependentContext) {
                            for (size_t t = 0; t < s->vftables.size(); ++t)
                                for (size_t slot = 0; slot < s->vftables[t].fns.size(); ++slot) {
                                    const char* why = returnThunkRefusal(s->vftables[t], slot);
                                    if (!why) continue;
                                    const sema::Symbol* fn = s->vftables[t].fns[slot];
                                    const sema::Symbol* root = s->vftables[t].returnOf[slot];
                                    std::cout << "  " << (fn->loc.file >= 0 && fn->loc.file < static_cast<int>(pp.files().size()) ? pp.files()[static_cast<size_t>(fn->loc.file)] : "?") << "(" << fn->loc.line
                                              << "): error S131: '" << fn->qualifiedName() << "' overrides '" << root->qualifiedName()
                                              << "' with a covariant return type, and " << why << ": not supported yet by --compile6\n";
                                    ++unsupported;
                                }
                        }
                        if (s->kind != sema::Symbol::Class || !s->isDefinition || s->isDependentContext || !hasVirtualBase(s, 0)) continue;
                        const char* why = s->layoutDone ? virtualBaseRefusal(s) : "a class not laid out";
                        if (!why) continue;
                        std::cout << "  " << (s->loc.file >= 0 && s->loc.file < static_cast<int>(pp.files().size()) ? pp.files()[static_cast<size_t>(s->loc.file)] : "?") << "(" << s->loc.line
                                  << "): error S130: '" << s->qualifiedName() << "' has a virtual base class: " << why << " is not supported yet by --compile6\n";
                        ++unsupported;
                    }
                totalErrors += unsupported;
                if (compile6 && errors + sema.errorCount() + unsupported > 0) {
                    // a unit with syntax or semantic errors is not compiled, as cl does: an object built on
                    // unresolved names and error types would reach the linker
                    std::cout << "  compile6: not compiled, " << (errors + sema.errorCount() + unsupported) << " error(s)\n";
                    removeUnitOutputs(base);
                }
                else if (compile6) {
                    // version 6 stage 4: IR from the analysed tree, then the shared back end
                    IrGen gen(program, types);
                    std::vector<IrFunction*> functions;
                    X86Module x86(copt.msvcNames);
                    x86.setTableExceptions(copt.tableEh);
                    unsigned long unitId = 2166136261UL;   // FNV-1a of the input path: this unit's anonymous namespace id
                    for (size_t k = 0; k < inputs[i].size(); ++k) unitId = (unitId ^ static_cast<unsigned char>(inputs[i][k])) * 16777619UL;
                    setMangleUnitId(unitId & 0xFFFFFFFFUL);
                    gen.setNamedReturnElision(copt.optLevel >= 1);   // NRVO only when optimising, as cl (/Od: a copy)
                    gen.registerMetadata(x86, copt.msvcNames);   // first: it queues the vtable members and the dynamic initialisers
                    gen.generate(functions);
                    if (!gen.unsupported().empty()) {
                        // constructs with virtual bases that would compile to wrong code: nothing is written (S132)
                        const std::vector<std::string>& bad = gen.unsupported();
                        for (size_t k = 0; k < bad.size(); ++k) std::cout << "  " << inputs[i] << ": error S132: " << bad[k] << "\n";
                        totalErrors += static_cast<int>(bad.size());
                        removeUnitOutputs(base);
                        for (size_t k = 0; k < functions.size(); ++k) delete functions[k];
                        continue;
                    }
                    if (!gen.failedMembersUsed().empty()) {
                        // a member of a class template instantiation whose body has errors is needed after all
                        // (an implicit call sema does not see): its held-back diagnostics are real, nothing is written
                        const std::vector<sema::Symbol*>& bad = gen.failedMembersUsed();
                        for (size_t k = 0; k < bad.size(); ++k) {
                            std::cout << "  error S128: '" << bad[k]->qualifiedName() << "' is used, but its instantiation has errors:\n";
                            const std::vector<sema::Diag>* held = sema.heldDiagnostics(bad[k]);
                            for (size_t h = 0; held && h < held->size() && h < 10; ++h)
                                std::cout << "    " << ((*held)[h].loc.file < static_cast<int>(pp.files().size()) ? pp.files()[static_cast<size_t>((*held)[h].loc.file)] : "?")
                                          << "(" << (*held)[h].loc.line << "): " << (*held)[h].code << ": " << (*held)[h].message << "\n";
                        }
                        totalErrors += static_cast<int>(bad.size());
                        removeUnitOutputs(base);
                        for (size_t k = 0; k < functions.size(); ++k) delete functions[k];
                        continue;
                    }
                    BackEndOptions bo;
                    bo.optLevel = copt.optLevel; bo.threads = copt.threads; bo.x86 = copt.x86;
                    BackEndResult br;
                    runBackEnd(functions, &x86, bo, br);
                    std::cout << "  compile6: " << gen.functionsGenerated() << " function(s), IR " << br.irBefore << " -> " << br.irAfter << " instructions, assembly " << br.asmInstructions << ", spills " << br.spills << "\n";
                    for (size_t k = 0; k < gen.notes().size() && k < 10; ++k) std::cout << "  note: " << gen.notes()[k] << "\n";
                    writeOut(base + "-ir6.txt", br.irText, quiet, written);
                    writeOut(base + "-opt6.log", br.log, quiet, written);
                    writeOut(base + "-asm6.txt", br.asmText, quiet, written);
                    if (copt.x86) {
                        writeOut(base + "6.asm", br.x86Text, quiet, written);
                        CoffResult cr;
                        if (!obj) { std::string bytes; assembleCoff(br.x86Text, copt.threads, bytes, cr); }   // only for the map
                        else if (writeCoffObject(br.x86Text, base + "6.obj", copt.threads, cr)) { if (!quiet) std::cout << "  wrote " << base << "6.obj\n"; }
                        else { std::cerr << "error: " << base << "6.obj not written:\n"; for (size_t e = 0; e < cr.errors.size() && e < 10; ++e) std::cerr << "  " << cr.errors[e] << "\n"; totalErrors += 1; }
                        std::string mapObject = obj ? baseName(base) + "6.obj" : baseName(base) + "6.asm, assembled in memory (no --obj, no object written)";
                        writeOut(base + "6.map", renderObjectMap(baseName(inputs[i]), mapObject, cr, br.symbolNames), quiet, written);
                    }
                    for (size_t k = 0; k < functions.size(); ++k) delete functions[k];
                }
            }
            if (!outDir.empty() || !outName.empty()) {
                std::string base = joinPath(outDir.empty() ? "." : outDir, (outName.empty() ? baseName(inputs[i]) : outName) + (inputs.size() > 1 ? "-" + intToString(static_cast<int>(i)) : ""));
                std::vector<std::string> written;
                writeOut(base + "-ast6.txt", cxx::dumpTranslationUnit(tu), quiet, written);
            }
        }
        return totalErrors == 0 ? 0 : 1;
    }

    if (preprocessOnly) {
        // version 6: run the preprocessor alone and print the result like cl /EP
        int rc = 0;
        for (size_t i = 0; i < inputs.size(); ++i) {
            Preprocessor pp(ppOpt);
            if (!pp.run(inputs[i])) rc = 1;
            std::cout << pp.text();
            const std::vector<PpDiag>& d = pp.diagnostics();
            for (size_t k = 0; k < d.size(); ++k)
                std::cerr << d[k].file << "(" << d[k].line << "): " << (d[k].severity == PpDiag::Error ? "error" : "warning") << ": " << d[k].message << "\n";
        }
        return rc;
    }

    CodeBase cb;
    cb.setFollowIncludes(follow);
    if (threads < 1) threads = 1;
    cb.setThreads(threads);
    // the per-function compile phases allocate heavily and share one heap: beyond four threads
    // they slow down on the measured machines, so the default stops there unless -j says otherwise
    copt.threads = threadsGiven ? threads : (threads > 4 ? 4 : threads);
    bool ok = ok0;
    // plain files are parsed together in parallel; projects, solutions and folders handle their own lists
    std::vector<std::string> plainFiles;
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::string ext = extension(inputs[i]);
        bool structured = ext == ".sln" || ext == ".vcxproj" || ext == ".vcproj" || isDirectory(inputs[i]);
        if (!structured) { plainFiles.push_back(inputs[i]); continue; }
        if (!quiet && !toStdout) std::cout << "reading " << inputs[i] << "\n";
        if (!cb.addInput(inputs[i])) ok = false;
    }
    if (!plainFiles.empty()) {
        if (!quiet && !toStdout) std::cout << "reading " << plainFiles.size() << " file(s) with "
                                           << (threads < static_cast<int>(plainFiles.size()) ? threads : static_cast<int>(plainFiles.size())) << " thread(s)\n";
        if (!cb.addFiles(plainFiles)) ok = false;
    }
    if (cb.files().empty()) {
        std::cerr << "error: no source files were read\n";
        for (size_t i = 0; i < cb.messages().size(); ++i) std::cerr << "  " << cb.messages()[i] << "\n";
        return 1;
    }
    // never more workers than files: parsing already takes one job per file, but the compile phases split
    // the work by function (and the object writer by block), so a small project would otherwise start
    // every thread -j allows
    const int fileCount = static_cast<int>(cb.files().size());
    if (threads > fileCount) threads = fileCount;
    if (copt.threads > fileCount) copt.threads = fileCount;
    cb.link();
    Compiler compiler(cb, copt);
    if (compile) compiler.run();          // sets FunctionInfo::typeChecked before the health check runs
    cb.checkHealth();
    if (compile) cb.addFindings(compiler.findings());
    int errors = 0, warnings = 0, infos = 0;
    countFindings(cb.findings(), errors, warnings, infos);
    const int strictExit = (strict && errors > 0) ? 3 : 0;

    if (healthOnly) { std::cout << renderHealthText(cb); return strictExit ? strictExit : (ok ? 0 : 1); }
    std::string text = renderTextReport(cb, opt);
    if (toStdout) { std::cout << text; return strictExit ? strictExit : (ok ? 0 : 1); }

    if (outDir.empty()) outDir = cb.baseDirectory();
    if (outName.empty()) outName = stem(inputs[0]);
    outDir = normalizePath(outDir);
    std::string base = outDir + "/" + outName;

    std::vector<std::string> written;
    if (!writeOut(base + "-report.txt", text, quiet, written)) ok = false;
    if (!writeOut(base + "-health.txt", renderHealthText(cb), quiet, written)) ok = false;
    if (!writeOut(base + "-symbols.json", renderSymbolsJson(cb), quiet, written)) ok = false;
    if (!writeOut(base + "-symbols.txt", renderSymbolsText(cb), quiet, written)) ok = false;
    if (!writeOut(base + "-symbols.map", renderSymbolsMap(cb), quiet, written)) ok = false;
    if (compile) {
        if (!writeOut(base + "-ast.txt", compiler.astText(), quiet, written)) ok = false;
        if (!writeOut(base + "-ir.txt", "; class layouts\n" + compiler.layoutText() + "\n" + compiler.irText(), quiet, written)) ok = false;
        if (!writeOut(base + "-opt.log", compiler.optLog(), quiet, written)) ok = false;
        if (!writeOut(base + "-asm.txt", compiler.asmText(), quiet, written)) ok = false;
        if (copt.x86) {
            if (!writeOut(base + ".asm", compiler.x86Text(), quiet, written)) ok = false;
            CoffResult cr;
            std::string objPath = base + (assemble ? "-cppparse.obj" : ".obj");   // --assemble keeps <name>.obj for ml64
            if (!obj) {
                // the map needs the real layout: assemble in memory when no object is written
                std::string bytes;
                assembleCoff(compiler.x86Text(), copt.threads, bytes, cr);
            } else if (writeCoffObject(compiler.x86Text(), objPath, copt.threads, cr)) {
                if (!quiet) std::cout << "  wrote " << objPath << "  (" << cr.functions << " functions, " << cr.instructions << " instructions, .text "
                                      << cr.textBytes << " bytes, .data " << cr.dataBytes << ", .rdata " << cr.rdataBytes << ", "
                                      << cr.relocations << " relocations, " << cr.symbols << " symbols)\n";
            } else {
                std::cerr << "error: " << objPath << " not written, " << cr.errors.size() << " line(s) could not be encoded:\n";
                for (size_t e = 0; e < cr.errors.size() && e < 10; ++e) std::cerr << "  " << cr.errors[e] << "\n";
                ok = false;
            }
            std::string mapObject = obj ? baseName(objPath) : outName + ".asm, assembled in memory (no --obj, no object written)";
            if (!writeOut(base + ".map", renderObjectMap(cb.title(), mapObject, cr, compiler.symbolNames()), quiet, written)) ok = false;
            if (assemble) {
                std::string cmd = "ml64 /nologo /c \"/Fo" + base + ".obj\" \"" + base + ".asm\"";
                if (!quiet) std::cout << "  running " << cmd << "\n";
                int rc = std::system(cmd.c_str());
                if (rc != 0) { std::cerr << "error: ml64 failed (exit code " << rc << ")\n"; ok = false; }
            }
        }
    }
    if (!textOnly) {
        DiagramLayout layout;
        layout.build(cb);
        if (!writeOut(base + "-diagram.svg", renderSvg(layout, cb.title() + " - class diagram", true), quiet, written)) ok = false;
        if (!writeOut(base + "-diagram.dot", renderDot(cb), quiet, written)) ok = false;
        if (!writeOut(base + "-report.html", renderHtmlReport(cb, layout, opt), quiet, written)) ok = false;
        if (!writeOut(base + "-report.pdf", renderPdfReport(cb, layout, text), quiet, written)) ok = false;
    }

    if (!quiet) {
        Totals t;
        computeTotals(cb, t);
        std::cout << "\n" << cb.title() << ": " << t.files << " files (" << t.headers << " headers, " << t.sources << " sources), "
                  << t.lines << " lines, " << (t.classes + t.structs + t.unions) << " classes, " << t.methods << " methods, "
                  << t.freeFunctions << " free functions, " << t.globals << " globals, " << t.macros << " macros\n"
                  << "health: " << errors << " errors, " << warnings << " warnings, " << infos << " info\n";
        if (compile) {
            std::vector<std::string> lines;
            compiler.summaryLines(lines);
            for (size_t i = 0; i < lines.size(); ++i) std::cout << lines[i] << "\n";
        }
        std::cout << "timing: parse " << cb.parseMilliseconds() << " ms" << (compile ? ", compile " + intToString(static_cast<int>(compiler.summary().compileMs)) + " ms" : std::string(""))
                  << " (parse threads " << threads << ", compile threads " << copt.threads << ")\n";
        for (size_t i = 0; i < cb.messages().size(); ++i) std::cout << "  note: " << cb.messages()[i] << "\n";
    }
    if (strictExit) return strictExit;
    return ok ? 0 : 1;
}
