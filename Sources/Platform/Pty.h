// =============================================================================
//  Pty.h - a pseudo-terminal pair (forkpty's first half).
//  Where GRIDEd starts cdb on a Windows pseudo-console, GRIDEm starts a
//  program that wants a terminal (lldb's prompt, a program's line-buffered
//  output, the Terminal pane's shell) on one of these.
// =============================================================================
#pragma once
#include <string>

namespace GRIDEm { namespace Platform {

    class Pty
    {
    public:
        // opens a pair; master is the IDE's end, slaveName the child's. False with why.
        static bool Open(int columns, int rows, int& master, std::string& slaveName, std::string& why);
        // in the child after fork: a new session with the slave as its terminal and standard streams
        static void AttachChild(const std::string& slaveName);
        static void Resize(int master, int columns, int rows);
        // the terminal echoes what is typed; GRIDEd's sessions say each line themselves
        static void SetEcho(int master, bool on);
    };
}}
