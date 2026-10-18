// Parallel.hpp - a minimal worker pool for independent jobs.
//
// C++98 has no threads; on Windows the pool uses CreateThread, elsewhere it
// falls back to running the jobs sequentially. Jobs must not touch shared
// state: the loader uses it to read, lex and parse source files, which only
// write into their own SourceFile.
//
// Strict C++98 (plus the Windows thread API on Windows).
#ifndef CPPPARSE_PARALLEL_HPP
#define CPPPARSE_PARALLEL_HPP

#include <cstddef>

namespace cppparse {

struct ParallelJob {
    virtual ~ParallelJob() {}
    virtual void run(size_t index) = 0;   // called for every index in [0, count), from any thread
};

int  hardwareThreads();                                       // logical processors (at least 1)
void runParallel(ParallelJob& job, size_t count, int threads); // threads <= 1 runs sequentially
long millisecondsNow();                                       // clock for timing

// a simple mutual exclusion lock for the few shared structures (type interning, instantiation queue)
class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
private:
    Mutex(const Mutex&);
    Mutex& operator=(const Mutex&);
    void* m_impl;
};

// the type table, the template instantiator and the class layouts call each other, so they
// share one recursive lock: a worker holds it while it parses a type or instantiates a template
Mutex& semanticMutex();

class Lock {
public:
    explicit Lock(Mutex& m) : m_m(m) { m_m.lock(); }
    ~Lock() { m_m.unlock(); }
private:
    Lock(const Lock&);
    Lock& operator=(const Lock&);
    Mutex& m_m;
};

} // namespace cppparse

#endif
