#include "blokkily/instruments/soundfont_synth.hpp"

#include <fluidsynth.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace blokkily {

namespace {
// A renderer whose output stays under this (-120 dB) for a quarter of a
// second, with no voice left, has finished its reverb and chorus tail.
constexpr float silence = 1e-6F;
constexpr double quiet_seconds = 0.25;

// FluidSynth registers every audio driver it was built with the first time a
// settings object is made, and each new settings object then opens them to
// list their devices (PortAudio and ALSA probe every card). The synth here
// only ever renders into the host's buffers, so it registers none: that was a
// tenth of a second per SoundFont track, every time one was created.
// A FluidSynth built with OpenMP sizes the team that renders a bank's voices
// from the thread count in force where the bank was loaded: every core,
// unless told otherwise. Each 64-frame block then woke a worker on every core
// and waited for all of them, on the audio thread, which cost twice the CPU
// of rendering alone and left the workers spinning. A bank is loaded with one
// thread instead, so it renders on the thread that asks for audio. The
// runtime is FluidSynth's own, found among the loaded libraries, so this
// changes the setting it reads; without one there is nothing to change.
class OneOpenMpThread {
public:
    OneOpenMpThread() {
        if (set_ == nullptr || get_ == nullptr) return;
        previous_ = get_();
        set_(1);
    }
    ~OneOpenMpThread() {
        if (previous_ > 0) set_(previous_);
    }
    OneOpenMpThread(const OneOpenMpThread&) = delete;
    OneOpenMpThread& operator=(const OneOpenMpThread&) = delete;

private:
    using Set = void (*)(int);
    using Get = int (*)();
    static void* find(const char* name) {
#if defined(_WIN32)
        for (const auto* runtime : {L"libgomp-1.dll", L"libomp.dll", L"vcomp140.dll"})
            if (auto* module = GetModuleHandleW(runtime))
                return reinterpret_cast<void*>(GetProcAddress(module, name));
        return nullptr;
#else
        return dlsym(RTLD_DEFAULT, name);
#endif
    }
    static inline const Set set_ = reinterpret_cast<Set>(find("omp_set_num_threads"));
    static inline const Get get_ = reinterpret_cast<Get>(find("omp_get_max_threads"));
    int previous_ = 0;
};

// Whether a voice of this bank can ever feed FluidSynth's reverb or chorus.
// Every renderer runs both effects on every block it plays, whatever reaches
// them, and on a bank that sends them nothing that was seven tenths of the
// track's processing spent on silence. A voice sends to one only through a
// send generator (15 chorus, 16 reverb) or a modulator aimed at one, both
// listed in the bank's pdta chunk, or through the default CC 91/93
// modulators, which a controller message raises (see process()). Anything
// unreadable, or a modulator linked onward to one that may be a send, is
// taken to send, so the effects stay on.
bool bank_may_send_to_effects(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    const auto read_u32 = [&](std::uint32_t& value) {
        std::array<unsigned char, 4> bytes{};
        if (!file.read(reinterpret_cast<char*>(bytes.data()), 4)) return false;
        value = bytes[0] | (bytes[1] << 8U) | (bytes[2] << 16U) |
                (static_cast<std::uint32_t>(bytes[3]) << 24U);
        return true;
    };
    std::array<char, 4> id{};
    std::uint32_t size = 0;
    if (!file.read(id.data(), 4) || std::memcmp(id.data(), "RIFF", 4) != 0 || !read_u32(size) ||
        !file.read(id.data(), 4) || std::memcmp(id.data(), "sfbk", 4) != 0)
        return true;
    std::vector<unsigned char> pdta;
    while (pdta.empty() && file.read(id.data(), 4) && read_u32(size)) {
        const auto next = static_cast<std::streamoff>(file.tellg()) + size + (size & 1U);
        std::array<char, 4> kind{};
        if (std::memcmp(id.data(), "LIST", 4) == 0 && size >= 4 && file.read(kind.data(), 4) &&
            std::memcmp(kind.data(), "pdta", 4) == 0) {
            pdta.resize(size - 4);
            if (!file.read(reinterpret_cast<char*>(pdta.data()),
                           static_cast<std::streamsize>(pdta.size())))
                return true;
        }
        file.seekg(next);
    }
    if (pdta.empty()) return true;
    const auto u16 = [&](std::size_t at) {
        return static_cast<std::uint16_t>(pdta[at] | (pdta[at + 1] << 8U));
    };
    const auto is_send = [](std::uint16_t generator) { return generator == 15 || generator == 16; };
    bool generators = false;
    bool modulators = false;
    for (std::size_t at = 0; at + 8 <= pdta.size();) {
        const std::string sub(reinterpret_cast<const char*>(&pdta[at]), 4);
        const std::size_t length = u16(at + 4) | (static_cast<std::size_t>(u16(at + 6)) << 16U);
        const std::size_t begin = at + 8;
        if (begin + length > pdta.size()) return true;
        if (sub == "pgen" || sub == "igen") {
            generators = true;
            for (std::size_t record = begin; record + 4 <= begin + length; record += 4)
                if (is_send(u16(record)) && static_cast<std::int16_t>(u16(record + 2)) > 0)
                    return true;
        } else if (sub == "pmod" || sub == "imod") {
            modulators = true;
            for (std::size_t record = begin; record + 10 <= begin + length; record += 10) {
                const auto destination = u16(record + 2);
                const auto amount = static_cast<std::int16_t>(u16(record + 4));
                if (amount != 0 && (is_send(destination) || (destination & 0x8000U) != 0))
                    return true;
            }
        }
        at = begin + length + (length & 1U);
    }
    return !(generators && modulators);
}

// A controller that can raise a voice's reverb or chorus send: the default
// modulators' CC 91 and 93, or a parameter number (NRPN) setting a generator.
bool controller_may_send_to_effects(int controller) {
    return controller == 91 || controller == 93 || controller == 6 || controller == 38 ||
           (controller >= 96 && controller <= 99);
}

void register_no_audio_drivers() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char* none[] = {nullptr};
        (void)fluid_audio_driver_register(none);
    });
}
} // namespace

