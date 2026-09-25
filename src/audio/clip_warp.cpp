#include "blokkily/audio/clip_warp.hpp"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace blokkily {

namespace {

// Source frames between the planner's breakpoints while a clip follows the
// tempo map: a quarter of a recorded beat. A ramp is followed in straight
// pieces this short, so a beat lands within a fraction of a millisecond of the
// curve. Breakpoints are kept even where the tempo is constant: each piece is
// rendered on its own, and Rubber Band places what is near the start of a
// render more exactly than what is seconds into it (a click drifts by up to a
// millisecond and more over a long constant render, and by well under one
// over a quarter beat), so every beat of the recording starts where the song
// says.
constexpr int breakpoints_per_beat = 4;
// Rendition frames over which two separately rendered stretches are blended.
constexpr std::uint64_t crossfade_frames = 512;
// Real audio fed to the stretcher beyond each end of a stretch, so its
// analysis window has context and the part that is kept is not an edge.
constexpr std::uint64_t context_frames = 2048;
// Frames handed to the stretcher per call.
constexpr std::size_t stretch_block = 4096;

std::uint64_t to_engine_frames(std::uint64_t frames, double ratio) {
    return static_cast<std::uint64_t>(std::llround(static_cast<double>(frames) * ratio));
}

std::string number(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.17g", value);
    return buffer;
}

bool fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

// Renders `frames` frames of `channels` (1 or 2 pointers) at `ratio` and
// `pitch` in Rubber Band's offline mode, which pads and compensates its own
// delay so the output starts exactly where the input does.
std::optional<std::vector<std::vector<float>>> stretch(const std::vector<const float*>& channels,
                                                      std::size_t frames, double rate,
                                                      double ratio, double pitch,
                                                      const std::atomic<bool>* cancel) {
    using RubberBand::RubberBandStretcher;
    const auto options = RubberBandStretcher::OptionProcessOffline |
                         RubberBandStretcher::OptionEngineFiner |
                         RubberBandStretcher::OptionPitchHighQuality |
                         RubberBandStretcher::OptionChannelsTogether;
    RubberBandStretcher stretcher(static_cast<std::size_t>(rate), channels.size(), options,
                                  ratio, pitch);
    stretcher.setExpectedInputDuration(frames);
    stretcher.setMaxProcessSize(stretch_block);
    const auto cancelled = [cancel] {
        return cancel != nullptr && cancel->load(std::memory_order_acquire);
    };

    std::vector<const float*> at(channels.size());
    for (std::size_t from = 0; from < frames; from += stretch_block) {
        if (cancelled()) return std::nullopt;
        const auto count = std::min(stretch_block, frames - from);
        for (std::size_t c = 0; c < channels.size(); ++c) at[c] = channels[c] + from;
        stretcher.study(at.data(), count, from + count >= frames);
    }

    std::vector<std::vector<float>> out(channels.size());
    const auto expected = static_cast<std::size_t>(std::ceil(static_cast<double>(frames) * ratio));
    for (auto& channel : out) channel.reserve(expected + stretch_block);
    std::vector<std::vector<float>> scratch(channels.size(), std::vector<float>(stretch_block));
    std::vector<float*> into(channels.size());
    const auto drain = [&] {
        for (int available = stretcher.available(); available > 0;
             available = stretcher.available()) {
            const auto count = std::min<std::size_t>(static_cast<std::size_t>(available),
                                                     stretch_block);
            for (std::size_t c = 0; c < channels.size(); ++c) into[c] = scratch[c].data();
            const auto got = stretcher.retrieve(into.data(), count);
            for (std::size_t c = 0; c < channels.size(); ++c)
                out[c].insert(out[c].end(), scratch[c].begin(),
                              scratch[c].begin() + static_cast<std::ptrdiff_t>(got));
            if (got == 0) break;
        }
    };
    for (std::size_t from = 0; from < frames; from += stretch_block) {
        if (cancelled()) return std::nullopt;
        const auto count = std::min(stretch_block, frames - from);
        for (std::size_t c = 0; c < channels.size(); ++c) at[c] = channels[c] + from;
        stretcher.process(at.data(), count, from + count >= frames);
        drain();
    }
    // After the final block the rest arrives; available() is -1 once done.
    while (stretcher.available() >= 0) {
        if (cancelled()) return std::nullopt;
        if (stretcher.available() == 0) {
            std::this_thread::yield();
            continue;
        }
        drain();
    }
    return out;
}

} // namespace

