#include "blokkily/instruments/sampler.hpp"

#include "blokkily/project/paths.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace blokkily {

// A zone ready to play: its file's samples and every constant a voice needs,
// computed on the control thread.
struct SamplerInstrument::Zone {
    const float* left = nullptr;
    const float* right = nullptr;   // == left for a mono file
    std::uint64_t frames = 0;       // in the file
    std::uint64_t start = 0, end = 0;
    LoopMode loop = LoopMode::off;
    std::uint64_t loop_start = 0, loop_end = 0;
    double file_rate = 48000.0;
    double root = 60.0;             // root key + fine / 100
    SamplerEnvelope envelope;
    double gain = 1.0;              // linear, from gain_db
    float left_pan = 1.0F, right_pan = 1.0F;
    int low_velocity = 1, high_velocity = 127;
    int choke_group = 0;
    bool track_pitch = true;
    bool one_shot = false;
};

// An immutable, decoded program. Owns the assets its zones point into.
struct SamplerInstrument::Kit {
    std::uint64_t epoch = 0;
    int polyphony = 32;
    std::vector<AudioAssetPtr> assets;
    std::vector<Zone> zones;
    // For each key, the zones whose key range holds it, in program order.
    std::array<std::vector<std::uint16_t>, 128> by_key;
};

namespace {

double time_scale(double value) noexcept { return std::exp2(2.0 * (2.0 * value - 1.0)); }

double gain_from(double value) noexcept {
    if (value <= 0.0) return 0.0;
    return std::pow(10.0, (-60.0 + 72.0 * value) / 20.0);
}

constexpr double default_gain = 60.0 / 72.0;

} // namespace

SamplerInstrument::SamplerInstrument(AudioAssetCache* cache)
    : own_cache_(cache ? nullptr : std::make_unique<AudioAssetCache>()),
      cache_(cache ? cache : own_cache_.get()) {
    base_.fill(0.0);
    base_[sampler_parameter::gain] = default_gain;
    base_[sampler_parameter::tune] = 0.5;
    base_[sampler_parameter::attack] = 0.5;
    base_[sampler_parameter::decay] = 0.5;
    base_[sampler_parameter::sustain] = 0.5;
    base_[sampler_parameter::release] = 0.5;
    base_[sampler_parameter::start] = 0.0;
    modulation_.fill(0.0);
}

SamplerInstrument::~SamplerInstrument() {
    // The callback is stopped by now; nothing reads the kits any more.
    (void)queued_.exchange(nullptr);
}

void SamplerInstrument::set_base_directory(std::filesystem::path directory) {
    base_directory_ = std::move(directory);
}

AudioAssetPtr SamplerInstrument::asset_for(const SamplerZone& zone, std::string* error) {
    if (zone.sample.empty()) {
        if (error) *error = "no sample";
        return nullptr;
    }
    const auto file = resolve_project_path(std::filesystem::path(zone.sample), base_directory_);
    return cache_->load(file, 0.0, std::nullopt, error);
}

std::optional<SamplerZone> SamplerInstrument::make_zone(const std::filesystem::path& file,
                                                        std::string* error) {
    SamplerZone zone;
    zone.sample = to_project_relative(file, base_directory_).generic_string();
    const auto asset = asset_for(zone, error);
    if (!asset) return std::nullopt;
    if (asset->root_key && *asset->root_key >= 0 && *asset->root_key <= 127)
        zone.root_key = *asset->root_key;
    if (asset->loop && asset->loop->end > asset->loop->start && asset->loop->end <= asset->frames) {
        zone.loop = LoopMode::forward;
        zone.loop_start = asset->loop->start;
        zone.loop_end = asset->loop->end;
    }
    return zone;
}

void SamplerInstrument::collect() {
    const Kit* queued = queued_.load(std::memory_order_acquire);
    const std::uint64_t oldest = oldest_live_epoch_.load(std::memory_order_acquire);
    std::erase_if(kits_, [&](const std::unique_ptr<Kit>& kit) {
        return kit.get() != queued && kit->epoch < oldest;
    });
}

