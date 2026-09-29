// Clip warp in the application (item 3.6): the renditions warped clips play,
// rendered by the warp worker off the control and audio threads and kept in
// the asset cache, and the edits the warp panel makes.
//
// A warped clip is silent until its rendition is ready (clip_warp.hpp says
// why). The song model marks such a clip `rendering`, so the lane shows it,
// and the moment the worker finishes the arrangement is recompiled with the
// rendition in it: the clip comes in without the device stopping.

#include "app_controller.hpp"

#include "blokkily/audio/clip_warp.hpp"

#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <set>

namespace {
// What following the tempo assumes when the audio has no steady beat to
// detect: the producer types the right one.
constexpr double fallback_source_bpm = 120.0;
} // namespace

blokkily::ClipRenditions AppController::clipRenditions(double rate) {
    blokkily::ClipRenditions ready;
    if (song_ == nullptr || !(rate > 0.0)) return ready;
    const auto& song = song_->song();
    const bool any = std::any_of(song.audio_clips.begin(), song.audio_clips.end(),
                                 [](const blokkily::AudioClip& clip) { return clip.warp.active(); });
    if (!any && !warp_) {
        song_->setRenderingClips({});
        return ready;
    }
    if (!warp_) {
        // The worker wakes the control thread through the event loop; what
        // it posts after the controller is gone is discarded with it.
        warp_ = std::make_unique<blokkily::WarpRenderer>([this] {
            QMetaObject::invokeMethod(this, [this] { collectWarpRenders(); },
                                      Qt::QueuedConnection);
        });
    }
    // The same clock the engine compiles with, so the plans here are the
    // plans the engine looks the renditions up by.
    const blokkily::TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    std::set<std::string> wanted;
    std::vector<blokkily::AudioClipId> rendering;
    for (const auto& clip : song.audio_clips) {
        if (!clip.warp.active() || clip.file >= clip_assets_.size() || !clip_assets_[clip.file])
            continue;   // unwarped, or its file is missing
        auto source = clip_assets_[clip.file];
        // A streamed file (wave 4.2) is decoded into memory for warping: a
        // stretch reads the whole clip. The rendition then plays from memory.
        if (source->is_streamed()) {
            const auto& file = song.audio_files[clip.file];
            source = assets_->load(file.path, rate,
                                   blokkily::AudioFileInfo{file.frames, file.sample_rate,
                                                           file.channels});
            if (!source) continue;
        }
        auto plan = blokkily::plan_clip_warp(song, clip, clock, source->frames);
        if (!plan) continue;
        auto key = plan->key();
        wanted.insert(key);
        if (auto rendition = assets_->derived(key)) {
            ready[key] = std::move(rendition);
            continue;
        }
        if (warp_failed_.contains(key)) continue;
        warp_->submit(source, std::move(*plan));
        rendering.push_back(clip.id);
    }
    // An edit made earlier requests stale: they are not rendered.
    warp_->retain(wanted);
    const int pending = static_cast<int>(warp_->pending().size());
    const auto status = pending > 0
                            ? QString("Rendering %1 warped clip%2…")
                                  .arg(pending)
                                  .arg(pending == 1 ? "" : "s")
                            : ready.empty() ? QStringLiteral("No warped clips")
                                            : QString("%1 warped clip%2 ready")
                                                  .arg(ready.size())
                                                  .arg(ready.size() == 1 ? "" : "s");
    if (pending != warp_pending_ || status != warp_status_) {
        warp_pending_ = pending;
        if (!warp_status_.startsWith("Warp failed") || pending > 0) warp_status_ = status;
        emit warpChanged();
    }
    song_->setRenderingClips(std::move(rendering));
    return ready;
}

void AppController::collectWarpRenders() {
    if (!warp_) return;
    auto finished = warp_->take_finished();
    if (finished.empty()) return;
    // Renditions of settings no arrangement plays any more are let go, so a
    // clip's pitch dragged through twenty values does not keep twenty.
    assets_->purge_unused_derived();
    for (auto& done : finished) {
        if (!done.asset) {
            warp_failed_.insert(done.key);
            warp_status_ = QString("Warp failed · %1").arg(QString::fromStdString(done.error));
            emit warpChanged();
            continue;
        }
        assets_->insert_derived(done.key,
                                std::make_shared<const blokkily::AudioAsset>(std::move(*done.asset)));
    }
    // The next recompile finds the renditions in the cache and plays them.
    requestRecompile();
}

void AppController::waitForWarpRenders() {
    // Whatever this turn edited asks for its renditions first.
    flushRecompile();
    if (!warp_) return;
    warp_->wait_idle();
    collectWarpRenders();
    flushRecompile();
}

double AppController::detectClipTempo(qint64 id) {
    if (song_ == nullptr) return 0.0;
    const auto& song = song_->song();
    const auto found = std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                                    [id](const blokkily::AudioClip& clip) {
                                        return static_cast<qint64>(clip.id) == id;
                                    });
    if (found == song.audio_clips.end() || found->file >= clip_assets_.size() ||
        !clip_assets_[found->file] || found->file >= song.audio_files.size())
        return 0.0;
    auto held = clip_assets_[found->file];
    if (held->is_streamed()) {
        // Tempo detection reads samples; a streamed file is decoded for it.
        const auto& file = song.audio_files[found->file];
        held = assets_->load(file.path, held->rate,
                             blokkily::AudioFileInfo{file.frames, file.sample_rate, file.channels});
        if (!held) return 0.0;
    }
    const auto& asset = *held;
    const auto file_rate = song.audio_files[found->file].sample_rate;
    if (file_rate == 0) return 0.0;
    // The clip's own stretch of the file, at the rate it was decoded for.
    const double ratio = static_cast<double>(asset.rate) / file_rate;
    const auto offset = static_cast<std::uint64_t>(std::llround(found->offset_frames * ratio));
    const auto frames = static_cast<std::uint64_t>(std::llround(found->length_frames * ratio));
    const auto bpm = blokkily::detect_tempo(asset, offset, frames);
    return bpm.value_or(0.0);
}

bool AppController::setClipFollowTempo(qint64 id, bool follow) {
    if (song_ == nullptr) return false;
    const auto row = song_->audioClip(id);
    if (row.isEmpty()) return false;
    double bpm = row.value("sourceBpm").toDouble();
    if (follow && bpm <= 0.0) {
        bpm = detectClipTempo(id);
        if (bpm <= 0.0) bpm = fallback_source_bpm;
    }
    return song_->setAudioClipWarp(id, follow, bpm, row.value("ratio").toDouble(),
                                   row.value("semitones").toInt(), row.value("cents").toDouble());
}