SoundFontSynth::SoundFontSynth() {
    register_no_audio_drivers();
    renderers_.resize(renderer_count);
    scratch_left_.resize(512);
    scratch_right_.resize(512);
    forget_notes();
    (void)rebuild(sample_rate_);
}

SoundFontSynth::~SoundFontSynth() = default;

bool SoundFontSynth::rebuild(double sample_rate) {
    sample_rate_ = sample_rate;
    activated_ = false;
    fresh_ = false;
    // The synths are destroyed before the settings they were made from.
    for (auto& renderer : renderers_) renderer.synth.reset();
    if (!settings_) {
        settings_ = SettingsPtr(new_fluid_settings(), delete_fluid_settings);
        if (!settings_) return false;
        fluid_settings_setint(settings_.get(), "synth.threadsafe-api", 0);
        // FluidSynth's default gain (0.2) leaves a General MIDI bank some
        // fourteen decibels under the other instruments. 0.5 brings it within
        // a few decibels of them while a full-velocity chord on a typical bank
        // stays clear of full scale.
        fluid_settings_setnum(settings_.get(), "synth.gain", 0.5);
    }
    fluid_settings_setnum(settings_.get(), "synth.sample-rate", sample_rate);
    for (auto& renderer : renderers_) {
        renderer.synth = SynthPtr(new_fluid_synth(settings_.get()), delete_fluid_synth);
        if (!renderer.synth) return false;
        renderer.rendered = 0;
        renderer.quiet = 0;
        renderer.running = false;
    }
    effects_on_ = true;   // FluidSynth makes a synth with both effects on
    fresh_ = true;
    return true;
}

