// =============================================================================
//  JobGroup.h - a tool and every process it starts, ended together.
//  GRIDEd puts each child in a Windows job object; here each child leads a
//  process group of its own, and ending the group ends what it started.
// =============================================================================
#pragma once
#include <sys/types.h>

namespace GRIDEm { namespace Platform {

    class JobGroup
    {
    public:
        // called in the child after fork, before exec: it leads a new group
        static void BecomeLeader();
        // SIGTERM to the group, then SIGKILL after graceMs if it is still there
        static void End(pid_t leader, int graceMs);
        static void Kill(pid_t leader);       // SIGKILL at once
        static bool Alive(pid_t leader);
    };
}}