std::string WarpPlan::key() const {
    std::string text = "warp|" + file.generic_string() + '|' + std::to_string(rate) + '|' +
                       std::to_string(source_offset) + '|' + std::to_string(source_frames) +
                       '|' + std::to_string(output_frames) + '|' + number(pitch_scale) + '|';
    for (const auto& [source, output] : points)
        text += std::to_string(source) + ':' + std::to_string(output) + ',';
    return text;
}

double WarpPlan::output_at(double frame) const noexcept {
    if (points.size() < 2) return frame;
    // The segment holding `frame`; beyond either end the nearest one extends.
    std::size_t segment = 0;
    while (segment + 2 < points.size() && static_cast<double>(points[segment + 1].first) <= frame)
        ++segment;
    const auto [x0, y0] = points[segment];
    const auto [x1, y1] = points[segment + 1];
    const double slope = static_cast<double>(y1 - y0) / static_cast<double>(x1 - x0);
    return static_cast<double>(y0) + (frame - static_cast<double>(x0)) * slope;
}

std::optional<WarpPlan> plan_clip_warp(const Song& song, const AudioClip& clip,
                                       const TickClock& clock, std::uint64_t source_frames) {
    if (!clip.warp.active() || !clip.warp.valid()) return std::nullopt;
    if (clip.file >= song.audio_files.size()) return std::nullopt;
    const auto& file = song.audio_files[clip.file];
    if (file.sample_rate == 0 || clock.sample_rate() <= 0.0) return std::nullopt;

    WarpPlan plan;
    plan.file = file.path;
    plan.rate = static_cast<std::uint32_t>(std::lround(clock.sample_rate()));
    const double engine_rate = static_cast<double>(plan.rate);
    // The same frames the engine would play unwarped (engine_clips.cpp).
    const double to_engine = engine_rate / file.sample_rate;
    plan.source_offset = to_engine_frames(clip.offset_frames, to_engine);
    if (plan.source_offset >= source_frames) return std::nullopt;
    plan.source_frames = std::min(source_frames - plan.source_offset,
                                  to_engine_frames(clip.length_frames, to_engine));
    if (plan.source_frames == 0) return std::nullopt;
    plan.pitch_scale = clip.warp.pitch_scale();
    const double frames = static_cast<double>(plan.source_frames);

    std::vector<std::pair<double, double>> curve;
    if (!clip.warp.follows()) {
        // A plain stretch: seconds become `ratio` times as many seconds,
        // whatever the tempo, so the rendition does not depend on the map.
        curve = {{0.0, 0.0}, {frames, frames * clip.warp.ratio}};
    } else {
        // Where each source frame sounds: its tick under the warp, then that
        // tick's sample under the song's clock, from the clip's first sample.
        const double first = static_cast<double>(sample_for_tick(clock, static_cast<double>(clip.start)));
        const auto output_of = [&](double x) {
            return clock.sample_at_tick(audio_clip_tick_at(song, clip, x / engine_rate)) - first;
        };
        std::vector<double> xs;
        const double step = 60.0 / clip.warp.source_bpm / breakpoints_per_beat * engine_rate;
        for (double x = 0.0; x < frames; x += step) xs.push_back(x);
        // Every tempo point inside the clip is a corner of the curve.
        for (const auto& point : song.tempo.points) {
            const double x = audio_clip_seconds_at(song, clip, static_cast<double>(point.at)) *
                             engine_rate;
            if (x > 0.0 && x < frames) xs.push_back(x);
        }
        xs.push_back(frames);
        std::sort(xs.begin(), xs.end());
        for (const double x : xs) {
            if (!curve.empty() && x - curve.back().first < 1.0) continue;
            curve.emplace_back(x, output_of(x));
        }
        if (curve.back().first != frames) curve.back() = {frames, output_of(frames)};
        curve.front() = {0.0, 0.0};
    }

    plan.output_frames = static_cast<std::uint64_t>(std::llround(std::max(1.0, curve.back().second)));
    plan.points.emplace_back(0, 0);
    for (std::size_t i = 1; i + 1 < curve.size(); ++i) {
        const auto x = static_cast<std::uint64_t>(std::llround(curve[i].first));
        const auto y = static_cast<std::uint64_t>(std::llround(curve[i].second));
        if (x <= plan.points.back().first || y <= plan.points.back().second) continue;
        if (x >= plan.source_frames || y >= plan.output_frames) continue;
        plan.points.emplace_back(x, y);
    }
    plan.points.emplace_back(plan.source_frames, plan.output_frames);
    return plan;
}

