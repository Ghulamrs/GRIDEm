// =============================================================================
//  StringUtil.cpp - see StringUtil.h
// =============================================================================
#include "StringUtil.h"
#include <cerrno>
#include <cstdlib>

namespace GRIDEm { namespace Core {

    bool CaseInsensitiveLess::operator()(const std::string& a, const std::string& b) const
    {
        return StringUtil::CompareIgnoreCase(a, b) < 0;
    }

    std::string StringUtil::Trim(const std::string& s) { return TrimEnd(TrimStart(s)); }

    std::string StringUtil::TrimStart(const std::string& s)
    {
        size_t i = 0;
        while (i < s.size() && IsSpace(s[i])) i++;
        return s.substr(i);
    }

    std::string StringUtil::TrimEnd(const std::string& s)
    {
        size_t n = s.size();
        while (n > 0 && IsSpace(s[n - 1])) n--;
        return s.substr(0, n);
    }

    std::string StringUtil::TrimEnd(const std::string& s, char c)
    {
        size_t n = s.size();
        while (n > 0 && s[n - 1] == c) n--;
        return s.substr(0, n);
    }

    std::string StringUtil::ToUpper(const std::string& s)
    {
        std::string r = s;
        for (char& c : r) c = ToUpperChar(c);
        return r;
    }

    std::string StringUtil::ToLower(const std::string& s)
    {
        std::string r = s;
        for (char& c : r) c = ToLowerChar(c);
        return r;
    }

    bool StringUtil::StartsWith(const std::string& s, const std::string& prefix)
    {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    bool StringUtil::EndsWith(const std::string& s, const std::string& suffix)
    {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool StringUtil::StartsWithIgnoreCase(const std::string& s, const std::string& prefix)
    {
        return s.size() >= prefix.size() && EqualsIgnoreCase(s.substr(0, prefix.size()), prefix);
    }

    bool StringUtil::EndsWithIgnoreCase(const std::string& s, const std::string& suffix)
    {
        return s.size() >= suffix.size() && EqualsIgnoreCase(s.substr(s.size() - suffix.size()), suffix);
    }

    bool StringUtil::EqualsIgnoreCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (ToLowerChar(a[i]) != ToLowerChar(b[i])) return false;
        return true;
    }

    int StringUtil::CompareIgnoreCase(const std::string& a, const std::string& b)
    {
        size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; i++)
        {
            unsigned char x = (unsigned char)ToLowerChar(a[i]);
            unsigned char y = (unsigned char)ToLowerChar(b[i]);
            if (x != y) return x < y ? -1 : 1;
        }
        if (a.size() == b.size()) return 0;
        return a.size() < b.size() ? -1 : 1;
    }

    bool StringUtil::Contains(const std::string& s, const std::string& part)
    {
        return s.find(part) != std::string::npos;
    }

    int StringUtil::IndexOfIgnoreCase(const std::string& s, const std::string& part, size_t from)
    {
        if (part.empty()) return from <= s.size() ? (int)from : -1;
        if (part.size() > s.size()) return -1;
        for (size_t i = from; i + part.size() <= s.size(); i++)
        {
            bool same = true;
            for (size_t j = 0; j < part.size(); j++)
                if (ToLowerChar(s[i + j]) != ToLowerChar(part[j])) { same = false; break; }
            if (same) return (int)i;
        }
        return -1;
    }

    bool StringUtil::ContainsIgnoreCase(const std::string& s, const std::string& part)
    {
        return IndexOfIgnoreCase(s, part, 0) >= 0;
    }

    bool StringUtil::IsNullOrWhiteSpace(const std::string& s)
    {
        for (char c : s)
            if (!IsSpace(c)) return false;
        return true;
    }

    std::string StringUtil::Replace(const std::string& s, const std::string& from, const std::string& to)
    {
        if (from.empty()) return s;
        std::string r;
        size_t i = 0;
        while (true)
        {
            size_t j = s.find(from, i);
            if (j == std::string::npos) { r += s.substr(i); break; }
            r += s.substr(i, j - i);
            r += to;
            i = j + from.size();
        }
        return r;
    }

