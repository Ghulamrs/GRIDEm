// Model.hpp - data model filled by the parser and consumed by the report writers.
//
// Everything the tool knows about a project ends up in these structures:
// preprocessor directives, classes (with bases, methods, fields, nested
// classes), free functions, global variables, typedefs, enums, and the
// project/solution description read from the Visual Studio files.
//
// Strict C++98 - no C++11 or later features are used.

#ifndef CPPPARSE_MODEL_HPP
#define CPPPARSE_MODEL_HPP

#include "Lexer.hpp"

#include <string>
#include <vector>

namespace cppparse {

struct ClassInfo;
struct FunctionInfo;

enum Access { AccessPublic, AccessProtected, AccessPrivate, AccessNone };
const char* accessName(Access a);

// ---- preprocessor ----------------------------------------------------------

struct Directive {
    enum Kind { Include, Define, Undef, If, Ifdef, Ifndef, Elif, Else, Endif,
                Pragma, Error, Warning, Line, Other };
    Kind        kind;
    int         line;
    std::string text;           // as written without '#', line continuations joined
    std::string argument;       // include target / macro name / condition / pragma text
    bool        systemInclude;  // #include <...>
    std::string resolvedPath;   // where a #include was found on disk ("" if not found)
    bool        isIncludeGuard; // part of the file's include guard
    bool        inactive;       // inside an "#if 0" region
    std::string macroParams;    // "(a, b)" for function-like macros
    std::string macroBody;      // replacement text of a #define

    Directive();
    static const char* kindName(Kind k);
};

// ---- templates -------------------------------------------------------------

struct TemplateInfo {
    bool                     isTemplate;
    std::vector<std::string> headers;            // one per "template <...>", e.g. "typename T, int N"
    std::vector<std::string> params;             // every parameter, split
    std::string              specializationArgs; // "<int, 8>" for a specialization

    TemplateInfo();
    std::string text() const;                    // "template <typename T, int N>"
};

// ---- functions -------------------------------------------------------------

struct Parameter {
    std::string type;
    std::string name;
    std::string defaultValue;
};

struct LocalVariable {
    std::string type;
    std::string name;
    std::string initializer;
    int         line;
    LocalVariable();
};

// a call site inside a function body
struct CallInfo {
    enum Kind { Plain, Member, Pointer, Scoped, New, Construct };
    Kind        kind;
    std::string name;        // callee name as written (unqualified)
    std::string qualifier;   // "Foo" for Foo::bar(...)
    std::string receiver;    // identifier before '.' / '->', or "this"
    int         argCount;
    int         line;
    CallInfo();
};

// a suspicious pattern noticed while scanning a body
struct BodyNote {
    std::string code;        // "assign-in-condition", "empty-statement", "catch-by-value", ...
    std::string text;
    int         line;
    BodyNote();
};

// a health finding produced by the version 2 checker
struct Finding {
    enum Severity { Error, Warning, Info };
    Severity    severity;
    std::string code;        // "E142"
    std::string file;
    int         line;
    std::string message;
    std::string detail;
    Finding();
};

struct ExceptionInfo {
    int                      tryBlocks;
    int                      catchHandlers;
    int                      throwStatements;
    bool                     functionTryBlock;
    std::vector<std::string> caughtTypes;
    ExceptionInfo();
};

struct FunctionInfo {
    std::string            name;           // "area", "operator<<", "~Shape", "Shape"
    std::string            qualifier;      // "geom::Circle" for an out-of-line definition (template args stripped)
    std::string            qualifierText;  // qualifier as written, e.g. "FixedStack<T, N>"
    std::string            scope;          // enclosing namespaces, e.g. "geom"
    std::string            returnType;     // empty for constructors, destructors, conversions
    std::vector<Parameter> params;
    std::string            paramText;
    bool isConst, isVolatile, isVirtual, isPureVirtual, isStatic, isInline;
    bool isExplicit, isExtern, isFriend;
    bool isConstructor, isDestructor, isOperator, isConversion;
    bool hasBody, hasInitializerList;
    std::string            initializerList;
    std::string            exceptionSpec;  // "throw(std::bad_alloc)"
    std::string            attributes;     // __declspec(...) and similar
    TemplateInfo           tmpl;
    Access                 access;
    std::string            file;
    int                    line;
    int                    endLine;
    std::string            condition;      // active #if conditions
    ExceptionInfo          exceptions;
    std::vector<LocalVariable> locals;     // variables declared inside the body
    std::vector<std::string>   bodyIdents; // identifiers used in the body (uses, not declarations)
    std::vector<CallInfo>      calls;      // call sites in the body
    std::vector<BodyNote>      notes;      // suspicious patterns seen in the body
    std::vector<std::string>   localTypedefs; // names introduced by typedefs inside the body
    int                        returnsValue; // "return expr;" statements
    int                        returnsVoid;  // "return;" statements
    size_t                     bodyBegin;    // token index of the body's '{' in SourceFile::tokens
    size_t                     bodyEnd;      // token index just past the body's '}'
    bool                       typeChecked;  // version 3 type checker handled the body completely