std::optional<AudioAsset> render_warp(const AudioAsset& source, const WarpPlan& plan,
                                      std::string* error, const std::atomic<bool>* cancel) {
    if (source.rate != plan.rate || plan.rate == 0) {
        fail(error, "the rendition's source is not at the plan's rate");
        return std::nullopt;
    }
    if (plan.source_offset + plan.source_frames > source.left.size() || plan.points.size() < 2 ||
        plan.output_frames == 0 || !(plan.pitch_scale > 0.0)) {
        fail(error, "the warp plan does not fit its source");
        return std::nullopt;
    }
    const bool stereo = source.right.size() == source.left.size();
    AudioAsset out;
    out.rate = plan.rate;
    out.frames = plan.output_frames;
    out.left.assign(plan.output_frames, 0.0F);
    if (stereo) out.right.assign(plan.output_frames, 0.0F);

    // The crossfade at each join between two stretches: never longer than
    // either of them.
    const std::size_t segments = plan.points.size() - 1;
    std::vector<std::uint64_t> join(segments + 1, 0);
    for (std::size_t b = 1; b < segments; ++b) {
        const auto before = plan.points[b].second - plan.points[b - 1].second;
        const auto after = plan.points[b + 1].second - plan.points[b].second;
        join[b] = std::min({crossfade_frames, before, after}) & ~std::uint64_t{1};
    }

    for (std::size_t s = 0; s < segments; ++s) {
        const auto [x0, y0] = plan.points[s];
        const auto [x1, y1] = plan.points[s + 1];
        const double ratio = static_cast<double>(y1 - y0) / static_cast<double>(x1 - x0);
        const auto lead = join[s] / 2;        // rendition frames before y0 this stretch fills
        const auto trail = join[s + 1] / 2;   // and after y1
        // The source this stretch reads, with context on both sides where
        // the file has it.
        const auto pad_before = static_cast<std::uint64_t>(std::ceil(static_cast<double>(lead) / ratio)) +
                                context_frames;
        const auto pad_after = static_cast<std::uint64_t>(std::ceil(static_cast<double>(trail) / ratio)) +
                               context_frames;
        const auto first = plan.source_offset + x0;
        const auto from = first > pad_before ? first - pad_before : 0;
        const auto to = std::min<std::uint64_t>(source.left.size(), plan.source_offset + x1 + pad_after);
        std::vector<const float*> channels{source.left.data() + from};
        if (stereo) channels.push_back(source.right.data() + from);
        auto rendered = stretch(channels, static_cast<std::size_t>(to - from),
                                static_cast<double>(plan.rate), ratio, plan.pitch_scale, cancel);
        if (!rendered) {
            fail(error, "rendering was cancelled");
            return std::nullopt;
        }
        // Rendered frame of the stretch's first source frame.
        const auto base = static_cast<std::int64_t>(std::llround(static_cast<double>(first - from) * ratio));
        const auto begin = y0 - lead;
        const auto end = std::min(plan.output_frames, y1 + trail);
        for (auto y = begin; y < end; ++y) {
            double weight = 1.0;
            if (lead > 0 && y < y0 + lead)
                weight = (static_cast<double>(y - begin) + 0.5) / static_cast<double>(2 * lead);
            else if (trail > 0 && y >= y1 - trail)
                weight = 1.0 - (static_cast<double>(y - (y1 - trail)) + 0.5) /
                                   static_cast<double>(2 * trail);
            const auto index = base + static_cast<std::int64_t>(y) - static_cast<std::int64_t>(y0);
            if (index < 0 || static_cast<std::size_t>(index) >= (*rendered)[0].size()) continue;
            out.left[y] += static_cast<float>((*rendered)[0][static_cast<std::size_t>(index)] * weight);
            if (stereo)
                out.right[y] +=
                    static_cast<float>((*rendered)[1][static_cast<std::size_t>(index)] * weight);
        }
    }
    out.peaks = compute_peaks(out.left, out.right);
    return out;
}

