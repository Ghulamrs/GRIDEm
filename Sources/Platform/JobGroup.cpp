// =============================================================================
//  JobGroup.cpp - see JobGroup.h
// =============================================================================
#include "JobGroup.h"
#include <cerrno>
#include <csignal>
#include <unistd.h>

namespace GRIDEm { namespace Platform {

    void JobGroup::BecomeLeader()
    {
        setpgid(0, 0);
    }

    bool JobGroup::Alive(pid_t leader)
    {
        if (leader <= 0) return false;
        return kill(-leader, 0) == 0 || errno == EPERM;
    }

    void JobGroup::Kill(pid_t leader)
    {
        if (leader <= 0) return;
        kill(-leader, SIGKILL);
        kill(leader, SIGKILL);
    }

    void JobGroup::End(pid_t leader, int graceMs)
    {
        if (leader <= 0) return;
        kill(-leader, SIGTERM);
        kill(leader, SIGTERM);
        for (int waited = 0; waited < graceMs; waited += 20)
        {
            if (!Alive(leader)) return;
            usleep(20 * 1000);
        }
        Kill(leader);
    }
}}
