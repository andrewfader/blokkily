// Plugin editors (item 2.6): opening a plugin's own window from a strip or
// the instrument panel, carrying what is turned in it into the song, one
// step of history per gesture (decision 12), and putting undone states back
// into the running instruments.

#include "app_controller.hpp"

#include <QVariantMap>

#include <cmath>

namespace {
blokkily::ProcessorAddress address_of(int track) {
    return blokkily::track_instrument(static_cast<std::uint32_t>(track));
}
} // namespace

PluginWindows& AppController::pluginWindows() {
    if (!windows_) {
        windows_ = std::make_unique<PluginWindows>();
        // A plugin that closes its own editor is seen in the strip too.
        QObject::connect(windows_.get(), &PluginWindows::changed, this,
                         &AppController::editorsChanged);
    }
    return *windows_;
}

QVariantList AppController::openEditors() const {
    QVariantList tracks;
    if (!windows_) return tracks;
    for (const auto& where : windows_->openAddresses())
        if (where.kind == blokkily::BusKind::track && where.instrument())
            tracks.push_back(static_cast<int>(where.bus));
    return tracks;
}

bool AppController::hasEditor(int track) const {
    if (!engine_ || track < 0) return false;
    const auto* instance = engine_->processor(address_of(track));
    return instance != nullptr && instance->has_editor();
}

bool AppController::editorOpen(int track) const {
    return windows_ && track >= 0 && windows_->isOpen(address_of(track));
}

bool AppController::openEditor(int track) {
    if (song_ == nullptr || track < 0 || track >= song_->trackCount()) return false;
    const auto report = [this](const QString& message) {
        editor_status_ = message;
        emit editorsChanged();
    };
    if (!engine_ && !instruments().empty()) (void)rebuildEngine();
    auto* instance = engine_ ? engine_->processor(address_of(track)) : nullptr;
    const auto& song = song_->song();
    const auto& name = song.tracks[static_cast<std::size_t>(track)].name;
    if (instance == nullptr) {
        report(QStringLiteral("No instrument on %1").arg(QString::fromStdString(name)));
        return false;
    }
    if (editorOpen(track)) return true;
    const auto row = song_->tracks().at(track).toMap();
    const QString title = QStringLiteral("%1 · %2")
                              .arg(row.value("instrument").toString(), QString::fromStdString(name));
    QString error;
    if (!pluginWindows().open(address_of(track), *instance, title, &error)) {
        report(error);
        return false;
    }
    // An instrument the song gave no state plays the plugin's own defaults.
    // The song records them now, so undoing the first knob turned in this
    // window has a state to go back to. Nothing about the song changes.
    auto& slot = song_->song().tracks[static_cast<std::size_t>(track)].instrument;
    if (slot.state.empty()) slot.state = instance->save_state();
    // The editor is served from here on, whether or not anything plays.
    editor_timer_.start();
    report(QStringLiteral("Editor open · %1").arg(QString::fromStdString(name)));
    return true;
}

void AppController::closeEditor(int track) {
    if (windows_ && track >= 0 && windows_->close(address_of(track))) {
        editor_status_ = QStringLiteral("Editor closed");
        emit editorsChanged();
    }
}

bool AppController::toggleEditor(int track) {
    if (editorOpen(track)) {
        closeEditor(track);
        return false;
    }
    return openEditor(track);
}

void AppController::serviceEditors() {
    if (!engine_) return;
    for (std::size_t track = 0; track < engine_->track_count(); ++track)
        if (auto* instance = engine_->processor(address_of(static_cast<int>(track))))
            instance->idle();
    drainPluginEdits();
}

void AppController::drainPluginEdits() {
    if (!engine_ || song_ == nullptr) return;
    blokkily::PluginEditEvent event;
    bool moved = false;
    while (engine_->take_plugin_edit(event)) {
        if (event.edit.kind == blokkily::ParameterEdit::Kind::value) moved = true;
        const auto done = gestures_.feed(event);
        if (!done || done->where.kind != blokkily::BusKind::track || !done->where.instrument())
            continue;
        auto* instance = engine_->processor(done->where);
        if (instance == nullptr) continue;
        // The instrument already plays the new value; the song records the
        // state it now holds, and the step before it holds the old one.
        (void)song_->commitInstrumentState(static_cast<int>(done->where.bus),
                                           instance->save_state());
    }
    if (!moved || !gestures_.last_value()) return;
    const auto& last = *gestures_.last_value();
    QString name = QStringLiteral("P%1").arg(last.parameter);
    if (auto* instance = engine_->processor(last.where))
        for (const auto& parameter : instance->parameters())
            if (parameter.id == last.parameter) name = QString::fromStdString(parameter.name);
    editor_readout_ = QStringLiteral("%1 %2").arg(name.toUpper()).arg(last.value, 0, 'f', 2);
    emit editorReadoutChanged();
}

void AppController::restoreInstrumentStates() {
    if (!engine_ || song_ == nullptr) return;
    const auto& song = song_->song();
    bool stopped = false;
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto where = address_of(static_cast<int>(track));
        auto* instance = engine_->processor(where);
        const auto& slot = song.tracks[track].instrument;
        const auto* built = engine_signature_.at(where);
        // Only an instance that is the instrument the restored song names; a
        // track whose instrument changed is rebuilt from the song anyway.
        if (instance == nullptr || slot.state.empty() || built == nullptr ||
            !(*built == blokkily::identity_of(slot)))
            continue;
        if (instance->save_state() == slot.state) continue;
        if (engine_->update_processor_state(where, slot.state)) continue;
        // A plugin that takes no state while it runs is given it with the
        // device stopped for the moment the load takes.
        if (!stopped && audio_output_ && audio_output_->is_running()) {
            audio_output_->stop();
            stopped = true;
        }
        (void)instance->load_state(slot.state);
    }
    if (stopped) {
        std::string error;
        if (!audio_output_->start(&error)) {
            status_ = QString("Audio restart failed · %1").arg(QString::fromStdString(error));
            emit statusChanged();
        }
    }
}

void AppController::reconcileEditors(const blokkily::TrackRemap* remap) {
    gestures_.clear();
    if (!windows_) return;
    windows_->reconcile(remap, [this](blokkily::ProcessorAddress where) {
        return engine_ ? engine_->processor(where) : nullptr;
    });
}
