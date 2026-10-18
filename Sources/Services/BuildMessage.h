// =============================================================================
//  BuildMessage.h - one error / warning line, and the parser that recognises
//  the usual compiler formats in tool output (GRIDEd's Services/BuildMessage.h):
//     kalman.c(4): warning W102: 'Q' could be const          (MSVC / GRIDE style)
//     kalman.c(4,9): error E10: ...
//     "kalman.c", line 4: error #20: identifier "x" is undefined   (TI style)
//     kalman.c:4:9: error: ...                                 (GNU style)
//     "main.asm", ERROR!   at line 5: [E0002] ...              (TI asm6x)
//     LINK : fatal error LNK1104: cannot open file 'x.lib'     (LINK)
//  plus an optional per-toolchain pattern (Tools > Options > Toolchains).
//
//  std::regex has no named groups, so a pattern written with (?<name>...) - as
//  GRIDEd's and the user's are - is rewritten into numbered groups once, and the
//  names are kept beside it (MessagePattern).
// =============================================================================
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    enum class Severity { Error, Warning, Info };

    class BuildMessage
    {
    public:
        BuildMessage() : Sev(Severity::Info), Line(0), Column(0) {}
        Severity Sev;
        std::string File;
        int Line;
        int Column;
        std::string Code;
        std::string Text;
        std::string Project;

        // "kalman.c(4): error E10: text"
        std::string ToString() const;
        static std::string SeverityName(Severity s);     // "Error", "Warning", "Info"
    };

    typedef std::shared_ptr<BuildMessage> BuildMessagePtr;
    typedef std::function<void(const std::string& line)> TextLineHandler;
    typedef std::function<void(const BuildMessagePtr& m)> BuildMessageHandler;

    // ---- a regular expression written with .NET's named groups ----------------
    class MessagePattern
    {
    public:
        // false (and why) when the pattern is not a valid regular expression
        bool Compile(const std::string& pattern, bool ignoreCase, std::string& why);
        bool Match(const std::string& line, std::smatch& m) const;
        bool Matches(const std::string& line) const;
        // the named group's text; false when it did not take part in the match
        bool Group(const std::smatch& m, const std::string& name, std::string& out) const;
        std::string GroupOr(const std::smatch& m, const std::string& name) const;

    private:
        std::regex rx;
        std::map<std::string, std::vector<size_t> > names;    // a name may be given twice
    };

    typedef std::shared_ptr<const MessagePattern> MessagePatternPtr;

    // Called from the tool output threads, so the built-in patterns are made once
    // (thread-safe statics) and the cache of custom patterns is locked.
    class BuildMessageParser
    {
    public:
        // workDir is used to make relative file names absolute; custom (may be null)
        // is tried before the built-in formats. nullptr when the line is no message.
        static BuildMessagePtr TryParse(const std::string& line, const std::string& workDir);
        static BuildMessagePtr TryParse(const std::string& line, const std::string& workDir,
                                        const MessagePatternPtr& custom);
        // shalimar's one diagnostic: "Error: line N: message", naming no file - the file is
        // the source being compiled (RIDE compile.cpp parseShalimar)
        static BuildMessagePtr TryParseShalimar(const std::string& line, const std::string& source);
        // c90's preprocessor: "file:line: <the directive>" then "        ^ message"
        static BuildMessagePtr TryParseCaret(const std::string& previous, const std::string& line,
                                             const std::string& workDir);
        // a line naming a place with no severity word: "file:line[:col]:" or "file(line[,col]):"
        static BuildMessagePtr TryParseLocation(const std::string& line, const std::string& workDir);
        // a compiler announcing itself, or saying it is done: output, never a message
        static bool IsBanner(const std::string& line);
        // a toolchain's custom error pattern; nullptr when empty or not a valid regex
        static MessagePatternPtr Compile(const std::string& pattern);

    private:
        static std::string Absolute(const std::string& file, const std::string& workDir);
    };

    // Reads one tool's output line by line, as RIDE's parseDiagnostic reads a build's output:
    // the toolchain's own pattern first, then the built-in shapes, shalimar's "Error: line N:"
    // (filed under the source being compiled), and c90's two-line preprocessor diagnostic. First
    // keeps the first error. A line the tool writes about itself ("c90.exe: ...") is an error
    // without a place.
    class DiagnosticReader
    {
    public:
        DiagnosticReader(const std::string& workDir, const MessagePatternPtr& custom,
                         const std::string& source, const std::string& toolName);

        BuildMessagePtr First;
        const std::string& Source() const { return source; }
        BuildMessagePtr Feed(const std::string& line);

    private:
        BuildMessagePtr OwnLine(const std::string& line);

        std::string workDir;
        MessagePatternPtr custom;
        std::string source;
        std::string toolName;
        std::string previous;
        bool hasPrevious;
    };
}}