std::optional<double> detect_tempo(const AudioAsset& audio, std::uint64_t offset,
                                   std::uint64_t frames) {
    if (audio.rate == 0 || offset >= audio.left.size()) return std::nullopt;
    // Thirty seconds say what the tempo is; more only costs time.
    frames = std::min<std::uint64_t>({frames, audio.left.size() - offset,
                                      static_cast<std::uint64_t>(audio.rate) * 30});
    // 1.3 ms at 48 kHz: fine enough that a click's onset is placed to well
    // under a hundredth of a beat.
    constexpr std::size_t hop = 64;
    const std::size_t hops = static_cast<std::size_t>(frames / hop);
    if (hops < 256) return std::nullopt;
    const bool stereo = audio.right.size() == audio.left.size();

    // Onset strength: the rise of log energy from one hop to the next.
    std::vector<double> energy(hops, 0.0);
    for (std::size_t h = 0; h < hops; ++h) {
        double sum = 0.0;
        for (std::size_t i = 0; i < hop; ++i) {
            const auto at = offset + h * hop + i;
            const double value = stereo ? (audio.left[at] + audio.right[at]) * 0.5 : audio.left[at];
            sum += value * value;
        }
        energy[h] = std::log(1e-9 + sum);
    }
    std::vector<double> onset(hops, 0.0);
    for (std::size_t h = 1; h < hops; ++h) onset[h] = std::max(0.0, energy[h] - energy[h - 1]);
    // Spread each onset over a few hops, so beats a fraction of a hop apart
    // from one another still line up at the lag between them.
    {
        constexpr int reach = 4;
        std::vector<double> spread(hops, 0.0);
        for (std::size_t h = 0; h < hops; ++h)
            for (int d = -reach; d <= reach; ++d) {
                const auto at = static_cast<std::ptrdiff_t>(h) + d;
                if (at < 0 || at >= static_cast<std::ptrdiff_t>(hops)) continue;
                spread[static_cast<std::size_t>(at)] +=
                    onset[h] * static_cast<double>(reach + 1 - std::abs(d));
            }
        onset = std::move(spread);
    }
    const double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / static_cast<double>(hops);
    for (auto& value : onset) value -= mean;
    const double power = std::inner_product(onset.begin(), onset.end(), onset.begin(), 0.0);
    if (power <= 0.0) return std::nullopt;

    // Autocorrelation at a fractional lag, in hops.
    const auto correlation = [&](double lag) {
        const auto whole = static_cast<std::size_t>(lag);
        const double part = lag - static_cast<double>(whole);
        if (whole + 1 >= hops) return 0.0;
        double sum = 0.0;
        for (std::size_t h = 0; h + whole + 1 < hops; ++h)
            sum += onset[h] * (onset[h + whole] * (1.0 - part) + onset[h + whole + 1] * part);
        return sum / power;
    };
    const double hops_per_minute = 60.0 * audio.rate / static_cast<double>(hop);
    // Every multiple of the period that fits: the more beats, the sharper.
    const auto score = [&](double bpm) {
        const double lag = hops_per_minute / bpm;
        double sum = 0.0;
        int counted = 0;
        for (double at = lag; at + 1.0 < static_cast<double>(hops) / 2.0 || counted == 0; at += lag) {
            sum += correlation(at);
            ++counted;
            if (at + 1.0 >= static_cast<double>(hops)) break;
        }
        return sum / counted;
    };
    double best = 0.0;
    double best_score = 0.0;
    for (double bpm = 60.0; bpm < 200.0; bpm += 0.5) {
        const double value = correlation(hops_per_minute / bpm);
        if (value > best_score) {
            best_score = value;
            best = bpm;
        }
    }
    if (best_score < 0.2) return std::nullopt;
    // A steady beat correlates as well at two beats as at one, so the slower
    // octave can win by a rounding of the lag: when twice the tempo is (near
    // enough) as periodic, the beat is the faster one.
    while (best * 2.0 < 200.0 &&
           correlation(hops_per_minute / (best * 2.0)) >= 0.7 * correlation(hops_per_minute / best))
        best *= 2.0;
    double refined = best;
    double refined_score = score(best);
    for (double bpm = best - 1.0; bpm <= best + 1.0; bpm += 0.01) {
        const double value = score(bpm);
        if (value > refined_score) {
            refined_score = value;
            refined = bpm;
        }
    }
    if (std::abs(refined - std::round(refined)) < 0.05) refined = std::round(refined);
    return refined;
}

