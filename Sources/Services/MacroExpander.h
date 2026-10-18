// =============================================================================
//  MacroExpander.h - replaces $(Name) macros in command templates
//  (GRIDEd's Services/MacroExpander.h)
//     "$(Assembler)" "$(AsmFile)"  ->  "/Applications/GRIDEm.app/.../asm6x" "obj/../main.asm"
//  Unknown macros are left untouched so mistakes are visible in the Output pane.
//  Names ignore case.
// =============================================================================
#pragma once
#include "../Core/StringUtil.h"
#include <map>
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    class MacroExpander
    {
    public:
        typedef std::map<std::string, std::string, Core::CaseInsensitiveLess> ValueMap;

        void Set(const std::string& name, const std::string& value) { values[name] = value; }
        std::string Get(const std::string& name) const;
        // the template with every known $(Name) replaced, runs of spaces collapsed, trimmed
        std::string Expand(const std::string& text) const;

        // "-I" + list -> -I"a" -I"b"
        static std::string JoinFlags(const std::string& flag, const std::vector<std::string>& items);
        // "-D" + list -> -DNAME -DNAME=1 -D"GREETING=hi there": quoted only when there is a
        // space, a tab or a quote in it (a quote inside is escaped as \")
        static std::string JoinDefines(const std::string& flag, const std::vector<std::string>& items);
        // plain list of quoted items: "a.obj" "b.obj"
        static std::string JoinQuoted(const std::vector<std::string>& items);

        const ValueMap& Values() const { return values; }
        ValueMap& Values() { return values; }

    private:
        ValueMap values;
    };
}}
