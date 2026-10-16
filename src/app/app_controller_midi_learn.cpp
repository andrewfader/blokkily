// MIDI learn (features/midi_learn.feature): a hardware controller bound to a
// mixer control or a macro. Controllers reach this thread through the MIDI
// input's control feed; a bound one moves its control exactly as the
// control's own dial does, as an edit of the song.

#include "app_controller.hpp"

#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace {

using Target = blokkily::MidiBinding::Target;

std::optional<Target> target_named(const QString& name) {
    if (name == "gain") return Target::track_gain;
    if (name == "pan") return Target::track_pan;
    if (name == "mute") return Target::track_mute;
    if (name == "solo") return Target::track_solo;
    if (name == "master") return Target::master_gain;
    if (name == "macro") return Target::macro;
    return std::nullopt;
}

// -60..+6 dB across the controller's travel, as the fader dial runs; its
// bottom is silence, as the dial's is.
double gain_of(std::uint8_t value) {
    if (value == 0) return -96.0;
    return -60.0 + 66.0 * value / 127.0;
}

} // namespace

QString AppController::describeControl(const QString& target, int index) const {
    const auto parsed = target_named(target);
    if (!parsed || song_ == nullptr) return {};
    const auto& song = song_->song();
    const auto track_name = [&] {
        return index >= 0 && static_cast<std::size_t>(index) < song.tracks.size()
                   ? QString::fromStdString(song.tracks[static_cast<std::size_t>(index)].name)
                   : QStringLiteral("track %1").arg(index + 1);
    };
    switch (*parsed) {
    case Target::track_gain: return track_name() + " gain";
    case Target::track_pan: return track_name() + " pan";
    case Target::track_mute: return track_name() + " mute";
    case Target::track_solo: return track_name() + " solo";
    case Target::master_gain: return QStringLiteral("master level");
    case Target::macro:
        return index >= 0 && static_cast<std::size_t>(index) < song.modulators.size()
                   ? QString::fromStdString(song.modulators[static_cast<std::size_t>(index)].name)
                   : QStringLiteral("macro");
    }
    return {};
}

bool AppController::learnMidi(const QString& target, int index) {
    const auto parsed = target_named(target);
    if (!parsed || song_ == nullptr) return false;
    const auto& song = song_->song();
    if (*parsed == Target::macro) {
        if (index < 0 || static_cast<std::size_t>(index) >= song.modulators.size() ||
            song.modulators[static_cast<std::size_t>(index)].kind != blokkily::Modulator::Kind::macro)
            return false;
    } else if (*parsed != Target::master_gain &&
               (index < 0 || static_cast<std::size_t>(index) >= song.tracks.size())) {
        return false;
    }
    learn_target_ = target;
    learn_index_ = *parsed == Target::master_gain ? 0 : index;
    // What was turned before LEARN was pressed is not the answer.
    blokkily::ControlChange stale;
    while (midi_input_->next_control_change(stale)) {}
    midi_learn_status_ = QStringLiteral("Move a control for %1").arg(describeControl(target, learn_index_));
    emit midiLearnChanged();
    return true;
}

void AppController::cancelMidiLearn() {
    if (learn_target_.isEmpty()) return;
    learn_target_.clear();
    midi_learn_status_ = QStringLiteral("MIDI learn cancelled");
    emit midiLearnChanged();
}

int AppController::forgetMidiBindings(const QString& target, int index) {
    const auto parsed = target_named(target);
    if (!parsed || song_ == nullptr) return 0;
    auto& bindings = song_->song().midi_bindings;
    const auto matches = [&](const blokkily::MidiBinding& binding) {
        return binding.target == *parsed &&
               binding.index == static_cast<std::uint32_t>(*parsed == Target::master_gain ? 0 : index);
    };
    const auto count = std::count_if(bindings.begin(), bindings.end(), matches);
    if (count == 0) return 0;
    song_->checkpoint();
    std::erase_if(bindings, matches);
    midi_learn_status_ = QStringLiteral("%1 forgets its controller").arg(describeControl(target, index));
    emit midiLearnChanged();
    return static_cast<int>(count);
}

QString AppController::midiBindingFor(const QString& target, int index) const {
    const auto parsed = target_named(target);
    if (!parsed || song_ == nullptr) return {};
    for (const auto& binding : song_->song().midi_bindings)
        if (binding.target == *parsed &&
            binding.index == static_cast<std::uint32_t>(*parsed == Target::master_gain ? 0 : index))
            return QStringLiteral("CC %1 · CH %2").arg(binding.controller).arg(binding.channel + 1);
    return {};
}

void AppController::serveControlChanges() {
    if (song_ == nullptr) return;
    blokkily::ControlChange change;
    // Each control moves once a poll, to the last value that arrived for it:
    // a fast knob is one edit, not a hundred.
    std::vector<blokkily::ControlChange> latest;
    while (midi_input_->next_control_change(change)) {
        if (!learn_target_.isEmpty()) {
            const auto target = *target_named(learn_target_);
            auto& bindings = song_->song().midi_bindings;
            song_->checkpoint();
            // A controller drives one control; the control keeps one controller.
            std::erase_if(bindings, [&](const blokkily::MidiBinding& binding) {
                return (binding.channel == change.channel && binding.controller == change.controller) ||
                       (binding.target == target && binding.index == static_cast<std::uint32_t>(learn_index_));
            });
            bindings.push_back({change.channel, change.controller, target,
                                static_cast<std::uint32_t>(learn_index_)});
            midi_learn_status_ = QStringLiteral("CC %1 · CH %2 → %3")
                                     .arg(change.controller)
                                     .arg(change.channel + 1)
                                     .arg(describeControl(learn_target_, learn_index_));
            learn_target_.clear();
            emit midiLearnChanged();
            continue;
        }
        auto same = std::find_if(latest.begin(), latest.end(), [&](const auto& seen) {
            return seen.channel == change.channel && seen.controller == change.controller;
        });
        if (same != latest.end()) *same = change;
        else latest.push_back(change);
    }
    for (const auto& moved : latest) {
        const auto& bindings = song_->song().midi_bindings;
        const auto bound = std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
            return binding.channel == moved.channel && binding.controller == moved.controller;
        });
        if (bound == bindings.end()) continue;
        const auto binding = *bound;
        const int index = static_cast<int>(binding.index);
        const bool on = moved.value >= 64;
        switch (binding.target) {
        case Target::track_gain: song_->setTrackGain(index, gain_of(moved.value)); break;
        case Target::track_pan: song_->setTrackPan(index, moved.value / 127.0 * 2.0 - 1.0); break;
        case Target::track_mute:
            if (index < song_->trackCount() &&
                song_->song().tracks[static_cast<std::size_t>(index)].mix.mute != on)
                song_->toggleMute(index);
            break;
        case Target::track_solo:
            if (index < song_->trackCount() &&
                song_->song().tracks[static_cast<std::size_t>(index)].mix.solo != on)
                song_->toggleSolo(index);
            break;
        case Target::master_gain: song_->setMasterGain(gain_of(moved.value)); break;
        case Target::macro: song_->setModulatorValue(index, moved.value / 127.0); break;
        }
    }
}
