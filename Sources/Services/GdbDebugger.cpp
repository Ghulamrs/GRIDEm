// =============================================================================
//  GdbDebugger.cpp - see GdbDebugger.h
// =============================================================================
#include "GdbDebugger.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;
    using Platform::Paths;
    using namespace Settings;
    using namespace Model;

    static bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
    static bool Starts(const std::string& s, const std::string& p) { return StringUtil::StartsWith(s, p); }
    static bool Ends(const std::string& s, const std::string& p) { return StringUtil::EndsWith(s, p); }
    static std::string Trim(const std::string& s) { return StringUtil::Trim(s); }
    static std::string Rep(const std::string& s, const std::string& a, const std::string& b) { return StringUtil::Replace(s, a, b); }
    static std::string From(const std::string& s, size_t at) { return at >= s.size() ? std::string() : s.substr(at); }
    // the first of any of chars at or after from, or npos (String::IndexOfAny)
    static size_t IndexOfAny(const std::string& s, const char* chars, size_t from = 0) { return s.find_first_of(chars, from); }

    // =========================================================================
    //  DebugTranscript
    // =========================================================================
    DebuggerKind DebugTranscript::DialectOf(const std::string& dialect)
    {
        if (StringUtil::EqualsIgnoreCase(dialect, "lldb")) return DebuggerKind::Lldb;
        if (StringUtil::EqualsIgnoreCase(dialect, "cdb")) return DebuggerKind::Cdb;
        if (StringUtil::EqualsIgnoreCase(dialect, "gdb")) return DebuggerKind::Gdb;
        return DebuggerKind::None;
    }

    std::vector<std::string> DebugTranscript::Lines(const std::string& text)
    {
        std::vector<std::string> r;
        if (text.empty()) return r;
        for (const std::string& l : StringUtil::Split(text, '\n')) r.push_back(StringUtil::TrimEnd(l, '\r'));
        if (!r.empty() && r.back().empty()) r.pop_back();
        return r;
    }

    std::string DebugTranscript::WithoutPrompt(const std::string& line)
    {
        std::string out = line;
        for (;;)
        {
            size_t at = 0;
            if (Starts(out, "(gdb) ")) at = 6;
            else if (Starts(out, "(lldb) ")) at = 7;
            else
            {
                size_t i = 0;
                while (i < out.size() && StringUtil::IsDigit(out[i])) i++;
                if (i == 0 || i >= out.size() || out[i] != ':') break;
                size_t j = i + 1;
                while (j < out.size() && IsHex(out[j])) j++;
                if (j == i + 1 || j >= out.size() || out[j] != '>') break;
                at = j + 1;
                if (at < out.size() && out[at] == ' ') at++;
            }
            out = From(out, at);
        }
        return out;
    }

    DebuggerKind DebugTranscript::PromptOn(const std::string& line)
    {
        if (Starts(line, "(gdb)")) return DebuggerKind::Gdb;
        if (Starts(line, "(lldb)")) return DebuggerKind::Lldb;
        size_t i = 0;
        while (i < line.size() && StringUtil::IsDigit(line[i])) i++;
        if (i == 0 || i >= line.size() || line[i] != ':') return DebuggerKind::None;
        size_t j = i + 1;
        while (j < line.size() && IsHex(line[j])) j++;
        if (j == i + 1 || j >= line.size() || line[j] != '>') return DebuggerKind::None;
        return DebuggerKind::Cdb;
    }

    DebugStopPtr DebugTranscript::ReadStop(DebuggerKind kind, const std::string& said)
    {
        if (kind == DebuggerKind::Cdb) return ReadCdbStop(said);
        DebugStopPtr stop = std::make_shared<DebugStop>();
        stop->Said = said;
        std::vector<std::string> all = Lines(said);
        for (const std::string& raw : all)
        {
            std::string line = WithoutPrompt(raw);
            if (Has(line, "exited with status") || Has(line, "exited with code") || Has(line, "exited normally"))
            {
                stop->Exited = true;
                stop->Status = 0;
                size_t at = line.find("status = ");
                int base = 10;
                if (at != std::string::npos) at += 9;
                else
                {
                    at = line.find("code ");
                    if (at != std::string::npos) { at += 5; base = 8; }
                }
                if (at != std::string::npos) stop->Status = ParseInt(From(line, at), base);
                continue;
            }
            if (Starts(line, "Run till exit from")) continue;
            bool interesting = kind == DebuggerKind::Lldb ? Has(line, "frame #0:") : Has(line, " at ");
            if (!interesting || stop->Stopped) continue;
            std::string file, fn;
            int ln = 0;
            if (!PlaceIn(line, file, ln, fn)) continue;
            stop->File = file;
            stop->Line = ln;
            stop->Function = fn;
            stop->Stopped = true;
        }
        if (!stop->Stopped && !stop->Exited)
            for (const std::string& raw : all)
            {
                // a step within the function answers the source echo alone: "19\t    printf(...)"
                std::string line = WithoutPrompt(raw);
                size_t tab = line.find('\t');
                if (tab == std::string::npos || tab == 0 || !Digits(line.substr(0, tab))) continue;
                stop->Line = ParseInt(line.substr(0, tab), 10);
                stop->Stopped = true;
                break;
            }
        return stop;
    }

    DebugStopPtr DebugTranscript::ReadCdbStop(const std::string& said)
    {
        DebugStopPtr stop = std::make_shared<DebugStop>();
        stop->Said = said;
        std::vector<std::string> all = Lines(said);
        for (size_t i = 0; i < all.size(); i++)
        {
            std::string line = Trim(WithoutPrompt(all[i]));
            size_t ended = line.find("Exit process");
            if (ended != std::string::npos)
            {
                stop->Exited = true;
                stop->Stopped = false;
                size_t code = line.find("code ", ended);
                if (code != std::string::npos) stop->Status = ParseInt(From(line, code + 5), 16);
                continue;
            }
            if (Has(line, "No runnable debuggees")) { stop->Exited = true; continue; }
            if (stop->Stopped || stop->Exited) continue;
            size_t open = line.rfind('(');
            if (open == std::string::npos || open == 0) continue;
            size_t close = line.find(')', open);
            if (close == std::string::npos) continue;
            std::string number = line.substr(open + 1, close - open - 1);
            if (!Digits(number)) continue;
            // ln says "path\file.c(9)" or "path\file.c(9)+0x2c"; the program's own "greet(4) is 10" is not a place
            std::string after = From(line, close + 1);
            if (!after.empty() && !Starts(after, "+0x")) continue;
            std::string where = line.substr(0, open);
            std::string leaf = LeafName(where);
            size_t dot = leaf.rfind('.');
            if (dot == std::string::npos || dot + 1 >= leaf.size()) continue;
            stop->File = where;
            stop->Line = ParseInt(number, 10);
            stop->Stopped = true;
            for (size_t j = i + 1; j < all.size() && stop->Function.empty(); j++)
            {
                std::string under = WithoutPrompt(all[j]);
                size_t bang = under.find('!');
                if (bang == std::string::npos) continue;
                std::string rest = From(under, bang + 1);
                size_t end = IndexOfAny(rest, "+ \t|(");
                stop->Function = Trim(end == std::string::npos ? rest : rest.substr(0, end));
            }
        }
        return stop;
    }

    bool DebugTranscript::PlaceIn(const std::string& line, std::string& file, int& ln, std::string& function)
    {
        file.clear();
        ln = 0;
        function.clear();
        size_t at = line.rfind(" at ");
        if (at == std::string::npos) return false;
        std::string where = Trim(From(line, at + 4));
        size_t colon = where.rfind(':');
        if (colon == std::string::npos) return false;
        std::string tail = From(where, colon + 1);
        std::string head = where.substr(0, colon);
        if (!Digits(tail)) return false;
        size_t second = head.rfind(':');
        if (second != std::string::npos && Digits(From(head, second + 1)))
        {
            tail = From(head, second + 1);
            head = head.substr(0, second);
        }
        file = head;
        ln = ParseInt(tail, 10);
        std::string front = line.substr(0, at);
        size_t tick = front.rfind('`');
        if (tick != std::string::npos) front = From(front, tick + 1);
        size_t bracket = front.find('(');
        if (bracket != std::string::npos) front = front.substr(0, bracket);
        size_t comma = front.rfind(',');
        if (comma != std::string::npos) front = From(front, comma + 1);
        size_t in = front.rfind(" in ");
        if (in != std::string::npos) front = From(front, in + 4);
        function = Trim(front);
        return true;
    }

    bool DebugTranscript::WentNowhere(DebuggerKind kind, const DebugStopPtr& before, const DebugStopPtr& after)
    {
        if (kind != DebuggerKind::Lldb && kind != DebuggerKind::Cdb) return false;
        if (!before || !after || !before->Stopped || !after->Stopped || after->Exited) return false;
        if (after->File.empty() || after->Function.empty() || after->Line == 0) return false;
        if (after->File != before->File || after->Line != before->Line) return false;
        if (after->Function != before->Function) return false;
        if (Has(after->Said, "stop reason = breakpoint")) return false;
        if (kind == DebuggerKind::Cdb)
        {
            if (Has(after->Said, "Breakpoint ") && Has(after->Said, " hit")) return false;
            unsigned long long was = CdbStopAddress(before->Said), now = CdbStopAddress(after->Said);
            return was != 0 && now > was;
        }
        unsigned long long w = LldbFrameAddress(before->Said), n = LldbFrameAddress(after->Said);
        return w != 0 && n > w;
    }

    std::vector<DebugVariable> DebugTranscript::ReadVariables(DebuggerKind kind, const std::string& said, const std::string& marker)
    {
        std::vector<DebugVariable> found;
        for (const std::string& raw : Lines(said))
        {
            std::string line = Trim(WithoutPrompt(raw));
            if (line.empty() || line == marker) continue;
            std::string type, address;
            if (kind == DebuggerKind::Lldb && Starts(line, "0x"))
            {
                size_t colon = line.find(": ");
                if (colon == std::string::npos) continue;
                address = line.substr(0, colon);
                line = Trim(From(line, colon + 2));
            }
            if (kind == DebuggerKind::Lldb)
            {
                if (line.empty() || line[0] != '(') continue;
                size_t close = line.find(')');
                if (close == std::string::npos) continue;
                type = line.substr(1, close - 1);
                line = Trim(From(line, close + 1));
            }
            size_t equals = line.find(" = ");
            if (equals == std::string::npos) continue;
            std::string name = Trim(line.substr(0, equals));
            std::string value = Trim(From(line, equals + 3));
            if (kind == DebuggerKind::Cdb && Has(name, " "))
            {
                // dv /t /V: "<address> <where> <type> <name> = value", the type free to hold spaces
                std::string left = name;
                size_t space = left.find_last_of(" \t");
                name = From(left, space + 1);
                std::string rest = Trim(left.substr(0, space));
                size_t first = IndexOfAny(rest, " \t");
                std::string at = first == std::string::npos ? rest : rest.substr(0, first);
                rest = first == std::string::npos ? std::string() : Trim(From(rest, first));
                size_t second = IndexOfAny(rest, " \t");
                type = second == std::string::npos ? std::string() : Trim(From(rest, second));
                address = Has(at, "@") ? std::string() : AddressIn("0x" + at);
            }
            if (kind == DebuggerKind::Cdb && Starts(value, "0n")) value = From(value, 2);
            if (name.empty() || Has(name, " ")) continue;
            found.push_back(DebugVariable(name, value, type, address));
        }
        return found;
    }

    std::vector<DebugFrame> DebugTranscript::ReadFrames(DebuggerKind kind, const std::string& said, const std::string& marker)
    {
        std::vector<DebugFrame> found;
        for (const std::string& raw : Lines(said))
        {
            std::string line = Trim(WithoutPrompt(raw));
            if (line.empty() || line == marker) continue;
            std::string file, fn;
            int ln = 0;
            if (kind == DebuggerKind::Cdb)
            {
                if (!CdbFrameIn(line, file, ln, fn)) continue;
            }
            else
            {
                std::string rest = AfterFrameNumber(kind, line);
                if (rest.empty() || !PlaceIn(rest, file, ln, fn)) continue;
            }
            found.push_back(DebugFrame(fn, file, ln));
            if (fn == "main") break;
        }
        return found;
    }

    std::string DebugTranscript::ReadValue(DebuggerKind kind, const std::string& said, const std::string& marker)
    {
        for (const std::string& raw : Lines(said))
        {
            std::string line = Trim(WithoutPrompt(raw));
            if (line.empty() || line == marker) continue;
            if (kind == DebuggerKind::Cdb)
            {
                if (line[0] == '?') continue;
                if (CdbRefused(line)) return std::string();
                size_t space = line.rfind(' ');
                if (space == std::string::npos || space + 1 >= line.size()) continue;
                std::string value = From(line, space + 1);
                if (Starts(value, "0n")) value = From(value, 2);
                if (!HasDigit(value)) continue;
                return value;
            }
            size_t dollar = line.find('$');
            if (dollar == std::string::npos) continue;
            size_t equals = line.find(" = ", dollar);
            if (equals == std::string::npos) continue;
            return Trim(From(line, equals + 3));
        }
        return std::string();
    }

    std::string DebugTranscript::ReadType(DebuggerKind kind, const std::string& said, const std::string& marker)
    {
        for (const std::string& raw : Lines(said))
        {
            std::string line = Trim(WithoutPrompt(raw));
            if (line.empty() || line == marker) continue;
            if (kind == DebuggerKind::Cdb)
            {
                if (line[0] == '?') continue;
                if (CdbRefused(line)) return std::string();
                size_t space = line.rfind(' ');
                if (space == std::string::npos) continue;
                std::string value = From(line, space + 1);
                if (!HasDigit(value)) continue;
                return Trim(line.substr(0, space));
            }
            if (GdbLike(kind))
            {
                if (Starts(line, "type = ")) return Trim(From(line, 7));
                continue;
            }
            if (line[0] == '(')
            {
                size_t close = line.find(')');
                if (close != std::string::npos && close > 0 && line.find(" = ", close) != std::string::npos)
                    return line.substr(1, close - 1);
            }
        }
        return std::string();
    }

    std::string DebugTranscript::AddressIn(const std::string& said)
    {
        size_t at = said.find("0x");
        if (at == std::string::npos) return std::string();
        std::string found = "0x";
        for (size_t i = at + 2; i < said.size(); i++)
        {
            char c = said[i];
            if (c == '`') continue;
            if (!IsHex(c)) break;
            found += c;
        }
        return found.size() > 2 ? found : std::string();
    }

    std::string DebugTranscript::ComplaintIn(const std::string& answer)
    {
        static const char* const words[] = { "error", "No symbol", "not an lvalue", "Couldn't", "cannot be",
                                             "Syntax error", "Type conflict", "Bad register error" };
        for (const std::string& raw : Lines(answer))
        {
            std::string line = Trim(WithoutPrompt(raw));
            for (const char* w : words)
            {
                if (!Has(line, w)) continue;
                size_t at = line.find("error: ");
                return at != std::string::npos ? From(line, at + 7) : line;
            }
        }
        return std::string();
    }

    bool DebugTranscript::StoppedWithNoSource(const std::string& said)
    {
        return Has(said, "stop reason") || Has(said, "frame #0") || Has(said, "#0  0x");
    }

    std::vector<std::string> DebugTranscript::ProgramOutput(DebuggerKind kind, const std::string& said, const std::string& markerStart)
    {
        std::vector<std::string> out;
        for (const std::string& raw : Lines(said))
        {
            std::string line = raw;
            DebuggerKind prompt = PromptOn(line);
            if (prompt == DebuggerKind::Lldb) continue;
            if (prompt != DebuggerKind::None) line = WithoutPrompt(line);
            if (Trim(line).empty()) continue;
            if (!markerStart.empty() && Has(line, markerStart)) continue;
            if (SourceEcho(line)) continue;
            if (kind == DebuggerKind::Lldb && LldbOwn(line)) continue;
            if (GdbLike(kind) && GdbOwn(line)) continue;
            if (kind == DebuggerKind::Cdb && CdbOwn(line)) continue;
            out.push_back(line);
        }
        return out;
    }

    std::string DebugTranscript::WithoutEscapes(const std::string& text)
    {
        std::string out;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] != '\x1b') { out += text[i]; continue; }
            if (i + 1 >= text.size()) break;
            char k = text[i + 1];
            if (k == '[')
            {
                size_t at = i + 2;
                while (at < text.size() && !(text[at] >= '@' && text[at] <= '~')) at++;
                i = at;
            }
            else if (k == ']')
            {
                size_t at = i + 2;
                while (at < text.size() && text[at] != '\x07'
                       && !(text[at] == '\x1b' && at + 1 < text.size() && text[at + 1] == '\\'))
                    at++;
                if (at < text.size() && text[at] == '\x1b') at++;
                i = at;
            }
            else i++;
        }
        return out;
    }

    std::string DebugTranscript::WithoutEcho(const std::string& said, const std::string& asked, const std::string& markerCommand)
    {
        bool droppedAsked = false, droppedMarker = false;
        std::string out;
        std::string a = Trim(asked), m = Trim(markerCommand);
        for (const std::string& l : Lines(said))
        {
            std::string bare = Trim(WithoutPrompt(l));
            if (!droppedAsked && bare == a) { droppedAsked = true; continue; }
            if (!droppedMarker && bare == m) { droppedMarker = true; continue; }
            out += l;
            out += "\n";
        }
        return out;
    }

    std::vector<RegisterValue> DebugTranscript::ReadRegisters(DebuggerKind kind, const std::string& said, const std::string& marker)
    {
        std::vector<RegisterValue> r;
        for (const std::string& raw : Lines(said))
        {
            std::string line = Trim(WithoutPrompt(raw));
            if (line.empty() || line == marker) continue;
            if (kind == DebuggerKind::Cdb || Has(line, "="))
            {
                for (const std::string& part : StringUtil::SplitNonEmpty(line, " \t"))
                {
                    size_t eq = part.find('=');
                    if (eq == std::string::npos || eq == 0 || eq == part.size() - 1) continue;
                    r.push_back(RegisterValue(part.substr(0, eq), From(part, eq + 1)));
                }
                // lldb: "rax = 0x0000000000000001"
                if (r.empty() || kind == DebuggerKind::Lldb)
                {
                    size_t eq = line.find(" = ");
                    if (eq != std::string::npos && eq > 0 && kind == DebuggerKind::Lldb)
                        r.push_back(RegisterValue(Trim(line.substr(0, eq)), Trim(From(line, eq + 3))));
                }
                continue;
            }
            std::vector<std::string> parts = StringUtil::SplitNonEmpty(line, " \t");
            if (parts.size() >= 2 && Starts(parts[1], "0x")) r.push_back(RegisterValue(parts[0], parts[1]));
        }
        return r;
    }

    std::vector<unsigned char> DebugTranscript::ReadMemoryBytes(DebuggerKind kind, const std::string& said, const std::string& marker, int wanted)
    {
        std::vector<unsigned char> bytes;
        for (const std::string& raw : Lines(said))
        {
            std::string line = Trim(WithoutPrompt(raw));
            if (line.empty() || line == marker || !ComplaintIn(line).empty()) continue;
            if (kind == DebuggerKind::Cdb)
            {
                // "00000000`00001000  01 02 03 04 ff 00 ab cd-ef 00 ..." - cdb joins the eighth and ninth bytes with a dash
                std::vector<std::string> parts = StringUtil::SplitNonEmpty(line, " -");
                for (size_t i = 1; i < parts.size() && (int)bytes.size() < wanted; i++)
                {
                    if (parts[i].size() != 2 || !IsHex(parts[i][0]) || !IsHex(parts[i][1])) break;
                    bytes.push_back((unsigned char)ParseHex64(parts[i]));
                }
                continue;
            }
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            for (const std::string& tok : StringUtil::SplitNonEmpty(From(line, colon + 1), " \t"))
            {
                if (!Starts(tok, "0x") || tok.size() > 4 || (int)bytes.size() >= wanted) continue;
                bytes.push_back((unsigned char)ParseHex64(From(tok, 2)));
            }
        }
        return bytes;
    }

    std::string DebugTranscript::StopLine(const std::string& file, int line, const std::string& function)
    {
        return "stopped at " + LeafName(file) + ":" + std::to_string(line) + (function.empty() ? std::string() : " in " + function);
    }

    std::string DebugTranscript::LookingAt(const DebugFrame& frame)
    {
        std::string where = LeafName(frame.File) + ":" + std::to_string(frame.Line);
        if (frame.Function.empty()) return "the variables are those of " + where;
        return "the variables are " + frame.Function + "'s, at " + where;
    }

    std::string DebugTranscript::FrameLine(const DebugFrame& frame, bool looking)
    {
        return std::string(looking ? "> " : "  ") + frame.Function + "   " + LeafName(frame.File) + ":" + std::to_string(frame.Line);
    }

    std::string DebugTranscript::VariableLine(const DebugVariable& v)
    {
        std::string s = "  " + v.Name + " = " + v.Value;
        if (!v.Type.empty()) s += "   [" + v.Type + "]";
        return s;
    }

    std::string DebugTranscript::WatchLine(const DebugWatch& w)
    {
        return "  " + w.Expression + " = " + (w.Ok ? w.Value : "[" + w.Value + "]");
    }

    std::vector<DebugExchange> DebugTranscript::ReadRecorded(const std::string& text)
    {
        std::vector<DebugExchange> r;
        DebugExchange cur;
        bool any = false;
        for (const std::string& line : Lines(text))
        {
            if (Starts(line, "> "))
            {
                if (any || !cur.Answer.empty()) r.push_back(cur);
                cur = DebugExchange();
                cur.Command = From(line, 2);
                any = true;
                continue;
            }
            // "! " lines were heard on standard error; the readers take them as said
            cur.Answer += (Starts(line, "! ") ? From(line, 2) : line) + "\n";
        }
        if (any || !cur.Answer.empty()) r.push_back(cur);
        return r;
    }

    bool DebugTranscript::LoadRecorded(const std::string& path, std::vector<DebugExchange>& exchanges, std::string& error)
    {
        std::string text;
        if (!Paths::ReadFile(path, text, error)) return false;
        exchanges = ReadRecorded(text);
        return true;
    }

    bool DebugTranscript::Digits(const std::string& s)
    {
        if (s.empty()) return false;
        for (char c : s)
            if (c < '0' || c > '9') return false;
        return true;
    }

    int DebugTranscript::ParseInt(const std::string& s, int base)
    {
        int value = 0;
        std::string t = StringUtil::TrimStart(s);
        for (size_t i = 0; i < t.size(); i++)
        {
            int d = HexDigit(t[i]);
            if (d < 0 || d >= base) break;
            value = value * base + d;
        }
        return value;
    }

    std::string DebugTranscript::LeafName(const std::string& path)
    {
        size_t at = path.find_last_of("/\\");
        return at == std::string::npos ? path : From(path, at + 1);
    }

    unsigned long long DebugTranscript::ParseHex64(const std::string& s)
    {
        unsigned long long v = 0;
        for (char c : s)
        {
            int d = HexDigit(c);
            if (d < 0) break;
            v = v * 16 + (unsigned long long)d;
        }
        return v;
    }

    bool DebugTranscript::HasDigit(const std::string& s)
    {
        for (char c : s)
            if (c >= '0' && c <= '9') return true;
        return false;
    }

    int DebugTranscript::HexDigit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    }

    bool DebugTranscript::SourceEcho(const std::string& line)
    {
        std::string rest = Trim(line);
        if (Starts(rest, "-> ")) rest = Trim(From(rest, 3));
        size_t i = 0;
        while (i < rest.size() && StringUtil::IsDigit(rest[i])) i++;
        if (i == 0) return false;
        while (i < rest.size() && rest[i] == ' ') i++;
        return i < rest.size() && rest[i] == '\t';
    }

    bool DebugTranscript::LldbOwn(const std::string& line)
    {
        std::string bare = Trim(line);
        return Starts(bare, "* thread #") || Starts(bare, "frame #") || bare == "^" || Starts(line, "Process ") || Starts(line, "Target ");
    }

    bool DebugTranscript::GdbOwn(const std::string& line)
    {
        if (Starts(line, "Starting program:") || Starts(line, "Continuing.") || Starts(line, "Breakpoint ") || Starts(line, "[")
            || Starts(line, "Using host ") || Starts(line, "Reading symbols"))
            return true;
        return Starts(line, "Missing ") && (Has(line, "debuginfo") || Has(line, "try:"));
    }

    bool DebugTranscript::OurCdbCommand(const std::string& bare)
    {
        static const char* const ours[] = { "g", "p", "t", "gu", "k", "q", "ln", "dv", "l+t", "l+s", "n 10", ".lines -e",
                                            ".lastevent", ".echo", ".printf" };
        for (const char* s : ours)
            if (bare == s) return true;
        return Starts(bare, "bp ") || Starts(bare, "bu ") || Starts(bare, "bc ") || Starts(bare, ".printf ") || Starts(bare, ".step_filter ");
    }

    bool DebugTranscript::CdbOwn(const std::string& line)
    {
        std::string bare = Trim(line);
        if (OurCdbCommand(bare)) return true;
        if (Starts(bare, "Breakpoint ") || Starts(bare, "Last event:") || Starts(bare, "debugger time:") || Starts(bare, "ModLoad:")) return true;
        if (Has(bare, "!") && Ends(bare, ":")) return true;
        if (Starts(bare, "(") && Has(bare, "!")) return true;
        if (Has(bare, "`") && !bare.empty() && IsHex(bare[0])) return true;
        if (Ends(bare, ")") && (Has(bare, ".c(") || Has(bare, ".cpp(") || Has(bare, ".h("))) return true;
        return false;
    }

    bool DebugTranscript::CdbRefused(const std::string& line)
    {
        return Has(line, "Couldn't resolve") || Has(line, "<<gride") || Has(line, "rror");
    }

    unsigned long long DebugTranscript::LldbFrameAddress(const std::string& said)
    {
        for (const std::string& raw : Lines(said))
        {
            std::string line = WithoutPrompt(raw);
            size_t frame = line.find("frame #0:");
            if (frame == std::string::npos) continue;
            size_t hex = line.find("0x", frame);
            if (hex == std::string::npos) continue;
            return ParseHex64(From(line, hex + 2));
        }
        return 0;
    }

    unsigned long long DebugTranscript::CdbStopAddress(const std::string& said)
    {
        std::vector<std::string> all = Lines(said);
        unsigned long long found = 0;
        for (size_t i = 0; i + 1 < all.size(); i++)
        {
            std::string head = Trim(WithoutPrompt(all[i]));
            if (!Has(head, "!") || !Ends(head, ":") || Has(head, " ")) continue;
            std::string word = Trim(WithoutPrompt(all[i + 1]));
            size_t sp = IndexOfAny(word, " \t");
            if (sp != std::string::npos) word = word.substr(0, sp);
            std::string hex = Rep(word, "`", "");
            if (hex.empty() || !IsHex(hex[0])) continue;
            found = ParseHex64(hex);
        }
        return found;
    }

    std::string DebugTranscript::AfterFrameNumber(DebuggerKind kind, const std::string& line)
    {
        if (kind == DebuggerKind::Lldb)
        {
            size_t frame = line.find("frame #");
            if (frame == std::string::npos) return std::string();
            size_t colon = line.find(':', frame);
            return colon == std::string::npos ? std::string() : Trim(From(line, colon + 1));
        }
        if (line.empty() || line[0] != '#') return std::string();
        size_t i = 1;
        while (i < line.size() && StringUtil::IsDigit(line[i])) i++;
        return i == 1 ? std::string() : Trim(From(line, i));
    }

    bool DebugTranscript::CdbFrameIn(const std::string& line, std::string& file, int& ln, std::string& function)
    {
        file.clear();
        ln = 0;
        function.clear();
        size_t open = line.rfind('['), close = line.rfind(']');
        if (open == std::string::npos || close == std::string::npos || close < open) return false;
        std::string where = line.substr(open + 1, close - open - 1);
        size_t sign = where.rfind(" @ ");
        if (sign == std::string::npos) return false;
        std::string tail = Trim(From(where, sign + 3));
        if (!Digits(tail)) return false;
        file = Trim(where.substr(0, sign));
        ln = ParseInt(tail, 10);
        std::string call = Trim(line.substr(0, open));
        size_t space = call.find_last_of(" \t");
        if (space != std::string::npos) call = From(call, space + 1);
        size_t bang = call.find('!');
        if (bang == std::string::npos) return false;
        call = From(call, bang + 1);
        size_t end = IndexOfAny(call, "+ \t(");
        function = Trim(end == std::string::npos ? call : call.substr(0, end));
        return true;
    }

    // =========================================================================
    //  ElfImage
    // =========================================================================
    static unsigned int U32(const std::string& d, long long at)
    {
        if (at < 0 || at + 4 > (long long)d.size()) return 0u;
        const unsigned char* b = (const unsigned char*)d.data() + at;
        return (unsigned int)b[0] | ((unsigned int)b[1] << 8) | ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
    }

    static int U16(const std::string& d, long long at)
    {
        if (at < 0 || at + 2 > (long long)d.size()) return 0;
        const unsigned char* b = (const unsigned char*)d.data() + at;
        return b[0] | (b[1] << 8);
    }

    static std::string CString(const std::string& d, size_t at)
    {
        size_t end = at;
        while (end < d.size() && d[end] != 0) end++;
        return d.substr(at, end - at);
    }

    bool ElfImage::HasLines(const std::string& out, std::string& why)
    {
        why.clear();
        std::string d, error;
        if (!Paths::IsFile(out) || !Paths::ReadFile(out, d, error)) { why = "the image is not there"; return false; }
        const unsigned char* b = (const unsigned char*)d.data();
        if (d.size() < 52 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F' || b[4] != 1)
        {
            why = "the image is not a 32-bit ELF";
            return false;
        }
        unsigned int shoff = U32(d, 32);
        int shentsize = U16(d, 46), shnum = U16(d, 48), shstrndx = U16(d, 50);
        if (shentsize < 40 || (long long)shoff + (long long)shnum * shentsize > (long long)d.size() || shstrndx >= shnum)
        {
            why = "the image's section table is not readable";
            return false;
        }
        unsigned int strOff = U32(d, (long long)shoff + (long long)shstrndx * shentsize + 16);
        std::vector<std::pair<unsigned int, unsigned int> > code;
        unsigned int lineOff = 0, lineSize = 0;
        for (int i = 0; i < shnum; i++)
        {
            long long h = (long long)shoff + (long long)i * shentsize;
            long long name = (long long)strOff + U32(d, h);
            std::string n = name < (long long)d.size() ? CString(d, (size_t)name) : std::string();
            unsigned int flags = U32(d, h + 8), addr = U32(d, h + 12), off = U32(d, h + 16), size = U32(d, h + 20);
            if ((flags & 4) != 0) code.push_back(std::make_pair(addr, addr + size));
            if (n == ".debug_line" && size != 0) { lineOff = off; lineSize = size; }
        }
        if (lineSize == 0) { why = "the image carries no .debug_line"; return false; }
        if ((long long)lineOff + lineSize > (long long)d.size()) { why = "the image's .debug_line runs past its end"; return false; }
        for (long long at = lineOff; at + 10 <= (long long)lineOff + lineSize;)
        {
            unsigned int unitLen = U32(d, at);
            if (unitLen == 0xffffffffu || unitLen < 6) break;
            long long unitEnd = at + 4 + unitLen;
            long long prog = at + 10 + U32(d, at + 6);
            if (prog + 7 <= unitEnd && prog + 7 <= (long long)d.size() && b[prog] == 0 && b[prog + 2] == 2)
            {
                unsigned int a = U32(d, prog + 3);
                for (const std::pair<unsigned int, unsigned int>& k : code)
                    if (a >= k.first && a < k.second) return true;
            }
            if (unitEnd <= at) break;
            at = unitEnd;
        }
        why = "the image's .debug_line has no row in the program's code";
        return false;
    }

    // =========================================================================
    //  ShalimarWords
    // =========================================================================
    const char* const ShalimarWords::SaysWhereOnly = "a Shalimar program says where it is, not what is in it";
    const char* const ShalimarWords::SaysHowDeepOnly = "a Shalimar program reports how deep it is, not what called it";
    const char* const ShalimarWords::ReleaseHasNoSession = "release links a runtime with no debugger in it";
    const char* const ShalimarWords::DidNotArm = "the program did not arm - it has no debugger in it";
    const char* const ShalimarWords::StopsItself = "a Shalimar program stops itself - it needs no debugger at all";

    // =========================================================================
    //  DebuggerChoice
    // =========================================================================
    static std::string Args(const ToolDefinitionPtr& tool, const std::string& op)
    {
        return tool ? tool->OperationArgs(op) : std::string();
    }

    DebugLaunchPtr DebuggerChoice::For(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg,
                                       const ToolchainSettingsPtr& tc, const GrideSettingsPtr& s,
                                       const std::string& program, const std::string& assembly, bool shalimar,
                                       const std::string& shalimarRuntimeDir)
    {
        DebugLaunchPtr l = std::make_shared<DebugLaunch>();
        Core::Target t = cfg->TargetKind;
        std::string arch = RideToolchain::Arch(t);
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        TargetDefinitionPtr td = defs->TargetOf(t);
        RideTool tool = shalimar ? RideTool::Shalimar : RideToolchain::Resolve(RideToolchain::FromWord(p->Toolchain), p->Lang);
        ToolDefinitionPtr shc = shalimar ? defs->ToolFor(t, "shalimarCompiler") : ToolDefinitionPtr();
        SessionDefinitionPtr own = shc ? shc->Session : SessionDefinitionPtr();
        l->WorkDir = p->ProjectDir();
        l->Program = program;
        // a part (a folder with its own compiler) that writes no line table: the debugger cannot stop in it
        for (const std::pair<std::string, std::string>& kv : p->FolderToolchains.Items())
        {
            RideTool part = RideToolchain::Resolve(RideToolchain::FromWord(kv.second), p->Lang);
            if (!RideToolchain::EmitsDebugInfo(part, t)) l->Blind.push_back(kv.first + " (" + RideToolchain::Name(part) + ")");
        }
        if (!cfg->IsDebug())
        {
            l->WhyNot = std::string(shalimar ? ShalimarWords::ReleaseHasNoSession : "release is built without -g")
                        + " - choose Build > Configuration > Debug, then Debug > Start Debugging";
            return l;
        }
        if (!RideToolchain::EmitsDebugInfo(tool, t) && !own)
        {
            l->WhyNot = RideToolchain::Name(tool) + " generates MASM for " + arch + ", which carries no line table";
            return l;
        }
        if (td && td->Emulated)
        {
            std::string vm = tc->ResolveFor("emulator", tc->Emulator);
            std::string sim = tc->ResolveFor("simulator", tc->Simulator);
            ToolDefinitionPtr vmDef = defs->ToolFor(t, "emulator");
            ToolDefinitionPtr simDef = defs->ToolFor(t, "simulator");
            if (!vmDef || vm.empty() || !Paths::IsFile(vm))
            {
                std::string vmName = vmDef ? vmDef->Name : "vm6747";
                if (own) return OwnSession(l, shc, own, arch, "no " + vmName + " inside GRIDEm", true, simDef, sim, RideToolchain::TiProgramOf(program));
                l->WhyNot = vmName + " is not inside GRIDEm and not on the PATH - the VM6747 emulator is the debugger for " + arch
                            + " (" + vmName + " --debug); name one with $" + (vmDef ? vmDef->Lookup.Env : std::string("VM6747"));
                return l;
            }
            l->Kind = DebuggerKind::C6x;
            std::string out = RideToolchain::TiProgramOf(program);
            std::string why;
            std::string simName = simDef ? simDef->Name : "sim6747";
            std::string backend = C6xBackend(assembly, out, sim, simName, vmDef->Name, s->Env->EffectiveC6747Debugger(), why);
            l->BackendWhy = why;
            l->Backend = backend;
            if (backend == simName && simDef)
            {
                l->Tool = simDef;
                l->Session = simDef->Session;
                l->Program = out;
                l->Command = Trim(RideToolchain::Quote(sim) + " "
                                  + Trim(Rep(Rep(Args(simDef, "debug"), "$(Program)", out), "$(ShalimarRuntimeDir)", "")));
            }
            else
            {
                l->Tool = vmDef;
                l->Session = vmDef->Session;
                l->Program = assembly;
                std::string rt = shalimar && !shalimarRuntimeDir.empty() ? RideToolchain::Quote(shalimarRuntimeDir) : std::string();
                l->Command = Trim(RideToolchain::Quote(vm) + " "
                                  + Trim(Rep(Rep(Args(vmDef, "debug"), "$(Program)", assembly), "$(ShalimarRuntimeDir)", rt)));
            }
            if (!l->Session) l->WhyNot = l->Tool->Name + " has no session in toolchain.json - nothing knows how to drive it";
            return l;
        }
        if (td && td->Host)
        {
            // the host's debugger: lldb on the Mac (DWARF); cdb where the host is Windows (CodeView, a .pdb)
            ToolDefinitionPtr dbg = defs->ToolFor(t, "debugger");
            std::string exe = tc->ResolveFor("debugger", tc->Debugger);
            DebuggerKind dk = dbg && dbg->Session ? DebugTranscript::DialectOf(dbg->Session->Dialect) : DebuggerKind::None;
#if !defined(_WIN32)
            // cdb drives Windows' own debugging engine: never on this machine
            if (dk == DebuggerKind::Cdb) exe.clear();
#endif
            if (!dbg || exe.empty() || !Paths::IsFile(exe))
            {
                std::string dbgName = dbg ? dbg->Name : std::string("the debugger");
                // the debugger named for the role fallbackDebugger stands in where it is found
                ToolDefinitionPtr fb = defs->ToolFor(t, "fallbackDebugger");
                std::string fbExe = fb ? tc->ResolveFor("fallbackDebugger", fb->Exe) : std::string();
                if (fb && fb->Session && fb->Operation("debug") && !fbExe.empty() && Paths::IsFile(fbExe))
                {
                    l->Kind = DebugTranscript::DialectOf(fb->Session->Dialect);
                    if (l->Kind == DebuggerKind::None) l->Kind = DebuggerKind::Lldb;
                    l->Tool = fb;
                    l->Session = fb->Session;
                    l->Backend = fb->Name;
                    l->BackendWhy = dbgName + " is not installed - " + fb->Name + " stands in";
                    l->Command = Trim(RideToolchain::Quote(fbExe) + " "
                                      + Trim(Rep(Rep(Args(fb, "debug"), "$(Program)", program), "$(OutDir)", Paths::DirectoryName(program))));
                    return l;
                }
                if (own) return OwnSession(l, shc, own, arch, dbgName + " is not installed", false, ToolDefinitionPtr(), std::string(), program);
                if (dk == DebuggerKind::Cdb)
                    l->WhyNot = RideToolchain::Name(tool) + " writes a .pdb and cdb reads one, but cdb is Windows only - debug the program on Windows";
                else
                    l->WhyNot = RideToolchain::Name(tool) + " writes DWARF and " + dbgName + " reads it, but " + dbgName
                                + " is not found - install Xcode's command line tools (xcode-select --install)";
                return l;
            }
            l->Kind = dk == DebuggerKind::None ? DebuggerKind::Lldb : dk;
            l->Tool = dbg;
            l->Session = dbg->Session;
            l->Backend = dbg->Name;
            l->Command = Trim(RideToolchain::Quote(exe) + " "
                              + Trim(Rep(Rep(Args(dbg, "debug"), "$(Program)", program), "$(OutDir)", Paths::DirectoryName(program))));
            if (!l->Session) l->WhyNot = dbg->Name + " has no session in toolchain.json - nothing knows how to drive it";
            return l;
        }
        // a cross target: its debugger lives on its own machine
        ToolDefinitionPtr theirs = td ? defs->ToolFor(t, "debugger") : ToolDefinitionPtr();
        l->WhyNot = "the program is built for " + arch + " - debug it there" + (theirs ? ", with " + theirs->Name : std::string())
                    + "; here " + arch + " only reaches assembly";
        return l;
    }

    DebugLaunchPtr DebuggerChoice::OwnSession(const DebugLaunchPtr& l, const ToolDefinitionPtr& shc,
                                              const SessionDefinitionPtr& own, const std::string& arch,
                                              const std::string& why, bool underSimulator,
                                              const ToolDefinitionPtr& simDef, const std::string& simulator,
                                              const std::string& program)
    {
        l->Kind = DebuggerKind::Shalimar;
        l->Tool = shc;
        l->Session = own;
        l->Backend = "the program itself";
        l->Program = program;
        l->Blind.clear();
        for (StringMap::const_iterator it = own->Environment.begin(); it != own->Environment.end(); ++it)
            l->Environment.push_back(it->first + "=" + it->second);
        if (!underSimulator)
        {
            l->BackendWhy = why + " - the program's own session";
            l->Command = RideToolchain::Quote(program);
            return l;
        }
        std::string simName = simDef ? simDef->Name : "sim6747";
        std::string head = "a Shalimar program on " + arch + " stops itself under " + simName + " on its .out - ";
        if (!simDef || simulator.empty() || !Paths::IsFile(simulator)) { l->WhyNot = head + "no " + simName + " inside GRIDEm"; return l; }
        if (!Paths::IsFile(program)) { l->WhyNot = head + "the build left no " + Paths::FileName(program); return l; }
        if (!simDef->Operation("session")) { l->WhyNot = head + simName + " has no 'session' operation in toolchain.json"; return l; }
        l->BackendWhy = why + " - the program's own session under " + simName;
        l->Command = Trim(RideToolchain::Quote(simulator) + " " + Trim(Rep(simDef->OperationArgs("session"), "$(Program)", program)));
        return l;
    }

    static bool NewestIn(const std::string& dir, std::time_t& newest)
    {
        bool any = false;
        newest = 0;
        for (const std::string& f : Paths::ListFiles(dir))
        {
            std::time_t t = 0;
            if (Paths::ModifiedTime(Paths::Combine(dir, f), t) && (!any || t > newest)) { newest = t; any = true; }
        }
        return any;
    }

    std::string DebuggerChoice::C6xBackend(const std::string& assembly, const std::string& out, const std::string& simulator,
                                           const std::string& simName, const std::string& vmName, const std::string& setting,
                                           std::string& why)
    {
        why.clear();
        bool haveSim = !simulator.empty() && Paths::IsFile(simulator);
        if (StringUtil::EqualsIgnoreCase(setting, vmName) || StringUtil::EqualsIgnoreCase(setting, "vm6747"))
        {
            why = "the setting asks for the emulator";
            return vmName;
        }
        if (StringUtil::EqualsIgnoreCase(setting, simName) || StringUtil::EqualsIgnoreCase(setting, "sim6747"))
        {
            if (haveSim && Paths::IsFile(out)) return simName;
            why = !haveSim ? "the setting asks for " + simName + ", and none is inside GRIDEm"
                           : "the setting asks for " + simName + ", and the build left no " + Paths::FileName(out);
            return vmName;
        }
        if (!haveSim) { why = "no " + simName + " inside GRIDEm"; return vmName; }
        if (!Paths::IsFile(out)) { why = "the build left no " + Paths::FileName(out) + " (asm6x, lnk6x and RTS6x make one)"; return vmName; }
        if (!assembly.empty() && Paths::Exists(assembly))
        {
            std::time_t builtOut = 0, builtAsm = 0;
            bool haveOut = Paths::ModifiedTime(out, builtOut);
            bool haveAsm = Paths::IsDirectory(assembly) ? NewestIn(assembly, builtAsm) : Paths::ModifiedTime(assembly, builtAsm);
            if (haveOut && haveAsm && builtOut < builtAsm)
            {
                why = Paths::FileName(out) + " is older than the assembly: not this build's";
                return vmName;
            }
        }
        std::string lines;
        if (!ElfImage::HasLines(out, lines)) { why = lines; return vmName; }
        return simName;
    }

    // =========================================================================
    //  ProcessDebugger
    // =========================================================================
    ProcessDebugger::ProcessDebugger(const DebugLaunchPtr& l)
        : launch(l), kind(l->Kind), dialect(DebuggerKind::None), moving(false), onConsole(false)
    {
        session = launch->Session;
        dialect = session ? DebugTranscript::DialectOf(session->Dialect) : DebuggerKind::None;
        if (dialect == DebuggerKind::None) dialect = kind == DebuggerKind::C6x ? DebuggerKind::Gdb : kind;
        last = std::make_shared<DebugStop>();
        onConsole = session && session->Console;
    }

    ProcessDebugger::~ProcessDebugger()
    {
        OnStateChanged = nullptr;
        OnStopped = nullptr;
        OnOutput = nullptr;
        OnProgramOutput = nullptr;
        DebugProcessPtr p = TakeProc();
        if (p) p->Stop(session ? session->Command("quit") : std::string("quit"));
        if (mover.joinable()) mover.join();
    }

    std::string ProcessDebugger::Name() const
    {
        return launch->Backend + " (" + (session ? session->Dialect : std::string("no session")) + ")";
    }

    ProcessDebugger::DebugProcessPtr ProcessDebugger::Proc()
    {
        std::lock_guard<std::mutex> hold(procLock);
        return proc;
    }

    ProcessDebugger::DebugProcessPtr ProcessDebugger::TakeProc()
    {
        std::lock_guard<std::mutex> hold(procLock);
        DebugProcessPtr p = proc;
        if (p) lastProc = p;
        proc.reset();
        return p;
    }

    std::string ProcessDebugger::Transcript()
    {
        std::lock_guard<std::mutex> hold(procLock);
        DebugProcessPtr p = proc ? proc : lastProc;
        return p ? p->Transcript() : std::string();
    }

    bool ProcessDebugger::Running()
    {
        DebugProcessPtr p = Proc();
        return p && p->Running();
    }

    void ProcessDebugger::AppendSaid(const std::string& text)
    {
        std::lock_guard<std::mutex> hold(saidLock);
        said += text;
    }

    std::string ProcessDebugger::Said()
    {
        std::lock_guard<std::mutex> hold(saidLock);
        return said;
    }

    bool ProcessDebugger::Start(const std::string& program, Core::Target target, const std::string& entryFile, int entryLine)
    {
        (void)program; (void)target; (void)entryFile; (void)entryLine;
        if (!StartSession()) return false;
        BeginMove("run", false);
        return true;
    }

    bool ProcessDebugger::StartSession()
    {
        if (!session) { Print(!launch->WhyNot.empty() ? launch->WhyNot : std::string("no session")); return false; }
        Stop();
        exited = false;
        exitStatus = 0;
        currentFrame = 0;
        last = std::make_shared<DebugStop>();
        DebugProcessPtr p = std::make_shared<DebugProcess>();
        std::string why;
        Print("[" + launch->Backend + "] " + launch->Command + (onConsole ? "   (on a pseudo-terminal)" : ""));
        if (!launch->BackendWhy.empty()) Print("(" + launch->BackendWhy + ")");
        for (const std::string& b : launch->Blind) Print("(" + b + " carries no debug information - the debugger cannot stop in it)");
        bool started = onConsole ? p->StartOnConsole(launch->Command, launch->WorkDir, launch->Environment, why)
                                 : p->Start(launch->Command, launch->WorkDir, launch->Environment, why);
        if (!started) { Print("the debugger could not be started: " + why); return false; }
        {
            std::lock_guard<std::mutex> hold(procLock);
            proc = p;
        }
        for (const std::string& line : session->Preamble) p->Say(line);
        p->Say(session->MarkerCommand);
        bool found = false;
        std::string first = p->ReadUntil(session->Marker, AskTimeoutMs, found);
        AppendSaid(first);
        if (!found)
        {
            Print("the debugger did not answer its first command - " + (!Trim(first).empty() ? Trim(first) : std::string("nothing came back")));
            DebugProcessPtr gone = TakeProc();
            if (gone) gone->Stop(std::string());
            return false;
        }
        SetState(DebugState::Running);
        SetEveryBreakpoint();
        return true;
    }

    void ProcessDebugger::Stop()
    {
        DebugProcessPtr p = TakeProc();
        if (!p)
        {
            if (state != DebugState::Idle) { location.reset(); SetState(DebugState::Idle); }
            return;
        }
        p->Stop(session ? session->Command("quit") : std::string("quit"));
        if (state != DebugState::Idle)
        {
            Print("Debugging stopped by user.");
            location.reset();
            SetState(DebugState::Terminated);
            SetState(DebugState::Idle);
        }
    }

    void ProcessDebugger::Continue() { if (state == DebugState::Break) BeginMove("continue", false); }
    void ProcessDebugger::StepOver() { if (state == DebugState::Break) BeginMove("stepOver", true); }
    void ProcessDebugger::StepInto() { if (state == DebugState::Break) BeginMove("stepInto", true); }
    void ProcessDebugger::StepOut()  { if (state == DebugState::Break) BeginMove("stepOut", false); }

    void ProcessDebugger::BreakAll()
    {
        Print("a program running under " + launch->Backend + " cannot be interrupted here - it stops at a breakpoint or at its end");
    }

    void ProcessDebugger::RunToCursor(const std::string& file, int line)
    {
        if (state != DebugState::Break || !session->Has("runToLine")) { Continue(); return; }
        std::string answer = Ask(Fill(session->Command("runToLine"), file, line));
        if (!Has(answer, "Breakpoint") && !Has(answer, "breakpoint"))
            Print("no breakpoint at " + DebugTranscript::LeafName(file) + ":" + std::to_string(line) + " - running on");
        BeginMove("continue", false);
    }

    std::vector<DebugVariable> ProcessDebugger::Locals()
    {
        if (state != DebugState::Break) return std::vector<DebugVariable>();
        if (dialect == DebuggerKind::Cdb)
        {
            std::vector<DebugVariable> vars = DebugTranscript::ReadVariables(dialect, Ask(session->Command("locals")), session->Marker);
            // a Shalimar array: cdb shows the pointer to the runtime's record; its contents are read here
            for (DebugVariable& v : vars)
                if (ShalimarArrayType(v.Type))
                {
                    unsigned long long at = 0;
                    if (HexValue(v.Value, at)) v.Value = ShalimarArray(at, ShalimarRank(v.Type), ShalimarScalar(v.Type), 0);
                }
            return vars;
        }
        if (dialect == DebuggerKind::Lldb) return DebugTranscript::ReadVariables(dialect, Ask(session->Command("locals")), session->Marker);
        // gdb lists no addresses, so each variable is asked where it is
        std::vector<DebugVariable> found;
        if (session->Has("args"))
        {
            std::vector<DebugVariable> a = DebugTranscript::ReadVariables(dialect, Ask(session->Command("args")), session->Marker);
            found.insert(found.end(), a.begin(), a.end());
        }
        std::vector<DebugVariable> l = DebugTranscript::ReadVariables(dialect, Ask(session->Command("locals")), session->Marker);
        found.insert(found.end(), l.begin(), l.end());
        for (DebugVariable& v : found)
        {
            v.Address = AddressOf(v.Name);
            // sim6747 and vm6747 know each variable's type from the compilers' .dwtag: whatis says it
            if (kind == DebuggerKind::C6x && v.Type.empty()) v.Type = TypeOf(v.Name);
        }
        return found;
    }

    bool ProcessDebugger::ShalimarArrayType(const std::string& type)
    {
        if (type.empty()) return false;
        std::string t = Rep(type, " ", "");
        return Ends(t, "[]*") && (Starts(t, "structreal[]") || Starts(t, "structint[]") || Starts(t, "structchar[]"));
    }

    int ProcessDebugger::ShalimarRank(const std::string& type)
    {
        int rank = 0;
        for (size_t i = type.find("[]"); i != std::string::npos; i = type.find("[]", i + 2)) rank++;
        return rank;
    }

    std::string ProcessDebugger::ShalimarScalar(const std::string& type)
    {
        std::string t = Trim(Rep(type, "struct", ""));
        size_t b = t.find('[');
        return Trim(b == std::string::npos ? t : t.substr(0, b));
    }

    bool ProcessDebugger::HexValue(const std::string& s, unsigned long long& value)
    {
        std::string a = DebugTranscript::AddressIn(s);
        value = 0;
        if (a.size() <= 2) return false;
        unsigned long long v = 0;
        if (!StringUtil::TryParseHex(a.substr(2), v)) return false;
        value = v;
        return true;
    }

    static unsigned int LeU32(const std::vector<unsigned char>& b, size_t at)
    {
        return (unsigned int)b[at] | ((unsigned int)b[at + 1] << 8) | ((unsigned int)b[at + 2] << 16) | ((unsigned int)b[at + 3] << 24);
    }

    static unsigned long long LeU64(const std::vector<unsigned char>& b, size_t at)
    {
        return (unsigned long long)LeU32(b, at) | ((unsigned long long)LeU32(b, at + 4) << 32);
    }

    // {1, 2, 0} - or {{...}, {...}} a rank deeper - as the C6747's simulators print the same array
    std::string ProcessDebugger::ShalimarArray(unsigned long long record, int rank, const std::string& scalar, int depth)
    {
        if (record == 0) return "0x0";
        std::vector<unsigned char> head = ReadMemory(record, 16);
        if (head.size() < 16 || depth > 4) return "{...}";
        int count = (int)LeU32(head, 0);
        unsigned long long data = LeU64(head, 8);
        if (count < 0) count = 0;
        int shown = count < 100 ? count : 100;
        if (rank == 1 && scalar == "char")
        {
            std::vector<unsigned char> b = ReadMemory(data, shown > 1 ? shown : 1);
            std::string s = "\"";
            for (size_t i = 0; i < b.size() && (int)i < count && b[i] != 0; i++)
                s += (b[i] >= 32 && b[i] < 127) ? (char)b[i] : '?';
            return s + "\"";
        }
        int size = rank > 1 ? 8 : scalar == "real" ? 8 : 4;
        std::vector<unsigned char> body;
        if (shown > 0) body = ReadMemory(data, shown * size);
        std::string out = "{";
        for (int i = 0; i < shown && (size_t)((i + 1) * size) <= body.size(); i++)
        {
            if (i > 0) out += ", ";
            size_t at = (size_t)(i * size);
            if (rank > 1) out += ShalimarArray(LeU64(body, at), rank - 1, scalar, depth + 1);
            else if (scalar == "real")
            {
                unsigned long long bits = LeU64(body, at);
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof d);
                char buf[40];
                std::snprintf(buf, sizeof buf, "%.6G", d);
                out += buf;
            }
            else out += std::to_string((int)LeU32(body, at));
        }
        if (count > shown) out += "...";
        return out + "}";
    }

    std::string ProcessDebugger::WhyNoLocals()
    {
        if (kind == DebuggerKind::C6x)
            return "no locals here: a Debug build (-g) names each function's variables - this function has none, or was built without -g";
        return std::string();
    }

    std::vector<DebugFrame> ProcessDebugger::CallStack()
    {
        if (state != DebugState::Break) return std::vector<DebugFrame>();
        std::vector<DebugFrame> frames = DebugTranscript::ReadFrames(dialect, Ask(session->Command("frames")), session->Marker);
        for (DebugFrame& f : frames) f.File = ResolveFile(f.File);
        return frames;
    }

    bool ProcessDebugger::SelectFrame(int index)
    {
        if (state != DebugState::Break || !session->Has("frame")) return false;
        std::string answer = Ask(Rep(session->Command("frame"), "$(N)", std::to_string(index)));
        if (Has(answer, "error:") || Has(answer, "No frame at level") || Has(answer, "Invalid frame")) return false;
        currentFrame = index;
        ReadWatches();
        return true;
    }

    std::string ProcessDebugger::Evaluate(const std::string& expression)
    {
        bool ok = false;
        return Evaluate(expression, ok);
    }

    std::string ProcessDebugger::Evaluate(const std::string& expression, bool& ok)
    {
        ok = false;
        if (state != DebugState::Break || StringUtil::IsNullOrWhiteSpace(expression)) return std::string();
        std::string answer = Ask(Rep(session->Command("evaluate"), "$(Expr)", expression));
        std::string value = DebugTranscript::ReadValue(dialect, answer, session->Marker);
        if (!value.empty()) { ok = true; return value; }
        std::string why = DebugTranscript::ComplaintIn(answer);
        return why.empty() ? std::string("no answer") : why;
    }

    std::string ProcessDebugger::AddressOf(const std::string& expression)
    {
        if (state != DebugState::Break || !session->Has("address")) return std::string();
        std::string answer = Ask(Rep(session->Command("address"), "$(Expr)", expression));
        std::string value = DebugTranscript::ReadValue(dialect, answer, session->Marker);
        return value.empty() ? std::string() : DebugTranscript::AddressIn(value);
    }

    std::string ProcessDebugger::TypeOf(const std::string& expression)
    {
        if (state != DebugState::Break || !session->Has("type")) return std::string();
        return DebugTranscript::ReadType(dialect, Ask(Rep(session->Command("type"), "$(Expr)", expression)), session->Marker);
    }

    bool ProcessDebugger::SetVariable(const std::string& name, const std::string& value, std::string& complaint)
    {
        complaint.clear();
        if (state != DebugState::Break || StringUtil::IsNullOrWhiteSpace(name) || StringUtil::IsNullOrWhiteSpace(value)) return false;
        std::string answer = Ask(Rep(Rep(session->Command("set"), "$(Name)", name), "$(Value)", value));
        std::string why = DebugTranscript::ComplaintIn(answer);
        if (!why.empty()) { complaint = why; return false; }
        ReadWatches();
        return true;
    }

    std::vector<RegisterValue> ProcessDebugger::Registers()
    {
        if (state != DebugState::Break || !session->Has("registers")) return std::vector<RegisterValue>();
        return DebugTranscript::ReadRegisters(dialect, Ask(session->Command("registers")), session->Marker);
    }

    std::vector<unsigned char> ProcessDebugger::ReadMemory(unsigned long long address, int length)
    {
        if (state != DebugState::Break || !session->Has("memory")) return std::vector<unsigned char>();
        char hex[32];
        std::snprintf(hex, sizeof hex, "0x%llx", address);
        std::string answer = Ask(Rep(Rep(session->Command("memory"), "$(Count)", std::to_string(length)), "$(Address)", hex));
        return DebugTranscript::ReadMemoryBytes(dialect, answer, session->Marker, length);
    }

    void ProcessDebugger::BreakpointsChanged()
    {
        if (state == DebugState::Break) SetEveryBreakpoint();
    }

    std::string ProcessDebugger::Ask(const std::string& command)
    {
        DebugProcessPtr p = Proc();
        if (!p || !p->Running() || !session) return std::string();
        std::lock_guard<std::mutex> hold(gate);
        p->Say(command);
        p->Say(session->MarkerCommand);
        bool found = false;
        std::string answer = p->ReadUntil(session->Marker, AskTimeoutMs, found);
        AppendSaid(answer);
        if (!found) { Print("the debugger stopped answering"); p->Stop(std::string()); }
        if (onConsole) answer = DebugTranscript::WithoutEcho(DebugTranscript::WithoutEscapes(answer), command, session->MarkerCommand);
        return answer;
    }

    std::string ProcessDebugger::Fill(const std::string& templ, const std::string& file, int line) const
    {
        return Rep(Rep(templ, "$(File)", DebugTranscript::LeafName(file)), "$(Line)", std::to_string(line));
    }

    // every stored breakpoint: clear, then break each (RIDE's SetEveryBreakpoint)
    void ProcessDebugger::SetEveryBreakpoint()
    {
        if (session->Has("clearBreakpoints")) Ask(session->Command("clearBreakpoints"));
        if (!breakpoints) return;
        for (BreakpointMap::const_iterator it = breakpoints->begin(); it != breakpoints->end(); ++it)
            for (int line : it->second)
            {
                std::string answer = Ask(Fill(session->Command("break"), it->first, line));
                bool ok = dialect == DebuggerKind::Cdb ? !Has(answer, "Couldn't resolve") && !Has(answer, "Bp expression")
                                                       : Has(answer, "Breakpoint") || Has(answer, "breakpoint");
                if (!ok) Print("no breakpoint at " + DebugTranscript::LeafName(it->first) + ":" + std::to_string(line) + " - " + Trim(answer));
            }
    }

    void ProcessDebugger::BeginMove(const std::string& command, bool stepping)
    {
        bool expected = false;
        if (!moving.compare_exchange_strong(expected, true)) return;
        if (state != DebugState::Running) SetState(DebugState::Running);
        if (mover.joinable()) mover.join();        // the last move has ended: moving was false
        mover = std::thread([this, command, stepping]() { MoveNow(command, stepping); });
    }

    // RIDE's afterMoving / afterStepping: the command, cdb's .lastevent and ln, the stop read, a
    // step that went nowhere repeated; then the state, the program's output and the watches
    DebugStopPtr ProcessDebugger::MoveNow(const std::string& commandId, bool stepping)
    {
        moving = true;
        if (state != DebugState::Running) state = DebugState::Running;
        DebugStopPtr origin = last;
        DebugStopPtr stop = commandId == "stepInto" && dialect == DebuggerKind::Cdb ? CdbStepInto(origin)
                          : stepping ? AfterStepping(commandId) : AfterMoving(commandId);
        Announce(stop);
        moving = false;
        return stop;
    }

    DebugStopPtr ProcessDebugger::AfterMoving(const std::string& commandId)
    {
        std::string command = session->Command(commandId);
        DebugProcessPtr p = Proc();
        if (!p || !p->Running())
        {
            DebugStopPtr gone = std::make_shared<DebugStop>();
            gone->Exited = true;
            return gone;
        }
        std::string answer;
        {
            std::lock_guard<std::mutex> hold(gate);
            p->Say(command);
            p->Say(session->MarkerCommand);
            bool found = false;
            answer = p->ReadUntil(session->Marker, MoveTimeoutMs, found);
            AppendSaid(answer);
            if (onConsole) answer = DebugTranscript::WithoutEcho(DebugTranscript::WithoutEscapes(answer), command, session->MarkerCommand);
            if (!found && p->Running()) { Print("the debugger stopped answering"); p->Stop(std::string()); }
        }
        if (dialect == DebuggerKind::Cdb && session->Has("afterMove"))
        {
            std::string ev = Ask(session->Command("afterMove"));
            answer += "\n" + ev;
            if (!Has(ev, "Exit process") && session->Has("where")) answer += "\n" + Ask(session->Command("where"));
        }
        for (const std::string& line : DebugTranscript::ProgramOutput(dialect, answer, MarkerStart())) PrintProgram(line);
        DebugStopPtr stop = DebugTranscript::ReadStop(dialect, answer);
        if (!Running()) { if (!stop->Exited) stop->Stopped = false; }
        if (stop->Stopped)
        {
            if (stop->File.empty()) stop->File = last->File;
            if (stop->Function.empty()) stop->Function = last->Function;
            last = stop;
        }
        else if (stop->Exited) last = std::make_shared<DebugStop>();
        return stop;
    }

    DebugStopPtr ProcessDebugger::AfterStepping(const std::string& commandId)
    {
        DebugStopPtr before = last;
        DebugStopPtr stop = AfterMoving(commandId);
        std::string all = stop->Said;
        for (int again = 0; again < 16 && DebugTranscript::WentNowhere(dialect, before, stop); again++)
        {
            before = stop;
            stop = AfterMoving(commandId);
            all += "\n" + stop->Said;
        }
        stop->Said = all;
        return stop;
    }

    // a step into a function with no source here steps out again and finishes the line
    bool ProcessDebugger::HasSource(const DebugStopPtr& stop) const
    {
        if (!stop->Stopped || stop->File.empty()) return false;
        return Paths::IsFile(ResolveFile(stop->File));
    }

    bool ProcessDebugger::SamePlace(const DebugStopPtr& a, const DebugStopPtr& b)
    {
        return a && b && a->Stopped && b->Stopped && a->Line == b->Line
               && StringUtil::EqualsIgnoreCase(DebugTranscript::LeafName(a->File), DebugTranscript::LeafName(b->File));
    }

    // the source line cdb echoes at a stop (">   16: Shape *make_rect(...) { return ...; }")
    std::string ProcessDebugger::EchoedLine(const std::string& text)
    {
        std::vector<std::string> all = DebugTranscript::Lines(text);
        for (size_t k = all.size(); k > 0; k--)
        {
            std::string bare = Trim(DebugTranscript::WithoutPrompt(all[k - 1]));
            if (!Starts(bare, ">")) continue;
            size_t colon = bare.find(':');
            if (colon != std::string::npos && colon > 1) return From(bare, colon + 1);
        }
        return std::string();
    }

    // cdb's Step Into as Visual Studio does it: each call on the line is entered in turn; one with no
    // source here is stepped back out of, and the step ends in the first function with source, or on
    // the next line when the line has none
    DebugStopPtr ProcessDebugger::CdbStepInto(const DebugStopPtr& origin)
    {
        std::string all;
        DebugStopPtr stop = std::make_shared<DebugStop>();
        for (int call = 0; call < 24; call++)
        {
            stop = AfterMoving("stepInto");
            std::string fresh = stop->Said;
            all += "\n" + fresh;
            // an incrementally linked program calls through a thunk: one more step reaches the function itself
            for (int thunk = 0; thunk < 4 && !stop->Stopped && !stop->Exited && Has(fresh, "!ILT+"); thunk++)
            {
                stop = AfterMoving("stepInto");
                fresh = stop->Said;
                all += "\n" + fresh;
            }
            if (stop->Exited || !Running()) break;
            if (HasSource(stop))
            {
                if (SamePlace(stop, origin) && stop->Function == origin->Function) continue;
                // at a function's entry its prologue is stepped past, unless the whole body is on the entry line
                std::string echoed = EchoedLine(fresh);
                size_t brace = echoed.find('{');
                bool oneLine = brace != std::string::npos && !Trim(From(echoed, brace + 1)).empty();
                if (AtFunctionEntry(fresh) && !oneLine)
                {
                    stop = AfterStepping("stepOver");
                    all += "\n" + stop->Said;
                }
                break;
            }
            // no source here: back out to a frame with source
            for (int up = 0; up < 8 && !stop->Exited && !HasSource(stop) && Running(); up++)
            {
                stop = AfterMoving("stepOut");
                all += "\n" + stop->Said;
            }
            if (stop->Exited || !HasSource(stop)) break;
            // back part way through the line the step began on: enter its next call
            if (!SamePlace(stop, origin)) break;
        }
        stop->Said = all;
        return stop;
    }

    bool ProcessDebugger::AtFunctionEntry(const std::string& text)
    {
        std::vector<std::string> all = DebugTranscript::Lines(text);
        for (size_t k = all.size(); k > 0; k--)
        {
            std::string bare = Trim(all[k - 1]);
            if (!Has(bare, "!") || !Ends(bare, ":")) continue;
            return !Has(bare, "+0x") && !Has(bare, " ");
        }
        return false;
    }

    std::string ProcessDebugger::MarkerStart() const
    {
        const std::string& m = session->Marker;
        size_t dash = m.find('-');
        return dash != std::string::npos && dash > 0 ? m.substr(0, dash) : m;
    }

    // the stop or the end, as RIDE's window says them
    void ProcessDebugger::Announce(const DebugStopPtr& stop)
    {
        if (stop->Stopped)
        {
            std::string file = ResolveFile(stop->File);
            ReadWatches();
            Print(DebugTranscript::StopLine(file, stop->Line, stop->Function));
            StopAt(file, stop->Line, Has(stop->Said, "Breakpoint") || Has(stop->Said, "breakpoint") ? "breakpoint" : "step", stop->Function);
            return;
        }
        // cdb answered and the program is still there, stopped where there is no line to show
        if (!stop->Exited && dialect == DebuggerKind::Cdb && Running())
        {
            Print("stopped where there is no source to show - Continue carries on, Step Out steps out");
            ReadWatches();
            location.reset();
            SetState(DebugState::Break);
            return;
        }
        exited = stop->Exited;
        exitStatus = stop->Status;
        if (stop->Exited) Print("the program ran to the end and returned " + std::to_string(stop->Status));
        else if (DebugTranscript::StoppedWithNoSource(stop->Said)) Print("stopped where there is no source to show");
        else Print("the debugger stopped answering");
        ReadWatches();
        DebugProcessPtr p = TakeProc();
        if (p) p->Stop(session->Command("quit"));
        location.reset();
        SetState(DebugState::Terminated);
        SetState(DebugState::Idle);
    }
}}
