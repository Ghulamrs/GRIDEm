// CxxAst.hpp - version 6: the syntax tree of a translation unit.
//
// One tree for everything: declarations, class and enum definitions,
// templates, statements and expressions, exactly as written. Types appear
// as written too (decl-specifiers plus declarators); semantic analysis
// turns them into Type objects and attaches symbols later. All nodes are
// owned by a CxxArena and live as long as it does.
//
// Strict C++98.
#ifndef CPPPARSE_CXXAST_HPP
#define CPPPARSE_CXXAST_HPP

#include <string>
#include <vector>

namespace cppparse {
namespace sema { struct Type; struct Symbol; struct Scope; }
namespace cxx {

struct Expr;
struct Stmt;
struct Decl;
struct TypeId;
struct ClassDecl;
struct EnumDecl;
struct Declarator;

struct Loc {
    int file, line;
    Loc() : file(0), line(0) {}
};


struct TemplateArg {               // a type-id or an expression (decided syntactically where possible)
    TypeId* type;
    Expr*   expr;
    TemplateArg() : type(0), expr(0) {}
};

struct NameComponent {
    std::string              ident;        // "vector", "operator+", "~Shape", "operator int" (conversion: see convType)
    bool                     hasTemplateArgs;
    std::vector<TemplateArg> templateArgs;
    TypeId*                  convType;     // conversion function: operator <type>
    NameComponent() : hasTemplateArgs(false), convType(0) {}
};

struct QualifiedName {              // [::] c1 :: c2 :: ... :: cn
    bool                       global;      // leading ::
    std::vector<NameComponent> parts;
    std::string                text() const;
    const std::string&         last() const { static const std::string empty; return parts.empty() ? empty : parts.back().ident; }
    bool                       empty() const { return parts.empty(); }
    QualifiedName() : global(false) {}
};


struct DeclSpec {                   // decl-specifier-seq
    // storage class and function specifiers
    bool isTypedef, isStatic, isExtern, isMutable, isRegister, isAuto, isFriend, isVirtual, isExplicit, isInline;
    // cv
    bool isConst, isVolatile;
    // the type part: either builtin flags, a named type, or an inline class/enum definition
    enum Kind { None, Builtin, Named, Class, Enum };
    Kind          kind;
    int           builtinChar, builtinInt, builtinLong, builtinShort, builtinSigned, builtinUnsigned, builtinFloat, builtinDouble, builtinVoid, builtinBool, builtinWchar;
    QualifiedName name;             // Named: possibly with "typename" / elaborated "class X" / "struct X" / "enum X"
    std::string   elaborated;       // "class", "struct", "union", "enum", "typename" or ""
    ClassDecl*    classDef;         // Class: the definition
    EnumDecl*     enumDef;          // Enum: the definition
    Loc           loc;
    int           align;            // __declspec(align(N)) among the specifiers (0: none)
    DeclSpec();
    bool hasType() const { return kind != None; }
    std::string text() const;       // "const unsigned int", "std::vector<int>"
};

struct Param {                      // parameter-declaration
    DeclSpec    spec;
    Declarator* decl;               // may be abstract (no name)
    Expr*       defaultValue;
    Loc         loc;
    Param() : decl(0), defaultValue(0) {}
};

struct DeclaratorPart {
    enum Kind { Pointer, Reference, Array, Function, MemberPointer, Paren };
    Kind                 kind;
    bool                 isConst, isVolatile;    // Pointer / MemberPointer cv, or the cv-qualifier of a Function (const method)
    Expr*                arraySize;              // Array: may be 0 for []
    std::vector<Param*>  params;                 // Function
    bool                 variadic;               // Function: trailing ...
    std::vector<TypeId*> throwSpec;              // Function: throw(...) types
    bool                 hasThrowSpec;
    QualifiedName        memberClass;            // MemberPointer: C in C::*
    DeclaratorPart() : kind(Pointer), isConst(false), isVolatile(false), arraySize(0), variadic(false), hasThrowSpec(false) {}
};

struct Declarator {                 // declarator or abstract-declarator
    QualifiedName               name;           // empty for abstract declarators
    std::vector<DeclaratorPart> parts;          // applied to the base type in this order: "int *a[3]" is Pointer, Array
    Expr*                       bitfield;       // member bit-field width
    Loc                         loc;
    sema::Symbol*               semaSym;        // the entity this declarator declares (stage 3)
    Declarator() : bitfield(0), semaSym(0) {}
    bool isFunction() const;                    // the outermost part is a function
    const DeclaratorPart* functionPart() const;
    std::string text() const;                   // "*p[3]"
};

struct TypeId {                     // type-specifier-seq abstract-declarator (casts, sizeof, new, template arguments)
    DeclSpec    spec;
    Declarator* decl;               // abstract
    TypeId() : decl(0) {}
    std::string text() const;
};


struct Expr {
    enum Kind {
        IntLit, FloatLit, CharLit, StringLit, BoolLit, This,
        Name,            // qualified name (possibly template-id)
        Paren,           // ( e )
        Unary,           // op e   (op: + - ! ~ * & ++ -- )
        PostIncDec,      // e ++ / e --
        Binary,          // e op e
        Assign,          // e op= e
        Cond,            // c ? a : b
        Comma,
        Call,            // callee ( args )
        Index,           // e [ e ]
        Member,          // e . name / e -> name  (text: "." or "->"), templateKeyword
        Cast,            // ( type-id ) e
        NamedCast,       // static_cast<type-id>(e) etc. (text: which)
        FunctionalCast,  // T ( args )
        SizeofExpr, SizeofType,
        New,             // new [(placement)] type [(init)] ; arrays via the declarator
        Delete,          // delete [ [] ] e
        Throw,           // throw [e]
        Typeid,          // typeid(e) / typeid(type)
        InitList,        // { e, e, ... } nested in a brace initializer (kids: the elements)
        Error
    };
    Kind               kind;
    std::string        text;        // literal text, operator text, member name text
    QualifiedName      name;        // Name, Member (the member name)
    std::vector<Expr*> kids;        // operands / arguments
    TypeId*            type;        // Cast, NamedCast, FunctionalCast, SizeofType, New, Typeid
    std::vector<Expr*> placement;   // New
    bool               arrayDelete; // Delete
    bool               hasInit;     // New: parentheses present
    bool               arrowMember; // Member: ->
    bool               globalScope; // New, Delete: ::new, ::delete (the class's operators are not looked up)
    Loc                loc;
    // filled by semantic analysis (version 6 stage 3)
    const sema::Type*  semaType;    // the expression's type after lvalue-to-rvalue etc. is NOT applied (see semaLvalue)
    sema::Symbol*      semaSym;     // the entity a Name/Member/Call refers to (variable, function chosen by overload resolution, ...)
    bool               semaLvalue;
    bool               semaConst;   // a constant expression
    long               semaValue;   // its value when semaConst
    Expr() : kind(Error), type(0), arrayDelete(false), hasInit(false), arrowMember(false), globalScope(false), semaType(0), semaSym(0), semaLvalue(false), semaConst(false), semaValue(0) {}
};


struct Stmt {
    enum Kind { Compound, ExprStmt, DeclStmt, If, Switch, While, DoWhile, For, Break, Continue, Return, Goto, Label, Case, Default, Try, Handler, Empty, Asm, Error };
    Kind               kind;
    Expr*              expr;        // condition / expression / return value / case value
    Decl*              decl;        // DeclStmt; If/Switch/While/For: a condition declaration; For: init declaration; Handler: the exception declaration
    Expr*              expr2;       // For: increment
    Stmt*              init;        // For: init statement
    std::vector<Stmt*> kids;        // Compound: statements; If: then [, else]; loops: body; Switch: body; Try: body + handlers; Label/Case/Default: the statement
    std::string        label;       // Goto / Label
    bool               catchAll;    // Handler: catch (...)
    Loc                loc;
    Stmt() : kind(Empty), expr(0), decl(0), expr2(0), init(0), catchAll(false) {}
};


enum Access { AccessDefault, AccessPublic, AccessProtected, AccessPrivate };

struct Initializer {
    enum Kind { None, Assign, Parens, Brace };  // = e  /  ( args )  /  { e, e }
    Kind               kind;
    std::vector<Expr*> args;
    Initializer() : kind(None) {}
};

struct InitDeclarator {
    Declarator* decl;
    Initializer init;
    bool        pureVirtual;        // = 0 on a function declarator
    InitDeclarator() : decl(0), pureVirtual(false) {}
};

struct MemInit {                    // constructor initializer: name ( args )
    QualifiedName      name;
    std::vector<Expr*> args;
    Loc                loc;
    sema::Symbol*      semaSym;   // the constructor chosen by overload resolution for a class (stage 3)
    MemInit() : semaSym(0) {}
};

struct TemplateParam {
    enum Kind { Type, NonType, Template };
    Kind         kind;
    std::string  name;              // may be empty
    TypeId*      defaultType;       // Type
    Param*       nonType;           // NonType: a parameter declaration (its defaultValue is the default)
    std::vector<TemplateParam*> params;   // Template template parameter
    Loc          loc;
    TemplateParam() : kind(Type), defaultType(0), nonType(0) {}
};

struct BaseSpec {
    Access        access;
    bool          isVirtual;
    QualifiedName name;
    Loc           loc;
    BaseSpec() : access(AccessDefault), isVirtual(false) {}
};

struct Enumerator {
    std::string name;
    Expr*       value;
    Loc         loc;
    Enumerator() : value(0) {}
};

struct Decl {
    enum Kind {
        Simple,          // decl-specifiers + init-declarators ;  (variables, typedefs, function declarations, members)
        Function,        // function definition
        Class,           // class-specifier used as a declaration (also inside DeclSpec of members)
        Enum,
        Namespace,
        NamespaceAlias,
        UsingDirective,
        UsingDecl,
        Template,        // template <params> declaration
        ExplicitInstantiation,
        LinkageSpec,     // extern "C" { ... } or extern "C" declaration
        AccessLabel,     // public: / protected: / private: inside a class
        Friend,          // friend class X; (friend functions are Simple/Function with spec.isFriend)
        Empty,
        Error
    };
    Kind                        kind;
    Loc                         loc;
    Access                      access;        // member access (classes); AccessDefault elsewhere
    // Simple / Function
    DeclSpec                    spec;
    std::vector<InitDeclarator> declarators;   // Simple
    Declarator*                 declarator;    // Function
    std::vector<MemInit>        memInits;      // Function (constructor)
    Stmt*                       body;          // Function: Compound (or Try for a function-try-block)
    // Class / Enum / Namespace / Template / LinkageSpec
    ClassDecl*                  classDef;
    EnumDecl*                   enumDef;
    std::string                 name;          // Namespace, NamespaceAlias, UsingDirective target text, LinkageSpec language
    QualifiedName               target;        // NamespaceAlias / UsingDirective / UsingDecl
    std::vector<Decl*>          decls;         // Namespace / LinkageSpec members; Template: the one declaration
    std::vector<TemplateParam*> templateParams;
    bool                        explicitSpecialization;   // template <>
    sema::Symbol*               semaSym;        // Function / Class / Enum / Namespace / Template: the symbol (stage 3)
    Decl() : kind(Empty), access(AccessDefault), declarator(0), body(0), classDef(0), enumDef(0), explicitSpecialization(false), semaSym(0) {}
};

struct ClassDecl {
    std::string           key;      // "class", "struct", "union"
    QualifiedName         name;     // may be empty (anonymous), qualified (out-of-line nested definition) or a template-id (specialisation)
    std::vector<BaseSpec> bases;
    std::vector<Decl*>    members;  // each with its access
    bool                  isDefinition;
    Loc                   loc;
    int                   pack;       // #pragma pack in force at the class key (0: the default)
    int                   declAlign;  // __declspec(align(N)) on the class (0: none)
    ClassDecl() : isDefinition(false), pack(0), declAlign(0) {}
};

struct EnumDecl {
    std::string             name;
    std::vector<Enumerator> enumerators;
    bool                    isDefinition;
    Loc                     loc;
    EnumDecl() : isDefinition(false) {}
};

struct TranslationUnit {
    std::string        path;
    std::vector<Decl*> decls;
};


class CxxArena {
public:
    CxxArena() {}
    ~CxxArena();
    Expr*          expr(Expr::Kind k, const Loc& l) { Expr* e = new Expr; e->kind = k; e->loc = l; m_exprs.push_back(e); return e; }
    Stmt*          stmt(Stmt::Kind k, const Loc& l) { Stmt* s = new Stmt; s->kind = k; s->loc = l; m_stmts.push_back(s); return s; }
    Decl*          decl(Decl::Kind k, const Loc& l) { Decl* d = new Decl; d->kind = k; d->loc = l; m_decls.push_back(d); return d; }
    Declarator*    declarator() { Declarator* d = new Declarator; m_declarators.push_back(d); return d; }
    TypeId*        typeId() { TypeId* t = new TypeId; m_typeIds.push_back(t); return t; }
    Param*         param() { Param* p = new Param; m_params.push_back(p); return p; }
    ClassDecl*     classDecl() { ClassDecl* c = new ClassDecl; m_classes.push_back(c); return c; }
    EnumDecl*      enumDecl() { EnumDecl* e = new EnumDecl; m_enums.push_back(e); return e; }
    TemplateParam* templateParam() { TemplateParam* t = new TemplateParam; m_tparams.push_back(t); return t; }
private:
    CxxArena(const CxxArena&);
    CxxArena& operator=(const CxxArena&);
    std::vector<Expr*> m_exprs;
    std::vector<Stmt*> m_stmts;
    std::vector<Decl*> m_decls;
    std::vector<Declarator*> m_declarators;
    std::vector<TypeId*> m_typeIds;
    std::vector<Param*> m_params;
    std::vector<ClassDecl*> m_classes;
    std::vector<EnumDecl*> m_enums;
    std::vector<TemplateParam*> m_tparams;
};

// text dump of a translation unit (one node per line, indented), used by --parse6 and the tests
std::string dumpTranslationUnit(const TranslationUnit& tu);
// an expression as source-like text, as the dump shows it ("R - 1")
std::string expressionText(const Expr* e);

struct Counts {
    int namespaces, classes, enums, functionsDefined, functionsDeclared, variables, typedefs, templates, statements, expressions, usings;
    Counts() : namespaces(0), classes(0), enums(0), functionsDefined(0), functionsDeclared(0), variables(0), typedefs(0), templates(0), statements(0), expressions(0), usings(0) {}
};
void countTranslationUnit(const TranslationUnit& tu, Counts& out);

// deep copies for template instantiation: fresh nodes without semantic annotations, owned by the arena
Decl*      cloneDecl(const Decl* d, CxxArena& arena);
ClassDecl* cloneClassDecl(const ClassDecl* c, CxxArena& arena);

} // namespace cxx
} // namespace cppparse

#endif
