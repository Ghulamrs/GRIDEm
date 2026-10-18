// =============================================================================
//  FileWatch.mm - see FileWatch.h
// =============================================================================
#include "FileWatch.h"
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

namespace GRIDEm { namespace Platform {

    FileWatch::FileWatch() : stream(nullptr) {}

    FileWatch::~FileWatch() { Stop(); }

    static void StreamCallback(ConstFSEventStreamRef ref, void* info, size_t count, void* paths,
                               const FSEventStreamEventFlags flags[], const FSEventStreamEventId ids[])
    {
        (void)ref; (void)flags; (void)ids;
        FileWatch::ChangeHandler* handler = static_cast<FileWatch::ChangeHandler*>(info);
        if (handler == nullptr || !*handler) return;
        std::vector<std::string> changed;
        char** list = static_cast<char**>(paths);
        for (size_t i = 0; i < count; i++) changed.push_back(list[i]);
        (*handler)(changed);
    }

    bool FileWatch::Start(const std::vector<std::string>& folders, const ChangeHandler& handler)
    {
        Stop();
        if (folders.empty()) return false;
        onChange = handler;
        NSMutableArray* list = [NSMutableArray array];
        for (const std::string& f : folders) [list addObject:[NSString stringWithUTF8String:f.c_str()]];
        FSEventStreamContext context;
        memset(&context, 0, sizeof context);
        context.info = &onChange;
        FSEventStreamRef s = FSEventStreamCreate(kCFAllocatorDefault, &StreamCallback, &context,
                                                 (__bridge CFArrayRef)list, kFSEventStreamEventIdSinceNow,
                                                 0.3, kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        if (s == nullptr) return false;
        FSEventStreamSetDispatchQueue(s, dispatch_get_main_queue());
        if (!FSEventStreamStart(s))
        {
            FSEventStreamInvalidate(s);
            FSEventStreamRelease(s);
            return false;
        }
        stream = s;
        return true;
    }

    void FileWatch::Stop()
    {
        if (stream == nullptr) return;
        FSEventStreamRef s = static_cast<FSEventStreamRef>(stream);
        FSEventStreamStop(s);
        FSEventStreamInvalidate(s);
        FSEventStreamRelease(s);
        stream = nullptr;
    }
}}
