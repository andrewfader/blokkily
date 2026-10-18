// The helper's side of plugin isolation (sandbox.hpp): serves one plugin to
// the application that started it. Its main thread answers requests and runs
// the plugin's timers, descriptors and idle work; its audio thread processes
// the blocks the application hands over in shared memory.

#include "blokkily/plugins/plugin_run_loop.hpp"
#include "blokkily/plugins/sandbox.hpp"
#include "sandbox_protocol.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace blokkily {

#if defined(_WIN32)

int run_sandbox_host(int, char**) { return 2; }

#else

namespace {

using namespace std::chrono_literals;
using sandbox::Job;
using sandbox::Notice;
using sandbox::Reader;
using sandbox::Reply;
using sandbox::Request;
using sandbox::Shared;
using sandbox::Writer;
using Clock = std::chrono::steady_clock;

// Timers and descriptors on a real clock and poll(): the run loop a plugin's
// editor asks for, in a process with no toolkit of its own.
class PollRunLoop final : public PluginRunLoop {
public:
    std::uint64_t add_timer(std::uint32_t period_ms, TimerCallback callback) override {
        const auto id = next_++;
        const auto period = std::chrono::milliseconds(std::max<std::uint32_t>(1, period_ms));
        timers_[id] = {period, Clock::now() + period,
                       std::make_shared<TimerCallback>(std::move(callback))};
        return id;
    }
    bool remove_timer(std::uint64_t id) override { return timers_.erase(id) > 0; }
    bool add_fd(int fd, std::uint32_t events, FdCallback callback) override {
        if (fds_.count(fd) > 0) return false;
        fds_[fd] = {events, std::make_shared<FdCallback>(std::move(callback))};
        return true;
    }
    bool modify_fd(int fd, std::uint32_t events) override {
        const auto found = fds_.find(fd);
        if (found == fds_.end()) return false;
        found->second.events = events;
        return true;
    }
    bool remove_fd(int fd) override { return fds_.erase(fd) > 0; }

    // Milliseconds until the next timer is due, at most `longest`.
    int wait_for(int longest) const {
        auto wait = std::chrono::milliseconds(longest);
        const auto now = Clock::now();
        for (const auto& [id, timer] : timers_)
            wait = std::min(wait, std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::max(timer.due - now, Clock::duration::zero())));
        return static_cast<int>(wait.count());
    }
    void watch(std::vector<pollfd>& into) const {
        for (const auto& [fd, entry] : fds_) {
            short events = 0;
            if ((entry.events & fd_read) != 0U) events |= POLLIN;
            if ((entry.events & fd_write) != 0U) events |= POLLOUT;
            into.push_back({fd, events, 0});
        }
    }
    void dispatch(const pollfd& ready) {
        const auto found = fds_.find(ready.fd);
        if (found == fds_.end() || ready.revents == 0) return;
        std::uint32_t events = 0;
        if ((ready.revents & POLLIN) != 0) events |= fd_read;
        if ((ready.revents & POLLOUT) != 0) events |= fd_write;
        if ((ready.revents & (POLLERR | POLLHUP)) != 0) events |= fd_error;
        const auto callback = found->second.callback;
        (*callback)(ready.fd, events);
    }
    void fire_due() {
        const auto now = Clock::now();
        std::vector<std::uint64_t> due;
        for (const auto& [id, timer] : timers_)
            if (timer.due <= now) due.push_back(id);
        for (const auto id : due) {
            const auto found = timers_.find(id);
            if (found == timers_.end()) continue;
            found->second.due = now + found->second.period;
            const auto callback = found->second.callback;
            (*callback)();
        }
    }

private:
    struct Timer {
        std::chrono::milliseconds period;
        Clock::time_point due;
        std::shared_ptr<TimerCallback> callback;
    };
    struct Watch {
        std::uint32_t events;
        std::shared_ptr<FdCallback> callback;
    };
    std::map<std::uint64_t, Timer> timers_;
    std::map<int, Watch> fds_;
    std::uint64_t next_ = 1;
};

// What the plugin's editor asks of its window, told to the application.
class ForwardingHost final : public EditorHost {
public:
    explicit ForwardingHost(int socket) : socket_(socket) {}
    void request_resize(std::uint32_t width, std::uint32_t height) override {
        notice(Notice::editor_resize, [&](Writer& out) { out.put(width).put(height); });
    }
    void request_show() override { notice(Notice::editor_show, [](Writer&) {}); }
    void request_hide() override { notice(Notice::editor_hide, [](Writer&) {}); }
    void closed() override { notice(Notice::editor_closed, [](Writer&) {}); }

    template <typename Fill>
    void notice(Notice what, Fill fill) {
        Writer out;
        out.put(static_cast<std::uint8_t>(Reply::notice)).put(static_cast<std::uint8_t>(what));
        fill(out);
        (void)sandbox::send_message(socket_, out.bytes());
    }

private:
    int socket_;
};