void SoundFontSynth::set_effects(bool on) noexcept {
    for (auto& renderer : renderers_) {
        if (!renderer.synth) continue;
        (void)fluid_synth_reverb_on(renderer.synth.get(), -1, on ? 1 : 0);
        (void)fluid_synth_chorus_on(renderer.synth.get(), -1, on ? 1 : 0);
    }
    effects_on_ = on;
}

void SoundFontSynth::forget_notes() noexcept {
    for (auto& renderer : renderers_) {
        renderer.channel_key.fill(-1);
        renderer.channel_expressive.fill(false);
        renderer.channel_expression.fill(0.0);
    }
    shared_holders_.fill(0);
    wheel_ = 8192;
}

bool SoundFontSynth::load_into(Renderer& renderer, const std::filesystem::path& path) {
    if (!renderer.synth) return false;
    if (fluid_synth_get_sfont(renderer.synth.get(), 0) != nullptr && path_ == path) return true;
    const auto utf8 = path.string();
    const OneOpenMpThread one_thread;
    return fluid_synth_sfload(renderer.synth.get(), utf8.c_str(), 1) != FLUID_FAILED;
}

bool SoundFontSynth::load(const std::filesystem::path& path) {
    if (renderers_.empty() || !renderers_.front().synth ||
        !std::filesystem::is_regular_file(path))
        return false;
    // Asking for the bank that is already open only changes the preset. A
    // General MIDI bank is hundreds of megabytes, and reloading it every time
    // the session is saved, restored, or recompiled would stall the interface
    // for a change that touches nothing the bank holds.
    if (path_ == path && fluid_synth_get_sfont(renderers_.front().synth.get(), 0) != nullptr)
        return select_preset(bank_, program_);
    // A renderer may hold the previous bank; each is given the new one.
    for (auto& renderer : renderers_) {
        if (!renderer.synth) return false;
        while (auto* old = fluid_synth_get_sfont(renderer.synth.get(), 0))
            if (fluid_synth_sfunload(renderer.synth.get(), fluid_sfont_get_id(old), 1) ==
                FLUID_FAILED)
                return false;
    }
    path_.clear();
    // The first renderer reads the bank, which leaves its samples in
    // FluidSynth's cache; the others only parse its presets again, each into
    // a synth of its own, so they do that side by side.
    if (!load_into(renderers_.front(), path)) return false;
    std::vector<char> loaded(renderers_.size(), 0);
    loaded.front() = 1;
    {
        std::vector<std::thread> loaders;
        loaders.reserve(renderers_.size() - 1);
        for (std::size_t index = 1; index < renderers_.size(); ++index)
            loaders.emplace_back([this, &loaded, &path, index] {
                loaded[index] = load_into(renderers_[index], path) ? 1 : 0;
            });
        for (auto& loader : loaders) loader.join();
    }
    if (std::find(loaded.begin(), loaded.end(), 0) != loaded.end()) return false;
    path_ = path;
    set_effects(bank_may_send_to_effects(path));
    // The bank a project asked for may not be in the file it was pointed at —
    // a percussion bank is missing from most single-instrument SoundFonts. The
    // file is still loaded, so it falls back to the first General MIDI program
    // rather than being reported as a failure and going silent.
    if (select_preset(bank_, program_)) return true;
    if (select_preset(0, 0)) return true;
    // Many game SoundFonts hold nothing at 0:0 (every preset of one in bank
    // 1, say), and falling back to it alone refused every preset they hold.
    // Such a file opens on the lowest bank and program it does hold.
    auto* soundfont = fluid_synth_get_sfont(renderers_.front().synth.get(), 0);
    if (soundfont == nullptr) return false;
    std::optional<std::pair<int, int>> first;
    fluid_sfont_iteration_start(soundfont);
    while (auto* preset = fluid_sfont_iteration_next(soundfont)) {
        const std::pair<int, int> found{fluid_preset_get_banknum(preset), fluid_preset_get_num(preset)};
        if (!first || found < *first) first = found;
    }
    return first && select_preset(first->first, first->second);
}

