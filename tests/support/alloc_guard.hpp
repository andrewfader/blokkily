#pragma once

// Counts heap allocations made on the audio thread.
//
// alloc_guard.cpp replaces the global operator new and operator delete (every
// form: plain, array, nothrow, sized, aligned) for the whole program it is
// linked into. That is why it is linked into exactly one binary,
// blokkily_realtime_checks: a replaced global allocator belongs to one
// executable only. Because the executable's definitions pre-empt the C++
// runtime's, they also catch allocations made by blokkily_core and by plugins
// the test loads with dlopen.
//
// Counting is per thread. While an AllocationGuard is alive on a thread, every
// operator new and every operator delete of a non-null pointer made by that
// thread is counted; other threads are never counted, so a port thread or a
// helper allocating at the same moment cannot make a render look guilty.
//
// Not caught: direct calls to malloc, calloc, realloc and free, and
// allocations made by the kernel or a driver. The real-time rule forbids those
// too, but this guard does not prove their absence.

#include <cstddef>
#include <cstdint>

namespace blokkily::test {

struct AllocationCount {
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
    std::uint64_t bytes = 0;   // requested by the counted allocations
    // The size of the first counted allocation, which usually names the
    // culprit well enough to find it (0 when nothing was allocated).
    std::size_t first_size = 0;

    [[nodiscard]] bool clean() const noexcept { return allocations == 0 && deallocations == 0; }
    AllocationCount& operator+=(const AllocationCount& other) noexcept {
        if (allocations == 0) first_size = other.first_size;
        allocations += other.allocations;
        deallocations += other.deallocations;
        bytes += other.bytes;
        return *this;
    }
};

// Arms counting on the calling thread for its lifetime. Guards nest: an inner
// guard reports only what happened while it was alive, and the outer guard
// still sees it.
class AllocationGuard {
public:
    AllocationGuard() noexcept;
    ~AllocationGuard();
    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    // What this thread allocated and freed since the guard was made.
    [[nodiscard]] AllocationCount count() const noexcept;

private:
    AllocationCount start_;
};

// Whether any guard is alive on the calling thread.
[[nodiscard]] bool allocation_guard_armed() noexcept;

// Runs `work` inside a fresh guard and returns what it allocated.
template <typename Work>
[[nodiscard]] AllocationCount count_allocations(Work&& work) {
    AllocationGuard guard;
    work();
    return guard.count();
}

} // namespace blokkily::test