bool SamplerInstrument::set_program(const SamplerProgram& program, std::string* error) {
    if (!validate_sampler(program, error)) return false;
    collect();

    auto kit = std::make_unique<Kit>();
    kit->epoch = next_epoch_++;
    kit->polyphony = program.polyphony;
    std::vector<std::string> missing;
    double tail_seconds = 0.0;
    for (std::size_t index = 0; index < program.zones.size(); ++index) {
        const auto& source = program.zones[index];
        std::string reason;
        auto asset = asset_for(source, &reason);
        const std::uint64_t frames = asset ? asset->frames : 0;
        const std::uint64_t end = source.end == 0 ? frames : std::min(source.end, frames);
        if (asset && (asset->left.size() < frames || source.start >= end)) {
            reason = "the zone's region lies outside the file";
            asset.reset();
        }
        if (!asset) {
            missing.push_back("zone " + std::to_string(index) + ": " + source.sample + ": " +
                              reason);
            continue;
        }
        Zone zone;
        zone.left = asset->left.data();
        zone.right = asset->right.size() >= frames ? asset->right.data() : zone.left;
        zone.frames = frames;
        zone.start = source.start;
        zone.end = end;
        if (source.loop != LoopMode::off && source.loop_start >= zone.start &&
            source.loop_end <= end && source.loop_end > source.loop_start + 1) {
            zone.loop = source.loop;
            zone.loop_start = source.loop_start;
            zone.loop_end = source.loop_end;
        }
        zone.file_rate = static_cast<double>(asset->rate);
        zone.root = source.root_key + source.fine_cents / 100.0;
        zone.envelope = source.envelope;
        zone.gain = std::pow(10.0, source.gain_db / 20.0);
        zone.left_pan = static_cast<float>(source.pan > 0.0 ? 1.0 - source.pan : 1.0);
        zone.right_pan = static_cast<float>(source.pan < 0.0 ? 1.0 + source.pan : 1.0);
        zone.low_velocity = source.low_velocity;
        zone.high_velocity = source.high_velocity;
        zone.choke_group = source.choke_group;
        zone.track_pitch = source.track_pitch;
        zone.one_shot = source.one_shot;

        // The longest the zone can go on after its last event: a release at
        // the slowest the parameter allows, or a one-shot's whole region
        // played two octaves down.
        const double region_seconds = static_cast<double>(end - zone.start) / zone.file_rate;
        tail_seconds = std::max(tail_seconds, source.envelope.release_s * 4.0 +
                                                  (source.one_shot ? region_seconds * 4.0 : 0.0));

        const auto zone_index = static_cast<std::uint16_t>(kit->zones.size());
        kit->zones.push_back(zone);
        for (int key = source.low_key; key <= source.high_key; ++key)
            kit->by_key[static_cast<std::size_t>(key)].push_back(zone_index);
        kit->assets.push_back(std::move(asset));
    }

    Kit* published = kit.get();
    kits_.push_back(std::move(kit));
    // A kit the callback never took is unreachable once replaced: free it now.
    if (Kit* skipped = queued_.exchange(published, std::memory_order_acq_rel)) {
        std::erase_if(kits_, [skipped](const std::unique_ptr<Kit>& held) {
            return held.get() == skipped;
        });
    }
    program_ = program;
    missing_ = std::move(missing);
    tail_frames_.store(static_cast<std::uint64_t>(std::ceil(tail_seconds * engine_rate_)),
                       std::memory_order_relaxed);
    if (error) error->clear();
    return true;
}

bool SamplerInstrument::activate(double sample_rate, std::uint32_t, std::uint32_t) {
    if (!(sample_rate > 0.0)) return false;
    if (sample_rate != engine_rate_) {
        // Voices were started with steps for the old rate.
        for (auto& voice : voices_) voice.active = false;
        for (auto& voice : fading_) voice.active = false;
        tail_frames_.store(static_cast<std::uint64_t>(std::ceil(
                               static_cast<double>(tail_frames_.load()) / engine_rate_ *
                               sample_rate)),
                           std::memory_order_relaxed);
        engine_rate_ = sample_rate;
    }
    return true;
}

std::uint64_t SamplerInstrument::tail_samples() const noexcept {
    return tail_frames_.load(std::memory_order_relaxed);
}

std::vector<ParameterInfo> SamplerInstrument::parameters() const {
    return {
        {sampler_parameter::gain, "Gain", 0.0, 1.0, default_gain, true},
        {sampler_parameter::tune, "Tune", 0.0, 1.0, 0.5, true},
        {sampler_parameter::attack, "Attack", 0.0, 1.0, 0.5, true},
        {sampler_parameter::decay, "Decay", 0.0, 1.0, 0.5, true},
        {sampler_parameter::sustain, "Sustain", 0.0, 1.0, 0.5, true},
        {sampler_parameter::release, "Release", 0.0, 1.0, 0.5, true},
        {sampler_parameter::start, "Start", 0.0, 1.0, 0.0, true},
    };
}