bool SoundFontSynth::select_in(Renderer& renderer, int bank, int program) {
    auto* synth = renderer.synth.get();
    auto* soundfont = synth != nullptr ? fluid_synth_get_sfont(synth, 0) : nullptr;
    if (soundfont == nullptr) return false;
    // The file must hold exactly that bank and program. Asked for one it does
    // not, FluidSynth quietly plays another, so the producer would hear a
    // sound other than the one chosen; the request is refused instead, and
    // the channels keep what they had.
    if (fluid_sfont_get_preset(soundfont, bank, program) == nullptr) return false;
    if (fluid_synth_sfont_select(synth, 0, fluid_sfont_get_id(soundfont)) == FLUID_FAILED)
        return false;
    // Every channel carries the same preset: a microtonal chord is one
    // instrument spread over several channels, not several instruments.
    for (int channel = 0; channel < channels; ++channel) {
        if (fluid_synth_bank_select(synth, channel, bank) == FLUID_FAILED) return false;
        if (fluid_synth_program_change(synth, channel, program) == FLUID_FAILED) return false;
    }
    // A bank and program the file does not actually hold leaves the channel
    // with nothing to sound, and FluidSynth reports that as success. Asking the
    // synth what it ended up with is the only way to know the preset is real,
    // and a silent instrument is exactly what a producer cannot debug.
    return fluid_synth_get_channel_preset(synth, shared_channel) != nullptr;
}

bool SoundFontSynth::select_preset(int bank, int program) {
    if (renderers_.empty() || path_.empty()) return false;
    // The first renderer decides whether the file holds the preset; then
    // every renderer plays it, so a note sounds the same whichever plays it.
    if (!select_in(renderers_.front(), bank, program)) return false;
    for (std::size_t index = 1; index < renderers_.size(); ++index)
        if (!select_in(renderers_[index], bank, program)) return false;
    bank_ = bank;
    program_ = program;
    return true;
}

bool SoundFontSynth::activate(double sample_rate, std::uint32_t, std::uint32_t max_frames) {
    // The renderers mix through scratch buffers made here, never while
    // processing. A block longer than them is rendered in pieces, so they
    // are capped: a host may announce a very large maximum block.
    const auto scratch = std::clamp<std::size_t>(
        std::max<std::size_t>(scratch_left_.size(), max_frames), 512, 4096);
    scratch_left_.resize(scratch);
    scratch_right_.resize(scratch);
    // Re-activating at the rate the synth already runs at leaves it exactly as
    // it was, so recompiling an arrangement does not rebuild the synth and
    // re-read its bank underneath a note that is still sounding.
    if (activated_ && sample_rate == sample_rate_) return true;
    // Renderers built at this rate and never played (a synth that was just
    // made and loaded) are already as a rebuild would leave them.
    if (!fresh_ || sample_rate != sample_rate_) {
        const auto previous_path = path_;
        if (!rebuild(sample_rate)) return false;
        forget_notes();
        path_.clear();
        if (!previous_path.empty() && !load(previous_path)) return false;
    }
    // Retuning a SoundFont is a tuning table's job rather than the bend wheel's:
    // a wheel is scaled by whatever sensitivity the instrument was written with,
    // while a tuning says the pitch of a key in cents and means it. Each voice
    // channel gets a table of its own so that two notes of a microtonal chord
    // can be tuned differently at the same time.
    std::array<double, 128> twelve_tone{};
    for (std::size_t key = 0; key < twelve_tone.size(); ++key)
        twelve_tone[key] = static_cast<double>(key) * 100.0;
    for (auto& renderer : renderers_)
        for (int channel = first_retuned_channel; channel < channels; ++channel) {
            if (fluid_synth_activate_key_tuning(renderer.synth.get(), 0, channel, "blokkily",
                                                twelve_tone.data(), 1) == FLUID_FAILED)
                return false;
            if (fluid_synth_activate_tuning(renderer.synth.get(), channel, 0, channel, 1) ==
                FLUID_FAILED)
                return false;
        }
    activated_ = true;
    return true;
}

