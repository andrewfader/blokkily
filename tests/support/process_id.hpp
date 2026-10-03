#pragma once

// This process's id, which keeps the files and ports of suites running side
// by side apart.

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace blokkily::test {

inline long process_id() {
#if defined(_WIN32)
    return _getpid();
#else
    return ::getpid();
#endif
}

} // namespace blokkily::test