    std::string StringUtil::ReplaceIgnoreCase(const std::string& s, const std::string& from, const std::string& to)
    {
        if (from.empty()) return s;
        std::string r;
        size_t i = 0;
        while (true)
        {
            int j = IndexOfIgnoreCase(s, from, i);
            if (j < 0) { r += s.substr(i); break; }
            r += s.substr(i, (size_t)j - i);
            r += to;
            i = (size_t)j + from.size();
        }
        return r;
    }

    std::string StringUtil::ReplaceChar(const std::string& s, char from, char to)
    {
        std::string r = s;
        for (char& c : r)
            if (c == from) c = to;
        return r;
    }

    std::vector<std::string> StringUtil::Split(const std::string& s, char sep)
    {
        std::vector<std::string> r;
        size_t i = 0;
        while (true)
        {
            size_t j = s.find(sep, i);
            if (j == std::string::npos) { r.push_back(s.substr(i)); break; }
            r.push_back(s.substr(i, j - i));
            i = j + 1;
        }
        return r;
    }

    std::vector<std::string> StringUtil::SplitNonEmpty(const std::string& s, const std::string& seps)
    {
        std::vector<std::string> r;
        std::string cur;
        for (char c : s)
        {
            if (seps.find(c) != std::string::npos)
            {
                if (!cur.empty()) r.push_back(cur);
                cur.clear();
            }
            else cur += c;
        }
        if (!cur.empty()) r.push_back(cur);
        return r;
    }

    std::vector<std::string> StringUtil::SplitLines(const std::string& s)
    {
        std::vector<std::string> r;
        std::string cur;
        for (size_t i = 0; i < s.size(); i++)
        {
            char c = s[i];
            if (c == '\r')
            {
                r.push_back(cur);
                cur.clear();
                if (i + 1 < s.size() && s[i + 1] == '\n') i++;
            }
            else if (c == '\n')
            {
                r.push_back(cur);
                cur.clear();
            }
            else cur += c;
        }
        r.push_back(cur);
        return r;
    }

    std::string StringUtil::Join(const std::string& sep, const std::vector<std::string>& parts)
    {
        std::string r;
        for (size_t i = 0; i < parts.size(); i++)
        {
            if (i > 0) r += sep;
            r += parts[i];
        }
        return r;
    }

    bool StringUtil::TryParseLong(const std::string& s, long long& out)
    {
        std::string t = Trim(s);
        if (t.empty()) return false;
        char* end = nullptr;
        errno = 0;
        long long v = std::strtoll(t.c_str(), &end, 10);
        if (errno != 0 || end == t.c_str() || *end != '\0') return false;
        out = v;
        return true;
    }

    bool StringUtil::TryParseInt(const std::string& s, int& out)
    {
        long long v = 0;
        if (!TryParseLong(s, v)) return false;
        if (v < -2147483647LL - 1 || v > 2147483647LL) return false;
        out = (int)v;
        return true;
    }

    bool StringUtil::TryParseHex(const std::string& s, unsigned long long& out)
    {
        std::string t = Trim(s);
        if (StartsWithIgnoreCase(t, "0x")) t = t.substr(2);
        if (t.empty()) return false;
        char* end = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(t.c_str(), &end, 16);
        if (errno != 0 || *end != '\0') return false;
        out = v;
        return true;
    }

    bool StringUtil::ContainsIgnoreCase(const std::vector<std::string>& list, const std::string& item)
    {
        return IndexOfIgnoreCase(list, item) >= 0;
    }

    int StringUtil::IndexOfIgnoreCase(const std::vector<std::string>& list, const std::string& item)
    {
        for (size_t i = 0; i < list.size(); i++)
            if (EqualsIgnoreCase(list[i], item)) return (int)i;
        return -1;
    }

    void StringUtil::AddUniqueIgnoreCase(std::vector<std::string>& list, const std::string& item)
    {
        if (!ContainsIgnoreCase(list, item)) list.push_back(item);
    }
}}
