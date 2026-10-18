// Model.cpp - constructors and small helpers for the data model.
#include "Model.hpp"

namespace cppparse {

const char* accessName(Access a)
{
    switch (a) {
    case AccessPublic:    return "public";
    case AccessProtected: return "protected";
    case AccessPrivate:   return "private";
    default:              return "";
    }
}

Directive::Directive()
    : kind(Other), line(0), systemInclude(false), isIncludeGuard(false), inactive(false)
{
}

const char* Directive::kindName(Kind k)
{
    switch (k) {
    case Include: return "include";
    case Define:  return "define";
    case Undef:   return "undef";
    case If:      return "if";
    case Ifdef:   return "ifdef";
    case Ifndef:  return "ifndef";
    case Elif:    return "elif";
    case Else:    return "else";
    case Endif:   return "endif";
    case Pragma:  return "pragma";
    case Error:   return "error";
    case Warning: return "warning";
    case Line:    return "line";
    default:      return "other";
    }
}

TemplateInfo::TemplateInfo() : isTemplate(false) {}

std::string TemplateInfo::text() const
{
    if (!isTemplate) return "";
    std::string out;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i) out += " ";
        out += "template <" + headers[i] + ">";
    }
    if (headers.empty()) out = "template <>";
    return out;
}

LocalVariable::LocalVariable() : line(0) {}

CallInfo::CallInfo() : kind(Plain), argCount(0), line(0) {}
BodyNote::BodyNote() : line(0) {}
Finding::Finding() : severity(Info), line(0) {}

ExceptionInfo::ExceptionInfo()
    : tryBlocks(0), catchHandlers(0), throwStatements(0), functionTryBlock(false)
{
}

FunctionInfo::FunctionInfo()
    : isConst(false), isVolatile(false), isVirtual(false), isPureVirtual(false),
      isStatic(false), isInline(false), isExplicit(false), isExtern(false), isFriend(false),
      isConstructor(false), isDestructor(false), isOperator(false), isConversion(false),
      hasBody(false), hasInitializerList(false), access(AccessNone), line(0), endLine(0),
      returnsValue(0), returnsVoid(0), bodyBegin(0), bodyEnd(0), typeChecked(false), definition(0), ownerClass(0)
{
}

std::string FunctionInfo::signature() const
{
    std::string s;
    if (isStatic)   s += "static ";
    if (isVirtual)  s += "virtual ";
    if (isExplicit) s += "explicit ";
    if (!returnType.empty()) s += returnType + " ";
    s += name + "(" + paramText + ")";
    if (isConst)    s += " const";
    if (isVolatile) s += " volatile";
    if (!exceptionSpec.empty()) s += " " + exceptionSpec;
    if (isPureVirtual) s += " = 0";
    return s;
}

std::string FunctionInfo::fullName() const
{
    std::string s = scope;
    if (!qualifier.empty()) { if (!s.empty()) s += "::"; s += qualifier; }
    if (!s.empty()) s += "::";
    return s + name;
}

std::string FunctionInfo::displayName() const
{
    if (ownerClass && qualifier.empty()) return ownerClass->qualifiedName + "::" + name;
    return fullName();
}

VariableInfo::VariableInfo()
    : isStatic(false), isExtern(false), isMutable(false), isConst(false),
      access(AccessNone), line(0)
{
}

std::string VariableInfo::fullName() const
{
    std::string s = scope;
    if (!qualifier.empty()) { if (!s.empty()) s += "::"; s += qualifier; }
    if (!s.empty()) s += "::";
    return s + name;
}

TypedefInfo::TypedefInfo() : access(AccessNone), line(0) {}
EnumInfo::EnumInfo()       : access(AccessNone), line(0) {}
UsingInfo::UsingInfo()     : line(0) {}
FriendInfo::FriendInfo()   : line(0) {}

BaseClass::BaseClass() : access(AccessPublic), isVirtual(false), resolved(0) {}

ClassInfo::ClassInfo()
    : kind(Class), owner(0), isDefinition(false), isAbstract(false), isTypedefName(false),
      access(AccessNone), line(0), endLine(0)
{
}

const char* ClassInfo::kindName(Kind k)
{
    switch (k) {
    case Struct: return "struct";
    case Union:  return "union";
    default:     return "class";
    }
}

std::string ClassInfo::displayName() const
{
    std::string s = qualifiedName;
    if (!tmpl.specializationArgs.empty()) s += tmpl.specializationArgs;
    if (tmpl.isTemplate) s = tmpl.text() + " " + s;
    return s;
}

SourceFile::SourceFile()
    : isHeader(false), lineCount(0), tokenCount(0), pragmaOnce(false)
{
}

SourceFile::~SourceFile()
{
    for (size_t i = 0; i < allClasses.size(); ++i) delete allClasses[i];
}

} // namespace cppparse
