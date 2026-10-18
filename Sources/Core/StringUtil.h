// =============================================================================
//  StringUtil.h - the System::String operations GRIDEd leans on, for UTF-8
//  std::string. Case-insensitive comparisons fold ASCII letters only, which is
//  what every use in GRIDEd needs (file names, tool words, option ids).
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace GRIDEm { namespace Core {

    // a std::map ordering that ignores ASCII case: GRIDEd's
    // Dictionary<String^, T>(StringComparer::OrdinalIgnoreCase)
    struct CaseInsensitiveLess
    {
        bool operator()(const std::string& a, const std::string& b) const;
    };

    class StringUtil
    {
    public:
        static std::string Trim(const std::string& s);
        static std::string TrimStart(const std::string& s);
        static std::string TrimEnd(const std::string& s);
        static std::string TrimEnd(const std::string& s, char c);
        static std::string ToUpper(const std::string& s);
        static std::string ToLower(const std::string& s);

        static bool StartsWith(const std::string& s, const std::string& prefix);
        static bool EndsWith(const std::string& s, const std::string& suffix);
        static bool StartsWithIgnoreCase(const std::string& s, const std::string& prefix);
        static bool EndsWithIgnoreCase(const std::string& s, const std::string& suffix);
        static bool EqualsIgnoreCase(const std::string& a, const std::string& b);
        static int CompareIgnoreCase(const std::string& a, const std::string& b);
        static bool Contains(const std::string& s, const std::string& part);
        static bool ContainsIgnoreCase(const std::string& s, const std::string& part);
        static int IndexOfIgnoreCase(const std::string& s, const std::string& part, size_t from);
        static bool IsNullOrWhiteSpace(const std::string& s);

        static std::string Replace(const std::string& s, const std::string& from, const std::string& to);
        static std::string ReplaceIgnoreCase(const std::string& s, const std::string& from, const std::string& to);
        static std::string ReplaceChar(const std::string& s, char from, char to);

        // keeps empty parts, as String::Split does
        static std::vector<std::string> Split(const std::string& s, char sep);
        // splits on any of the characters, dropping empty parts (StringSplitOptions::RemoveEmptyEntries)
        static std::vector<std::string> SplitNonEmpty(const std::string& s, const std::string& seps);
        // lines on \n, \r\n or \r
        static std::vector<std::string> SplitLines(const std::string& s);
        static std::string Join(const std::string& sep, const std::vector<std::string>& parts);

        static bool TryParseInt(const std::string& s, int& out);
        static bool TryParseLong(const std::string& s, long long& out);
        static bool TryParseHex(const std::string& s, unsigned long long& out);   // with or without 0x

        static bool ContainsIgnoreCase(const std::vector<std::string>& list, const std::string& item);
        static int IndexOfIgnoreCase(const std::vector<std::string>& list, const std::string& item);
        // adds item unless an equal one (ignoring case) is there
        static void AddUniqueIgnoreCase(std::vector<std::string>& list, const std::string& item);

        static bool IsDigit(char c) { return c >= '0' && c <= '9'; }
        static bool IsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || ((unsigned char)c >= 0x80); }
        static bool IsLetterOrDigit(char c) { return IsLetter(c) || IsDigit(c); }
        static bool IsIdentifierChar(char c) { return IsLetterOrDigit(c) || c == '_'; }
        static bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
        static char ToUpperChar(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }
        static char ToLowerChar(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
    };
}}
