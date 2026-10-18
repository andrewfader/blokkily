// The application's side of plugin isolation (sandbox.hpp): a PluginInstance
// whose plugin runs in a helper process. See sandbox_protocol.hpp for what
// crosses between them and sandbox_host.cpp for the helper.

#include "blokkily/plugins/sandbox.hpp"

#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "sandbox_protocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace blokkily {

namespace {
std::atomic<bool> waits_flag{false};
std::atomic<std::uint32_t> period_frames{0};
} // namespace

void set_sandbox_period(std::uint32_t frames) noexcept {
    period_frames.store(std::min<std::uint32_t>(frames, 4096), std::memory_order_release);
}
std::uint32_t sandbox_period() noexcept { return period_frames.load(std::memory_order_acquire); }

void set_sandbox_waits(bool waits) noexcept { waits_flag.store(waits, std::memory_order_release); }
bool sandbox_waits() noexcept { return waits_flag.load(std::memory_order_acquire); }

ScopedSandboxWait::ScopedSandboxWait() noexcept : previous_(sandbox_waits()) {
    set_sandbox_waits(true);
}
ScopedSandboxWait::~ScopedSandboxWait() { set_sandbox_waits(previous_); }

bool is_plugin_format(const std::string& format) {
    return format == "CLAP" || format == "VST3" || format == "LV2";
}

std::unique_ptr<PluginInstance> create_format_processor(const InstrumentSlot& slot,
                                                        std::string* error) {
    if (slot.format == "CLAP") return ClapPluginInstance::create(slot.path, slot.identifier, error);
    if (slot.format == "VST3") {
        std::size_t index = 0;
        const auto found = Vst3PluginInstance::scan(slot.path);
        for (std::size_t candidate = 0; candidate < found.size(); ++candidate)
            if (found[candidate].identifier == slot.identifier) {
                index = candidate;
                break;
            }
        return Vst3PluginInstance::create(slot.path, index, error);
    }
    if (slot.format == "LV2") return Vst3PluginInstance::create_lv2(slot.path, slot.identifier, error);
    if (error != nullptr) *error = "not a plugin format";
    return nullptr;
}

#if defined(_WIN32)

bool sandbox_supported() noexcept { return false; }

std::unique_ptr<PluginInstance> create_sandboxed(const InstrumentSlot&, const std::filesystem::path&,
                                                 std::string* error) {
    if (error != nullptr) *error = "plugin isolation is not available on this platform";
    return nullptr;
}

#else

// macOS has the pieces, but a helper there would also have to run Cocoa's
// event loop for plugin editors, which it does not yet: plugins load in the
// window there.
bool sandbox_supported() noexcept {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

namespace sandbox {

bool send_message(int fd, const std::vector<std::byte>& message) {
    const auto size = static_cast<std::uint32_t>(message.size());
    std::vector<std::byte> framed(sizeof(size) + message.size());
    std::memcpy(framed.data(), &size, sizeof(size));
    if (!message.empty()) std::memcpy(framed.data() + sizeof(size), message.data(), message.size());
    std::size_t sent = 0;
    while (sent < framed.size()) {
#if defined(MSG_NOSIGNAL)
        const auto wrote = ::send(fd, framed.data() + sent, framed.size() - sent, MSG_NOSIGNAL);
#else
        const auto wrote = ::send(fd, framed.data() + sent, framed.size() - sent, 0);
#endif
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return false;
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
}

namespace {
// Reads exactly `size` bytes before `deadline`.
bool read_exactly(int fd, std::byte* into, std::size_t size,
                  std::chrono::steady_clock::time_point deadline) {
    std::size_t got = 0;
    while (got < size) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd watch{fd, POLLIN, 0};
        const int ready = ::poll(&watch, 1, static_cast<int>(std::max<long long>(0, left.count())));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        const auto read = ::recv(fd, into + got, size - got, 0);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) return false;
        got += static_cast<std::size_t>(read);
    }
    return true;
}
} // namespace

std::optional<std::vector<std::byte>> receive_message(int fd, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    // Nothing waiting and no time to wait: say so without reading.
    if (timeout.count() == 0) {
        pollfd watch{fd, POLLIN, 0};
        if (::poll(&watch, 1, 0) <= 0) return std::nullopt;
    }
    std::uint32_t size = 0;
    if (!read_exactly(fd, reinterpret_cast<std::byte*>(&size), sizeof(size),
                      deadline + std::chrono::seconds(1)))
        return std::nullopt;
    std::vector<std::byte> message(size);
    // A message once begun is read to its end.
    if (size > 0 && !read_exactly(fd, message.data(), size, deadline + std::chrono::seconds(10)))
        return std::nullopt;
    return message;
}

Wake::~Wake() {
#if defined(__APPLE__)
    if (semaphore_ != nullptr) sem_close(semaphore_);
    if (owner_ && !name_.empty()) sem_unlink(name_.c_str());
#else
    if (owner_ && semaphore_ != nullptr) sem_destroy(semaphore_);
#endif
}

bool Wake::create(Shared& shared, const std::string& name) {
    owner_ = true;
    name_ = name;
#if defined(__APPLE__)
    (void)shared;
    semaphore_ = sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, 0);
    if (semaphore_ == SEM_FAILED) semaphore_ = nullptr;
#else
    semaphore_ = sem_init(&shared.wake, 1, 0) == 0 ? &shared.wake : nullptr;
#endif
    return semaphore_ != nullptr;
}

