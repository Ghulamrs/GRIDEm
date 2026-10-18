// =============================================================================
//  Pty.cpp - see Pty.h
// =============================================================================
#include "Pty.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace GRIDEm { namespace Platform {

    bool Pty::Open(int columns, int rows, int& master, std::string& slaveName, std::string& why)
    {
        int m = posix_openpt(O_RDWR | O_NOCTTY);
        if (m < 0) { why = std::string("posix_openpt: ") + std::strerror(errno); return false; }
        if (grantpt(m) != 0 || unlockpt(m) != 0)
        {
            why = std::string("grantpt/unlockpt: ") + std::strerror(errno);
            close(m);
            return false;
        }
        const char* name = ptsname(m);
        if (name == nullptr) { why = "ptsname failed"; close(m); return false; }
        slaveName = name;
        master = m;
        fcntl(m, F_SETFD, FD_CLOEXEC);
        Resize(m, columns, rows);
        return true;
    }

    void Pty::AttachChild(const std::string& slaveName)
    {
        setsid();
        int s = open(slaveName.c_str(), O_RDWR);
        if (s < 0) _exit(126);
#if defined(TIOCSCTTY)
        ioctl(s, TIOCSCTTY, 0);
#endif
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2) close(s);
    }

    void Pty::Resize(int master, int columns, int rows)
    {
        struct winsize ws;
        std::memset(&ws, 0, sizeof ws);
        ws.ws_col = (unsigned short)columns;
        ws.ws_row = (unsigned short)rows;
        ioctl(master, TIOCSWINSZ, &ws);
    }

    void Pty::SetEcho(int master, bool on)
    {
        struct termios t;
        if (tcgetattr(master, &t) != 0) return;
        if (on) t.c_lflag |= ECHO;
        else t.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(master, TCSANOW, &t);
    }
}}
