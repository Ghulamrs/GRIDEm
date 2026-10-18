// Parallel.cpp - worker pool (Windows threads) with a sequential fallback.
#include "Parallel.hpp"

#include <ctime>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cppparse {

int hardwareThreads()
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = static_cast<int>(si.dwNumberOfProcessors);
    return n < 1 ? 1 : (n > 63 ? 63 : n);
#else
    return 1;
#endif
}

long millisecondsNow()
{
#ifdef _WIN32
    return static_cast<long>(GetTickCount());
#else
    return static_cast<long>(std::clock() * 1000L / CLOCKS_PER_SEC);
#endif
}

#ifdef _WIN32
namespace {

struct PoolState {
    ParallelJob*  job;
    size_t        count;
    volatile LONG next;     // next job index to take (interlocked)
};

DWORD WINAPI worker(LPVOID param)
{
    PoolState* st = static_cast<PoolState*>(param);
    for (;;) {
        LONG i = InterlockedIncrement(&st->next) - 1;
        if (i < 0 || static_cast<size_t>(i) >= st->count) break;
        st->job->run(static_cast<size_t>(i));
    }
    return 0;
}

} // namespace
#endif

void runParallel(ParallelJob& job, size_t count, int threads)
{
    if (count == 0) return;
#ifdef _WIN32
    if (threads > static_cast<int>(count)) threads = static_cast<int>(count);
    if (threads > 63) threads = 63;
    if (threads > 1) {
        PoolState st;
        st.job = &job;
        st.count = count;
        st.next = 0;
        HANDLE handles[63];
        int started = 0;
        for (int t = 0; t < threads; ++t) {
            HANDLE h = CreateThread(0, 0, worker, &st, 0, 0);
            if (!h) break;
            handles[started++] = h;
        }
        if (started > 0) {
            WaitForMultipleObjects(static_cast<DWORD>(started), handles, TRUE, INFINITE);
            for (int t = 0; t < started; ++t) CloseHandle(handles[t]);
            for (;;) {   // anything not taken (thread creation failed early) runs here
                LONG i = InterlockedIncrement(&st.next) - 1;
                if (i < 0 || static_cast<size_t>(i) >= count) break;
                job.run(static_cast<size_t>(i));
            }
            return;
        }
    }
#else
    (void)threads;
#endif
    for (size_t i = 0; i < count; ++i) job.run(i);
}

Mutex& semanticMutex() { static Mutex m; return m; }

#ifdef _WIN32
Mutex::Mutex() : m_impl(new CRITICAL_SECTION) { InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(m_impl)); }
Mutex::~Mutex() { DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(m_impl)); delete static_cast<CRITICAL_SECTION*>(m_impl); }
void Mutex::lock() { EnterCriticalSection(static_cast<CRITICAL_SECTION*>(m_impl)); }
void Mutex::unlock() { LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(m_impl)); }
#else
Mutex::Mutex() : m_impl(0) {}
Mutex::~Mutex() {}
void Mutex::lock() {}
void Mutex::unlock() {}
#endif

} // namespace cppparse
