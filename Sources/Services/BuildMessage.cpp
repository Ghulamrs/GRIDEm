// =============================================================================
//  BuildMessage.cpp - see BuildMessage.h
// =============================================================================
#include "BuildMessage.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <mutex>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;
    using Platform::Paths;

    // =========================================================================
    //  BuildMessage
    // =========================================================================
    std::string BuildMessage::SeverityName(Severity s)
    {
        switch (s)
        {
        case Severity::Error:   return "Error";
        case Severity::Warning: return "Warning";
        default:                return "Info";
        }
    }

    std::string BuildMessage::Leaf(const std::string& name)
    {
        size_t slash = name.find_last_of("/\\");
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }

    std::string BuildMessage::ToString() const
    {
        return Leaf(File) + "(" + std::to_string(Line) + "): "
            + StringUtil::ToLower(SeverityName(Sev)) + " " + Code + ": " + Text;
    }

    // =========================================================================
    //  MessagePattern
    // =========================================================================
    // (?<name> becomes (, and the name is kept with the group's number
    bool MessagePattern::Compile(const std::string& pattern, bool ignoreCase, std::string& why)
    {
        std::string out;
        names.clear();
        size_t group = 0;
        bool inClass = false;
        for (size_t i = 0; i < pattern.size(); i++)
        {
            char c = pattern[i];
            if (c == '\\' && i + 1 < pattern.size())
            {
                out += c;
                out += pattern[++i];
                continue;
            }
            if (inClass)
            {
                if (c == ']') inClass = false;
                out += c;
                continue;
            }
            if (c == '[') { inClass = true; out += c; continue; }
            if (c == '(')
            {
                if (i + 2 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == '<'
                    && i + 3 < pattern.size() && pattern[i + 3] != '=' && pattern[i + 3] != '!')
                {
                    size_t end = pattern.find('>', i + 3);
                    if (end == std::string::npos) { why = "unterminated group name"; return false; }
                    group++;
                    names[pattern.substr(i + 3, end - i - 3)].push_back(group);
                    out += '(';
                    i = end;
                    continue;
                }
                if (i + 1 < pattern.size() && pattern[i + 1] == '?') { out += c; continue; }
                group++;
                out += c;
                continue;
            }
            out += c;
        }
        try
        {
            std::regex::flag_type flags = std::regex::ECMAScript;
            if (ignoreCase) flags |= std::regex::icase;
            rx = std::regex(out, flags);
        }
        catch (const std::regex_error& e)
        {
            why = e.what();
            return false;
        }
        return true;
    }

    bool MessagePattern::Match(const std::string& line, std::smatch& m) const
    {
        return std::regex_search(line, m, rx);
    }

    bool MessagePattern::Matches(const std::string& line) const
    {
        return std::regex_search(line, rx);
    }

    bool MessagePattern::Group(const std::smatch& m, const std::string& name, std::string& out) const
    {
        std::map<std::string, std::vector<size_t> >::const_iterator it = names.find(name);
        if (it == names.end()) return false;
        for (size_t g : it->second)
            if (g < m.size() && m[g].matched) { out = m[g].str(); return true; }
        return false;
    }

    std::string MessagePattern::GroupOr(const std::smatch& m, const std::string& name) const
    {
        std::string s;
        if (Group(m, name, s)) return s;
        return std::string();
    }

    // =========================================================================
    //  the patterns
    // =========================================================================
    static MessagePatternPtr Make(const std::string& pattern, bool ignoreCase)
    {
        std::shared_ptr<MessagePattern> p = std::make_shared<MessagePattern>();
        std::string why;
        p->Compile(pattern, ignoreCase, why);
        return p;
    }

    static const std::vector<MessagePatternPtr>& BuiltIn()
    {
        static const std::vector<MessagePatternPtr> list = {
            // MSVC / MASM / GRIDE:  kalman.c(4,9): error E10: ...
            Make("^\\s*(?<file>[^(\"]+?)\\((?<line>\\d+)(,(?<col>\\d+))?\\)\\s*:\\s*(?<sev>fatal error|error|warning|remark|note)\\s*(?<code>[A-Za-z#]*\\d*)\\s*:?\\s*(?<msg>.*)$", true),
            // TI asm6x:  "main.asm", ERROR!   at line 5: [E0002] Illegal mnemonic specified
            Make("^\\s*\"(?<file>[^\"]+)\",\\s*(?<sev>ERROR|WARNING|REMARK)!\\s+at\\s+(?:EOF|line\\s+(?<line>\\d+))\\s*:\\s*(?:\\[(?<code>\\w+)\\])?\\s*(?<msg>.*)$", true),
            // TI:  "kalman.c", line 4: error #20: identifier "x" is undefined
            Make("^\\s*\"(?<file>[^\"]+)\",\\s*line\\s+(?<line>\\d+)(:\\s*|\\s+)(?<sev>fatal error|error|warning|remark)\\s*(?<code>#?\\w*)\\s*:?\\s*(?<msg>.*)$", true),
            // GNU:  kalman.c:4:9: error: ...
            Make("^\\s*(?<file>(?:[A-Za-z]:)?[^:]+):(?<line>\\d+):((?<col>\\d+):)?\\s*(?<sev>fatal error|error|warning|note)\\s*:?\\s*(?<code>)(?<msg>.*)$", true),
            // LINK : fatal error LNK1104: cannot open file 'x.lib'
            Make("^\\s*(?<file>(?:[A-Za-z]:)?[^:]*?)\\s*:\\s*(?<sev>fatal error|error|warning)\\s+(?<code>[A-Za-z]+\\d+)\\s*:\\s*(?<msg>.*)$", true),
            // RIDE's masm:  bad.s: line 3: unknown instruction 'BOGUS'
            Make("^\\s*(?<file>(?:[A-Za-z]:)?[^:]+\\.(?:s|asm|S)):\\s+line\\s+(?<line>\\d+):\\s*(?<sev>)(?<code>)(?<msg>.*)$", false)
        };
        return list;
    }

    static const MessagePattern& ShalimarPattern()
    {
        static const MessagePatternPtr p = Make("^Error: line (?<line>\\d+):\\s*(?<msg>.*)$", false);
        return *p;
    }

    static const MessagePattern& LocationPattern()
    {
        static const MessagePatternPtr p = Make(
            "^\\s*(?<file>[^:(]*(?::[\\\\/][^:(]*)?)(?::(?<line>\\d+)(?::(?<col>\\d+))?:|\\((?<line>\\d+)(?:,(?<col>\\d+))?\\)\\s*:)",
            false);
        return *p;
    }

    static const MessagePattern& BannerPattern()
    {
        static const MessagePatternPtr p = Make(
            "^(c90|cpp11|shalimar|shc|cc1|cxx1): (compilation completed successfully|also compiled )", true);
        return *p;
    }

    // System::IO::Path::HasExtension
    static bool HasExtension(const std::string& file)
    {
        std::string name = Paths::FileName(StringUtil::ReplaceChar(file, '\\', '/'));
        size_t dot = name.find_last_of('.');
        return dot != std::string::npos && dot + 1 < name.size();
    }

    static int ParseIntOr(const std::string& s, int def)
    {
        int v = 0;
        if (StringUtil::TryParseInt(s, v)) return v;
        return def;
    }

    // =========================================================================
    //  BuildMessageParser
    // =========================================================================
    std::string BuildMessageParser::Absolute(const std::string& file, const std::string& workDir)
    {
        if (!file.empty() && !Paths::IsRooted(file) && !workDir.empty())
            return Paths::FullPath(workDir, StringUtil::ReplaceChar(file, '\\', '/'));
        return file;
    }

    BuildMessagePtr BuildMessageParser::TryParse(const std::string& line, const std::string& workDir)
    {
        return TryParse(line, workDir, nullptr);
    }

    BuildMessagePtr BuildMessageParser::TryParse(const std::string& line, const std::string& workDir,
                                                 const MessagePatternPtr& custom)
    {
        if (line.empty()) return nullptr;
        std::smatch m;
        const MessagePattern* used = nullptr;
        if (custom && custom->Match(line, m)) used = custom.get();
        if (used == nullptr)
            for (const MessagePatternPtr& r : BuiltIn())
                if (r->Match(line, m)) { used = r.get(); break; }
        if (used == nullptr) return nullptr;

        BuildMessagePtr b = std::make_shared<BuildMessage>();
        std::string file = StringUtil::Trim(used->GroupOr(m, "file"));
        // "LINK : fatal error LNK1104 ..." names a tool, not a file
        if (!file.empty() && !HasExtension(file) && file.find_first_of("\\/") == std::string::npos) file.clear();
        b->File = Absolute(file, workDir);
        int lineNo = 0, col = 0;
        if (StringUtil::TryParseInt(used->GroupOr(m, "line"), lineNo)) b->Line = lineNo;
        if (StringUtil::TryParseInt(used->GroupOr(m, "col"), col)) b->Column = col;
        std::string sev = "error";
        std::string sevText;
        if (used->Group(m, "sev", sevText) && !sevText.empty()) sev = StringUtil::ToLower(sevText);
        if (StringUtil::StartsWith(sev, "err") || sev == "fatal error") b->Sev = Severity::Error;
        else if (StringUtil::StartsWith(sev, "warn")) b->Sev = Severity::Warning;
        else b->Sev = Severity::Info;
        b->Code = StringUtil::Trim(used->GroupOr(m, "code"));
        std::string msg;
        b->Text = StringUtil::Trim(used->Group(m, "msg", msg) ? msg : line);
        return b;
    }

    BuildMessagePtr BuildMessageParser::TryParseShalimar(const std::string& line, const std::string& source)
    {
        if (line.empty()) return nullptr;
        std::smatch m;
        if (!ShalimarPattern().Match(line, m)) return nullptr;
        BuildMessagePtr b = std::make_shared<BuildMessage>();
        b->Sev = Severity::Error;
        b->File = source;
        b->Line = ParseIntOr(ShalimarPattern().GroupOr(m, "line"), 0);
        b->Column = 1;
        b->Text = StringUtil::Trim(ShalimarPattern().GroupOr(m, "msg"));
        return b;
    }

    BuildMessagePtr BuildMessageParser::TryParseCaret(const std::string& previous, const std::string& line,
                                                      const std::string& workDir)
    {
        if (previous.empty() || line.empty()) return nullptr;
        size_t caret = line.find('^');
        if (caret == std::string::npos) return nullptr;
        for (size_t i = 0; i < caret; i++)
            if (line[i] != ' ' && line[i] != '\t') return nullptr;
        std::string message = StringUtil::Trim(line.substr(caret + 1));
        if (message.empty()) return nullptr;
        size_t after = previous.rfind(": ");
        if (after == std::string::npos || after == 0) return nullptr;
        std::string where = previous.substr(0, after);
        size_t lineAt = where.find_last_of(':');
        if (lineAt == std::string::npos || lineAt == 0 || lineAt + 1 >= where.size()) return nullptr;
        int lineNo = 0;
        if (!StringUtil::TryParseInt(where.substr(lineAt + 1), lineNo) || lineNo <= 0) return nullptr;
        BuildMessagePtr b = std::make_shared<BuildMessage>();
        b->Sev = Severity::Error;
        b->File = Absolute(where.substr(0, lineAt), workDir);
        b->Line = lineNo;
        size_t prefix = after + 2;
        b->Column = caret >= prefix ? (int)(caret - prefix + 1) : 1;
        b->Text = message;
        return b;
    }

    BuildMessagePtr BuildMessageParser::TryParseLocation(const std::string& line, const std::string& workDir)
    {
        if (line.empty()) return nullptr;
        std::smatch m;
        const MessagePattern& p = LocationPattern();
        if (!p.Match(line, m)) return nullptr;
        std::string file = StringUtil::Trim(p.GroupOr(m, "file"));
        if (file.empty()) return nullptr;
        BuildMessagePtr b = std::make_shared<BuildMessage>();
        b->Sev = Severity::Info;
        b->File = Absolute(file, workDir);
        b->Line = ParseIntOr(p.GroupOr(m, "line"), 0);
        std::string col;
        b->Column = p.Group(m, "col", col) ? ParseIntOr(col, 1) : 1;
        b->Text = StringUtil::Trim(line);
        return b;
    }

    bool BuildMessageParser::IsBanner(const std::string& line)
    {
        if (line.empty()) return false;
        std::string t = StringUtil::Trim(line);
        return StringUtil::Contains(t, "G. R. Akhtar - ") || BannerPattern().Matches(t);
    }

    MessagePatternPtr BuildMessageParser::Compile(const std::string& pattern)
    {
        if (StringUtil::IsNullOrWhiteSpace(pattern)) return nullptr;
        static std::mutex lock;
        static std::map<std::string, MessagePatternPtr> custom;
        std::lock_guard<std::mutex> hold(lock);
        std::map<std::string, MessagePatternPtr>::const_iterator it = custom.find(pattern);
        if (it != custom.end()) return it->second;
        std::shared_ptr<MessagePattern> r = std::make_shared<MessagePattern>();
        std::string why;
        MessagePatternPtr kept;
        if (r->Compile(pattern, true, why)) kept = r;
        custom[pattern] = kept;
        return kept;
    }

    // =========================================================================
    //  DiagnosticReader
    // =========================================================================
    DiagnosticReader::DiagnosticReader(const std::string& workDir, const MessagePatternPtr& custom,
                                       const std::string& source, const std::string& toolName)
        : workDir(workDir), custom(custom), source(source), toolName(StringUtil::ToLower(toolName)),
          hasPrevious(false) {}

    BuildMessagePtr DiagnosticReader::Feed(const std::string& line)
    {
        BuildMessagePtr m;
        if (!BuildMessageParser::IsBanner(line))
        {
            m = BuildMessageParser::TryParse(line, workDir, custom);
            if (!m) m = BuildMessageParser::TryParseShalimar(line, source);
            if (!m && hasPrevious) m = BuildMessageParser::TryParseCaret(previous, line, workDir);
            if (!m)
            {
                BuildMessagePtr own = OwnLine(line);
                if (own)
                {
                    std::lock_guard<std::mutex> hold(itselfLock);
                    itself.push_back(own);
                }
            }
        }
        previous = line;
        hasPrevious = true;
        if (m && m->Sev == Severity::Error && !First) First = m;
        return m;
    }

    BuildMessagePtr DiagnosticReader::OwnLine(const std::string& line)
    {
        if (toolName.empty() || line.empty()) return nullptr;
        // c90 and cpp11 say a missing source without their name: "cannot open nofile.c"
        std::string bare = StringUtil::Trim(line);
        if (StringUtil::StartsWith(bare, "cannot open ") || StringUtil::StartsWith(bare, "cannot read "))
        {
            BuildMessagePtr missing = std::make_shared<BuildMessage>();
            missing->Sev = Severity::Error;
            missing->Text = bare;
            return missing;
        }
        size_t at = line.find(": ");
        if (at == std::string::npos || at == 0) return nullptr;
        std::string who = StringUtil::ToLower(BuildMessage::Leaf(StringUtil::Trim(line.substr(0, at))));
        if (who != toolName && who != toolName + ".exe") return nullptr;
        std::string text = StringUtil::Trim(line.substr(at + 2));
        if (text.empty()) return nullptr;
        BuildMessagePtr b = std::make_shared<BuildMessage>();
        b->Sev = Severity::Error;
        b->Text = text;
        return b;
    }

    std::vector<BuildMessagePtr> DiagnosticReader::TakeItself(bool failed)
    {
        std::vector<BuildMessagePtr> r;
        {
            std::lock_guard<std::mutex> hold(itselfLock);
            r.swap(itself);
        }
        for (const BuildMessagePtr& m : r)
        {
            m->Sev = failed ? Severity::Error : Severity::Info;
            if (failed && !First) First = m;
        }
        return r;
    }
}}
