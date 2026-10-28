// =============================================================================
//  ShalimarSession.cpp - see ShalimarSession.h
// =============================================================================
#include "ShalimarSession.h"
#include "../Core/StringUtil.h"
#include <chrono>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;

    ShalimarSession::ShalimarSession(const DebugLaunchPtr& l)
        : launch(l), moving(false), unit(0), line(0), depth(0), temporaryUnit(-1), temporaryLine(0)
    {
        session = launch->Session;
        last = std::make_shared<DebugStop>();
    }

    ShalimarSession::~ShalimarSession()
    {
        OnStateChanged = nullptr;
        OnStopped = nullptr;
        OnOutput = nullptr;
        OnProgramOutput = nullptr;
        DebugProcessPtr p = TakeProc();
        if (p) p->Stop(session ? session->Command("quit") : std::string());
        if (mover.joinable()) mover.join();
    }

    ShalimarSession::DebugProcessPtr ShalimarSession::Proc()
    {
        std::lock_guard<std::mutex> hold(procLock);
        return proc;
    }

    ShalimarSession::DebugProcessPtr ShalimarSession::TakeProc()
    {
        std::lock_guard<std::mutex> hold(procLock);
        DebugProcessPtr p = proc;
        if (p) lastProc = p;
        proc.reset();
        return p;
    }

    std::string ShalimarSession::Transcript()
    {
        std::lock_guard<std::mutex> hold(procLock);
        DebugProcessPtr p = proc ? proc : lastProc;
        return p ? p->Transcript() : std::string();
    }

    bool ShalimarSession::Running()
    {
        DebugProcessPtr p = Proc();
        return p && p->Running();
    }

    void ShalimarSession::AppendSaid(const std::string& text)
    {
        std::lock_guard<std::mutex> hold(saidLock);
        said += text;
    }

    std::string ShalimarSession::Said()
    {
        std::lock_guard<std::mutex> hold(saidLock);
        return said;
    }

    bool ShalimarSession::Start(const std::string& program, Core::Target target, const std::string& entryFile, int entryLine)
    {
        (void)program; (void)target; (void)entryFile; (void)entryLine;
        if (!StartSession()) return false;
        BeginMove("run");
        return true;
    }

    bool ShalimarSession::StartSession()
    {
        if (!session) { Print(!launch->WhyNot.empty() ? launch->WhyNot : std::string("no session")); return false; }
        Stop();
        exited = false;
        exitStatus = 0;
        currentFrame = 0;
        last = std::make_shared<DebugStop>();
        units.clear();
        sent.clear();
        depth = 0;
        unit = 0;
        line = 0;
        temporaryUnit = -1;
        DebugProcessPtr p = std::make_shared<DebugProcess>();
        Print("[" + launch->Backend + "] " + launch->Command);
        if (!launch->BackendWhy.empty()) Print("(" + launch->BackendWhy + ")");
        std::string why;
        if (!p->StartApart(launch->Command, launch->WorkDir, launch->Environment, why))
        {
            Print("the program could not be started: " + why);
            return false;
        }
        {
            std::lock_guard<std::mutex> hold(procLock);
            proc = p;
        }
        std::string ready = session->ProtocolWord("ready");
        for (;;)
        {
            bool alive = true;
            std::string heard;
            if (!p->Hear(ReadyTimeoutMs, alive, heard))
            {
                PrintProgramLines(p);
                Print(std::string(ShalimarWords::DidNotArm)
                      + (alive ? " (nothing came in " + std::to_string(ReadyTimeoutMs / 1000) + " s)" : std::string()));
                DebugProcessPtr gone = TakeProc();
                if (gone) gone->Stop(std::string());
                return false;
            }
            AppendSaid(heard + "\n");
            if (heard == ready) break;
            if (!Register(heard)) PrintProgram(heard);
        }
        for (;;)
        {
            bool alive = true;
            std::string heard;
            if (!p->Hear(SettleMs, alive, heard)) break;
            AppendSaid(heard + "\n");
            if (!Register(heard)) PrintProgram(heard);
        }
        if (!session->AfterReady.empty() && session->Has(session->AfterReady)) p->Say(session->Command(session->AfterReady));
        SetState(DebugState::Running);
        int set = SendWanted(p);
        Print("started the program itself with " + std::to_string(set) + " breakpoint" + (set == 1 ? "" : "s"));
        return true;
    }

    void ShalimarSession::Stop()
    {
        DebugProcessPtr p = TakeProc();
        if (!p)
        {
            if (state != DebugState::Idle) { location.reset(); SetState(DebugState::Idle); }
            return;
        }
        p->Stop(session ? session->Command("quit") : std::string());
        if (state != DebugState::Idle)
        {
            Print("Debugging stopped by user.");
            location.reset();
            SetState(DebugState::Terminated);
            SetState(DebugState::Idle);
        }
    }

    void ShalimarSession::Continue() { if (state == DebugState::Break) BeginMove("continue"); }
    void ShalimarSession::StepOver() { if (state == DebugState::Break) BeginMove("stepOver"); }
    void ShalimarSession::StepInto() { if (state == DebugState::Break) BeginMove("stepInto"); }
    void ShalimarSession::StepOut()  { if (state == DebugState::Break) BeginMove("stepOut"); }

    void ShalimarSession::BreakAll()
    {
        Print("a Shalimar program cannot be interrupted here - it stops at a breakpoint or at its end");
    }

    void ShalimarSession::RunToCursor(const std::string& file, int atLine)
    {
        if (state != DebugState::Break) return;
        int u = UnitFor(file);
        DebugProcessPtr p = Proc();
        if (u < 0 || !p || !session->Has("break")) { Continue(); return; }
        p->Say(Fill(session->Command("break"), u, atLine));
        temporaryUnit = u;
        temporaryLine = atLine;
        BeginMove("continue");
    }

    std::vector<DebugFrame> ShalimarSession::CallStack()
    {
        std::vector<DebugFrame> frames;
        if (state != DebugState::Break) return frames;
        frames.push_back(DebugFrame(std::to_string(depth) + " call" + (depth == 1 ? "" : "s") + " deep", ResolveFile(FileFor(unit)), line));
        return frames;
    }

    std::string ShalimarSession::Evaluate(const std::string& expression)
    {
        bool ok = false;
        return Evaluate(expression, ok);
    }

    std::string ShalimarSession::Evaluate(const std::string& expression, bool& ok)
    {
        (void)expression;
        ok = false;
        return std::string(ShalimarWords::SaysWhereOnly) + " - nothing to watch with";
    }

    bool ShalimarSession::SetVariable(const std::string& name, const std::string& value, std::string& answer)
    {
        (void)name; (void)value;
        answer = ShalimarWords::SaysWhereOnly;
        return false;
    }

    std::vector<unsigned char> ShalimarSession::ReadMemory(unsigned long long address, int length)
    {
        (void)address; (void)length;
        return std::vector<unsigned char>();
    }

    void ShalimarSession::BreakpointsChanged()
    {
        if (state != DebugState::Break) return;
        DebugProcessPtr p = Proc();
        if (!p) return;
        std::lock_guard<std::mutex> hold(gate);
        if (session->Has("clearBreakpoint"))
            for (const std::pair<int, int>& key : sent)
                p->Say(Fill(session->Command("clearBreakpoint"), key.first, key.second));
        sent.clear();
        SendWanted(p);
    }

    std::string ShalimarSession::Fill(const std::string& templ, int u, int l)
    {
        return StringUtil::Replace(StringUtil::Replace(templ, "$(Unit)", std::to_string(u)), "$(Line)", std::to_string(l));
    }

    bool ShalimarSession::Register(const std::string& heard)
    {
        std::string head = session->ProtocolWord("file");
        if (head.empty() || !StringUtil::StartsWith(heard, head + " ")) return false;
        std::string rest = StringUtil::Trim(heard.substr(head.size() + 1));
        size_t space = rest.find(' ');
        int u = 0;
        if (space == std::string::npos || space == 0 || !StringUtil::TryParseInt(rest.substr(0, space), u)) return false;
        units[u] = StringUtil::Trim(rest.substr(space + 1));
        return true;
    }

    int ShalimarSession::UnitFor(const std::string& file) const
    {
        std::string leaf = DebugTranscript::LeafName(file);
        for (std::map<int, std::string>::const_iterator it = units.begin(); it != units.end(); ++it)
            if (StringUtil::EqualsIgnoreCase(it->second, leaf)) return it->first;
        return -1;
    }

    std::string ShalimarSession::FileFor(int u) const
    {
        std::map<int, std::string>::const_iterator it = units.find(u);
        return it == units.end() ? std::string() : it->second;
    }

    // every stored breakpoint whose file the program named (RIDE's sendWanted); the others said
    int ShalimarSession::SendWanted(const DebugProcessPtr& p)
    {
        int set = 0;
        if (!p || !session->Has("break") || !breakpoints) return 0;
        for (BreakpointMap::const_iterator it = breakpoints->begin(); it != breakpoints->end(); ++it)
        {
            if (it->second.empty()) continue;
            int u = UnitFor(it->first);
            if (u < 0)
            {
                Print("no breakpoint in " + DebugTranscript::LeafName(it->first) + " - the program names no such source");
                continue;
            }
            for (int l : it->second)
            {
                p->Say(Fill(session->Command("break"), u, l));
                sent.push_back(std::make_pair(u, l));
                set++;
            }
        }
        return set;
    }

    bool ShalimarSession::Three(const std::string& rest, int& a, int& b, int& c)
    {
        a = 0; b = 0; c = 0;
        std::vector<std::string> parts = StringUtil::SplitNonEmpty(rest, " \t");
        if (parts.size() < 3) return false;
        return StringUtil::TryParseInt(parts[0], a) && StringUtil::TryParseInt(parts[1], b) && StringUtil::TryParseInt(parts[2], c);
    }

    void ShalimarSession::BeginMove(const std::string& command)
    {
        bool expected = false;
        if (!moving.compare_exchange_strong(expected, true)) return;
        if (state != DebugState::Running) SetState(DebugState::Running);
        if (mover.joinable()) mover.join();        // the last move has ended: moving was false
        mover = std::thread([this, command]() { MoveNow(command); });
    }

    DebugStopPtr ShalimarSession::MoveNow(const std::string& commandId)
    {
        moving = true;
        if (state != DebugState::Running) state = DebugState::Running;
        DebugStopPtr stop;
        {
            std::lock_guard<std::mutex> hold(gate);
            DebugProcessPtr p = Proc();
            if (!p || !p->Running())
            {
                stop = std::make_shared<DebugStop>();
                stop->Exited = true;
                stop->Status = exitStatus;
            }
            else
            {
                p->Say(session->Command(commandId));
                stop = Listen(p, MoveTimeoutMs);
                if (temporaryUnit >= 0 && p->Running() && session->Has("clearBreakpoint"))
                {
                    p->Say(Fill(session->Command("clearBreakpoint"), temporaryUnit, temporaryLine));
                    temporaryUnit = -1;
                }
            }
        }
        Announce(stop, commandId == "run" || commandId == "continue");
        moving = false;
        return stop;
    }

    // RIDE's Session::listen: the next stop or exit line, the program's own lines shown as they
    // come; the end of the program with neither is an exit with its exit code
    DebugStopPtr ShalimarSession::Listen(const DebugProcessPtr& p, int timeoutMs)
    {
        DebugStopPtr where = std::make_shared<DebugStop>();
        std::string stopWord = session->ProtocolWord("stop");
        std::string exitWord = session->ProtocolWord("exit");
        std::string atWord = session->ProtocolWord("at");
        for (;;)
        {
            bool alive = true;
            std::string heard;
            if (!p->Hear(timeoutMs, alive, heard))
            {
                if (!alive)
                {
                    where->Exited = true;
                    where->Status = p->ExitCode();
                }
                where->Said = PrintProgramLines(p);
                return where;
            }
            AppendSaid(heard + "\n");
            if (!stopWord.empty() && StringUtil::StartsWith(heard, stopWord + " "))
            {
                int u = 0, l = 0, d = 0;
                if (!Three(heard.substr(stopWord.size()), u, l, d)) continue;
                unit = u;
                line = l;
                depth = d;
                where->Stopped = true;
                where->File = FileFor(u);
                where->Line = l;
                where->Said = PrintProgramLines(p);
                return where;
            }
            if (!exitWord.empty() && StringUtil::StartsWith(heard, exitWord + " "))
            {
                int status = 0, ignored = 0, alsoIgnored = 0;
                if (!Three(heard.substr(exitWord.size()) + " 0 0", status, ignored, alsoIgnored)) status = 0;
                where->Exited = true;
                where->Status = status;
                WaitForEnd(p, 2000);
                where->Said = PrintProgramLines(p);
                return where;
            }
            if (Register(heard) || (!atWord.empty() && StringUtil::StartsWith(heard, atWord + " "))) continue;
            PrintProgram(heard);
        }
    }

    // the program's standard output is flushed as it ends: a moment for it after the exit line
    void ShalimarSession::WaitForEnd(const DebugProcessPtr& p, int ms)
    {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        while (p && p->Running()
               && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::string ShalimarSession::PrintProgramLines(const DebugProcessPtr& p)
    {
        if (!p) return std::string();
        std::string text = p->Printed();
        for (const std::string& l : DebugTranscript::Lines(text)) PrintProgram(l);
        return text;
    }

    // the stop or the end, as RIDE's window says them
    void ShalimarSession::Announce(const DebugStopPtr& stop, bool ran)
    {
        if (stop->Stopped)
        {
            std::string file = ResolveFile(stop->File);
            last = stop;
            Print(DebugTranscript::StopLine(file, stop->Line, std::string()));
            StopAt(file, stop->Line, ran ? "breakpoint" : "step");
            return;
        }
        exited = stop->Exited;
        exitStatus = stop->Status;
        if (stop->Exited) Print("the program ran to the end and returned " + std::to_string(stop->Status));
        else Print("the program stopped answering");
        ReadWatches();
        DebugProcessPtr p = TakeProc();
        if (p) p->Stop(session->Command("quit"));
        location.reset();
        SetState(DebugState::Terminated);
        SetState(DebugState::Idle);
    }
}}
