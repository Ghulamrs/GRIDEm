// =============================================================================
//  TextFile.h - reads text files while remembering their encoding, so that
//  saving (or Replace All on a file that is not open) writes them back the
//  same way: UTF-8, UTF-8 with BOM, UTF-16 LE/BE (BOM) or the single-byte
//  code page (Windows-1252, what GRIDEd's Encoding::Default is on a Western
//  Windows). In memory every text is UTF-8. Line endings are kept as found.
//  (GRIDEd's Core/TextFile.h)
// =============================================================================
#pragma once
#include <string>

namespace GRIDEm { namespace Core {

    // FileEncoding, not TextEncoding: Apple's CoreServices declares a global TextEncoding,
    // and the two names would collide in the Objective-C++ files
    enum class FileEncoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Windows1252 };

    class TextFile
    {
    public:
        static std::string Decode(const std::string& bytes, FileEncoding& enc);
        static std::string Encode(const std::string& text, FileEncoding enc);
        // false with error set when the file cannot be read
        static bool Read(const std::string& path, std::string& text, FileEncoding& enc, std::string& error);
        // a BOM is written exactly when the encoding has one
        static bool Write(const std::string& path, const std::string& text, FileEncoding enc, std::string& error);
        // "UTF-8", "UTF-8 BOM", "UTF-16 LE", "UTF-16 BE", "WINDOWS-1252"
        static std::string EncodingLabel(FileEncoding enc);
        static bool IsValidUtf8(const std::string& bytes);
    };
}}