void SamplerInstrument::reset() {
    for (auto& voice : voices_) voice.active = false;
    for (auto& voice : fading_) voice.active = false;
    bend_semitones_ = 0.0;
    pedal_down_ = false;
}

std::vector<std::byte> SamplerInstrument::save_state() {
    collect();
    SamplerProgram stored = program_;
    for (auto& zone : stored.zones)
        zone.sample = to_project_relative(std::filesystem::path(zone.sample), base_directory_)
                          .generic_string();
    return serialize_sampler(stored);
}

bool SamplerInstrument::load_state(std::span<const std::byte> state) {
    const auto program = parse_sampler(state);
    return program && set_program(*program);
}

double SamplerInstrument::parameter(std::int32_t id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    return std::clamp(base_[index] + modulation_[index], 0.0, 1.0);
}

void SamplerInstrument::fast_release(Voice& voice) noexcept {
    voice.stage = Voice::Stage::release;
    voice.release_step = voice.level / static_cast<double>(sampler_fast_release_frames);
    if (voice.release_step <= 0.0) voice.active = false;
}

void SamplerInstrument::start_note(const PluginEvent& event) noexcept {
    if (!live_ || event.key_or_parameter < 0 || event.key_or_parameter > 127) return;
    const int key = event.key_or_parameter;
    const int velocity =
        std::clamp(static_cast<int>(std::lround(event.value * 127.0)), 1, 127);
    const double velocity_gain = sampler_velocity_gain(event.value);
    const auto& candidates = live_->by_key[static_cast<std::size_t>(key)];

    // A new note in a choke group cuts whatever else is sounding in it.
    for (const auto index : candidates) {
        const Zone& zone = live_->zones[index];
        if (zone.choke_group == 0 || velocity < zone.low_velocity ||
            velocity > zone.high_velocity)
            continue;
        for (auto& voice : voices_)
            if (voice.active && voice.zone->choke_group == zone.choke_group) fast_release(voice);
    }

    const double attack_scale = time_scale(parameter(sampler_parameter::attack));
    const double decay_scale = time_scale(parameter(sampler_parameter::decay));
    const double release_scale = time_scale(parameter(sampler_parameter::release));
    const double sustain_scale = 2.0 * parameter(sampler_parameter::sustain);
    const double start_offset = parameter(sampler_parameter::start);

    for (const auto index : candidates) {
        const Zone& zone = live_->zones[index];
        if (velocity < zone.low_velocity || velocity > zone.high_velocity) continue;
        const double region = static_cast<double>(zone.end - zone.start);
        const double position =
            static_cast<double>(zone.start) + std::floor(start_offset * region);
        if (position >= static_cast<double>(zone.end)) continue;

        // Find a slot, stealing within the program's polyphony.
        int sounding = 0;
        Voice* slot = nullptr;
        for (auto& voice : voices_) {
            if (voice.active) ++sounding;
            else if (!slot) slot = &voice;
        }
        const int polyphony = std::clamp(program_polyphony_, 1, sampler_max_voices);
        if (sounding >= polyphony || !slot) {
            Voice* victim = nullptr;
            for (auto& voice : voices_) {
                if (!voice.active) continue;
                const bool releasing = voice.stage == Voice::Stage::release;
                const bool victim_releasing =
                    victim && victim->stage == Voice::Stage::release;
                if (!victim || (releasing && !victim_releasing) ||
                    (releasing == victim_releasing && voice.age < victim->age))
                    victim = &voice;
            }
            if (!victim) return;
            // The stolen voice fades out in a spare slot so the new one can
            // start now without a click.
            Voice* fade = &fading_[0];
            for (auto& spare : fading_) {
                if (!spare.active) {
                    fade = &spare;
                    break;
                }
                if (spare.level < fade->level) fade = &spare;
            }
            *fade = *victim;
            fast_release(*fade);
            victim->active = false;
            slot = victim;
        }

        Voice voice;
        voice.zone = &zone;
        voice.epoch = live_->epoch;
        voice.age = ++voice_clock_;
        voice.key = key;
        voice.active = true;
        voice.position = position;
        const double rate_ratio = zone.file_rate / engine_rate_;
        voice.step = zone.track_pitch
                         ? rate_ratio * std::exp2((key + event.cents / 100.0 - zone.root) / 12.0)
                         : rate_ratio;
        voice.may_loop = zone.loop != LoopMode::off &&
                         voice.position < static_cast<double>(zone.loop_end);
        const double gain = velocity_gain * zone.gain;
        voice.left_gain = static_cast<float>(gain * zone.left_pan);
        voice.right_gain = static_cast<float>(gain * zone.right_pan);

        const double attack = zone.envelope.attack_s * attack_scale * engine_rate_;
        const double decay = zone.envelope.decay_s * decay_scale * engine_rate_;
        voice.sustain_level = std::clamp(zone.envelope.sustain * sustain_scale, 0.0, 1.0);
        voice.release_frames =
            std::max(1.0, zone.envelope.release_s * release_scale * engine_rate_);
        voice.decay_step = decay >= 1.0 ? (1.0 - voice.sustain_level) / decay : 1.0;
        if (attack >= 1.0) {
            voice.stage = Voice::Stage::attack;
            voice.level = 0.0;
            voice.attack_step = 1.0 / attack;
        } else {
            voice.stage = Voice::Stage::decay;
            voice.level = 1.0;
        }
        *slot = voice;
    }
}

