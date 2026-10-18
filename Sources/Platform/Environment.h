// =============================================================================
//  Environment.h - the process environment, and expanding names in text
//  (System::Environment::ExpandEnvironmentVariables and GetEnvironmentVariables)
// =============================================================================
#pragma once
#include <map>
#include <string>
#include <vector>

namespace GRIDEm { namespace Platform {

    class Environment
    {
    public:
        // %NAME% (as GRIDEd's files write it), $NAME and ${NAME}; an unset name is left as written
        static std::string Expand(const std::string& text);
        // every variable of this process
        static std::map<std::string, std::string> All();
        // this process's environment with "NAME=value" changes laid over it, as a block for exec:
        // NAME= with nothing after it removes the variable, as GRIDEd's ToolRunner does
        static std::vector<std::string> WithChanges(const std::vector<std::string>& changes);
    };
}}
