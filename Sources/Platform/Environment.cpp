// =============================================================================
//  Environment.cpp - see Environment.h
// =============================================================================
#include "Environment.h"
#include <cstdlib>
#include <cstring>

extern char** environ;

namespace GRIDEm { namespace Platform {

    static bool IsNameChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    std::string Environment::Expand(const std::string& text)
    {
        std::string out;
        size_t i = 0;
        while (i < text.size())
        {
            char c = text[i];
            if (c == '%')
            {
                size_t end = text.find('%', i + 1);
                if (end != std::string::npos && end > i + 1)
                {
                    std::string name = text.substr(i + 1, end - i - 1);
                    const char* v = std::getenv(name.c_str());
                    if (v != nullptr) { out += v; i = end + 1; continue; }
                }
            }
            else if (c == '$' && i + 1 < text.size())
            {
                if (text[i + 1] == '{')
                {
                    size_t end = text.find('}', i + 2);
                    if (end != std::string::npos)
                    {
                        std::string name = text.substr(i + 2, end - i - 2);
                        const char* v = std::getenv(name.c_str());
                        if (v != nullptr) { out += v; i = end + 1; continue; }
                    }
                }
                else if (IsNameChar(text[i + 1]))
                {
                    size_t end = i + 1;
                    while (end < text.size() && IsNameChar(text[end])) end++;
                    std::string name = text.substr(i + 1, end - i - 1);
                    const char* v = std::getenv(name.c_str());
                    if (v != nullptr) { out += v; i = end; continue; }
                }
            }
            out += c;
            i++;
        }
        return out;
    }

    std::map<std::string, std::string> Environment::All()
    {
        std::map<std::string, std::string> r;
        for (char** e = environ; e != nullptr && *e != nullptr; e++)
        {
            const char* eq = std::strchr(*e, '=');
            if (eq == nullptr) continue;
            r[std::string(*e, (size_t)(eq - *e))] = std::string(eq + 1);
        }
        return r;
    }

    static std::string TrimBlanks(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t')) b++;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
        return s.substr(b, e - b);
    }

    std::vector<std::string> Environment::WithChanges(const std::vector<std::string>& changes)
    {
        std::map<std::string, std::string> env = All();
        for (const std::string& pair : changes)
        {
            size_t eq = pair.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            std::string name = TrimBlanks(pair.substr(0, eq));
            std::string value = Expand(TrimBlanks(pair.substr(eq + 1)));
            if (name.empty()) continue;
            if (value.empty()) env.erase(name);
            else env[name] = value;
        }
        std::vector<std::string> block;
        for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
            block.push_back(it->first + "=" + it->second);
        return block;
    }
}}
