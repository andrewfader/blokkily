// Choosing the audio system (features/audio_systems.feature): ALSA, PulseAudio,
// JACK - which is how PipeWire's JACK interface is reached - or RtAudio's own
// choice. Remembered between launches; $BLOKKILY_AUDIO_API overrides it.

#include "app_controller.hpp"

#include <QSettings>
#include <QVariantMap>

namespace {
constexpr auto setting = "audio/system";
}

QVariantList AppController::audioSystems() const {
    QVariantList systems;
    systems.append(QVariantMap{{"name", QString()}, {"display", QStringLiteral("AUTO")}});
    for (const auto& system : blokkily::RtAudioOutput::audio_systems())
        systems.append(QVariantMap{{"name", QString::fromStdString(system.name)},
                                   {"display", QString::fromStdString(system.display).toUpper()}});
    return systems;
}

QString AppController::audioSystem() const {
    const auto overridden = qEnvironmentVariable("BLOKKILY_AUDIO_API");
    if (!overridden.isEmpty()) return overridden;
    if (!audio_system_.isNull()) return audio_system_;
    return QSettings().value(setting).toString();
}

QString AppController::audioDevice() const {
    if (audio_output_ && audio_output_->mode() == blokkily::RtAudioOutput::Mode::deterministic)
        return QStringLiteral("Deterministic pump");
    if (!audio_output_ || !audio_output_->is_open()) return QStringLiteral("no device");
    const auto info = audio_output_->device_info();
    return QStringLiteral("%1 · %2 Hz · %3")
        .arg(QString::fromStdString(info.api))
        .arg(info.sample_rate)
        .arg(info.buffer_frames);
}

bool AppController::setAudioSystem(const QString& name) {
    bool known = name.isEmpty();
    for (const auto& system : blokkily::RtAudioOutput::audio_systems())
        known = known || QString::fromStdString(system.name) == name;
    if (!known) {
        status_ = QStringLiteral("Audio system %1 is not available in this build").arg(name);
        emit statusChanged();
        return false;
    }
    audio_system_ = name;
    // A gate's deterministic device stays what it is, and a gate never writes
    // the user's settings.
    // Known by how the output was made, whether or not it is open yet.
    const bool deterministic =
        audio_output_ && audio_output_->mode() == blokkily::RtAudioOutput::Mode::deterministic;
    if (!deterministic) {
        QSettings().setValue(setting, name);
        if (audio_output_) {
            audio_output_->stop();
            audio_output_->close();
            audio_output_.reset();
        }
        const bool playing = transport_ != nullptr && transport_->playing();
        if (playing) togglePlayback();
        (void)rebuildEngine();
        status_ = audio_output_ && audio_output_->is_open()
                      ? QStringLiteral("Audio: %1").arg(audioDevice())
                      : QStringLiteral("Audio system %1 could not open a device").arg(name.isEmpty() ? "AUTO" : name);
        emit statusChanged();
    }
    emit audioSystemChanged();
    return true;
}
