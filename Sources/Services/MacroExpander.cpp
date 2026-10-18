// =============================================================================
//  MacroExpander.cpp - see MacroExpander.h
// =============================================================================
#include "MacroExpander.h"

namespace GRIDEm { namespace Services {

    using Core::StringUtil;

    std::string MacroExpander::Get(const std::string& name) const
    {
        ValueMap::const_iterator it = values.find(name);
        if (it != values.end()) return it->second;
        return std::string();
    }

    // \$\((\w+)\) by hand: \w is a letter, a digit or '_'
    std::string MacroExpander::Expand(const std::string& text) const
    {
        if (text.empty()) return std::string();
        std::string r;
        size_t i = 0;
        while (i < text.size())
        {
            if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '(')
            {
                size_t j = i + 2;
                while (j < text.size() && StringUtil::IsIdentifierChar(text[j])) j++;
                if (j > i + 2 && j < text.size() && text[j] == ')')
                {
                    std::string name = text.substr(i + 2, j - i - 2);
                    ValueMap::const_iterator it = values.find(name);
                    if (it != values.end()) r += it->second;
                    else r += text.substr(i, j - i + 1);
                    i = j + 1;
                    continue;
                }
            }
            r += text[i];
            i++;
        }
        // collapse runs of spaces left by empty macros
        std::string collapsed;
        for (size_t k = 0; k < r.size(); k++)
        {
            if (r[k] == ' ' && !collapsed.empty() && collapsed[collapsed.size() - 1] == ' ') continue;
            collapsed += r[k];
        }
        return StringUtil::Trim(collapsed);
    }

    std::string MacroExpander::JoinFlags(const std::string& flag, const std::vector<std::string>& items)
    {
        std::string sb;
        for (const std::string& s : items)
        {
            if (s.empty()) continue;
            if (!sb.empty()) sb += ' ';
            sb += flag;
            sb += '"';
            sb += s;
            sb += '"';
        }
        return sb;
    }

    std::string MacroExpander::JoinDefines(const std::string& flag, const std::vector<std::string>& items)
    {
        std::string sb;
        for (const std::string& s : items)
        {
            if (s.empty()) continue;
            if (!sb.empty()) sb += ' ';
            sb += flag;
            if (s.find_first_of(" \t\"") == std::string::npos) sb += s;
            else sb += "\"" + StringUtil::Replace(s, "\"", "\\\"") + "\"";
        }
        return sb;
    }

    std::string MacroExpander::JoinQuoted(const std::vector<std::string>& items)
    {
        return JoinFlags(std::string(), items);
    }
}}
