// =============================================================================
//  FileWatch.h - tells when files under a folder change on disk (FSEvents).
//  For the Files pane, and for a source changed outside the IDE. New in
//  GRIDEm: GRIDEd checks a file's time when its window is activated.
//  Implemented in FileWatch.mm (macOS only).
// =============================================================================
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace GRIDEm { namespace Platform {

    class FileWatch
    {
    public:
        // the changed paths, on the main queue
        typedef std::function<void(const std::vector<std::string>& paths)> ChangeHandler;

        FileWatch();
        ~FileWatch();
        FileWatch(const FileWatch&) = delete;
        FileWatch& operator=(const FileWatch&) = delete;

        bool Start(const std::vector<std::string>& folders, const ChangeHandler& handler);
        void Stop();

    private:
        void* stream;          // FSEventStreamRef
        ChangeHandler onChange;
    };
}}