class Host {
public:
    Host(Shared& shared, int socket) : shared_(shared), socket_(socket), forward_(socket) {}
    ~Host() { stop_audio(); }

    bool open_wake(const std::string& name) { return wake_.open(shared_, name); }

    int run() {
        ScopedPluginRunLoop scoped(loop_);
        auto next_idle = Clock::now();
        while (!closing_) {
            std::vector<pollfd> watched{{socket_, POLLIN, 0}};
            loop_.watch(watched);
            const int ready = ::poll(watched.data(), watched.size(), loop_.wait_for(10));
            if (ready < 0 && errno != EINTR) break;
            if (ready > 0 && watched[0].revents != 0) {
                auto message = sandbox::receive_message(socket_, 1000ms);
                // The application has gone (or closed the channel): so do we.
                if (!message) break;
                serve(*message);
            }
            for (std::size_t index = 1; index < watched.size(); ++index) loop_.dispatch(watched[index]);
            loop_.fire_due();
            if (inner_ && Clock::now() >= next_idle) {
                inner_->idle();
                if (inner_->latency_changed())
                    forward_.notice(Notice::latency_changed,
                                    [&](Writer& out) { out.put(inner_->latency_samples()); });
                next_idle = Clock::now() + 10ms;
            }
        }
        stop_audio();
        if (inner_ && inner_->editor_open()) inner_->close_editor();
        inner_.reset();
        return 0;
    }

private:
    void answer(const Writer& body) {
        Writer out;
        out.put(static_cast<std::uint8_t>(Reply::answer));
        std::vector<std::byte> bytes = out.bytes();
        bytes.insert(bytes.end(), body.bytes().begin(), body.bytes().end());
        (void)sandbox::send_message(socket_, bytes);
    }

    void serve(const std::vector<std::byte>& message) {
        Reader in(message);
        const auto what = static_cast<Request>(in.get<std::uint8_t>());
        Writer out;
        switch (what) {
        case Request::hello:
            break;
        case Request::create: {
            InstrumentSlot slot;
            slot.format = in.text();
            slot.path = in.text();
            slot.identifier = in.text();
            std::string error;
            inner_ = create_format_processor(slot, &error);
            if (!inner_) {
                out.put(std::uint8_t{0}).text(error.empty() ? "the plugin did not load" : error);
                break;
            }
            const auto ports = inner_->ports();
            out.put(std::uint8_t{1}).text(inner_->format());
            out.put(ports.audio_inputs).put(static_cast<std::uint8_t>(ports.note_input));
            out.put(ports.sidechain_inputs).put(std::min(ports.aux_outputs, sandbox::max_aux));
            out.put(static_cast<std::uint8_t>(inner_->has_editor()));
            out.put(static_cast<std::uint8_t>(inner_->accepts_state_while_running()));
            aux_outputs_ = std::min(ports.aux_outputs, sandbox::max_aux);
            start_audio();
            break;
        }
        case Request::activate: {
            const auto rate = in.get<double>();
            const auto low = in.get<std::uint32_t>();
            const auto high = in.get<std::uint32_t>();
            const bool ok = inner_ && inner_->activate(rate, low, high);
            out.put(static_cast<std::uint8_t>(ok));
            out.put(ok ? inner_->latency_samples() : 0U);
            out.put(ok ? inner_->tail_samples() : std::uint64_t{0});
            break;
        }
        case Request::reset:
            if (inner_) inner_->reset();
            break;
        case Request::save_state:
            out.blob(inner_ ? inner_->save_state() : std::vector<std::byte>{});
            break;
        case Request::load_state: {
            const auto state = in.blob();
            out.put(static_cast<std::uint8_t>(inner_ && in.ok && inner_->load_state(state)));
            break;
        }
        case Request::parameters: {
            const auto list = inner_ ? inner_->parameters() : std::vector<ParameterInfo>{};
            out.put(static_cast<std::uint32_t>(list.size()));
            for (const auto& info : list)
                out.put(info.id).text(info.name).put(info.min).put(info.max).put(info.default_value)
                    .put(static_cast<std::uint8_t>(info.automatable));
            break;
        }
        case Request::parameter_value: {
            const auto value = inner_ ? inner_->parameter_value(in.get<std::int32_t>()) : std::nullopt;
            out.put(static_cast<std::uint8_t>(value.has_value())).put(value.value_or(0.0));
            break;
        }
        case Request::portable_state: {
            const auto state = inner_ ? inner_->portable_state() : std::nullopt;
            out.put(static_cast<std::uint8_t>(state.has_value()));
            if (state) {
                out.text(state->device_id).text(state->device_name).text(state->device_vendor)
                    .text(state->preset_extension).blob(state->preset);
                out.put(static_cast<std::uint32_t>(state->parameters.size()));
                for (const auto& parameter : state->parameters)
                    out.put(parameter.id).put(parameter.portable_id).text(parameter.name)
                        .put(parameter.min).put(parameter.max).put(parameter.value);
            }
            break;
        }
        case Request::has_editor:
            out.put(static_cast<std::uint8_t>(inner_ && inner_->has_editor()));
            break;
        case Request::supports_editor: {
            const auto api = static_cast<WindowApi>(in.get<std::uint8_t>());
            const bool floating = in.get<std::uint8_t>() != 0;
            out.put(static_cast<std::uint8_t>(inner_ && inner_->supports_editor(api, floating)));
            break;
        }
        case Request::open_editor: {
            const bool embedded = in.get<std::uint8_t>() != 0;
            NativeParent parent{static_cast<WindowApi>(in.get<std::uint8_t>()),
                                static_cast<std::uintptr_t>(in.get<std::uint64_t>()), 1.0};
            parent.scale = in.get<double>();
            EditorSize size;
            std::string error;
            const bool ok = inner_ && inner_->open_editor(embedded ? &parent : nullptr, forward_, &size, &error);
            out.put(static_cast<std::uint8_t>(ok)).put(size.width).put(size.height)
                .put(static_cast<std::uint8_t>(size.resizable)).put(static_cast<std::uint8_t>(size.logical))
                .text(error);
            break;
        }
        case Request::resize_editor: {
            auto width = in.get<std::uint32_t>();
            auto height = in.get<std::uint32_t>();
            const bool ok = inner_ && inner_->resize_editor(width, height);
            out.put(static_cast<std::uint8_t>(ok)).put(width).put(height);
            break;
        }
        case Request::set_editor_scale:
            if (inner_) inner_->set_editor_scale(in.get<double>());
            break;
        case Request::close_editor:
            if (inner_) inner_->close_editor();
            break;
        case Request::idle:
            if (inner_) inner_->idle();
            break;
        case Request::close:
            closing_ = true;
            break;
        }
        answer(out);
    }