bool Wake::open(Shared& shared, const std::string& name) {
    name_ = name;
#if defined(__APPLE__)
    (void)shared;
    semaphore_ = sem_open(name.c_str(), 0);
    if (semaphore_ == SEM_FAILED) semaphore_ = nullptr;
#else
    semaphore_ = &shared.wake;
#endif
    return semaphore_ != nullptr;
}

void Wake::post() noexcept {
    if (semaphore_ != nullptr) (void)sem_post(semaphore_);
}

void Wake::wait() noexcept {
    if (semaphore_ == nullptr) return;
    while (sem_wait(semaphore_) != 0 && errno == EINTR) {
    }
}

} // namespace sandbox

namespace {

using namespace std::chrono_literals;
using sandbox::Job;
using sandbox::Notice;
using sandbox::Reader;
using sandbox::Reply;
using sandbox::Request;
using sandbox::Shared;
using sandbox::Writer;

// What an offline render waits for a block before calling the helper hung.
constexpr auto offline_deadline = 10s;
// How long a real-time helper may finish nothing it was handed before it is
// called hung and stopped.
constexpr auto stall_deadline = 3s;
// The output the helper has made, ahead of what has been played: every block
// in flight lands between what is being played and the delay after it.
constexpr std::size_t fifo_capacity = 8192;
static_assert(fifo_capacity >= 4096 + sandbox::max_frames);
constexpr std::size_t edit_capacity = 1024;

std::atomic<std::uint32_t> shared_counter{0};

class SandboxedInstance final : public PluginInstance {
public:
    ~SandboxedInstance() override { shut_down(); }

