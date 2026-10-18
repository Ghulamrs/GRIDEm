// Health.hpp - version 2: the health checker.
//
// Walks the linked model and produces findings with a severity (error,
// warning, info) and a stable code. Codes:
//
//   E100 include not found            E101 unbalanced #if/#else/#endif
//   W102 header without include guard I103 circular include
//   E110 missing ';' after class body W111 unrecognised construct
//   I112 macro invocation             E113 unterminated body
//   E120 duplicate class definition   E121 duplicate or self base
//   W122 base class not defined       E123 duplicate member
//   E124 field of incomplete type     W125 virtual methods, non-virtual dtor
//   W126 static member never defined  I127 derived class still abstract
//   W128 method hides a base virtual
//   E130 definition without declaration   E131 duplicate definition
//   W132 method never defined             W133 free function never defined
//   E134 return type mismatch             E135 default argument repeated
//   E136 initializer names unknown member W137 initializer order
//   W138 member not initialised
//   W140 missing return                   W141 return value in void function
//   E142 wrong number of arguments        E143 no such member
//   E144 '.' on pointer / '->' on object  E145 no matching constructor
//   I146 call to undeclared function      W147 throw in destructor
//   W150 unused parameter                 W151 unused local variable
//   W152 shadowing                        W153 assignment in condition
//   W154 empty statement after if/for/while   W155 catch by value
//   E156 handler after catch(...)         W157 unreachable code
//   E160 global defined twice             W161 extern never defined
//   E162 duplicate enumerator             E163 function defined twice
//   W164 global defined in header         W165 non-inline function in header
//
// Strict C++98.
#ifndef CPPPARSE_HEALTH_HPP
#define CPPPARSE_HEALTH_HPP

#include "CodeBase.hpp"

#include <string>
#include <vector>

namespace cppparse {

void        runHealthChecks(const CodeBase& cb, std::vector<Finding>& out);
const char* severityName(Finding::Severity s);
void        countFindings(const std::vector<Finding>& f, int& errors, int& warnings, int& infos);
int         projectNumber(const CodeBase& cb, const Finding& f);              // 1-based, input order
std::string findingLine(const CodeBase& cb, const Finding& f);                 // "N>file(line): error CODE: message"
void        healthLines(const CodeBase& cb, std::vector<std::string>& lines);  // for the reports
std::string renderHealthText(const CodeBase& cb);                               // standalone file

} // namespace cppparse

#endif