void SamplerInstrument::release_key(int key) noexcept {
    for (auto& voice : voices_) {
        if (!voice.active || voice.key != key || voice.zone->one_shot ||
            voice.stage == Voice::Stage::release)
            continue;
        // The pedal holds the note until it comes up.
        if (pedal_down_) {
            voice.pedal_held = true;
            continue;
        }
        voice.stage = Voice::Stage::release;
        voice.release_step = voice.level / voice.release_frames;
        if (voice.release_step <= 0.0) voice.active = false;
    }
}

bool SamplerInstrument::render_voice(Voice& voice, StereoBlock audio, std::size_t from,
                                     std::size_t to, double gain, double tune) noexcept {
    const Zone& zone = *voice.zone;
    const double step = voice.step * tune;
    const auto file_frames = static_cast<std::int64_t>(zone.frames);
    const auto end = static_cast<std::int64_t>(zone.end);
    const auto loop_start = static_cast<std::int64_t>(zone.loop_start);
    const auto loop_end = static_cast<std::int64_t>(zone.loop_end);
    const auto loop_length = loop_end - loop_start;
    const bool forward_loop = voice.may_loop && zone.loop == LoopMode::forward;
    const bool ping_pong = voice.may_loop && zone.loop == LoopMode::ping_pong;

    // Where tap `index` of the interpolator reads, following the loop the
    // voice is in. Taps just outside the region read the file itself, so the
    // first and last frames interpolate against their true neighbours; only
    // the file's own ends read as silence. Once a forward loop has wrapped,
    // the frame before loop_start is the loop's last frame, not the file's
    // lead-in: both sides of the seam read the loop.
    const auto tap = [&](std::int64_t index, const float* channel) noexcept -> float {
        if (forward_loop && (index >= loop_end || (voice.in_loop && index < loop_start)))
            index = loop_start + ((index - loop_start) % loop_length + loop_length) % loop_length;
        else if (ping_pong) {
            if (index > loop_end - 1) index = 2 * (loop_end - 1) - index;
            else if (voice.in_loop && index < loop_start) index = 2 * loop_start - index;
        }
        if (index < 0 || index >= file_frames) return 0.0F;
        return channel[index];
    };
    const auto hermite = [&](const float* channel, std::int64_t i, double f) noexcept {
        const double xm1 = tap(i - 1, channel);
        const double x0 = tap(i, channel);
        const double x1 = tap(i + 1, channel);
        const double x2 = tap(i + 2, channel);
        const double c1 = 0.5 * (x1 - xm1);
        const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    };

    const bool stereo = zone.right != zone.left;
    for (std::size_t frame = from; frame < to; ++frame) {
        const double whole = std::floor(voice.position);
        const auto index = static_cast<std::int64_t>(whole);
        const double fraction = voice.position - whole;
        const double left = hermite(zone.left, index, fraction);
        const double right = stereo ? hermite(zone.right, index, fraction) : left;
        const double level = voice.level * gain;
        audio.left[frame] += static_cast<float>(left * level * voice.left_gain);
        audio.right[frame] += static_cast<float>(right * level * voice.right_gain);

        switch (voice.stage) {
        case Voice::Stage::attack:
            voice.level += voice.attack_step;
            if (voice.level >= 1.0) {
                voice.level = 1.0;
                voice.stage = Voice::Stage::decay;
            }
            break;
        case Voice::Stage::decay:
            voice.level -= voice.decay_step;
            if (voice.level <= voice.sustain_level) {
                voice.level = voice.sustain_level;
                voice.stage = Voice::Stage::sustain;
            }
            break;
        case Voice::Stage::sustain:
            break;
        case Voice::Stage::release:
            voice.level -= voice.release_step;
            if (voice.level <= 0.0) return false;
            break;
        }

        if (ping_pong && voice.backward) {
            voice.position -= step;
            if (voice.position < static_cast<double>(loop_start)) {
                voice.position = 2.0 * static_cast<double>(loop_start) - voice.position;
                voice.backward = false;
            }
        } else {
            voice.position += step;
            if (forward_loop && voice.position >= static_cast<double>(loop_end)) {
                voice.position = static_cast<double>(loop_start) +
                                 std::fmod(voice.position - static_cast<double>(loop_start),
                                           static_cast<double>(loop_length));
                voice.in_loop = true;
            } else if (ping_pong && voice.position > static_cast<double>(loop_end - 1)) {
                voice.position = 2.0 * static_cast<double>(loop_end - 1) - voice.position;
                voice.backward = true;
                voice.in_loop = true;
            } else if (voice.position >= static_cast<double>(end)) {
                return false;
            }
        }
    }
    return true;
}