int SoundFontSynth::claim_channel(Renderer& renderer, int key) noexcept {
    auto& held = renderer.channel_key;
    for (int channel = first_retuned_channel; channel < channels; ++channel)
        if (held[static_cast<std::size_t>(channel)] < 0) {
            held[static_cast<std::size_t>(channel)] = key;
            return channel;
        }
    // Every channel is busy, so the oldest retuned voice gives way rather than
    // the new note being dropped: it is released here, since its own note-off
    // will find no channel holding its key and would leave it ringing.
    const int displaced = held[first_retuned_channel];
    if (displaced >= 0) fluid_synth_noteoff(renderer.synth.get(), first_retuned_channel, displaced);
    held[first_retuned_channel] = key;
    return first_retuned_channel;
}

int SoundFontSynth::expressive_bend(const Renderer& renderer, int channel) const noexcept {
    const double semitones = (wheel_ - 8192) / 8192.0 * 2.0 +
                             renderer.channel_expression[static_cast<std::size_t>(channel)];
    return std::clamp(static_cast<int>(std::lround(8192.0 + semitones * 8192.0 /
                                                              expressive_bend_range)),
                      0, 16383);
}

std::size_t SoundFontSynth::choose_renderer() noexcept {
    // One already running whose block begins here plays the note on time.
    for (std::size_t index = 0; index < renderers_.size(); ++index) {
        const auto& renderer = renderers_[index];
        if (renderer.running && renderer.rendered % block_frames == 0) return index;
    }
    // Otherwise an idle one starts its blocks here.
    for (std::size_t index = 0; index < renderers_.size(); ++index) {
        auto& renderer = renderers_[index];
        if (renderer.running) continue;
        renderer.running = true;
        renderer.rendered = 0;
        renderer.quiet = 0;
        return index;
    }
    // Every renderer is sounding, each in another phase: the one whose next
    // block comes soonest plays the note, as little late as can be.
    std::size_t best = 0;
    std::uint64_t soonest = block_frames;
    for (std::size_t index = 0; index < renderers_.size(); ++index) {
        const auto wait = block_frames - renderers_[index].rendered % block_frames;
        if (wait < soonest) {
            soonest = wait;
            best = index;
        }
    }
    return best;
}

void SoundFontSynth::render(StereoBlock audio, std::size_t offset, std::size_t frames) noexcept {
    const auto quiet_frames = static_cast<std::uint64_t>(sample_rate_ * quiet_seconds);
    for (auto& renderer : renderers_) {
        std::size_t done = 0;
        while (done < frames && renderer.running) {
            auto piece = std::min(frames - done, scratch_left_.size());
            // Silent, with no voice left: it stops where its block ends, so
            // the next note it plays can start a block of its own.
            if (renderer.quiet >= quiet_frames &&
                fluid_synth_get_active_voice_count(renderer.synth.get()) == 0) {
                const auto to_block_end =
                    (block_frames - renderer.rendered % block_frames) % block_frames;
                if (to_block_end == 0) {
                    renderer.running = false;
                    break;
                }
                piece = std::min<std::size_t>(piece, to_block_end);
            }
            fluid_synth_write_float(renderer.synth.get(), static_cast<int>(piece),
                                    scratch_left_.data(), 0, 1, scratch_right_.data(), 0, 1);
            float peak = 0.0F;
            for (std::size_t frame = 0; frame < piece; ++frame) {
                audio.left[offset + done + frame] += scratch_left_[frame];
                audio.right[offset + done + frame] += scratch_right_[frame];
                peak = std::max({peak, std::abs(scratch_left_[frame]),
                                 std::abs(scratch_right_[frame])});
            }
            renderer.quiet = peak < silence ? renderer.quiet + piece : 0;
            renderer.rendered += piece;
            done += piece;
        }
    }
}

