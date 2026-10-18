#pragma once

// What the application and a plugin helper share (sandbox.hpp): the shared
// memory blocks of audio cross in, the requests and answers on the socket
// between them, and how one wakes the other. POSIX only.

#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#if !defined(_WIN32)
#include <semaphore.h>
#endif

namespace blokkily::sandbox {

// The largest block a helper processes (the engine's are 512 at most), the
// most events and edits one block carries, the auxiliary outputs it renders,
// and how many blocks can be in flight at once: a device period of 4096
// frames cut into the engine's 128-frame modulation chunks.
inline constexpr std::uint32_t max_frames = 1024;
inline constexpr std::uint32_t max_events = 1024;
inline constexpr std::uint32_t max_edits = 256;
inline constexpr std::uint32_t max_aux = 8;
inline constexpr std::uint32_t ring = 32;

static_assert(std::is_trivially_copyable_v<PluginEvent>);
static_assert(std::is_trivially_copyable_v<TransportInfo>);
static_assert(std::is_trivially_copyable_v<ParameterEdit>);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

// One block: what the application hands over, and what the helper hands back
// in the same place.
struct Job {
    std::uint32_t frames = 0;
    std::uint32_t event_count = 0;
    std::uint32_t edit_count = 0;
    std::uint32_t has_sidechain = 0;
    TransportInfo transport{};
    PluginEvent events[max_events];
    ParameterEdit edits[max_edits];
    // The input on entry, the output on return.
    float main[2][max_frames];
    float side[2][max_frames];
    float aux[max_aux][2][max_frames];
};

struct Shared {
    std::uint32_t magic = 0;
    // Blocks the application has handed over, and blocks the helper has
    // finished. Job n lives in jobs[n % ring].
    std::atomic<std::uint64_t> posted{0};
    std::atomic<std::uint64_t> done{0};
    // Set (then the helper woken) to end the helper's audio thread.
    std::atomic<std::uint32_t> stop{0};
#if defined(__linux__)
    sem_t wake;
#endif
    Job jobs[ring];
};
inline constexpr std::uint32_t shared_magic = 0x424C4B53; // "BLKS"

// Requests, answered in order; notices are what the helper says unasked.
enum class Request : std::uint8_t {
    hello = 1, create, activate, reset, save_state, load_state, parameters, parameter_value,
    portable_state, has_editor, supports_editor, open_editor, resize_editor, set_editor_scale,
    close_editor, idle, close,
};
enum class Reply : std::uint8_t { answer = 1, notice = 2 };
enum class Notice : std::uint8_t {
    editor_resize = 1, editor_show, editor_hide, editor_closed, latency_changed,
};

// A message being written: fixed-size values as their bytes, strings and
// blobs with their length first.
class Writer {
public:
    template <typename T>
    Writer& put(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        data_.insert(data_.end(), bytes, bytes + sizeof(T));
        return *this;
    }
    Writer& text(const std::string& value) {
        put(static_cast<std::uint32_t>(value.size()));
        const auto* bytes = reinterpret_cast<const std::byte*>(value.data());
        data_.insert(data_.end(), bytes, bytes + value.size());
        return *this;
    }
    Writer& blob(std::span<const std::byte> value) {
        put(static_cast<std::uint32_t>(value.size()));
        data_.insert(data_.end(), value.begin(), value.end());
        return *this;
    }
    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return data_; }

private:
    std::vector<std::byte> data_;
};

// A message being read. A read past its end leaves `ok` false and yields
// zeros, so a short message is noticed once, at the end.
class Reader {
public:
    explicit Reader(std::span<const std::byte> data) : data_(data) {}
    template <typename T>
    T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (at_ + sizeof(T) > data_.size()) {
            ok = false;
            return value;
        }
        std::memcpy(&value, data_.data() + at_, sizeof(T));
        at_ += sizeof(T);
        return value;
    }
    std::string text() {
        const auto size = get<std::uint32_t>();
        if (!ok || at_ + size > data_.size()) {
            ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(data_.data() + at_), size);
        at_ += size;
        return value;
    }
    std::vector<std::byte> blob() {
        const auto size = get<std::uint32_t>();
        if (!ok || at_ + size > data_.size()) {
            ok = false;
            return {};
        }
        std::vector<std::byte> value(data_.begin() + static_cast<std::ptrdiff_t>(at_),
                                     data_.begin() + static_cast<std::ptrdiff_t>(at_ + size));
        at_ += size;
        return value;
    }
    bool ok = true;

private:
    std::span<const std::byte> data_;
    std::size_t at_ = 0;
};

// Whole messages on a stream socket: a 32-bit length, then the bytes.
// Sending never raises SIGPIPE; a peer that has gone makes it return false.
bool send_message(int fd, const std::vector<std::byte>& message);
// Waits up to `timeout` for one message; nothing on a timeout or a closed or
// broken socket.
std::optional<std::vector<std::byte>> receive_message(int fd, std::chrono::milliseconds timeout);

// Wakes the helper's audio thread: a process-shared semaphore (in the shared
// memory on Linux, a named one on macOS).
class Wake {
public:
    Wake() = default;
    ~Wake();
    Wake(const Wake&) = delete;
    Wake& operator=(const Wake&) = delete;
    // The application makes it; the helper opens the one named `name`.
    bool create(Shared& shared, const std::string& name);
    bool open(Shared& shared, const std::string& name);
    // Async-signal-safe and non-blocking: fine on the audio thread.
    void post() noexcept;
    void wait() noexcept;

private:
#if !defined(_WIN32)
    sem_t* semaphore_ = nullptr;
#endif
    std::string name_;
    bool owner_ = false;
};

} // namespace blokkily::sandbox