    bool start(const InstrumentSlot& slot, const std::filesystem::path& host, std::string* error) {
        const auto say = [error](std::string why) {
            if (error != nullptr) *error = std::move(why);
            return false;
        };
        const auto serial = shared_counter.fetch_add(1);
        shm_name_ = "/blk-" + std::to_string(::getpid()) + "-" + std::to_string(serial);
        const int memory = ::shm_open(shm_name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (memory < 0) return say("could not share memory with the plugin's process");
        shm_created_ = true;
        if (::ftruncate(memory, static_cast<off_t>(sizeof(Shared))) != 0) {
            ::close(memory);
            return say("could not size the memory shared with the plugin's process");
        }
        void* mapped = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0);
        ::close(memory);
        if (mapped == MAP_FAILED) return say("could not map the memory shared with the plugin");
        shared_ = new (mapped) Shared();
        shared_->magic = sandbox::shared_magic;
        wake_name_ = "/blkw-" + std::to_string(::getpid()) + "-" + std::to_string(serial);
        if (!wake_.create(*shared_, wake_name_)) return say("could not make the plugin's wake-up");

        int sockets[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
            return say("could not open a channel to the plugin's process");
        socket_ = sockets[0];
        (void)::fcntl(socket_, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
        const int on = 1;
        (void)::setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, sockets[1], 3);
        if (sockets[1] != 3) posix_spawn_file_actions_addclose(&actions, sockets[1]);
        const std::string program = host.string();
        std::string fd_text = "3";
        std::array<char*, 6> argv{const_cast<char*>(program.c_str()), const_cast<char*>("--sandbox"),
                                  shm_name_.data(), fd_text.data(), wake_name_.data(), nullptr};
        const int spawned = ::posix_spawn(&pid_, program.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(sockets[1]);
        if (spawned != 0) {
            pid_ = -1;
            return say("could not start " + program);
        }
        std::vector<std::byte> answer;
        if (!request(Request::hello, Writer{}, &answer, 10s))
            return say("the plugin's process did not start");
        // Both sides have it now; nothing else may open it.
        ::shm_unlink(shm_name_.c_str());
        shm_created_ = false;

        Writer create;
        create.text(slot.format).text(slot.path).text(slot.identifier);
        if (!request(Request::create, create, &answer, 60s))
            return say("the plugin did not load in its own process (it crashed or hung)");
        Reader reader(answer);
        if (reader.get<std::uint8_t>() == 0) return say(reader.text());
        format_ = reader.text();
        ports_.audio_inputs = reader.get<std::uint32_t>();
        ports_.note_input = reader.get<std::uint8_t>() != 0;
        ports_.sidechain_inputs = reader.get<std::uint32_t>();
        ports_.aux_outputs = std::min(reader.get<std::uint32_t>(), sandbox::max_aux);
        has_editor_ = reader.get<std::uint8_t>() != 0;
        accepts_running_ = reader.get<std::uint8_t>() != 0;
        if (!reader.ok) return say("the plugin's process answered with something unreadable");
        for (int side = 0; side < 2; ++side) fifo_[side].assign(fifo_capacity, 0.0F);
        aux_fifo_.assign(static_cast<std::size_t>(ports_.aux_outputs) * 2,
                         std::vector<float>(fifo_capacity, 0.0F));
        edits_.resize(edit_capacity);
        return true;
    }

    // --- Audio thread ------------------------------------------------------

    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override {
        const auto frames = std::min(audio.left.size(), audio.right.size());
        if (failed_.load(std::memory_order_acquire) || !active_ || frames > sandbox::max_frames) {
            silence(audio, frames);
            return;
        }
        collect();
        // A region of the output for this block, silent until the helper fills it.
        clear_fifo(write_, frames);
        const auto posted = shared_->posted.load(std::memory_order_relaxed);
        if (posted - collected_ < sandbox::ring) {
            const auto slot = posted % sandbox::ring;
            Job& job = shared_->jobs[slot];
            job.frames = static_cast<std::uint32_t>(frames);
            job.event_count = static_cast<std::uint32_t>(std::min<std::size_t>(events.size(), sandbox::max_events));
            std::copy_n(events.begin(), job.event_count, job.events);
            job.transport = transport_;
            std::copy_n(audio.left.begin(), frames, job.main[0]);
            std::copy_n(audio.right.begin(), frames, job.main[1]);
            job.has_sidechain = sidechain_.left.size() >= frames && sidechain_.right.size() >= frames;
            if (job.has_sidechain) {
                std::copy_n(sidechain_.left.begin(), frames, job.side[0]);
                std::copy_n(sidechain_.right.begin(), frames, job.side[1]);
            }
            job.edit_count = 0;
            job_at_[slot] = write_;
            shared_->posted.store(posted + 1, std::memory_order_release);
            wake_.post();
        } else {
            late_.fetch_add(1, std::memory_order_relaxed);
        }
        write_ += frames;
        if (sandbox_waits()) {
            // Offline: every block handed over is waited for, so the render
            // hears what a punctual helper would have made.
            const auto wanted = shared_->posted.load(std::memory_order_relaxed);
            const auto deadline = std::chrono::steady_clock::now() + offline_deadline;
            while (shared_->done.load(std::memory_order_acquire) < wanted) {
                if (std::chrono::steady_clock::now() > deadline) {
                    failed_.store(true, std::memory_order_release);
                    break;
                }
                std::this_thread::sleep_for(20us);
            }
            collect();
        }
        read_fifo(audio, frames);
        sidechain_ = {};
    }

    void set_transport(const TransportInfo& transport) noexcept override { transport_ = transport; }
    void set_sidechain(StereoBlock sidechain) noexcept override { sidechain_ = sidechain; }
    void set_aux_output(std::uint32_t output, StereoBlock destination) noexcept override {
        if (output >= 1 && output <= ports_.aux_outputs) aux_destination_[output - 1] = destination;
    }

    std::size_t take_parameter_edits(std::span<ParameterEdit> out) noexcept override {
        std::size_t taken = 0;
        while (taken < out.size() && edit_count_ > 0) {
            out[taken++] = edits_[edit_head_];
            edit_head_ = (edit_head_ + 1) % edit_capacity;
            --edit_count_;
        }
        return taken;
    }

    // --- Main thread -------------------------------------------------------

    bool activate(double sample_rate, std::uint32_t min_frames, std::uint32_t max_frames) override {
        if (failed() || max_frames > sandbox::max_frames || max_frames == 0) return false;
        drain();
        Writer args;
        args.put(sample_rate).put(min_frames).put(max_frames);
        std::vector<std::byte> answer;
        if (!request(Request::activate, args, &answer, 30s)) return false;
        Reader reader(answer);
        if (reader.get<std::uint8_t>() == 0) return false;
        inner_latency_.store(reader.get<std::uint32_t>(), std::memory_order_relaxed);
        tail_ = reader.get<std::uint64_t>();
        // One block behind - one device period, when that is longer - so
        // whatever the block, the helper has had a period to make it.
        delay_ = std::max(max_frames, sandbox_period());
        restart_fifo();
        active_ = true;
        return reader.ok;
    }

    void reset() override {
        if (failed()) return;
        drain();
        std::vector<std::byte> answer;
        (void)request(Request::reset, Writer{}, &answer, 10s);
        restart_fifo();
    }

    std::vector<std::byte> save_state() override {
        if (failed()) return cached_state_;
        std::vector<std::byte> answer;
        if (!request(Request::save_state, Writer{}, &answer, 10s)) return cached_state_;
        Reader reader(answer);
        auto state = reader.blob();
        if (reader.ok) cached_state_ = state;
        return cached_state_;
    }

    bool load_state(std::span<const std::byte> state) override {
        cached_state_.assign(state.begin(), state.end());
        if (failed()) return false;
        Writer args;
        args.blob(state);
        std::vector<std::byte> answer;
        if (!request(Request::load_state, args, &answer, 30s)) return false;
        return Reader(answer).get<std::uint8_t>() != 0;
    }

    std::string format() const override { return format_; }
    PluginPorts ports() const override { return ports_; }
    std::uint32_t latency_samples() const noexcept override {
        return inner_latency_.load(std::memory_order_relaxed) + delay_;
    }
    std::uint64_t tail_samples() const noexcept override { return tail_; }
    bool latency_changed() noexcept override {
        return latency_flag_.exchange(false, std::memory_order_acq_rel);
    }
    bool accepts_state_while_running() const noexcept override { return accepts_running_; }

    std::vector<ParameterInfo> parameters() const override {
        std::vector<ParameterInfo> list;
        std::vector<std::byte> answer;
        if (failed() || !const_cast<SandboxedInstance*>(this)->request(Request::parameters, Writer{}, &answer, 10s))
            return list;
        Reader reader(answer);
        const auto count = reader.get<std::uint32_t>();
        for (std::uint32_t index = 0; index < count && reader.ok; ++index) {
            ParameterInfo info{};
            info.id = reader.get<std::int32_t>();
            info.name = reader.text();
            info.min = reader.get<double>();
            info.max = reader.get<double>();
            info.default_value = reader.get<double>();
            info.automatable = reader.get<std::uint8_t>() != 0;
            if (reader.ok) list.push_back(std::move(info));
        }
        return list;
    }

    std::optional<double> parameter_value(std::int32_t id) const override {
        std::vector<std::byte> answer;
        Writer args;
        args.put(id);
        if (failed() || !const_cast<SandboxedInstance*>(this)->request(Request::parameter_value, args, &answer, 10s))
            return std::nullopt;
        Reader reader(answer);
        const bool has = reader.get<std::uint8_t>() != 0;
        const auto value = reader.get<double>();
        if (!has || !reader.ok) return std::nullopt;
        return value;
    }

    std::optional<PortableState> portable_state() override {
        std::vector<std::byte> answer;
        if (failed() || !request(Request::portable_state, Writer{}, &answer, 10s)) return std::nullopt;
        Reader reader(answer);
        if (reader.get<std::uint8_t>() == 0) return std::nullopt;
        PortableState state;
        state.device_id = reader.text();
        state.device_name = reader.text();
        state.device_vendor = reader.text();
        state.preset_extension = reader.text();
        state.preset = reader.blob();
        const auto count = reader.get<std::uint32_t>();
        for (std::uint32_t index = 0; index < count && reader.ok; ++index) {
            PortableState::Parameter parameter;
            parameter.id = reader.get<std::int32_t>();
            parameter.portable_id = reader.get<std::int64_t>();
            parameter.name = reader.text();
            parameter.min = reader.get<double>();
            parameter.max = reader.get<double>();
            parameter.value = reader.get<double>();
            state.parameters.push_back(std::move(parameter));
        }
        if (!reader.ok) return std::nullopt;
        return state;
    }

    void idle() override {
        if (pid_ < 0) return;
        // A request under way on another thread (a bounce's reset) owns the
        // channel; its notices are served there.
        std::unique_lock<std::recursive_mutex> channel(channel_, std::try_to_lock);
        if (!channel.owns_lock()) return;
        // What the helper said unasked: its editor's wishes, a new latency.
        while (!failed()) {
            auto message = sandbox::receive_message(socket_, 0ms);
            if (!message) break;
            Reader reader(*message);
            if (static_cast<Reply>(reader.get<std::uint8_t>()) == Reply::notice) handle_notice(reader);
        }
        // A helper that has died, or stopped finishing the blocks it is
        // handed, is turned off, and the track plays silence.
        int status = 0;
        if (::waitpid(pid_, &status, WNOHANG) == pid_) {
            pid_ = -1;
            failed_.store(true, std::memory_order_release);
            stop_helper();
            return;
        }
        if (failed()) {
            stop_helper();
            return;
        }
        const auto posted = shared_->posted.load(std::memory_order_relaxed);
        const auto done = shared_->done.load(std::memory_order_acquire);
        const auto now = std::chrono::steady_clock::now();
        if (done >= posted || done != stalled_at_) {
            stalled_at_ = done;
            stalled_since_ = now;
        } else if (now - stalled_since_ > stall_deadline) {
            fail();
        }
    }

    bool failed() const noexcept override { return failed_.load(std::memory_order_acquire); }

    // Editors: the plugin's window belongs to the helper. On X11 and Win32 a
    // window of another process can be embedded by its handle; elsewhere the
    // editor floats in a window of its own.
    bool has_editor() const override { return has_editor_; }
    bool supports_editor(WindowApi api, bool floating) const override {
        if (failed() || !has_editor_) return false;
        if (!floating && api != WindowApi::x11 && api != WindowApi::win32) return false;
        Writer args;
        args.put(static_cast<std::uint8_t>(api)).put(static_cast<std::uint8_t>(floating));
        std::vector<std::byte> answer;
        if (!const_cast<SandboxedInstance*>(this)->request(Request::supports_editor, args, &answer, 10s))
            return false;
        return Reader(answer).get<std::uint8_t>() != 0;
    }
    bool open_editor(const NativeParent* parent, EditorHost& host, EditorSize* size,
                     std::string* error) override {
        if (failed()) {
            if (error != nullptr) *error = "the plugin has stopped";
            return false;
        }
        Writer args;
        args.put(static_cast<std::uint8_t>(parent != nullptr));
        args.put(static_cast<std::uint8_t>(parent != nullptr ? parent->api : native_window_api()));
        args.put(static_cast<std::uint64_t>(parent != nullptr ? parent->handle : 0));
        args.put(parent != nullptr ? parent->scale : 1.0);
        editor_host_ = &host;
        std::vector<std::byte> answer;
        if (!request(Request::open_editor, args, &answer, 20s)) {
            editor_host_ = nullptr;
            if (error != nullptr) *error = "the plugin's process did not open its editor";
            return false;
        }
        Reader reader(answer);
        const bool opened = reader.get<std::uint8_t>() != 0;
        EditorSize got;
        got.width = reader.get<std::uint32_t>();
        got.height = reader.get<std::uint32_t>();
        got.resizable = reader.get<std::uint8_t>() != 0;
        got.logical = reader.get<std::uint8_t>() != 0;
        const auto why = reader.text();
        if (!opened) {
            editor_host_ = nullptr;
            if (error != nullptr) *error = why;
            return false;
        }
        if (size != nullptr) *size = got;
        editor_open_ = true;
        return true;
    }
    bool resize_editor(std::uint32_t& width, std::uint32_t& height) override {
        Writer args;
        args.put(width).put(height);
        std::vector<std::byte> answer;
        if (failed() || !request(Request::resize_editor, args, &answer, 10s)) return false;
        Reader reader(answer);
        const bool ok = reader.get<std::uint8_t>() != 0;
        width = reader.get<std::uint32_t>();
        height = reader.get<std::uint32_t>();
        return ok && reader.ok;
    }
    void set_editor_scale(double scale) override {
        Writer args;
        args.put(scale);
        std::vector<std::byte> answer;
        if (!failed()) (void)request(Request::set_editor_scale, args, &answer, 10s);
    }
    void close_editor() override {
        std::vector<std::byte> answer;
        if (!failed() && editor_open_) (void)request(Request::close_editor, Writer{}, &answer, 10s);
        editor_open_ = false;
        editor_host_ = nullptr;
    }
    bool editor_open() const override { return editor_open_ && !failed(); }

private:
    static void silence(StereoBlock audio, std::size_t frames) noexcept {
        std::fill_n(audio.left.begin(), frames, 0.0F);
        std::fill_n(audio.right.begin(), frames, 0.0F);
    }

    // Moves what the helper has finished into the output it is waiting in.
    void collect() noexcept {
        const auto done = shared_->done.load(std::memory_order_acquire);
        while (collected_ < done) {
            const auto slot = collected_ % sandbox::ring;
            const Job& job = shared_->jobs[slot];
            const auto at = job_at_[slot];
            // A block finished too late for its turn has already been played
            // as silence; what is left of it still lands.
            for (std::uint32_t frame = 0; frame < job.frames; ++frame) {
                const auto position = at + frame;
                if (position < read_) continue;
                const auto index = position % fifo_capacity;
                fifo_[0][index] = job.main[0][frame];
                fifo_[1][index] = job.main[1][frame];
                for (std::uint32_t output = 0; output < ports_.aux_outputs; ++output) {
                    aux_fifo_[output * 2][index] = job.aux[output][0][frame];
                    aux_fifo_[output * 2 + 1][index] = job.aux[output][1][frame];
                }
            }
            for (std::uint32_t edit = 0; edit < std::min(job.edit_count, sandbox::max_edits); ++edit) {
                if (edit_count_ == edit_capacity) break;
                edits_[(edit_head_ + edit_count_) % edit_capacity] = job.edits[edit];
                ++edit_count_;
            }
            ++collected_;
        }
    }

    void clear_fifo(std::uint64_t from, std::size_t frames) noexcept {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto index = (from + frame) % fifo_capacity;
            fifo_[0][index] = fifo_[1][index] = 0.0F;
            for (auto& channel : aux_fifo_) channel[index] = 0.0F;
        }
    }

    void read_fifo(StereoBlock audio, std::size_t frames) noexcept {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto index = (read_ + frame) % fifo_capacity;
            audio.left[frame] = fifo_[0][index];
            audio.right[frame] = fifo_[1][index];
        }
        // Every auxiliary output moves on, whether it is heard this block or not.
        for (std::uint32_t output = 0; output < ports_.aux_outputs; ++output) {
            auto& into = aux_destination_[output];
            const bool wanted = into.left.size() >= frames && into.right.size() >= frames;
            if (wanted)
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    const auto index = (read_ + frame) % fifo_capacity;
                    into.left[frame] = aux_fifo_[output * 2][index];
                    into.right[frame] = aux_fifo_[output * 2 + 1][index];
                }
            into = {};
        }
        read_ += frames;
    }

