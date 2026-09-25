#include "alloc_guard.hpp"

#include <array>
#include <cstdlib>
#include <new>

// The replaced global allocator. Everything here runs inside operator new, so
// it may not allocate itself: the per-thread state is plain constant-
// initialised data, which needs no dynamic TLS initialisation on first use.

namespace {

// How many guards may nest before the innermost ones stop recording their
// first allocation's size. Counting itself does not depend on this.
constexpr std::size_t maximum_depth = 8;

struct ThreadCounters {
    std::size_t depth = 0;
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
    std::uint64_t bytes = 0;
    // The size of the first allocation seen by the guard at each depth.
    std::array<std::size_t, maximum_depth> first_size{};
};

constinit thread_local ThreadCounters counters{};

void note_allocation(std::size_t size) noexcept {
    if (counters.depth == 0) return;
    ++counters.allocations;
    counters.bytes += size;
    const auto levels = counters.depth < maximum_depth ? counters.depth : maximum_depth;
    for (std::size_t level = 0; level < levels; ++level)
        if (counters.first_size[level] == 0) counters.first_size[level] = size == 0 ? 1 : size;
}

void note_deallocation(void* pointer) noexcept {
    if (pointer != nullptr && counters.depth > 0) ++counters.deallocations;
}

void* allocate(std::size_t size, std::size_t alignment, bool may_throw) {
    note_allocation(size);
    // operator new of zero bytes must still return a distinct pointer.
    const std::size_t request = size == 0 ? 1 : size;
    for (;;) {
        void* pointer = nullptr;
        if (alignment <= alignof(std::max_align_t)) {
            pointer = std::malloc(request);
        } else {
            const auto align = alignment < sizeof(void*) ? sizeof(void*) : alignment;
            if (posix_memalign(&pointer, align, request) != 0) pointer = nullptr;
        }
        if (pointer != nullptr) return pointer;
        const auto handler = std::get_new_handler();
        if (handler == nullptr) {
            if (may_throw) throw std::bad_alloc();
            return nullptr;
        }
        if (!may_throw) {
            try {
                handler();
            } catch (...) {
                return nullptr;
            }
        } else {
            handler();
        }
    }
}

void release(void* pointer) noexcept {
    note_deallocation(pointer);
    std::free(pointer);
}

} // namespace

namespace blokkily::test {

AllocationGuard::AllocationGuard() noexcept
    : start_{counters.allocations, counters.deallocations, counters.bytes, 0} {
    if (counters.depth < maximum_depth) counters.first_size[counters.depth] = 0;
    ++counters.depth;
}

AllocationGuard::~AllocationGuard() { --counters.depth; }

AllocationCount AllocationGuard::count() const noexcept {
    const auto level = counters.depth - 1;
    return {counters.allocations - start_.allocations,
            counters.deallocations - start_.deallocations, counters.bytes - start_.bytes,
            level < maximum_depth ? counters.first_size[level] : 0};
}

bool allocation_guard_armed() noexcept { return counters.depth > 0; }

} // namespace blokkily::test

// Every replaceable form, so that no allocation slips past through a form the
// runtime would otherwise supply.
void* operator new(std::size_t size) { return allocate(size, 0, true); }
void* operator new[](std::size_t size) { return allocate(size, 0, true); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size, 0, false);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size, 0, false);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment), true);
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment), true);
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate(size, static_cast<std::size_t>(alignment), false);
}
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    return allocate(size, static_cast<std::size_t>(alignment), false);
}

void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    release(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    release(pointer);
}
