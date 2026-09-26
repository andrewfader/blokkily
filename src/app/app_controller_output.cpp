#include "app_controller.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/audio_file.hpp"

#include <cmath>
#include <system_error>

namespace {
blokkily::Track output_track(const std::string& name) {
    blokkily::Track track;
    track.name = name;
    // The file already contains stereo pan: compensate the centre pan law
    // so each channel plays at unity. It has no input or instrument.
    track.mix.gain_db = 20.0 * std::log10(std::sqrt(2.0));
    track.input.source = blokkily::TrackInput::Source::none;
    return track;
}
}

void AppController::toggleRackOutputRecording() {
    if (song_ == nullptr || (engine_ && engine_->is_playing())) {
        status_ = "Stop playback before choosing a resampling source";
        emit statusChanged();
        return;
    }
    const bool running = audio_output_ && audio_output_->is_running();
    if (output_writer_->active()) {
        if (running) audio_output_->stop();
        if (engine_) engine_->disconnect_output_capture();
        (void)output_writer_->finish();
        if (running) (void)audio_output_->start();
    }
    if (output_source_) output_source_.reset();
    else {
        const auto bus = song_->rackBus();
        output_source_ = blokkily::OutputTap{bus.kind, bus.bus};
    }
    status_ = output_source_ ? QString("Resampling %1 · arm recording and press Play")
                                  .arg(song_->rack().value("title").toString())
                            : QStringLiteral("Output recording off");
    emit statusChanged();
    emit outputRecordingChanged();
    if (record_armed_) startOutputTake();
}

void AppController::startOutputTake() {
    if (!engine_ || !output_source_ || output_writer_->active()) return;
    if (!engine_->valid_tap(*output_source_)) return;
    const bool running = audio_output_ && audio_output_->is_running();
    if (running) audio_output_->stop();
    // A separate folder prevents simultaneous input/output writers choosing
    // the same filename. First-save relocation includes both folders.
    output_writer_->begin(recordingDirectory() / "outputs",
                           static_cast<std::uint32_t>(std::lround(engine_->sample_rate())));
    output_take_latency_ = engine_->tap_latency(*output_source_);
    (void)engine_->configure_output_capture(*output_source_, &output_writer_->ring());
    if (running) (void)audio_output_->start();
}

void AppController::finishOutputTake() {
    if (!output_writer_->active()) return;
    const bool running = audio_output_ && audio_output_->is_running();
    if (running) audio_output_->stop();
    if (engine_) engine_->disconnect_output_capture();
    const auto dropped = output_writer_->dropped_frames();
    auto takes = output_writer_->finish();
    int placed = 0;
    QString failure;
    if (song_ && engine_ && !takes.empty()) {
        const auto clock = engine_->published_clock();
        // New destinations are created only now. They cannot monitor or
        // feed the source during the recording that created them.
        int destination = -1;
        for (auto& take : takes) {
            if (!take.error.empty()) { failure = QString::fromStdString(take.error); continue; }
            if (!blokkily::place_take(clock, take.first_sample, take.frames, output_take_latency_)) continue;
            song_->checkpointTake();
            auto& song = song_->song();
            if (destination < 0) {
                destination = static_cast<int>(song.tracks.size());
                song.tracks.push_back(output_track("RESAMPLE"));
            }
            take.track = static_cast<std::uint32_t>(destination);
            assets_->adopt(take.file, {take.frames, take.sample_rate, take.channels}, take.audio);
            if (blokkily::commit_take(song, take, clock, output_take_latency_)) ++placed;
        }
        if (placed) song_->notifyStructureChanged();
    }
    if (running && audio_output_) (void)audio_output_->start();
    status_ = QString("%1 output takes recorded · %2 frames dropped").arg(placed).arg(dropped);
    if (!failure.isEmpty()) status_ += " · " + failure;
    emit statusChanged();
}

bool AppController::bounceRackInPlace(bool muteSource) {
    if (!song_ || !engine_ || output_writer_->active() || take_writer_->active()) return false;
    flushRecompile();
    waitForWarpRenders();
    const auto rack = song_->rackBus();
    const blokkily::OutputTap source{rack.kind, rack.bus};
    const bool running = audio_output_ && audio_output_->is_running();
    if (running) audio_output_->stop();
    std::error_code ec;
    const auto directory = recordingDirectory();
    std::filesystem::create_directories(directory, ec);
    auto file = directory / "stem.wav";
    for (int n = 2; !ec && std::filesystem::exists(file, ec); ++n)
        file = directory / ("stem-" + std::to_string(n) + ".wav");
    std::string error = ec.message();
    blokkily::BounceOptions options;
    options.source = source;
    const auto report = ec ? std::optional<blokkily::BounceReport>{} :
        blokkily::bounce_song(*engine_, file, blokkily::WaveFormat::float32, 0, options, &error);
    bool success = false;
    if (report) {
        auto asset = assets_->load(file, engine_->sample_rate(), {}, &error);
        if (asset) {
            auto rendered = song_->song();
            const auto count = rendered.tracks.size();
            if (muteSource) {
                if (source.kind == blokkily::BusKind::track) rendered.tracks[source.bus].mix.mute = true;
                else if (source.kind == blokkily::BusKind::ret) rendered.returns[source.bus].mix.mute = true;
                else {
                    for (auto& track : rendered.tracks) { track.mix.mute = true; track.mix.solo = false; }
                    for (auto& bus : rendered.returns) bus.mix.mute = true;
                    for (auto& fx : rendered.master_inserts) fx.bypass = true;
                    rendered.master_gain_db = 0.0;
                }
            }
            auto track = output_track(song_->rack().value("title").toString().toStdString() + " STEM");
            if (rendered.any_solo()) track.mix.solo = true;
            rendered.tracks.push_back(std::move(track));
            blokkily::RecordedTake take{static_cast<std::uint32_t>(count), 0, report->frames, 2,
                static_cast<std::uint32_t>(std::lround(engine_->sample_rate())), file, asset, {}};
            if (blokkily::commit_take(rendered, take, engine_->published_clock(), 0)) {
                song_->commitSongEdit(std::move(rendered));
                success = true;
            }
        }
    }
    if (running && audio_output_) (void)audio_output_->start();
    status_ = success ? "Stem placed on a new audio track" :
        QString("Stem failed · %1").arg(QString::fromStdString(error));
    emit statusChanged();
    return success;
}
