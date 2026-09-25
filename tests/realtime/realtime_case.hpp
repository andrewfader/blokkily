#pragma once

// The named cases of blokkily_realtime_checks. Each case lives in its own file,
// tests/realtime/<case>.cpp, and puts itself into a static table with
// BLOKKILY_REALTIME_CASE; each is run as its own CTest test,
// realtime_<case>, through blokkily_add_realtime_case() in
// cmake/feature_test_support.cmake. A feature that changes process() adds one
// file and one call there, and touches nothing else.
//
// A case fails by throwing (require() does it); returning is a pass.

#include "../support/alloc_guard.hpp"

#include <cstdio>
#include <span>
#include <stdexcept>
#include <string>

namespace blokkily::realtime {

using CaseFunction = void (*)();

struct Case {
    const char* name;
    CaseFunction run;
};

// The table, in registration order. Filled during static initialisation,
// before main() and therefore before any guard is armed.
[[nodiscard]] std::span<const Case> cases() noexcept;

struct Registration {
    Registration(const char* name, CaseFunction run) noexcept;
};

inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Fails unless `count` is clean, naming what was measured and what it cost.
inline void require_no_allocations(const test::AllocationCount& count, const char* what) {
    if (count.clean()) return;
    char buffer[256];
    std::snprintf(buffer, sizeof buffer,
                  "%s: %llu allocation(s) (%llu bytes, first %zu bytes) and %llu "
                  "deallocation(s) inside the armed region",
                  what, static_cast<unsigned long long>(count.allocations),
                  static_cast<unsigned long long>(count.bytes), count.first_size,
                  static_cast<unsigned long long>(count.deallocations));
    throw std::runtime_error(buffer);
}

} // namespace blokkily::realtime

#define BLOKKILY_REALTIME_CASE(case_name)                                               \
    static void blokkily_realtime_case_##case_name();                                   \
    static const ::blokkily::realtime::Registration blokkily_realtime_registration_##case_name{ \
        #case_name, &blokkily_realtime_case_##case_name};                               \
    static void blokkily_realtime_case_##case_name()
