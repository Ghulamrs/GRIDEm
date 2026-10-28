// =============================================================================
//  Debugger.cpp - see Debugger.h
// =============================================================================
#include "Debugger.h"
#include "../Core/TextFile.h"
#include "../Platform/Paths.h"
#include <cstdio>
#include <cstring>
#include <regex>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;
    using Core::Target;
    using Platform::Paths;

    // =========================================================================
    //  DebugEngine
    // =========================================================================
    DebugEngine::DebugEngine()
        : StopAtEntry(false), state(DebugState::Idle), breakpoints(std::make_shared<BreakpointMap>()),
          currentFrame(0), exitStatus(0), exited(false) {}

    const char* const DebugEngine::ContinuedMark = "<<more>>";

    bool DebugEngine::Continues(const std::string& line, std::string& rest)
    {
        rest = line;
        if (!StringUtil::StartsWith(line, ContinuedMark)) return false;
        rest = line.substr(std::strlen(ContinuedMark));
        return true;
    }

    bool DebugEngine::SetVariable(const std::string& name, const std::string& value, std::string& said)
    {
        (void)name; (void)value;
        said = "this debugger cannot set a value";
        return false;
    }

    void DebugEngine::AddWatch(const std::string& expression)
    {
        if (StringUtil::IsNullOrWhiteSpace(expression)) return;
        watches.push_back(DebugWatch(StringUtil::Trim(expression)));
        ReadWatches();
    }

    void DebugEngine::SetWatch(int index, const std::string& expression)
    {
        if (index < 0 || index >= (int)watches.size()) return;
        if (StringUtil::IsNullOrWhiteSpace(expression)) { watches.erase(watches.begin() + index); return; }
        watches[(size_t)index] = DebugWatch(StringUtil::Trim(expression));
        ReadWatches();
    }

    void DebugEngine::RemoveWatch(int index)
    {
        if (index >= 0 && index < (int)watches.size()) watches.erase(watches.begin() + index);
    }

    void DebugEngine::ReadWatches()
    {
        for (DebugWatch& w : watches)
        {
            if (state != DebugState::Break)
            {
                w.Value = "not running"; w.Ok = false; w.Type.clear(); w.Address.clear();
                continue;
            }
            bool ok = false;
            w.Value = Evaluate(w.Expression, ok);
            w.Ok = ok;
            w.Address = ok ? AddressOf(w.Expression) : std::string();
            w.Type = ok ? TypeOf(w.Expression) : std::string();
        }
    }

    std::string DebugEngine::ResolveFile(const std::string& named) const
    {
        if (named.empty()) return named;
        if (Paths::IsRooted(named) && Paths::IsFile(named)) return Paths::FullPath(named);
        std::string leaf = Paths::FileName(StringUtil::ReplaceChar(named, '\\', '/'));
        for (const std::string& f : sources)
            if (StringUtil::EqualsIgnoreCase(Paths::FileName(f), leaf)) return f;
        for (BreakpointMap::const_iterator it = breakpoints->begin(); it != breakpoints->end(); ++it)
            if (StringUtil::EqualsIgnoreCase(Paths::FileName(it->first), leaf)) return it->first;
        return named;
    }

    void DebugEngine::SetState(DebugState s)
    {
        state = s;
        if (OnStateChanged) OnStateChanged(s);
    }

    void DebugEngine::Print(const std::string& s) { if (OnOutput) OnOutput(s); }

    void DebugEngine::PrintProgram(const std::string& s) { if (OnProgramOutput) OnProgramOutput(s); }

    void DebugEngine::StopAt(const std::string& file, int line, const std::string& reason, const std::string& function)
    {
        location = std::make_shared<StopInfo>(file, line, reason, function);
        currentFrame = 0;
        state = DebugState::Break;
        ReadWatches();      // read stopped, so the Watch pane shows values, not "not running"
        if (OnStateChanged) OnStateChanged(state);
        if (OnStopped) OnStopped(location);
    }

    // =========================================================================
    //  PlaceholderDebugger
    // =========================================================================
    PlaceholderDebugger::PlaceholderDebugger() : target(Target::Windows), steps(0) {}

    bool PlaceholderDebugger::Start(const std::string& program, Target t, const std::string& entryFile, int entryLine)
    {
        target = t;
        file = entryFile;
        steps = 0;
        lines.clear();
        if (!file.empty() && Paths::IsFile(file))
        {
            std::string text, error;
            Core::FileEncoding enc;
            if (Core::TextFile::Read(file, text, enc, error))
            {
                lines = StringUtil::SplitLines(text);
                // File::ReadAllLines gives no empty line after a final line end
                if (!lines.empty() && lines.back().empty()) lines.pop_back();
            }
        }
        Print("Placeholder debugger started - no real target is attached.");
        Print("Program: " + program);
        if (!program.empty() && !Paths::IsFile(program))
            Print("(program file does not exist yet - build the project to produce it)");
        if (t == Target::C6747) Print("VM6747 integration pending: execution is simulated line by line.");
        SetState(DebugState::Running);

        int first = NextBreakpoint(entryLine - 1);
        if (first > 0) StopAt(file, first, "breakpoint");
        else
        {
            int l = NextCodeLine(entryLine);
            if (l > 0) StopAt(file, l, "entry");
            else Finish();
        }
        return true;
    }

    void PlaceholderDebugger::Stop()
    {
        if (state == DebugState::Idle) return;
        Print("Debugging stopped by user.");
        location.reset();
        SetState(DebugState::Terminated);
        SetState(DebugState::Idle);
    }

    void PlaceholderDebugger::Continue()
    {
        if (state != DebugState::Break) return;
        SetState(DebugState::Running);
        int next = NextBreakpoint(location->Line);
        if (next > 0) StopAt(file, next, "breakpoint");
        else Finish();
    }

    void PlaceholderDebugger::BreakAll()
    {
        if (state == DebugState::Running) StopAt(file, location ? location->Line : 1, "break");
    }

    void PlaceholderDebugger::RunToCursor(const std::string& f, int line)
    {
        if (state != DebugState::Break) return;
        if (!StringUtil::EqualsIgnoreCase(f, file) || line <= location->Line) { Continue(); return; }
        int bp = NextBreakpoint(location->Line);
        if (bp > 0 && bp < line) StopAt(file, bp, "breakpoint");
        else StopAt(file, line, "cursor");
    }

    std::string PlaceholderDebugger::Hex32(int v)
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08X", (unsigned int)v);
        return buf;
    }

    // the seeds wrap as GRIDEd's int arithmetic does
    static int Wrap(long long v) { return (int)(unsigned int)(unsigned long long)v; }

    std::vector<RegisterValue> PlaceholderDebugger::Registers()
    {
        std::vector<RegisterValue> r;
        int at = location ? location->Line : 0;
        int seed = Wrap((long long)at * 2654435LL + (long long)steps * 977LL);
        if (target == Target::C6747)
        {
            for (int i = 0; i < 16; i++) r.push_back(RegisterValue("A" + std::to_string(i), Hex32(Wrap((long long)seed + i * 4099LL))));
            for (int i = 0; i < 16; i++) r.push_back(RegisterValue("B" + std::to_string(i), Hex32(Wrap((long long)seed * 3LL + i * 7919LL))));
            r.push_back(RegisterValue("PC", Hex32(0x11800000 + at * 32)));
            r.push_back(RegisterValue("CSR", "0x14000100"));
            r.push_back(RegisterValue("AMR", "0x00000000"));
            r.push_back(RegisterValue("IER", "0x00000003"));
        }
        else
        {
            const char* names[] = { "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP", "R8", "R9", "R10", "R11" };
            for (int i = 0; i < 12; i++) r.push_back(RegisterValue(names[i], Hex32(Wrap((long long)seed + i * 104729LL))));
            r.push_back(RegisterValue("RIP", Hex32(0x00401000 + at * 16)));
            r.push_back(RegisterValue("EFLAGS", "0x00000246"));
        }
        return r;
    }

    std::vector<DebugFrame> PlaceholderDebugger::CallStack()
    {
        std::vector<DebugFrame> r;
        if (!location) return r;
        r.push_back(DebugFrame(FunctionAt(location->Line) + "()", file, location->Line));
        if (target == Target::C6747) r.push_back(DebugFrame("_c_int00()  [RTS6747]", std::string(), 0));
        else r.push_back(DebugFrame("[startup code]", std::string(), 0));
        return r;
    }

    std::vector<DebugVariable> PlaceholderDebugger::Locals()
    {
        // simple scan of declarations "type name" between the function start and the current line
        std::vector<DebugVariable> r;
        if (!location) return r;
        int start = FunctionStartLine(location->Line);
        static const std::regex decl(
            "^\\s*(?:const\\s+|static\\s+|unsigned\\s+|signed\\s+)*(int|char|short|long|float|double|[A-Z_]\\w*)\\s*\\*?\\s*([A-Za-z_]\\w*)\\s*(=|;|\\[)");
        for (int i = start; i < location->Line - 1 && i < (int)lines.size(); i++)
        {
            std::smatch m;
            if (std::regex_search(lines[(size_t)i], m, decl))
                r.push_back(DebugVariable(m[2].str(), "<simulated>", m[1].str()));
        }
        return r;
    }

    std::string PlaceholderDebugger::Evaluate(const std::string& expression)
    {
        (void)expression;
        return "<no target attached - value unavailable>";
    }

    std::vector<unsigned char> PlaceholderDebugger::ReadMemory(unsigned long long address, int length)
    {
        std::vector<unsigned char> b;
        for (int i = 0; i < length; i++) b.push_back((unsigned char)(((address + (unsigned long long)i) * 31u + 7u) & 0xFF));
        return b;
    }

    void PlaceholderDebugger::Finish()
    {
        Print("Program exited with code 0 (simulated).");
        location.reset();
        SetState(DebugState::Terminated);
        SetState(DebugState::Idle);
    }

    void PlaceholderDebugger::Step()
    {
        if (state != DebugState::Break) return;
        steps++;
        int l = NextCodeLine(location->Line + 1);
        if (l > 0) StopAt(file, l, "step");
        else Finish();
    }

    int PlaceholderDebugger::NextBreakpoint(int afterLine) const
    {
        if (file.empty()) return -1;
        BreakpointMap::const_iterator it = breakpoints->find(file);
        if (it == breakpoints->end()) return -1;
        for (int l : it->second)
            if (l > afterLine) return l;
        return -1;
    }

    int PlaceholderDebugger::NextCodeLine(int from) const
    {
        if (from < 1) from = 1;
        for (int i = from; i <= (int)lines.size(); i++)
        {
            std::string t = StringUtil::Trim(lines[(size_t)(i - 1)]);
            if (t.empty() || t == "{" || t == "}" || StringUtil::StartsWith(t, "//") || StringUtil::StartsWith(t, "/*")
                || StringUtil::StartsWith(t, "*") || StringUtil::StartsWith(t, "#"))
                continue;
            return i;
        }
        return -1;
    }

    static const std::regex& FunctionPattern()
    {
        static const std::regex fn("^[A-Za-z_][\\w\\s\\*]*\\b(\\w+)\\s*\\([^;]*\\)\\s*\\{?\\s*$");
        return fn;
    }

    int PlaceholderDebugger::FunctionStartLine(int line) const
    {
        int from = line - 1 < (int)lines.size() - 1 ? line - 1 : (int)lines.size() - 1;
        for (int i = from; i >= 0; i--)
            if (std::regex_search(lines[(size_t)i], FunctionPattern())) return i;
        return 0;
    }

    std::string PlaceholderDebugger::FunctionAt(int line) const
    {
        int from = line - 1 < (int)lines.size() - 1 ? line - 1 : (int)lines.size() - 1;
        for (int i = from; i >= 0; i--)
        {
            std::smatch m;
            if (std::regex_search(lines[(size_t)i], m, FunctionPattern())) return m[1].str();
        }
        return "main";
    }
}}
