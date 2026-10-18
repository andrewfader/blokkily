// Outboard gear (features/midi_output.feature): a track whose instrument is a
// MIDI output port and channel, and the port the song sends MIDI clock to.
// The outputs are the controller's, opened when first asked for and shared by
// every track playing through one; the factory makes a MIDI Out instrument on
// one of them for a slot of format blokkily::midi_out_format.

#include "app_controller.hpp"
#include "processor_factory.hpp"

#include "blokkily/midi/midi_out_instrument.hpp"

#include <QSettings>

#include <memory>

namespace {
constexpr auto clock_setting = "midi/clockPort";

const blokkily::InstrumentSlot* midi_out_slot(const SongModel* song, int track) {
    if (song == nullptr || track < 0 ||
        static_cast<std::size_t>(track) >= song->song().tracks.size())
        return nullptr;
    const auto& slot = song->song().tracks[static_cast<std::size_t>(track)].instrument;
    return slot.format == blokkily::midi_out_format ? &slot : nullptr;
}
} // namespace

std::shared_ptr<blokkily::MidiOutput> AppController::midiOutput(const std::string& port) {
    if (port.empty()) return nullptr;
    if (const auto found = midi_outputs_.find(port); found != midi_outputs_.end())
        return found->second;
    auto output = std::make_shared<blokkily::MidiOutput>(
        midi_input_->mode() == blokkily::MidiInput::Mode::deterministic
            ? blokkily::MidiOutput::Mode::deterministic
            : blokkily::MidiOutput::Mode::device);
    std::string error;
    if (!output->open(port, &error)) {
        status_ = QString("MIDI output unavailable · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return nullptr;
    }
    midi_outputs_.emplace(port, output);
    return output;
}

void AppController::registerMidiOut() {
    blokkily::register_internal(
        blokkily::midi_out_format,
        [this](const blokkily::InstrumentSlot& slot, const blokkily::ProcessorContext&,
               std::string* error) -> std::unique_ptr<blokkily::PluginInstance> {
            auto output = midiOutput(slot.identifier);
            if (output == nullptr) {
                if (error != nullptr) *error = "MIDI output " + slot.identifier + " is not available";
                return nullptr;
            }
            const auto channel =
                slot.state.empty() ? std::uint8_t{0}
                                   : static_cast<std::uint8_t>(static_cast<std::uint8_t>(slot.state.front()) & 0x0FU);
            return std::make_unique<blokkily::MidiOutInstrument>(std::move(output), channel);
        });
}

bool AppController::setTrackMidiOut(int track, const QString& port, int channel) {
    if (song_ == nullptr || track < 0 || static_cast<std::size_t>(track) >= song_->song().tracks.size())
        return false;
    blokkily::InstrumentSlot slot;
    if (!port.isEmpty()) {
        if (midiOutput(port.toStdString()) == nullptr) return false;
        slot.format = blokkily::midi_out_format;
        slot.identifier = port.toStdString();
        slot.state = {static_cast<std::byte>(std::clamp(channel, 0, 15))};
    }
    song_->setInstrument(track, slot);
    status_ = port.isEmpty() ? QStringLiteral("MIDI OUT · off")
                             : QString("MIDI OUT · %1 · channel %2").arg(port).arg(std::clamp(channel, 0, 15) + 1);
    emit statusChanged();
    emit midiChanged();
    return true;
}

QString AppController::trackMidiOut(int track) const {
    const auto* slot = midi_out_slot(song_, track);
    return slot != nullptr ? QString::fromStdString(slot->identifier) : QString();
}

int AppController::trackMidiOutChannel(int track) const {
    const auto* slot = midi_out_slot(song_, track);
    return slot != nullptr && !slot->state.empty() ? static_cast<int>(slot->state.front()) & 0x0F : 0;
}

bool AppController::setTrackMidiOutChannel(int track, int channel) {
    if (midi_out_slot(song_, track) == nullptr || channel < 0 || channel > 15) return false;
    if (!setInstrumentState(track, {static_cast<std::byte>(channel)}, QStringLiteral("midiout")))
        return false;
    emit midiChanged();
    return true;
}

bool AppController::setMidiClockPort(const QString& port) {
    std::shared_ptr<blokkily::MidiOutput> output;
    if (!port.isEmpty()) {
        output = midiOutput(port.toStdString());
        if (output == nullptr) return false;
    }
    // The engine lets go of the old output before it may go.
    if (engine_) engine_->set_clock_output(output.get());
    clock_output_ = std::move(output);
    midi_clock_port_ = clock_output_ ? port : QString();
    status_ = midi_clock_port_.isEmpty() ? QStringLiteral("MIDI clock · off")
                                         : QString("MIDI clock · %1").arg(midi_clock_port_);
    if (remember_clock_) {
        const auto settings = clock_settings_file_.isEmpty()
                                  ? std::make_unique<QSettings>()
                                  : std::make_unique<QSettings>(clock_settings_file_, QSettings::IniFormat);
        settings->setValue(clock_setting, midi_clock_port_);
    }
    emit statusChanged();
    emit midiChanged();
    return true;
}

void AppController::rememberMidiClock(const QString& settings_file) {
    remember_clock_ = true;
    clock_settings_file_ = settings_file;
}

void AppController::restoreMidiClock() {
    if (!remember_clock_ || !midi_clock_port_.isEmpty()) return;
    const QString port = (clock_settings_file_.isEmpty()
                              ? QSettings().value(clock_setting)
                              : QSettings(clock_settings_file_, QSettings::IniFormat).value(clock_setting))
                             .toString();
    if (!port.isEmpty() && midi_out_ports_.contains(port)) (void)setMidiClockPort(port);
}