    void restart_fifo() {
        for (auto& side : fifo_) std::fill(side.begin(), side.end(), 0.0F);
        for (auto& channel : aux_fifo_) std::fill(channel.begin(), channel.end(), 0.0F);
        read_ = 0;
        write_ = delay_;
        collected_ = shared_->done.load(std::memory_order_acquire);
        edit_count_ = 0;
    }

    // Waits for every block in flight (main thread, with processing stopped).
    void drain() {
        const auto deadline = std::chrono::steady_clock::now() + offline_deadline;
        while (!failed() && shared_->done.load(std::memory_order_acquire) <
                                shared_->posted.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() > deadline) {
                fail();
                return;
            }
            std::this_thread::sleep_for(100us);
        }
    }

    // Sends one request and waits for its answer, serving the helper's
    // notices meanwhile. A helper that does not answer in time is stopped.
    bool request(Request what, const Writer& args, std::vector<std::byte>* answer,
                 std::chrono::milliseconds timeout) {
        const std::lock_guard<std::recursive_mutex> channel(channel_);
        if (socket_ < 0 || failed()) return false;
        std::vector<std::byte> message;
        message.push_back(static_cast<std::byte>(what));
        message.insert(message.end(), args.bytes().begin(), args.bytes().end());
        if (!sandbox::send_message(socket_, message)) {
            fail();
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0) break;
            auto reply = sandbox::receive_message(socket_, left);
            if (!reply) break;
            Reader reader(*reply);
            const auto kind = static_cast<Reply>(reader.get<std::uint8_t>());
            if (kind == Reply::notice) {
                handle_notice(reader);
                continue;
            }
            answer->assign(reply->begin() + 1, reply->end());
            return true;
        }
        fail();
        return false;
    }

    void handle_notice(Reader& reader) {
        const auto notice = static_cast<Notice>(reader.get<std::uint8_t>());
        switch (notice) {
        case Notice::editor_resize: {
            const auto width = reader.get<std::uint32_t>();
            const auto height = reader.get<std::uint32_t>();
            if (editor_host_ != nullptr) editor_host_->request_resize(width, height);
            break;
        }
        case Notice::editor_show:
            if (editor_host_ != nullptr) editor_host_->request_show();
            break;
        case Notice::editor_hide:
            if (editor_host_ != nullptr) editor_host_->request_hide();
            break;
        case Notice::editor_closed:
            editor_open_ = false;
            if (editor_host_ != nullptr) editor_host_->closed();
            break;
        case Notice::latency_changed:
            inner_latency_.store(reader.get<std::uint32_t>(), std::memory_order_relaxed);
            latency_flag_.store(true, std::memory_order_release);
            break;
        }
    }

    // The helper is broken: it is stopped, and the track plays silence.
    void fail() {
        failed_.store(true, std::memory_order_release);
        stop_helper();
    }

    void stop_helper() {
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            int status = 0;
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
        if (editor_open_ && editor_host_ != nullptr) {
            editor_open_ = false;
            editor_host_->closed();
        }
    }

    void shut_down() {
        // As an adapter in the window does: an editor still open is closed,
        // and its host is told, before the instance goes.
        if (editor_open_ && pid_ > 0 && !failed()) {
            std::vector<std::byte> answer;
            (void)request(Request::close_editor, Writer{}, &answer, 2s);
        }
        if (editor_open_ && editor_host_ != nullptr) {
            editor_open_ = false;
            editor_host_->closed();
        }
        editor_host_ = nullptr;
        if (pid_ > 0 && !failed()) {
            std::vector<std::byte> answer;
            (void)request(Request::close, Writer{}, &answer, 2s);
        }
        if (shared_ != nullptr) {
            shared_->stop.store(1, std::memory_order_release);
            wake_.post();
        }
        if (pid_ > 0) {
            // A moment to leave on its own, then it is made to.
            const auto deadline = std::chrono::steady_clock::now() + 1s;
            int status = 0;
            while (::waitpid(pid_, &status, WNOHANG) == 0) {
                if (std::chrono::steady_clock::now() > deadline) {
                    ::kill(pid_, SIGKILL);
                    ::waitpid(pid_, &status, 0);
                    break;
                }
                std::this_thread::sleep_for(5ms);
            }
            pid_ = -1;
        }
        if (socket_ >= 0) ::close(socket_);
        socket_ = -1;
        if (shm_created_) ::shm_unlink(shm_name_.c_str());
        if (shared_ != nullptr) ::munmap(shared_, sizeof(Shared));
        shared_ = nullptr;
    }

    pid_t pid_ = -1;
    int socket_ = -1;
    Shared* shared_ = nullptr;
    std::string shm_name_;
    bool shm_created_ = false;
    std::string wake_name_;
    sandbox::Wake wake_;
    std::atomic<bool> failed_{false};
    // The socket is the main thread's, and a bounce thread's while it resets.
    std::recursive_mutex channel_;

    // What the helper said about its plugin when it was made.
    std::string format_;
    PluginPorts ports_;
    bool has_editor_ = false;
    bool accepts_running_ = false;
    std::atomic<std::uint32_t> inner_latency_{0};
    std::atomic<bool> latency_flag_{false};
    std::uint64_t tail_ = 0;
    std::vector<std::byte> cached_state_;
    EditorHost* editor_host_ = nullptr;
    bool editor_open_ = false;
    std::uint64_t stalled_at_ = 0;
    std::chrono::steady_clock::time_point stalled_since_ = std::chrono::steady_clock::now();

    // The audio thread's: the output the helper has made, by absolute sample.
    bool active_ = false;
    std::uint32_t delay_ = 0;
    std::vector<float> fifo_[2];
    std::vector<std::vector<float>> aux_fifo_;
    std::uint64_t read_ = 0;
    std::uint64_t write_ = 0;
    std::uint64_t collected_ = 0;
    std::uint64_t job_at_[sandbox::ring] = {};
    std::atomic<std::uint64_t> late_{0};
    TransportInfo transport_{};
    StereoBlock sidechain_{};
    StereoBlock aux_destination_[sandbox::max_aux] = {};
    std::vector<ParameterEdit> edits_;
    std::size_t edit_head_ = 0;
    std::size_t edit_count_ = 0;
};

} // namespace

std::unique_ptr<PluginInstance> create_sandboxed(const InstrumentSlot& slot,
                                                 const std::filesystem::path& host,
                                                 std::string* error) {
    if (!is_plugin_format(slot.format)) {
        if (error != nullptr) *error = "only plugins run in a process of their own";
        return nullptr;
    }
    auto instance = std::make_unique<SandboxedInstance>();
    if (!instance->start(slot, host, error)) return nullptr;
    return instance;
}

#endif

} // namespace blokkily