void SoundFontSynth::process(StereoBlock audio,
                             std::span<const PluginEvent> events) noexcept {
    if (renderers_.empty() || !renderers_.front().synth || scratch_left_.empty() ||
        audio.left.size() != audio.right.size())
        return;
    fresh_ = false;
    std::fill(audio.left.begin(), audio.left.end(), 0.0F);
    std::fill(audio.right.begin(), audio.right.end(), 0.0F);
    std::size_t cursor = 0;
    const auto render_to = [&](std::size_t end) {
        if (end > cursor) render(audio, cursor, end - cursor);
        cursor = std::max(cursor, end);
    };
    for (const auto& event : events) {
        const auto offset = std::min<std::size_t>(event.sample_offset, audio.left.size());
        render_to(offset);
        const int key = event.key_or_parameter;
        if (event.type == PluginEvent::Type::note_on) {
            if (key < 0 || key > 127) continue;
            const int velocity = std::clamp(static_cast<int>(event.value * 127.0), 0, 127);
            const auto index = choose_renderer();
            auto& renderer = renderers_[index];
            auto* synth = renderer.synth.get();
            if (event.cents == 0.0 && event.expression == 0) {
                // A key struck again lets go of where it was still held, as
                // one synth's channel would.
                auto& holders = shared_holders_[static_cast<std::size_t>(key)];
                for (std::size_t other = 0; other < renderers_.size(); ++other)
                    if (other != index && (holders & (1U << other)) != 0)
                        fluid_synth_noteoff(renderers_[other].synth.get(), shared_channel, key);
                holders = 1U << index;
                fluid_synth_noteon(synth, shared_channel, key, velocity);
            } else {
                // A retuned note, and a note with per-note expression (an
                // MPE note), each get a channel of their own.
                const int channel = claim_channel(renderer, key);
                const auto slot = static_cast<std::size_t>(channel);
                const bool expressive = event.expression != 0;
                if (expressive || renderer.channel_expressive[slot]) {
                    fluid_synth_pitch_wheel_sens(synth, channel,
                                                 expressive ? expressive_bend_range : 2);
                    renderer.channel_expressive[slot] = expressive;
                    renderer.channel_expression[slot] = 0.0;
                    fluid_synth_pitch_bend(synth, channel,
                                           expressive ? expressive_bend(renderer, channel)
                                                      : wheel_);
                    fluid_synth_channel_pressure(synth, channel, 0);
                }
                // The channel's tuning table is told what this key is worth in
                // cents, and the note is then played as any other note.
                int tuned_key = key;
                double tuned = key * 100.0 + event.cents;
                fluid_synth_tune_notes(synth, 0, channel, 1, &tuned_key, &tuned, 1);
                fluid_synth_noteon(synth, channel, key, velocity);
            }
        } else if (event.type == PluginEvent::Type::note_off) {
            if (key < 0 || key > 127) continue;
            bool retuned = false;
            for (auto& renderer : renderers_)
                for (int channel = first_retuned_channel; channel < channels && !retuned; ++channel)
                    if (renderer.channel_key[static_cast<std::size_t>(channel)] == key) {
                        renderer.channel_key[static_cast<std::size_t>(channel)] = -1;
                        fluid_synth_noteoff(renderer.synth.get(), channel, key);
                        retuned = true;
                    }
            if (retuned) continue;
            auto& holders = shared_holders_[static_cast<std::size_t>(key)];
            for (std::size_t index = 0; index < renderers_.size(); ++index)
                if ((holders & (1U << index)) != 0)
                    fluid_synth_noteoff(renderers_[index].synth.get(), shared_channel, key);
            holders = 0;
        } else if (event.type == PluginEvent::Type::note_expression) {
            // Per-note expression on the note's own channel: pitch as its
            // bend, timbre as CC 74, pressure as channel pressure. A note on
            // the shared channel has none to take it.
            const int level =
                std::clamp(static_cast<int>(std::lround(event.value * 127.0)), 0, 127);
            for (auto& renderer : renderers_)
                for (int channel = first_retuned_channel; channel < channels; ++channel) {
                    const auto slot = static_cast<std::size_t>(channel);
                    if (renderer.channel_key[slot] != key || !renderer.channel_expressive[slot])
                        continue;
                    auto* synth = renderer.synth.get();
                    if (event.expression == note_dimension::pitch) {
                        renderer.channel_expression[slot] = event.value;
                        fluid_synth_pitch_bend(synth, channel, expressive_bend(renderer, channel));
                    } else if (event.expression == note_dimension::timbre) {
                        fluid_synth_cc(synth, channel, 74, level);
                    } else if (event.expression == note_dimension::pressure) {
                        fluid_synth_channel_pressure(synth, channel, level);
                    }
                }
        } else if (event.type == PluginEvent::Type::midi_raw) {
            // The track is one instrument, so the channel a controller was
            // played on names nothing here. A channel-wide message reaches
            // every channel of every renderer: the shared one and each
            // retuned voice's, so a keyboard on channel 2 bends the notes it
            // plays and a microtonal chord bends with the wheel.
            const auto raw = static_cast<std::uint32_t>(event.key_or_parameter);
            const auto kind = static_cast<std::uint8_t>(raw & 0xF0U);
            const auto first = static_cast<int>((raw >> 8) & 0x7FU);
            const auto second = static_cast<int>((raw >> 16) & 0x7FU);
            if (kind == 0xE0) wheel_ = first | (second << 7);
            if (kind == 0xB0 && !effects_on_ && controller_may_send_to_effects(first))
                set_effects(true);
            for (auto& renderer : renderers_) {
                auto* synth = renderer.synth.get();
                if (kind == 0xA0) {
                    // Poly pressure belongs to the channel holding that key.
                    int channel = shared_channel;
                    for (int candidate = first_retuned_channel; candidate < channels; ++candidate)
                        if (renderer.channel_key[static_cast<std::size_t>(candidate)] == first)
                            channel = candidate;
                    fluid_synth_key_pressure(synth, channel, first, second);
                    continue;
                }
                for (int channel = 0; channel < channels; ++channel) {
                    if (kind == 0xE0)
                        fluid_synth_pitch_bend(
                            synth, channel,
                            renderer.channel_expressive[static_cast<std::size_t>(channel)]
                                ? expressive_bend(renderer, channel)
                                : wheel_);
                    else if (kind == 0xB0) fluid_synth_cc(synth, channel, first, second);
                    else if (kind == 0xD0) fluid_synth_channel_pressure(synth, channel, first);
                }
            }
        }
    }
    render_to(audio.left.size());
}

