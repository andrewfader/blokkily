// The scene launcher in the controller (phase 2, wave 6.1): launches and
// stops go to the running engine as commands (never a recompile or a
// rebuild), what each track is doing comes back with the meters, and every
// take arrangement recording made is printed into the song as it arrives.

#include "app_controller.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include <QVariantMap>

bool AppController::launchCell(int scene, int track) {
    if (song_ == nullptr || scene < 0 || track < 0) return false;
    const auto& launcher = song_->song().launcher;
    if (!launcher.slot(static_cast<std::size_t>(scene), static_cast<std::size_t>(track)))
        return false;
    // The cell as it is now, this turn's edits included.
    flushRecompile();
    const bool rolling = transport_ != nullptr && transport_->playing();
    // With no engine yet, Play makes one; the launch then waits for the
    // first bar boundary its quantization allows.
    if (!engine_ && !rolling) togglePlayback();
    if (!engine_) return false;
    const bool queued = engine_->launch_cell(static_cast<std::size_t>(scene),
                                             static_cast<std::size_t>(track));
    // Queued before the transport starts, so it plays from the first block.
    if (queued && transport_ != nullptr && !transport_->playing()) togglePlayback();
    return queued;
}

bool AppController::launchScene(int scene) {
    if (song_ == nullptr || scene < 0 ||
        static_cast<std::size_t>(scene) >= song_->song().launcher.scenes.size())
        return false;
    flushRecompile();
    const bool rolling = transport_ != nullptr && transport_->playing();
    if (!engine_ && !rolling) togglePlayback();
    if (!engine_) return false;
    const bool queued = engine_->launch_scene(static_cast<std::size_t>(scene));
    if (queued && transport_ != nullptr && !transport_->playing()) togglePlayback();
    return queued;
}

bool AppController::stopLauncherTrack(int track) {
    if (!engine_ || track < 0) return false;
    return engine_->stop_launched(static_cast<std::size_t>(track));
}

bool AppController::stopLauncher() {
    if (!engine_) return false;
    return engine_->stop_all_launched();
}

void AppController::toggleLauncherRecording() {
    launcher_recording_ = !launcher_recording_;
    if (engine_) engine_->set_launcher_recording(launcher_recording_);
    emit launcherChanged();
}

void AppController::pollLauncher() {
    if (!engine_ || song_ == nullptr) return;
    QVariantList state;
    const auto tracks = std::min<std::size_t>(engine_->track_count(), song_->song().tracks.size());
    for (std::size_t track = 0; track < tracks; ++track) {
        const auto status = engine_->launcher_status(track);
        state.push_back(QVariantMap{{"playing", status.playing},
                                    {"queued", status.queued_play},
                                    {"stopping", status.queued_stop},
                                    {"scene", static_cast<int>(status.scene)},
                                    {"queuedScene", static_cast<int>(status.queued_scene)}});
    }
    bool changed = state != launcher_state_;
    launcher_state_ = std::move(state);
    blokkily::LauncherTake take;
    while (engine_->take_launcher_take(take)) {
        if (song_->printLauncherTake(take)) {
            ++launcher_takes_;
            changed = true;
        }
    }
    if (changed) emit launcherChanged();
}