    // filled by CodeBase::link()
    std::string   definedAt;    // "file:line" of the body when this is a declaration
    std::string   declaredAt;   // "file:line" of the declaration when this is a definition
    FunctionInfo* definition;   // non-owning; the FunctionInfo that holds the body (may be this)
    ClassInfo*    ownerClass;   // non-owning; class the method belongs to (NULL for free functions)
    std::string   linkNote;

    FunctionInfo();
    std::string signature() const;   // "virtual double area() const = 0"
    std::string fullName() const;    // "geom::Circle::area"
    std::string displayName() const; // fullName, with the owning class prefix for methods declared in class
    bool isDefinition() const { return hasBody; }
};

// ---- variables, typedefs, enums ---------------------------------------------

struct VariableInfo {
    std::string type, name, initializer, bitField;
    std::string qualifier;      // "Circle" for "int Circle::s_count = 0;"
    std::string scope, file, condition;
    bool        isStatic, isExtern, isMutable, isConst;
    Access      access;
    int         line;
    std::string definedAt;      // for a static member: where the definition lives
    VariableInfo();
    std::string fullName() const;
};

struct TypedefInfo {
    std::string text, name, scope, file, condition;
    Access      access;
    int         line;
    TypedefInfo();
};

struct EnumInfo {
    std::string              name, scope, file, condition;
    std::vector<std::string> enumerators;
    Access                   access;
    int                      line;
    EnumInfo();
};

struct UsingInfo  { std::string text, file; int line; UsingInfo();  };
struct FriendInfo { std::string text, file; int line; FriendInfo(); };

// ---- classes ---------------------------------------------------------------

struct BaseClass {
    std::string name;       // as written, e.g. "geom::Shape" or "Base<T>"
    Access      access;
    bool        isVirtual;
    ClassInfo*  resolved;   // non-owning, filled by link()
    BaseClass();
};

struct ClassInfo {
    enum Kind { Class, Struct, Union };
    Kind                      kind;
    std::string               name;           // "Circle"
    std::string               qualifiedName;  // "geom::Circle" or "Outer::Inner"
    std::string               scope;          // enclosing namespaces
    std::string               attributes;     // "__declspec(dllexport)", "SAMPLE_API"
    std::string               file, condition;
    TemplateInfo              tmpl;
    std::vector<BaseClass>    bases;
    std::vector<FunctionInfo> methods;
    std::vector<VariableInfo> fields;
    std::vector<TypedefInfo>  typedefs;
    std::vector<EnumInfo>     enums;
    std::vector<FriendInfo>   friends;
    std::vector<UsingInfo>    usings;
    std::vector<ClassInfo*>   nested;         // non-owning
    ClassInfo*                owner;          // enclosing class or NULL
    bool                      isDefinition;   // false for a forward declaration
    bool                      isAbstract;     // has a pure virtual method
    bool                      isTypedefName;  // "typedef struct {...} Name;"
    Access                    access;         // when nested
    int                       line, endLine;

    // filled by link()
    std::vector<ClassInfo*>   derived;        // classes that list this one as a base
    std::vector<ClassInfo*>   associations;   // classes used as field types

    ClassInfo();
    static const char* kindName(Kind k);
    std::string displayName() const;          // qualified name plus template header
};

// ---- files -----------------------------------------------------------------

struct SourceFile {
    std::string path;
    std::string origin;         // "project", "included by X", "command line"
    std::string project;
    bool        isHeader;
    int         lineCount;
    int         tokenCount;

    std::vector<Directive>    directives;
    std::string               includeGuard;
    bool                      pragmaOnce;

    std::vector<ClassInfo*>   classes;      // top level (including inside namespaces), non-owning
    std::vector<ClassInfo*>   allClasses;   // owning, flat, in order of appearance
    std::vector<FunctionInfo> functions;    // free functions and out-of-line member definitions
    std::vector<VariableInfo> variables;    // globals and static member definitions
    std::vector<TypedefInfo>  typedefs;
    std::vector<EnumInfo>     enums;
    std::vector<UsingInfo>    usings;
    std::vector<std::string>  namespaces;
    std::vector<std::string>  diagnostics;  // "L12: ..." notes about things the parser skipped
    std::vector<Token>        tokens;       // kept for the version 3 body parser

    SourceFile();
    ~SourceFile();
private:
    SourceFile(const SourceFile&);
    SourceFile& operator=(const SourceFile&);
};

// ---- project / solution ----------------------------------------------------

struct ProjectInfo {
    std::string path;
    std::string name;
    std::string kind;        // "vcxproj", "vcproj", "files"
    std::string version;     // "Visual Studio 2010 (ToolsVersion 4.0)"
    std::string toolset;     // "v100"
    std::vector<std::string> configurations;
    std::vector<std::string> headerFiles;    // resolved absolute paths
    std::vector<std::string> sourceFiles;
    std::vector<std::string> otherFiles;
    std::vector<std::string> includeDirs;
    std::vector<std::string> preprocessorDefinitions;
    std::vector<std::string> warnings;
};

struct SolutionInfo {
    std::string path;
    std::string version;
    std::vector<std::string> projectPaths;
    std::vector<std::string> warnings;
};

} // namespace cppparse

#endif
