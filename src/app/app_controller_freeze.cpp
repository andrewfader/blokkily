// Track freeze (features/track_freeze.feature): a track's instrument and
// inserts rendered to audio through the engine the speakers hear, played in
// their place so they need not be loaded; unfreezing brings them back as
// they were.

#include "app_controller.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/take_writer.hpp"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

bool AppController::freezeTrack(int track) {
    if (song_ == nullptr || !engine_ || output_writer_->active() || take_writer_->active()) return false;
    if (track < 0 || track >= song_->trackCount()) return false;
    const auto index = static_cast<std::size_t>(track);
    if (song_->song().tracks[index].frozen) return true;
    flushRecompile();
    waitForWarpRenders();
    // What every processor holds now is the song's, so unfreezing brings
    // the instrument and inserts back exactly as they were heard.
    (void)blokkily::capture_processor_states(*engine_, song_->song(), engine_signature_);
    const auto name = song_->song().tracks[index].name;
    std::error_code ec;
    const auto directory = recordingDirectory();
    std::filesystem::create_directories(directory, ec);
    auto file = directory / ("freeze-" + std::to_string(track + 1) + ".wav");
    for (int n = 2; !ec && std::filesystem::exists(file, ec); ++n)
        file = directory / ("freeze-" + std::to_string(track + 1) + "-" + std::to_string(n) + ".wav");
    std::string error = ec.message();
    bool success = false;
    {
        const LoadProgress::Scope freezing(
            progress_, QStringLiteral("FREEZING %1").arg(QString::fromStdString(name).toUpper()));
        std::optional<blokkily::BounceReport> report;
        if (!ec)
            withEngineStopped([&](std::uint64_t tail) {
                blokkily::BounceOptions options;
                options.source = blokkily::OutputTap{blokkily::BusKind::track,
                                                     static_cast<std::uint32_t>(track), true};
                options.progress = bounceProgress(engine_->sample_rate());
                report = blokkily::bounce_song(*engine_, file, blokkily::WaveFormat::float32, tail,
                                               options, &error);
            });
        if (report) {
            if (auto asset = assets_->load(file, engine_->sample_rate(), {}, &error)) {
                auto frozen = song_->song();
                blokkily::RecordedTake take{static_cast<std::uint32_t>(track), 0, report->frames, 2,
                    static_cast<std::uint32_t>(std::lround(engine_->sample_rate())), file, asset, {}};
                if (const auto id = blokkily::commit_take(frozen, take, engine_->published_clock(), 0)) {
                    for (auto& clip : frozen.audio_clips)
                        if (clip.id == *id) clip.freeze = true;
                    frozen.tracks[index].frozen = true;
                    // One step of history; the graph rebuilt without its
                    // instrument and inserts.
                    song_->commitSongEdit(std::move(frozen));
                    success = true;
                }
            }
        }
    }
    status_ = success ? QString("%1 frozen").arg(QString::fromStdString(name))
                      : QString("Freeze failed · %1").arg(QString::fromStdString(error));
    emit statusChanged();
    return success;
}

bool AppController::unfreezeTrack(int track) {
    if (song_ == nullptr || track < 0 || track >= song_->trackCount()) return false;
    const auto index = static_cast<std::size_t>(track);
    if (!song_->song().tracks[index].frozen) return true;
    auto thawed = song_->song();
    thawed.tracks[index].frozen = false;
    std::erase_if(thawed.audio_clips, [index](const blokkily::AudioClip& clip) {
        return clip.track == index && clip.freeze;
    });
    song_->commitSongEdit(std::move(thawed));
    status_ = QString("%1 unfrozen").arg(QString::fromStdString(song_->song().tracks[index].name));
    emit statusChanged();
    return true;
}

bool AppController::toggleFreeze(int track) {
    if (song_ == nullptr || track < 0 || track >= song_->trackCount()) return false;
    return song_->song().tracks[static_cast<std::size_t>(track)].frozen ? unfreezeTrack(track)
                                                                         : freezeTrack(track);
}
