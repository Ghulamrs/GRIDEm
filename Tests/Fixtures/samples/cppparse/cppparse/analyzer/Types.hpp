// Types.hpp - version 3: the type system used by the type checker, the IR
// builder and the lowering pass.
//
// Types are interned: two equal types are the same pointer, so they can be
// compared by address. Declaration text from the model ("const Shape*&",
// "std::vector<int>", "unsigned long") is turned into a Type by
// TypeTable::parse, which resolves classes, typedefs and enums through the
// CodeBase. Names it cannot resolve become opaque Unknown types that convert
// to anything and never produce findings.
//
// Strict C++98.
#ifndef CPPPARSE_TYPES_HPP
#define CPPPARSE_TYPES_HPP

#include "CodeBase.hpp"
#include "Parallel.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

class Instantiator;

struct Type {
    enum Kind { Void, Bool, Char, Short, Int, Long, LongLong, Float, Double,
                Pointer, Reference, Array, Class, Enum, Function, Unknown, Null };
    Kind             kind;
    bool             isConst;
    bool             isUnsigned;
    const Type*      inner;      // pointee / referent / element / return type
    int              count;      // array element count (0 = unknown)
    const ClassInfo* cls;        // Class
    std::string      name;       // Enum / Unknown / Class display name

    Type();
    bool isIntegral()   const { return (kind >= Bool && kind <= LongLong) || kind == Enum; }
    bool isFloating()   const { return kind == Float || kind == Double; }
    bool isArithmetic() const { return isIntegral() || isFloating(); }
    bool isPointer()    const { return kind == Pointer || kind == Null || kind == Function; }
    bool isScalar()     const { return isArithmetic() || isPointer() || kind == Unknown; }
    bool isClass()      const { return kind == Class; }
    bool isUnknown()    const { return kind == Unknown; }
    bool isVoid()       const { return kind == Void; }
    int  rank()         const;   // arithmetic conversion rank
    int  builtinSize()  const;   // bytes for builtin / pointer types, 0 for class or unknown
    std::string str()   const;
    std::string key()   const;   // interning key
};

class TypeTable {
public:
    explicit TypeTable(const CodeBase& cb);
    ~TypeTable();

    const Type* builtin(Type::Kind k, bool isUnsigned = false, bool isConst = false);
    const Type* voidType()                  { return builtin(Type::Void); }
    const Type* boolType()                  { return builtin(Type::Bool); }
    const Type* intType()                   { return builtin(Type::Int); }
    const Type* doubleType()                { return builtin(Type::Double); }
    const Type* nullType();
    const Type* pointer(const Type* to, bool isConst = false);
    const Type* reference(const Type* to);
    const Type* array(const Type* elem, int count);
    const Type* classType(const ClassInfo* cls, bool isConst = false);
    const Type* enumType(const std::string& name, bool isConst = false);
    const Type* unknown(const std::string& name, bool isConst = false);
    const Type* function(const Type* ret);
    const Type* withConst(const Type* t, bool c);
    const Type* unref(const Type* t) const  { return t && t->kind == Type::Reference ? t->inner : t; }
    const Type* decay(const Type* t);       // reference -> referent, array -> pointer
    const Type* charPtr()                   { return pointer(builtin(Type::Char, false, true)); }

    // Parses declaration text. 'scope' and 'owner' direct the class lookup.
    const Type* parse(const std::string& text, const std::string& scope, const ClassInfo* owner);
    bool        isTypeName(const std::string& name, const std::string& scope, const ClassInfo* owner);
    void        addTypedef(const std::string& name, const std::string& target) { Lock l(semanticMutex()); m_typedefs[name] = target; }
    void        setInstantiator(Instantiator* inst) { m_instantiator = inst; }
    Instantiator* instantiator() const { return m_instantiator; }
    void        addEnumerator(const std::string& name, const std::string& enumName, long value) { Lock l(semanticMutex()); m_enumerators[name] = std::make_pair(enumName, value); }
    bool        isEnumName(const std::string& name) const { return m_enums.count(name) != 0; }

    // enumerator lookup: "Red" -> enum name, value index
    bool        findEnumerator(const std::string& name, const ClassInfo* owner, std::string& enumName, long& value) const;

private:
    const Type* intern(const Type& t);
    void        indexTypedefsAndEnums();

    const CodeBase&                     m_cb;
    std::map<std::string, Type*>        m_types;
    std::map<std::string, std::string>  m_typedefs;     // name and Class::name -> target text
    std::map<std::string, std::string>  m_enums;        // enum name -> scope
    std::map<std::string, std::pair<std::string, long> > m_enumerators; // name -> (enum, value)
    int                                 m_depth;
    Instantiator*                       m_instantiator;
};

} // namespace cppparse

#endif
