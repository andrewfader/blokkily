// Choosing the audio system (features/audio_systems.feature): ALSA, PulseAudio,
// JACK - which is how PipeWire's JACK interface is reached - or RtAudio's own
// choice; WASAPI, DirectSound and ASIO on Windows, Core Audio on a Mac.
// Remembered between launches; $BLOKKILY_AUDIO_API overrides it. The SETTINGS
// view also picks the device, its rate and its block
// (features/audio_settings.feature), and keeps the window's own preferences.

#include "app_controller.hpp"

#include <QSettings>
#include <QVariantMap>

#include <algorithm>

namespace {
constexpr auto system_key = "audio/system";
constexpr auto output_key = "audio/outputDevice";
constexpr auto input_key = "audio/inputDevice";
constexpr auto rate_key = "audio/sampleRate";
constexpr auto buffer_key = "audio/bufferFrames";
constexpr int standard_rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
constexpr int buffer_sizes[] = {64, 128, 256, 512, 1024, 2048};
constexpr int default_rate = 48000;
constexpr int default_buffer = 512;
} // namespace

bool AppController::deterministicAudio() const {
    return audio_output_ && audio_output_->mode() == blokkily::RtAudioOutput::Mode::deterministic;
}

// A gate's settings live in memory only: it neither reads the user's file,
// whose choices would change what it sees, nor writes it.
QVariant AppController::setting(const QString& key, const QVariant& fallback) const {
    if (deterministicAudio()) return memory_settings_.value(key, fallback);
    return QSettings().value(key, fallback);
}

void AppController::storeSetting(const QString& key, const QVariant& value) {
    if (deterministicAudio()) {
        if (value.isValid()) memory_settings_.insert(key, value);
        else memory_settings_.remove(key);
        return;
    }
    if (value.isValid()) QSettings().setValue(key, value);
    else QSettings().remove(key);
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
    return QSettings().value(system_key).toString();
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
        QSettings().setValue(system_key, name);
        // Another system has other devices: the old names mean nothing there.
        QSettings().remove(output_key);
        QSettings().remove(input_key);
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
    refreshAudioDevices();
    return true;
}

QString AppController::audioOutputDevice() const { return setting(output_key).toString(); }
QString AppController::audioInputDevice() const { return setting(input_key).toString(); }

int AppController::audioSampleRate() const {
    const int rate = setting(rate_key, default_rate).toInt();
    return rate > 0 ? rate : default_rate;
}

int AppController::audioBufferFrames() const {
    const int frames = setting(buffer_key, default_buffer).toInt();
    return frames > 0 ? frames : default_buffer;
}

bool AppController::serverClocked() const {
    return !deterministicAudio() && audioSystem() == QStringLiteral("jack");
}

QVariantList AppController::audioBufferSizes() const {
    QVariantList sizes;
    for (const int frames : buffer_sizes) sizes.append(frames);
    return sizes;
}

// The standard rates the chosen output takes; all of them while it has not
// said (the default device, a gate's pump).
QVariantList AppController::audioSampleRates() const {
    QVariantList offered;
    const QString output = audioOutputDevice();
    for (const auto& entry : audio_devices_) {
        const auto device = entry.toMap();
        const bool chosen = output.isEmpty() ? device.value("defaultOutput").toBool()
                                             : device.value("name").toString() == output;
        if (!chosen) continue;
        for (const auto& rate : device.value("rates").toList())
            for (const int standard : standard_rates)
                if (rate.toInt() == standard) offered.append(standard);
    }
    if (offered.isEmpty())
        for (const int standard : standard_rates) offered.append(standard);
    return offered;
}

void AppController::refreshAudioDevices() {
    QVariantList devices;
    if (deterministicAudio()) {
        devices.append(QVariantMap{{"name", QStringLiteral("Deterministic pump")},
                                   {"outputs", 2},
                                   {"inputs", static_cast<int>(audio_output_->input_channels())},
                                   {"rates", QVariantList{}},
                                   {"defaultOutput", true},
                                   {"defaultInput", true}});
    } else {
        for (const auto& device : blokkily::RtAudioOutput::devices(audioSystem().toStdString())) {
            QVariantList rates;
            for (const auto rate : device.sample_rates) rates.append(static_cast<int>(rate));
            devices.append(QVariantMap{{"name", QString::fromStdString(device.name)},
                                       {"outputs", static_cast<int>(device.outputs)},
                                       {"inputs", static_cast<int>(device.inputs)},
                                       {"rates", rates},
                                       {"defaultOutput", device.default_output},
                                       {"defaultInput", device.default_input}});
        }
    }
    audio_devices_ = devices;
    emit audioDevicesChanged();
}

