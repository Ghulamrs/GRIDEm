// Report.hpp - totals and the plain-text report, plus helpers shared by
// the HTML and PDF writers.
// Strict C++98.
#ifndef CPPPARSE_REPORT_HPP
#define CPPPARSE_REPORT_HPP

#include "CodeBase.hpp"

#include <string>
#include <vector>

namespace cppparse {

struct ReportOptions {
    bool directives;   // list every preprocessor directive per file
    bool locals;       // list local variables per function
    ReportOptions() : directives(true), locals(true) {}
};

struct Totals {
    int files, headers, sources, discovered, lines, tokens;
    int directives, includes, includesResolved, macros, conditionals, pragmas, inactiveDirectives;
    int classes, structs, unions, classTemplates, abstractClasses, forwardOnly, nestedClasses;
    int methods, methodsDefined, methodsUndefined, pureVirtual, virtualMethods, constructors, destructors, operators, staticMethods;
    int fields, staticFields;
    int freeFunctions, freeDefined, freeDeclaredOnly, memberDefinitionsOutOfLine;
    int globals, globalExterns, globalStatics, staticMemberDefinitions;
    int locals, parameters;
    int typedefs, enums, enumerators, usings, namespaces, friends;
    int tryBlocks, catchHandlers, throwStatements, functionTryBlocks, functionsWithTry;
    int diagnostics, messages;
    int healthErrors, healthWarnings, healthInfos;
    Totals();
};

void        computeTotals(const CodeBase& cb, Totals& t);
std::string renderTextReport(const CodeBase& cb, const ReportOptions& opt);

// helpers shared with the HTML / PDF writers
std::string functionKind(const FunctionInfo& fn);           // "constructor, explicit"
std::string declText(const std::string& type, const std::string& name); // "void (*cb)(int)" / "int x"
std::string locationText(const CodeBase& cb, const std::string& file, int line, int endLine);
std::string directiveNote(const CodeBase& cb, const Directive& d);
const FunctionInfo* bodyOf(const FunctionInfo& fn);          // the definition holding locals/exceptions
std::string exceptionSummary(const ExceptionInfo& e);
void        inheritanceTreeLines(const CodeBase& cb, std::vector<std::string>& lines);
void        collectMacros(const CodeBase& cb, std::vector<std::string>& lines);
void        collectDiagnostics(const CodeBase& cb, std::vector<std::string>& lines);
void        totalsLines(const Totals& t, std::vector<std::string>& lines); // "label: value"

} // namespace cppparse

#endif
