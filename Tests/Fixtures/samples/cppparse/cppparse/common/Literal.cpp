// Literal.cpp - escape sequences, string literal pieces and character constants decoded as cl does.
#include "Literal.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cppparse {

namespace {

// a non-negative integer in 32-bit words, least significant first: exact comparisons of decimal literals
typedef std::vector<unsigned int> BigNum;

void bigMulAdd(BigNum& a, unsigned int m, unsigned int add)
{
    unsigned long long carry = add;
    for (size_t i = 0; i < a.size(); ++i) {
        const unsigned long long p = static_cast<unsigned long long>(a[i]) * m + carry;
        a[i] = static_cast<unsigned int>(p & 0xFFFFFFFFULL);
        carry = p >> 32;
    }
    if (carry) a.push_back(static_cast<unsigned int>(carry));
}

void bigPow(BigNum& a, unsigned int base, long n)
{
    for (; n > 0; --n) bigMulAdd(a, base, 0);
}

int bigCompare(BigNum a, BigNum b)
{
    while (!a.empty() && a.back() == 0) a.pop_back();
    while (!b.empty() && b.back() == 0) b.pop_back();
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i-- > 0; ) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

// the decimal literal s (no sign, no suffix) compared exactly with the positive finite double m
int compareDecimal(const std::string& s, double m)
{
    BigNum left, right;
    long exp10 = 0;
    bool point = false;
    size_t i = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') { bigMulAdd(left, 10, static_cast<unsigned int>(c - '0')); if (point) --exp10; }
        else if (c == '.' && !point) point = true;
        else break;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        const bool negative = i < s.size() && s[i] == '-';
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        long e = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) if (e < 100000) e = e * 10 + (s[i] - '0');
        exp10 += negative ? -e : e;
    }
    int e2 = 0;
    const double mant = std::frexp(m, &e2);
    const unsigned long long whole = static_cast<unsigned long long>(std::ldexp(mant, 53));
    right.push_back(static_cast<unsigned int>(whole & 0xFFFFFFFFULL));
    right.push_back(static_cast<unsigned int>(whole >> 32));
    const long k = static_cast<long>(e2) - 53;
    if (exp10 > 0) bigPow(left, 10, exp10); else bigPow(right, 10, -exp10);
    if (k > 0) bigPow(right, 2, k); else bigPow(left, 2, -k);
    return bigCompare(left, right);
}

// a positive float as a double, its infinity as 2^128 (the float above the largest one when rounding)
double floatAsDouble(float v) { return v > 3.4028235e38f ? std::ldexp(1.0, 128) : static_cast<double>(v); }

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// one source character or escape sequence of a literal's body at text[i] (before end); i moves past it. A
// universal character name gives its code point
unsigned long literalChar(const std::string& text, size_t& i, size_t end, bool& escape)
{
    const unsigned char c = static_cast<unsigned char>(text[i++]);
    escape = c == '\\' && i < end;
    if (!escape) return c;
    const char x = text[i++];
    switch (x) {
    case 'n': return 10;
    case 't': return 9;
    case 'v': return 11;
    case 'b': return 8;
    case 'r': return 13;
    case 'f': return 12;
    case 'a': return 7;
    case 'x': case 'u': case 'U': {
        // \x takes every hex digit that follows; \u four, \U eight
        const size_t most = x == 'x' ? end : x == 'u' ? 4 : 8;
        unsigned long v = 0;
        for (size_t k = 0; k < most && i < end && hexDigit(text[i]) >= 0; ++k, ++i) v = ((v << 4) | static_cast<unsigned long>(hexDigit(text[i]))) & 0xFFFFFFFFUL;
        return v;
    }
    default:
        if (x >= '0' && x <= '7') {
            unsigned long v = static_cast<unsigned long>(x - '0');
            for (int k = 1; k < 3 && i < end && text[i] >= '0' && text[i] <= '7'; ++k, ++i) v = v * 8 + static_cast<unsigned long>(text[i] - '0');
            return v;
        }
        return static_cast<unsigned char>(x);   // \\ \' \" \? and unknown escapes (cl: warning C4129) are the character
    }
}

// the code units of one character for a narrow or wide literal: a universal character name beyond 16 bits is a
// surrogate pair; beyond 8 bits in a narrow literal it is '?', as cl gives a character the code page lacks
void appendUnits(unsigned long v, bool universal, bool wide, std::vector<unsigned long>& units)
{
    if (wide && universal && v > 0xFFFFUL && v <= 0x10FFFFUL) {
        units.push_back(0xD800UL + ((v - 0x10000UL) >> 10));
        units.push_back(0xDC00UL + ((v - 0x10000UL) & 0x3FFUL));
        return;
    }
    if (!wide && universal && v > 0xFFUL) v = '?';
    units.push_back(v & (wide ? 0xFFFFUL : 0xFFUL));
}

