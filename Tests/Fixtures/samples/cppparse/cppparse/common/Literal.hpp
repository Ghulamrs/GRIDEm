// Literal.hpp - the values of character and string literals as cl gives them, shared by the preprocessor, sema,
// IR generation and the back end. Strict C++98.
#ifndef CPPPARSE_LITERAL_HPP
#define CPPPARSE_LITERAL_HPP

#include <string>
#include <vector>

namespace cppparse {

// the code units of a string literal's array without the terminator. text holds one literal or several adjacent
// ones as written ("\x41" "b", L"a" L"b"); each piece's escapes are decoded on their own before the pieces are
// joined (C++98 2.1, phase 5 before phase 6), so a hex escape never takes the next piece's digits. Returns true
// for a wide literal (any piece with the L prefix): units of 16 bits, else of 8
bool stringLiteralUnits(const std::string& text, std::vector<unsigned long>& units);
long stringLiteralLength(const std::string& text);

// the value of a character literal and the number of characters it holds (more than one: a multicharacter
// constant of type int; L'ab' is L'a' as with cl). A single narrow character is a signed char
long charLiteralValue(const std::string& text, int& count);

// the value of a floating literal (digits, '.', an exponent, an f/F/l/L suffix ignored): as a double, or with
// asFloat as a float rounded once from the decimal, as cl rounds it (decimal -> double -> float differs when the
// double lies exactly halfway between two floats: 1.00000005960464477550f is 3F800001h)
double floatingLiteralValue(const std::string& text, bool asFloat);

// a floating constant converted to an integer of `bits` bits as cl folds it: truncated toward zero, taken modulo
// 2^64 (not saturated) and narrowed, so (int)3e9 is B2D05E00h and (int)-2147483649.0 7FFFFFFFh. raw is the
// 64-bit value before narrowing, read as signed or unsigned: cl converts the folded constant to a floating type
// from there ((unsigned char)300.0 + 0.5 is 300.5). False for NaN and the infinities
bool foldFloatToInteger(double d, int bits, bool isUnsigned, unsigned long long& out, double& raw);

} // namespace cppparse

#endif
