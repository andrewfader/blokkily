// Recording automation (item 3.1; decision 3). Strip controls moved in the
// interface reach the engine as moves (SongEngine::move), which play at once
// - overriding their lane while held, in touch mode - and come back stamped
// with where the song was when they sounded. Parameters turned in a plugin's
// own window come back through the engine's edit ring the same way. While the
// transport plays, each is recorded into a pass over its lane when its
// track's mode is touch, latch or write, and closed passes are written into
// the song as part of the take: one step of history for the notes and the
// automation played in one pass of the transport.

#include "app_controller.hpp"

#include <QtGlobal>

#include <algorithm>

namespace {

using blokkily::AutomationTarget;

int control_index(const QString& control) {
    const auto name = control.trimmed().toLower();
    if (name == "gain") return 0;
    if (name == "pan") return 1;
    if (name == "mute") return 2;
    return -1;
}

double strip_value(const blokkily::MixerStrip& mix, int control) {
    return control == 0 ? mix.gain_db : control == 1 ? mix.pan : (mix.mute ? 1.0 : 0.0);
}

} // namespace

bool AppController::stripHeld(int track, const QString& control) const {
    const auto key = std::pair{track, control_index(control)};
    return std::find(held_controls_.begin(), held_controls_.end(), key) != held_controls_.end();
}

void AppController::touchStrip(int track, const QString& control, bool touching) {
    const int index = control_index(control);
    if (song_ == nullptr || index < 0 || track < 0 || track >= song_->trackCount()) return;
    const auto key = std::pair{track, index};
    const auto found = std::find(held_controls_.begin(), held_controls_.end(), key);
    if (touching && found == held_controls_.end()) held_controls_.push_back(key);
    if (!touching && found != held_controls_.end()) held_controls_.erase(found);
    const double value =
        strip_value(song_->song().tracks[static_cast<std::size_t>(track)].mix, index);
    // Taking hold of a control that records begins the take's one step of
    // history, before the control has moved; every move it makes belongs to
    // that step.
    if (touching && engine_ && engine_->is_playing() && song_->captures(track))
        song_->checkpointTake();
    if (engine_)
        (void)engine_->move({static_cast<std::uint32_t>(track),
                             static_cast<blokkily::StripControl>(index), value, touching});
}

void AppController::stripMoved(int track, int control, double value, double previous) {
    if (!engine_ || track < 0 || control < 0 || control > 2) return;
    const blokkily::StripMove move{static_cast<std::uint32_t>(track),
                                   static_cast<blokkily::StripControl>(control), value, true};
    const auto key = std::pair{track, control};
    if (std::find(held_controls_.begin(), held_controls_.end(), key) != held_controls_.end()) {
        (void)engine_->move(move);
        return;
    }
    // A control nobody is holding (a click on mute, a typed value) is taken
    // hold of at the value it had, moved, and let go, all at once.
    auto touched = move;
    touched.value = previous;
    (void)engine_->move(touched);
    (void)engine_->move(move);
    auto released = move;
    released.touching = false;
    (void)engine_->move(released);
}

blokkily::Tick AppController::renderTick() const {
    if (!engine_ || engine_->song_samples() == 0) return 0;
    return blokkily::tick_at_sample(engine_->published_clock(),
                                    engine_->sample_position() % engine_->song_samples());
}

void AppController::drainAutomation() {
    if (!engine_ || song_ == nullptr) return;
    blokkily::StripMoveEvent played;
    while (engine_->take_strip_move(played)) automation_take_.record(song_->song(), played);
    if (automation_take_.ready()) commitAutomation();
}

void AppController::captureParameterEdit(const blokkily::PluginEditEvent& event) {
    if (song_ == nullptr || !engine_) return;
    // What the parameter was before, when no edit has said: the plugin's
    // default.
    double fallback = 0.0;
    if (!automation_take_.knows(event.where, event.edit.parameter))
        if (auto* instance = engine_->processor(event.where))
            for (const auto& parameter : instance->parameters())
                if (parameter.id == event.edit.parameter) fallback = parameter.default_value;
    const auto tick =
        engine_->song_samples() == 0
            ? blokkily::Tick{0}
            : blokkily::tick_at_sample(engine_->published_clock(),
                                       event.song_sample % engine_->song_samples());
    automation_take_.record(song_->song(), event, tick, fallback);
}

void AppController::commitAutomation() {
    if (song_ == nullptr || !automation_take_.ready()) return;
    // Written into a copy first, so a pass that changes nothing takes no
    // step of history.
    auto song = song_->song();
    if (automation_take_.commit(song) == 0) return;
    song_->checkpointTake();
    song_->song() = std::move(song);
    // A lane is part of what the song plays: recompiled, never rebuilt.
    song_->refreshStructure();
}

void AppController::startAutomationTake() {
    automation_take_.clear();
    if (song_ == nullptr || !engine_) return;
    const auto& song = song_->song();
    automation_take_.set_length(song.length());
    song_->setCapturing(true);
    // Write mode overwrites every strip lane the playhead crosses, from here,
    // with what the strip plays.
    const auto at = renderTick();
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        if (song.tracks[track].automation_mode != blokkily::AutomationMode::write) continue;
        for (const auto& lane : song.tracks[track].automation) {
            if (lane.target.kind == AutomationTarget::Kind::parameter ||
                lane.target.processor != blokkily::track_instrument(
                                             static_cast<std::uint32_t>(track)))
                continue;
            const int control = lane.target.kind == AutomationTarget::Kind::gain ? 0
                                : lane.target.kind == AutomationTarget::Kind::pan ? 1
                                                                                  : 2;
            const double value = strip_value(song.tracks[track].mix, control);
            automation_take_.touch(track, lane.target, at, value);
            automation_take_.value(track, lane.target, at, value);
        }
    }
}

void AppController::finishAutomationTake() {
    if (song_ != nullptr) song_->setCapturing(false);
    if (!engine_ || song_ == nullptr) {
        automation_take_.clear();
        return;
    }
    // Whatever the engine played up to the stop is recorded first.
    drainAutomation();
    drainPluginEdits();
    automation_take_.finish(renderTick());
    commitAutomation();
    automation_take_.clear();
}