bool isUniversal(const std::string& text, size_t at) { return text[at] == '\\' && at + 1 < text.size() && (text[at + 1] == 'u' || text[at + 1] == 'U'); }

} // namespace

bool stringLiteralUnits(const std::string& text, std::vector<unsigned long>& units)
{
    units.clear();
    // the bodies of the pieces, between their quotes; a narrow piece next to a wide one is widened
    std::vector<size_t> begins, ends;
    bool wide = false;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] != '"') ++i;   // the prefix and the space between pieces
        if (i >= text.size()) break;
        if (i > 0 && text[i - 1] == 'L') wide = true;
        size_t end = ++i;
        while (end < text.size() && text[end] != '"') end += text[end] == '\\' ? 2 : 1;
        if (end > text.size()) end = text.size();
        begins.push_back(i);
        ends.push_back(end);
        i = end + 1;
    }
    for (size_t p = 0; p < begins.size(); ++p) {
        for (size_t k = begins[p]; k < ends[p]; ) {
            const bool universal = isUniversal(text, k);
            bool escape = false;
            const unsigned long v = literalChar(text, k, ends[p], escape);
            appendUnits(v, universal, wide, units);
        }
    }
    return wide;
}

long stringLiteralLength(const std::string& text)
{
    std::vector<unsigned long> units;
    stringLiteralUnits(text, units);
    return static_cast<long>(units.size());
}

// cl shifts each character of a multicharacter constant in from the right, until the second escape sequence:
// from there on each character goes above the ones before ('ab' 6162h, 'a\n\tb' 6209610Ah, '\x12\x34' 3412h)
long charLiteralValue(const std::string& text, int& count)
{
    count = 0;
    const bool wide = !text.empty() && text[0] == 'L';
    size_t i = wide ? 2 : 1;
    size_t end = text.size();
    if (end > i && text[end - 1] == '\'') --end;
    unsigned long v = 0, first = 0;
    int escapes = 0;
    bool above = false;
    while (i < end) {
        const bool universal = isUniversal(text, i);
        bool escape = false;
        unsigned long c = literalChar(text, i, end, escape);
        if (!wide && universal && c > 0xFFUL) c = '?';
        c &= wide ? 0xFFFFUL : 0xFFUL;
        if (count == 0) first = c;
        if (escape && ++escapes >= 2) above = true;
        if (above) { if (count < 4) v |= c << (8 * count); }
        else v = ((v << 8) | c) & 0xFFFFFFFFUL;
        ++count;
    }
    if (wide) return static_cast<long>(first);
    if (count <= 1) return static_cast<signed char>(static_cast<unsigned char>(first));
    return static_cast<long>(static_cast<int>(static_cast<unsigned int>(v & 0xFFFFFFFFUL)));
}

double floatingLiteralValue(const std::string& text, bool asFloat)
{
    std::string s = text;
    while (!s.empty() && std::strchr("fFlL", s[s.size() - 1])) s.erase(s.size() - 1);
    const double x = std::strtod(s.c_str(), 0);
    if (!asFloat) return x;
    const float f = static_cast<float>(x);
    if (static_cast<double>(f) == x || x != x) return f;
    // the double rounding goes wrong only when x lies exactly halfway between f and the float on its other side;
    // then the decimal itself, compared exactly with that midpoint, decides (a tie stays f, the even one)
    unsigned int bits = 0;
    std::memcpy(&bits, &f, 4);
    const double fd = floatAsDouble(f);
    const unsigned int nb = fd < x ? bits + 1 : bits - 1;
    float g = 0;
    std::memcpy(&g, &nb, 4);
    const double gd = floatAsDouble(g);
    if ((fd + gd) / 2 != x) return f;
    const int c = compareDecimal(s, x);
    if (c == 0) return f;
    return (c > 0) == (fd < gd) ? g : f;
}

bool foldFloatToInteger(double d, int bits, bool isUnsigned, unsigned long long& out, double& raw)
{
    if (d != d || d > 1.7976931348623157e308 || d < -1.7976931348623157e308) return false;
    const double n = std::fmod(d < 0 ? std::ceil(d) : std::floor(d), 18446744073709551616.0);   // exact
    unsigned long long u = n < 0 ? 0ULL - static_cast<unsigned long long>(-n) : static_cast<unsigned long long>(n);
    raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<long long>(u));
    if (bits < 64) {
        const unsigned long long mask = (1ULL << bits) - 1;
        u &= mask;
        if (!isUnsigned && (u >> (bits - 1)) != 0) u |= ~mask;   // sign-extended
    }
    out = u;
    return true;
}

} // namespace cppparse