void SamplerInstrument::apply_midi(std::uint32_t raw) noexcept {
    const auto kind = raw & 0xF0U;
    const auto first = (raw >> 8) & 0x7FU;
    const auto second = (raw >> 16) & 0x7FU;
    if (kind == 0xE0U) {
        const auto wheel = static_cast<double>(first | (second << 7));
        bend_semitones_ = (wheel - 8192.0) / 8192.0 * 2.0;
    } else if (kind == 0xB0U && first == 64) {
        pedal_down_ = second >= 64;
        if (pedal_down_) return;
        // Pedal up: every note it was holding is released now.
        for (auto& voice : voices_) {
            if (!voice.active || !voice.pedal_held) continue;
            voice.pedal_held = false;
            if (voice.stage == Voice::Stage::release) continue;
            voice.stage = Voice::Stage::release;
            voice.release_step = voice.level / voice.release_frames;
            if (voice.release_step <= 0.0) voice.active = false;
        }
    }
}

void SamplerInstrument::render(StereoBlock audio, std::size_t from, std::size_t to) noexcept {
    if (to <= from) return;
    const double gain = gain_from(parameter(sampler_parameter::gain));
    // The tune parameter and the wheel, together.
    const double tune = std::exp2(((parameter(sampler_parameter::tune) - 0.5) * 48.0 +
                                   bend_semitones_) / 12.0);
    for (auto& voice : voices_)
        if (voice.active && !render_voice(voice, audio, from, to, gain, tune)) voice.active = false;
    for (auto& voice : fading_)
        if (voice.active && !render_voice(voice, audio, from, to, gain, tune)) voice.active = false;
}

void SamplerInstrument::process(StereoBlock audio, std::span<const PluginEvent> events) noexcept {
    if (Kit* next = queued_.exchange(nullptr, std::memory_order_acq_rel)) {
        live_ = next;
        program_polyphony_ = next->polyphony;
    }
    const std::size_t frames = std::min(audio.left.size(), audio.right.size());
    std::fill(audio.left.begin(), audio.left.end(), 0.0F);
    std::fill(audio.right.begin(), audio.right.end(), 0.0F);

    std::size_t cursor = 0;
    for (const auto& event : events) {
        const auto offset = std::min<std::size_t>(event.sample_offset, frames);
        render(audio, cursor, offset);
        cursor = std::max(cursor, offset);
        const auto index = static_cast<std::size_t>(event.key_or_parameter);
        switch (event.type) {
        case PluginEvent::Type::note_on:
            start_note(event);
            break;
        case PluginEvent::Type::note_off:
            release_key(event.key_or_parameter);
            break;
        case PluginEvent::Type::parameter_value:
            if (event.key_or_parameter >= 0 && index < base_.size())
                base_[index] = std::clamp(event.value, 0.0, 1.0);
            break;
        case PluginEvent::Type::parameter_modulation:
            if (event.key_or_parameter >= 0 && index < modulation_.size())
                modulation_[index] = std::clamp(event.value, -1.0, 1.0);
            break;
        case PluginEvent::Type::midi_raw:
            apply_midi(static_cast<std::uint32_t>(event.key_or_parameter));
            break;
        }
    }
    render(audio, cursor, frames);

    // Tell the control thread which kits are still in reach.
    std::uint64_t oldest = live_ ? live_->epoch : std::numeric_limits<std::uint64_t>::max();
    for (const auto& voice : voices_)
        if (voice.active) oldest = std::min(oldest, voice.epoch);
    for (const auto& voice : fading_)
        if (voice.active) oldest = std::min(oldest, voice.epoch);
    oldest_live_epoch_.store(oldest, std::memory_order_release);
}

} // namespace blokkily
