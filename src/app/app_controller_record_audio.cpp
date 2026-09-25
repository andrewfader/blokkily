// Audio input in the application (plan item 3.2, feature 4c; decisions 3, 4 and
// 8). Every track's input route reaches the running engine, which monitors it
// and, while recording, captures the raw input into the take writer's ring. A
// take is written to disk off the audio thread as it is played, and when the
// pass ends each take becomes an audio clip on its track, placed where it was
// heard (plan C21), in the same step of history as the notes of that pass.
// Takes are written beside the project, in <project>.audio/, or, until the
// session is first saved, in a temporary folder of its own that the save
// moves into the project's.
//
// The take's lifecycle is kept in these functions so the notes' take
// (app_controller_take.cpp) calls them at its edges and nothing more.

#include "app_controller.hpp"

#include "blokkily/audio/audio_input.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <system_error>

void AppController::connectAudioInput() {
    if (song_ == nullptr) return;
    // Arming, the input chips and the selected track all decide which tracks
    // hear and record the inputs; every change to the song re-reads them.
    // An input armed for monitoring is heard at once, on a stopped song too,
    // the way a keyboard is.
    QObject::connect(song_, &SongModel::songChanged, this, [this] {
        // A track that starts or stops taking audio input changes what the
        // device has to be opened for, which needs the stream stopped and
        // opened again: a rebuild, which keeps playback where it was.
        if (engine_ && audio_output_ && !audio_output_->opened_for_input(songTakesAudioInput())) {
            (void)rebuildEngine();
            return;
        }
        updateAudioInputs();
        if (monitorsAudioInput()) (void)ensureAudioRunning();
    });
    updateAudioInputs();
}

bool AppController::songTakesAudioInput() const {
    if (song_ == nullptr) return false;
    const auto& tracks = song_->song().tracks;
    return std::any_of(tracks.begin(), tracks.end(), [](const blokkily::Track& track) {
        return blokkily::takes_audio(track.input);
    });
}

bool AppController::monitorsAudioInput() const {
    if (song_ == nullptr || !engine_) return false;
    for (std::size_t track = 0; track < engine_->track_count(); ++track)
        if (engine_->audio_input(track).monitor) return true;
    return false;
}

void AppController::updateAudioInputs() {
    if (song_ == nullptr) return;
    if (audio_output_ && audio_output_->is_open())
        song_->setAudioInputChannels(static_cast<int>(audio_output_->input_channels()));
    if (!engine_) return;
    engine_->set_audio_inputs(blokkily::audio_input_routes(
        song_->song(), static_cast<std::size_t>(std::max(0, song_->selectedTrack()))));
}

std::filesystem::path AppController::recordingDirectory() const {
    if (!project_path_.isEmpty()) {
        const QFileInfo project(project_path_);
        return std::filesystem::path(project.absolutePath().toStdString()) /
               (project.completeBaseName().toStdString() + ".audio");
    }
    // An unsaved session records into a folder of its own, named for this
    // process and this controller so two sessions never share one.
    if (session_audio_dir_.empty())
        session_audio_dir_ =
            std::filesystem::path(QDir::tempPath().toStdString()) /
            QString("blokkily-session-%1-%2")
                .arg(QCoreApplication::applicationPid())
                .arg(reinterpret_cast<quintptr>(this), 0, 16)
                .toStdString();
    return session_audio_dir_;
}

std::uint64_t AppController::takeCompensation() const {
    const auto round_trip = audio_output_ ? audio_output_->round_trip_latency() : 0U;
    const auto latency = engine_ ? engine_->output_latency() : 0U;
    const auto offset = song_ != nullptr ? song_->song().record_offset_samples : 0;
    return blokkily::take_compensation(round_trip, latency, offset);
}

void AppController::startAudioTake() {
    if (!engine_ || take_writer_->active() || engine_->sample_rate() <= 0.0) return;
    take_writer_->begin(recordingDirectory(),
                        static_cast<std::uint32_t>(std::lround(engine_->sample_rate())));
    engine_->connect_capture(&take_writer_->ring());
}

void AppController::discardAudioTake() {
    if (take_writer_->active()) (void)take_writer_->finish();
}

void AppController::finishAudioTake() {
    if (!take_writer_->active()) return;
    dropped_input_frames_ = take_writer_->dropped_frames();
    commitAudioTakes(take_writer_->finish());
}

void AppController::commitAudioTakes(std::vector<blokkily::RecordedTake> takes) {
    if (song_ == nullptr || !engine_) return;
    const auto compensation = takeCompensation();
    const auto clock = engine_->published_clock();
    int placed = 0;
    QString failure;
    for (const auto& take : takes) {
        if (!take.error.empty()) {
            failure = QString::fromStdString(take.error);
            continue;
        }
        if (!blokkily::place_take(clock, take.first_sample, take.frames, compensation)) continue;
        // One step of history for the whole pass: the notes it wrote and the
        // clips it recorded are undone together.
        if (!take_checkpointed_) {
            song_->checkpoint();
            take_checkpointed_ = true;
        }
        // The store already holds the take, so the clip is heard at the next
        // recompile without reading its own file back.
        assets_->adopt(take.file, {take.frames, take.sample_rate, take.channels}, take.audio);
        if (const auto id = blokkily::commit_take(song_->song(), take, clock, compensation)) {
            last_recorded_clip_ = static_cast<qint64>(*id);
            ++placed;
        }
    }
    if (placed > 0) song_->notifyStructureChanged();
    if (placed == 0 && failure.isEmpty() && dropped_input_frames_ == 0) return;
    audio_take_status_ = placed == 1 ? QStringLiteral("1 audio take recorded")
                                     : QString("%1 audio takes recorded").arg(placed);
    if (dropped_input_frames_ > 0)
        audio_take_status_ += QString(" · %1 input frames dropped").arg(dropped_input_frames_);
    if (!failure.isEmpty()) audio_take_status_ += QString(" · write failed: %1").arg(failure);
    status_ = audio_take_status_;
    emit statusChanged();
    emit audioTakeChanged();
}

void AppController::relocateRecordings(const QString& path) {
    if (song_ == nullptr || session_audio_dir_.empty() || take_writer_->active()) return;
    std::error_code failure;
    if (!std::filesystem::is_directory(session_audio_dir_, failure)) return;
    const QFileInfo project(path);
    const auto target = std::filesystem::path(project.absolutePath().toStdString()) /
                        (project.completeBaseName().toStdString() + ".audio");
    // Listed first, so moving them does not disturb the listing.
    std::vector<std::filesystem::path> recorded;
    for (const auto& entry : std::filesystem::directory_iterator(session_audio_dir_, failure))
        if (entry.is_regular_file()) recorded.push_back(entry.path());
    std::map<std::filesystem::path, std::filesystem::path> moved;
    for (const auto& file : recorded) {
        std::filesystem::create_directories(target, failure);
        // A take never overwrites a file already in the project's folder.
        auto destination = target / file.filename();
        for (int copy = 2; std::filesystem::exists(destination, failure); ++copy)
            destination = target / (file.stem().string() + "-" + std::to_string(copy) +
                                    file.extension().string());
        std::filesystem::rename(file, destination, failure);
        if (failure) {
            // Another file system: copied, then the temporary one let go.
            failure.clear();
            if (!std::filesystem::copy_file(file, destination, failure)) continue;
            std::filesystem::remove(file, failure);
        }
        moved.emplace(file, destination);
    }
    // The song and every step of its history name the files where they are now.
    song_->relocateAudioFiles(moved);
    std::filesystem::remove(session_audio_dir_, failure);
}
