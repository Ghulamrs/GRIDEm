// =============================================================================
//  TextFile.cpp - see TextFile.h
// =============================================================================
#include "TextFile.h"
#include "../Platform/Paths.h"

namespace GRIDEm { namespace Core {

    // Windows-1252's 0x80..0x9F; 0 where the code page has no character
    static const unsigned int kCp1252High[32] = {
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
    };

    static void AppendUtf8(std::string& out, unsigned int cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800)
        {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    // the next code point of UTF-8 text; a bad byte reads as U+FFFD
    static unsigned int NextCodePoint(const std::string& s, size_t& i)
    {
        unsigned char c = (unsigned char)s[i++];
        if (c < 0x80) return c;
        int more = 0;
        unsigned int cp = 0;
        if ((c & 0xE0) == 0xC0) { more = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { more = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { more = 3; cp = c & 0x07; }
        else return 0xFFFD;
        for (int k = 0; k < more; k++)
        {
            if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
            cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
        }
        return cp;
    }

    bool TextFile::IsValidUtf8(const std::string& bytes)
    {
        size_t i = 0;
        while (i < bytes.size())
        {
            unsigned char c = (unsigned char)bytes[i];
            int more;
            unsigned int cp, min;
            if (c < 0x80) { i++; continue; }
            if ((c & 0xE0) == 0xC0) { more = 1; cp = c & 0x1F; min = 0x80; }
            else if ((c & 0xF0) == 0xE0) { more = 2; cp = c & 0x0F; min = 0x800; }
            else if ((c & 0xF8) == 0xF0) { more = 3; cp = c & 0x07; min = 0x10000; }
            else return false;
            i++;
            for (int k = 0; k < more; k++)
            {
                if (i >= bytes.size() || ((unsigned char)bytes[i] & 0xC0) != 0x80) return false;
                cp = (cp << 6) | ((unsigned char)bytes[i++] & 0x3F);
            }
            if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        }
        return true;
    }

    static std::string DecodeUtf16(const std::string& bytes, size_t start, bool bigEndian)
    {
        std::string out;
        size_t i = start;
        while (i + 1 < bytes.size())
        {
            unsigned int a = (unsigned char)bytes[i], b = (unsigned char)bytes[i + 1];
            unsigned int unit = bigEndian ? ((a << 8) | b) : ((b << 8) | a);
            i += 2;
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < bytes.size())
            {
                unsigned int c = (unsigned char)bytes[i], d = (unsigned char)bytes[i + 1];
                unsigned int low = bigEndian ? ((c << 8) | d) : ((d << 8) | c);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    i += 2;
                    AppendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                    continue;
                }
            }
            if (unit >= 0xD800 && unit <= 0xDFFF) unit = 0xFFFD;
            AppendUtf8(out, unit);
        }
        return out;
    }

    std::string TextFile::Decode(const std::string& bytes, TextEncoding& enc)
    {
        size_t n = bytes.size();
        const unsigned char* b = (const unsigned char*)bytes.data();
        if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
        {
            enc = TextEncoding::Utf8Bom;
            return bytes.substr(3);
        }
        if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE)
        {
            enc = TextEncoding::Utf16LE;
            return DecodeUtf16(bytes, 2, false);
        }
        if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF)
        {
            enc = TextEncoding::Utf16BE;
            return DecodeUtf16(bytes, 2, true);
        }
        // valid UTF-8 without BOM, else the single-byte code page
        if (IsValidUtf8(bytes))
        {
            enc = TextEncoding::Utf8;
            return bytes;
        }
        enc = TextEncoding::Windows1252;
        std::string out;
        for (size_t i = 0; i < n; i++)
        {
            unsigned int c = b[i];
            if (c >= 0x80 && c < 0xA0) c = kCp1252High[c - 0x80] != 0 ? kCp1252High[c - 0x80] : c;
            AppendUtf8(out, c);
        }
        return out;
    }

    static void PutUnit(std::string& out, unsigned int unit, bool bigEndian)
    {
        if (bigEndian) { out += (char)(unit >> 8); out += (char)(unit & 0xFF); }
        else { out += (char)(unit & 0xFF); out += (char)(unit >> 8); }
    }

    std::string TextFile::Encode(const std::string& text, TextEncoding enc)
    {
        switch (enc)
        {
        case TextEncoding::Utf8: return text;
        case TextEncoding::Utf8Bom: return std::string("\xEF\xBB\xBF") + text;
        case TextEncoding::Utf16LE:
        case TextEncoding::Utf16BE:
        {
            bool be = enc == TextEncoding::Utf16BE;
            std::string out;
            PutUnit(out, 0xFEFF, be);
            size_t i = 0;
            while (i < text.size())
            {
                unsigned int cp = NextCodePoint(text, i);
                if (cp >= 0x10000)
                {
                    cp -= 0x10000;
                    PutUnit(out, 0xD800 + (cp >> 10), be);
                    PutUnit(out, 0xDC00 + (cp & 0x3FF), be);
                }
                else PutUnit(out, cp, be);
            }
            return out;
        }
        case TextEncoding::Windows1252:
        {
            std::string out;
            size_t i = 0;
            while (i < text.size())
            {
                unsigned int cp = NextCodePoint(text, i);
                if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) { out += (char)cp; continue; }
                char c = '?';
                for (int k = 0; k < 32; k++)
                    if (kCp1252High[k] == cp) { c = (char)(0x80 + k); break; }
                if (c == '?' && cp >= 0x80 && cp < 0xA0) c = (char)cp;
                out += c;
            }
            return out;
        }
        }
        return text;
    }

    bool TextFile::Read(const std::string& path, std::string& text, TextEncoding& enc, std::string& error)
    {
        std::string bytes;
        if (!Platform::Paths::ReadFile(path, bytes, error)) return false;
        text = Decode(bytes, enc);
        return true;
    }

    bool TextFile::Write(const std::string& path, const std::string& text, TextEncoding enc, std::string& error)
    {
        return Platform::Paths::WriteFile(path, Encode(text, enc), error);
    }

    std::string TextFile::EncodingLabel(TextEncoding enc)
    {
        switch (enc)
        {
        case TextEncoding::Utf8:        return "UTF-8";
        case TextEncoding::Utf8Bom:     return "UTF-8 BOM";
        case TextEncoding::Utf16LE:     return "UTF-16 LE";
        case TextEncoding::Utf16BE:     return "UTF-16 BE";
        case TextEncoding::Windows1252: return "WINDOWS-1252";
        }
        return "UTF-8";
    }
}}