    void start_audio() {
        if (audio_.joinable()) return;
        audio_ = std::thread([this] { serve_blocks(); });
    }

    void stop_audio() {
        if (!audio_.joinable()) return;
        shared_.stop.store(1, std::memory_order_release);
        wake_.post();
        audio_.join();
    }

    // The audio thread: every block handed over, in order, as it arrives. It
    // asks for real-time scheduling, as the application's audio thread has;
    // a system that refuses leaves it as it is.
    void serve_blocks() {
        sched_param priority{};
        priority.sched_priority = std::min(70, sched_get_priority_max(SCHED_FIFO));
        (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);
        while (true) {
            wake_.wait();
            if (shared_.stop.load(std::memory_order_acquire) != 0U) return;
            auto done = shared_.done.load(std::memory_order_relaxed);
            while (done < shared_.posted.load(std::memory_order_acquire)) {
                process(shared_.jobs[done % sandbox::ring]);
                shared_.done.store(++done, std::memory_order_release);
            }
        }
    }

    void process(Job& job) noexcept {
        const auto frames = std::min(job.frames, sandbox::max_frames);
        inner_->set_transport(job.transport);
        if (job.has_sidechain != 0U)
            inner_->set_sidechain({std::span{job.side[0], frames}, std::span{job.side[1], frames}});
        else
            inner_->set_sidechain({});
        for (std::uint32_t output = 0; output < aux_outputs_; ++output) {
            std::fill_n(job.aux[output][0], frames, 0.0F);
            std::fill_n(job.aux[output][1], frames, 0.0F);
            inner_->set_aux_output(output + 1, {std::span{job.aux[output][0], frames},
                                                std::span{job.aux[output][1], frames}});
        }
        inner_->process({std::span{job.main[0], frames}, std::span{job.main[1], frames}},
                        std::span{job.events, std::min(job.event_count, sandbox::max_events)});
        job.edit_count = static_cast<std::uint32_t>(
            inner_->take_parameter_edits(std::span{job.edits, sandbox::max_edits}));
    }

    Shared& shared_;
    int socket_;
    sandbox::Wake wake_;
    ForwardingHost forward_;
    PollRunLoop loop_;
    std::unique_ptr<PluginInstance> inner_;
    std::uint32_t aux_outputs_ = 0;
    std::thread audio_;
    bool closing_ = false;
};

} // namespace

int run_sandbox_host(int argc, char** argv) {
    if (argc != 5 || std::string(argv[1]) != "--sandbox") return 2;
    // A plugin that writes to a channel the application has closed must not
    // end the helper before it can be told so.
    std::signal(SIGPIPE, SIG_IGN);
    const int memory = ::shm_open(argv[2], O_RDWR, 0);
    if (memory < 0) return 3;
    void* mapped = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0);
    ::close(memory);
    if (mapped == MAP_FAILED) return 3;
    auto& shared = *static_cast<Shared*>(mapped);
    if (shared.magic != sandbox::shared_magic) return 3;
    const int socket = std::atoi(argv[3]);
    int status = 0;
    {
        Host host(shared, socket);
        if (!host.open_wake(argv[4])) return 4;
        status = host.run();
    }
    ::munmap(mapped, sizeof(Shared));
    ::close(socket);
    return status;
}

#endif

} // namespace blokkily