namespace {
bool offers(const QVariantList& devices, const QString& name, const char* channels) {
    return std::any_of(devices.begin(), devices.end(), [&](const QVariant& entry) {
        const auto device = entry.toMap();
        return device.value("name").toString() == name && device.value(channels).toInt() > 0;
    });
}
} // namespace

void AppController::reopenAudio(const QString& what) {
    const bool playing = transport_ != nullptr && transport_->playing();
    if (playing) togglePlayback();
    if (deterministicAudio()) {
        // The pump is opened again at the new rate and block; the engine is
        // then built for what it reports, as for a real device.
        if (engine_) (void)audio_output_->open(*engine_, static_cast<unsigned int>(audioSampleRate()),
                                               static_cast<unsigned int>(audioBufferFrames()));
    } else if (audio_output_) {
        audio_output_->stop();
        audio_output_->close();
        audio_output_.reset();
    }
    (void)rebuildEngine();
    status_ = audio_output_ && audio_output_->is_open()
                  ? QStringLiteral("Audio: %1").arg(audioDevice())
                  : QStringLiteral("Audio could not open with %1").arg(what);
    emit statusChanged();
    emit audioSystemChanged();
    emit audioDevicesChanged();
}

bool AppController::setAudioOutputDevice(const QString& name) {
    if (!name.isEmpty() && !offers(audio_devices_, name, "outputs")) {
        status_ = QStringLiteral("No output device called %1").arg(name);
        emit statusChanged();
        return false;
    }
    storeSetting(output_key, name.isEmpty() ? QVariant{} : QVariant(name));
    reopenAudio(name.isEmpty() ? QStringLiteral("the default output") : name);
    return true;
}

bool AppController::setAudioInputDevice(const QString& name) {
    if (!name.isEmpty() && !offers(audio_devices_, name, "inputs")) {
        status_ = QStringLiteral("No input device called %1").arg(name);
        emit statusChanged();
        return false;
    }
    storeSetting(input_key, name.isEmpty() ? QVariant{} : QVariant(name));
    reopenAudio(name.isEmpty() ? QStringLiteral("the default input") : name);
    return true;
}

bool AppController::setAudioSampleRate(int rate) {
    if (!audioSampleRates().contains(rate) || serverClocked()) {
        status_ = serverClocked() ? QStringLiteral("The JACK server sets the sample rate")
                                  : QStringLiteral("%1 Hz is not offered by this device").arg(rate);
        emit statusChanged();
        return false;
    }
    storeSetting(rate_key, rate);
    reopenAudio(QStringLiteral("%1 Hz").arg(rate));
    return true;
}

bool AppController::setAudioBufferFrames(int frames) {
    if (!audioBufferSizes().contains(frames) || serverClocked()) {
        status_ = serverClocked() ? QStringLiteral("The JACK server sets the buffer size")
                                  : QStringLiteral("A %1-frame buffer is not offered").arg(frames);
        emit statusChanged();
        return false;
    }
    storeSetting(buffer_key, frames);
    reopenAudio(QStringLiteral("a %1-frame buffer").arg(frames));
    return true;
}

QVariantMap AppController::uiSettings() const {
    QVariantMap settings;
    if (deterministicAudio()) {
        for (auto it = memory_settings_.begin(); it != memory_settings_.end(); ++it)
            if (it.key().startsWith(QStringLiteral("ui/"))) settings.insert(it.key(), it.value());
        return settings;
    }
    QSettings stored;
    stored.beginGroup(QStringLiteral("ui"));
    for (const auto& key : stored.childKeys())
        settings.insert(QStringLiteral("ui/") + key, stored.value(key));
    return settings;
}

void AppController::setUiSetting(const QString& key, const QVariant& value) {
    if (!key.startsWith(QStringLiteral("ui/"))) return;
    if (setting(key) == value) return;
    storeSetting(key, value);
    emit uiSettingsChanged();
}

void AppController::resetUiSettings() {
    for (const auto& key : uiSettings().keys()) storeSetting(key, {});
    emit uiSettingsChanged();
}
