// Instantiate.hpp - version 4: template instantiation.
//
// A class template used with concrete arguments ("FixedStack<double, 32>")
// becomes a real ClassInfo: the template parameters are substituted in the
// field types, method signatures, bases and typedefs, and every method
// body (in-class or out-of-line) gets its own copy of the body tokens with
// the parameters replaced. A function template called with arguments whose
// types let the parameters be deduced ("maxOf(7, 2)") becomes a real
// FunctionInfo in the same way. The instantiated bodies are then parsed,
// type checked and compiled like any other function.
//
// Strict C++98.
#ifndef CPPPARSE_INSTANTIATE_HPP
#define CPPPARSE_INSTANTIATE_HPP

#include "Types.hpp"

#include <map>
#include <string>
#include <vector>

namespace cppparse {

typedef std::map<std::string, std::string> Substitution;   // template parameter name -> argument text

class Instantiator {
public:
    explicit Instantiator(CodeBase& cb);
    ~Instantiator();

    // "FixedStack<double, 32>": returns the instantiated class (cached), a user-written
    // explicit specialization when one matches, or 0 when the template cannot be instantiated.
    ClassInfo* instantiateClass(const ClassInfo* tmpl, const std::string& argsText, TypeTable& types);

    // function template with the parameters bound by 'subst'
    FunctionInfo* instantiateFunction(const FunctionInfo* tmpl, const Substitution& subst, TypeTable& types);

    // instantiated functions whose bodies still have to be compiled (the list is cleared)
    void takePending(std::vector<FunctionInfo*>& out);
    const std::vector<Token>* tokensFor(const FunctionInfo* fn) const;
    const std::vector<ClassInfo*>& classes() const { return m_ownedClasses; }
    int classCount() const { return static_cast<int>(m_ownedClasses.size()); }
    int functionCount() const { return m_functionCount; }

    // helpers
    static void parameterNames(const TemplateInfo& t, std::vector<std::string>& names, std::vector<std::string>& defaults);
    static void splitArgs(const std::string& argsText, std::vector<std::string>& out);
    static std::string substitute(const std::string& text, const Substitution& subst);
    static std::string normalizeArgs(const std::string& argsText);
    // true when an argument is still a template parameter or an unknown name ("T", "N - 1", "K")
    bool argumentsDependent(const std::string& argsText, TypeTable& types) const;

private:
    Instantiator(const Instantiator&);
    Instantiator& operator=(const Instantiator&);

    std::vector<Token>* substituteBody(const FunctionInfo& fn, const Substitution& subst, size_t& bodyBegin, size_t& bodyEnd);
    void substituteFunction(FunctionInfo& fn, const Substitution& subst);

    CodeBase&                                  m_cb;
    std::map<std::string, ClassInfo*>          m_classes;      // key: template qualified name + <args>
    std::map<std::string, FunctionInfo*>       m_functions;
    std::map<const FunctionInfo*, std::vector<Token>*> m_bodies;
    std::vector<FunctionInfo*>                 m_pending;
    std::vector<ClassInfo*>                    m_ownedClasses;
    std::vector<FunctionInfo*>                 m_ownedFunctions;
    std::vector<std::vector<Token>*>           m_ownedTokens;
    int                                        m_functionCount;
};

} // namespace cppparse

#endif