void SoundFontSynth::reset() {
    // This hook is called only with processing stopped (offline export).
    // all_sounds_off/system_reset leave FluidSynth's buffered output and
    // oscillator/effect phase behind. Recreate the renderers at their current
    // rate and reload the same preset, including the per-voice tuning tables,
    // so a fresh pass cannot contain earlier audio or depend on block phase.
    // File loading and allocation stay here, never in process().
    activated_ = false;
    fresh_ = false;
    (void)activate(sample_rate_, 1, 1);
}

std::vector<std::byte> SoundFontSynth::save_state() {
    const std::string value = path_.string() + '\n' + std::to_string(bank_) + '\n' +
                              std::to_string(program_);
    std::vector<std::byte> bytes(value.size());
    std::memcpy(bytes.data(), value.data(), value.size());
    return bytes;
}

bool SoundFontSynth::load_state(std::span<const std::byte> state) {
    const std::string value(reinterpret_cast<const char*>(state.data()), state.size());
    const auto first = value.find('\n');
    const auto second = first == std::string::npos ? first : value.find('\n', first + 1);
    if (first == std::string::npos || second == std::string::npos) return false;
    try {
        const int bank = std::stoi(value.substr(first + 1, second - first - 1));
        const int program = std::stoi(value.substr(second + 1));
        return load(value.substr(0, first)) && select_preset(bank, program);
    } catch (...) {
        return false;
    }
}

} // namespace blokkily