WarpRenderer::WarpRenderer(std::function<void()> on_finished)
    : on_finished_(std::move(on_finished)), worker_([this] { run(); }) {}

WarpRenderer::~WarpRenderer() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    cancel_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void WarpRenderer::submit(AudioAssetPtr source, WarpPlan plan) {
    if (!source) return;
    auto key = plan.key();
    {
        std::lock_guard lock(mutex_);
        if (busy_ && current_ == key) return;
        for (const auto& job : queue_)
            if (job.key == key) return;
        queue_.push_back({std::move(source), std::move(plan), std::move(key)});
    }
    wake_.notify_all();
}

void WarpRenderer::retain(const std::set<std::string>& wanted) {
    std::lock_guard lock(mutex_);
    std::erase_if(queue_, [&](const Job& job) { return !wanted.contains(job.key); });
    if (busy_ && !wanted.contains(current_)) cancel_.store(true, std::memory_order_release);
    if (!busy_ && queue_.empty()) idle_.notify_all();
}

std::vector<WarpRenderer::Finished> WarpRenderer::take_finished() {
    std::lock_guard lock(mutex_);
    return std::exchange(finished_, {});
}

std::set<std::string> WarpRenderer::pending() const {
    std::lock_guard lock(mutex_);
    std::set<std::string> keys;
    if (busy_) keys.insert(current_);
    for (const auto& job : queue_) keys.insert(job.key);
    return keys;
}

void WarpRenderer::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return (!busy_ && queue_.empty()) || stopping_; });
}

void WarpRenderer::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            // Newest first: the last edit is the one the producer is waiting
            // to hear.
            job = std::move(queue_.back());
            queue_.pop_back();
            current_ = job.key;
            busy_ = true;
            cancel_.store(false, std::memory_order_release);
        }
        Finished result;
        result.key = job.key;
        result.asset = render_warp(*job.source, job.plan, &result.error, &cancel_);
        const bool cancelled = cancel_.load(std::memory_order_acquire);
        job.source.reset();
        {
            std::lock_guard lock(mutex_);
            busy_ = false;
            current_.clear();
            if (!cancelled) finished_.push_back(std::move(result));
            if (queue_.empty()) idle_.notify_all();
        }
        if (!cancelled) rendered_.fetch_add(1, std::memory_order_acq_rel);
        if (on_finished_ && !cancelled) on_finished_();
    }
}

} // namespace blokkily
