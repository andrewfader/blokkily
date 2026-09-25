// The harness checks itself before anything relies on it: a guard that never
// fires would make every "zero allocations" case pass for nothing. So a
// deliberate allocation inside an armed region must be seen, by the very
// check the engine cases use, and nothing outside the region may be blamed.

#include "realtime_case.hpp"

#include <atomic>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {

// Storing a pointer here makes it escape, so the compiler cannot elide the
// allocation the check is looking for.
void* volatile sink = nullptr;

struct alignas(64) Wide {
    float lanes[16];
};

} // namespace

BLOKKILY_REALTIME_CASE(alloc_guard_self_check) {
    using blokkily::realtime::require;
    using blokkily::realtime::require_no_allocations;
    using namespace blokkily::test;

    require(!allocation_guard_armed(), "no guard may be armed outside a case's region");

    // The deliberate allocation: the check the engine cases rely on must
    // reject it and say how much was allocated.
    const auto deliberate = count_allocations([] {
        auto* value = new int(7);
        sink = value;
        delete value;
    });
    require(deliberate.allocations == 1 && deliberate.deallocations == 1 &&
                deliberate.bytes == sizeof(int) && deliberate.first_size == sizeof(int),
            "a deliberate new/delete inside an armed region must be counted exactly");
    bool rejected = false;
    try {
        require_no_allocations(deliberate, "deliberate allocation");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "the zero-allocation check must reject a deliberate allocation");

    // Every form the language offers is caught: arrays, containers, over-aligned
    // types, nothrow, and allocations made through the standard library.
    const auto forms = count_allocations([] {
        auto* array = new float[32];
        sink = array;
        delete[] array;
        auto* wide = new Wide{};
        sink = wide;
        delete wide;
        auto* quiet = new (std::nothrow) double(1.0);
        sink = quiet;
        delete quiet;
        std::vector<float> grown(128, 0.0F);
        sink = grown.data();
        auto shared = std::make_shared<int>(3);
        sink = shared.get();
    });
    require(forms.allocations == 5 && forms.deallocations == 5,
            "array, over-aligned, nothrow, container and shared allocations must all count");

    // Freeing on the audio thread is as forbidden as allocating there.
    auto* made_outside = new int(1);
    sink = made_outside;
    const auto freed = count_allocations([&] { delete made_outside; });
    require(freed.allocations == 0 && freed.deallocations == 1 && !freed.clean(),
            "a delete inside an armed region must be counted");
    rejected = false;
    try {
        require_no_allocations(freed, "deliberate deallocation");
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "the zero-allocation check must reject a deliberate deallocation");

    // Nothing is counted outside an armed region, and a region that does no
    // heap work reports clean.
    auto* unarmed = new int(2);
    sink = unarmed;
    delete unarmed;
    int on_the_stack = 0;
    const auto clean = count_allocations([&] { on_the_stack += 1; });
    require(clean.clean() && clean.first_size == 0 && on_the_stack == 1,
            "a region without heap work must report clean");

    // Guards nest: the inner one reports only its own region. Counts are read
    // into locals, because building a failure message would itself allocate.
    AllocationCount inner_count;
    AllocationCount outer_count;
    {
        AllocationGuard outer;
        auto* first = new char[3];
        sink = first;
        {
            AllocationGuard inner;
            auto* second = new char[5];
            sink = second;
            delete[] second;
            inner_count = inner.count();
        }
        delete[] first;
        outer_count = outer.count();
    }
    require(inner_count.allocations == 1 && inner_count.deallocations == 1 &&
                inner_count.first_size == 5,
            "an inner guard counts only its own region");
    require(outer_count.allocations == 2 && outer_count.deallocations == 2 &&
                outer_count.first_size == 3,
            "an outer guard also sees what an inner guard counted");
    require(!allocation_guard_armed(), "the guard must disarm when the region ends");

    // Counting belongs to the armed thread. Another thread allocating while
    // this one is armed is not this thread's allocation. The thread is made
    // before arming, because starting a thread allocates on the caller.
    std::atomic<int> step{0};
    bool other_was_armed = true;
    std::thread other([&] {
        while (step.load(std::memory_order_acquire) != 1) std::this_thread::yield();
        other_was_armed = allocation_guard_armed();
        auto* elsewhere = new int(4);
        sink = elsewhere;
        delete elsewhere;
        step.store(2, std::memory_order_release);
    });
    AllocationCount across_threads;
    {
        AllocationGuard guard;
        step.store(1, std::memory_order_release);
        while (step.load(std::memory_order_acquire) != 2) std::this_thread::yield();
        across_threads = guard.count();
    }
    other.join();
    require(!other_was_armed, "arming one thread must not arm another");
    require(across_threads.clean(),
            "an allocation on another thread must not be charged to the armed thread");
}
