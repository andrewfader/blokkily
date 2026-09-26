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
    if (track < 0) return false;
    return openProcessorEditor(address_of(track));
}

bool AppController::openProcessorEditor(blokkily::ProcessorAddress where) {
    if (song_ == nullptr) return false;
    if (!engine_) (void)rebuildEngine();
    auto* instance = engine_ ? engine_->processor(where) : nullptr;
    auto* slot = blokkily::song_slot(song_->song(), where);
    if (instance == nullptr || slot == nullptr) return false;
    if (windows_ && windows_->isOpen(where)) return true;
    const QString title = QString::fromStdString(slot->identifier);
    QString error;
    if (!pluginWindows().open(where, *instance, title, &error)) {
        editor_status_ = error;
        emit editorsChanged();
        return false;
    }
    if (slot->state.empty()) slot->state = instance->save_state();
    editor_timer_.start();
    editor_status_ = QStringLiteral("Editor open · %1").arg(title);
    emit editorsChanged();
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
    // Every processor, not only the instruments: an insert asks the main
    // thread for callbacks, flushes and tail rescans as an instrument does.
    blokkily::serve_processors(*engine_);
    drainPluginEdits();
}

void AppController::drainPluginEdits() {
    if (!engine_ || song_ == nullptr) return;
    blokkily::PluginEditEvent event;
    bool moved = false;
    while (engine_->take_plugin_edit(event)) {
        if (event.edit.kind == blokkily::ParameterEdit::Kind::value) moved = true;
        // A knob turned while the song plays is automation, when its track's
        // mode records (item 3.1).
        captureParameterEdit(event);
        const auto done = gestures_.feed(event);
        if (!done)
            continue;
        auto* instance = engine_->processor(done->where);
        if (instance == nullptr) continue;
        // The instrument already plays the new value; the song records the
        // state it now holds, and the step before it holds the old one.
        (void)song_->commitProcessorState(done->where,
                                           instance->save_state());
    }
    if (automation_take_.ready()) commitAutomation();
    if (!moved || !gestures_.last_value()) return;
    const auto& last = *gestures_.last_value();
    QString name = QStringLiteral("P%1").arg(last.parameter);
    if (auto* instance = engine_->processor(last.where))
        for (const auto& parameter : instance->parameters())
            if (parameter.id == last.parameter) name = QString::fromStdString(parameter.name);
    editor_readout_ = QStringLiteral("%1 %2").arg(name.toUpper()).arg(last.value, 0, 'f', 2);
    emit editorReadoutChanged();
}

void AppController::restoreInstrumentStates(const QList<int>&) {
    if (!engine_ || song_ == nullptr) return;
    const auto& song = song_->song();
    bool stopped = false;
    for (const auto where : engine_->processor_addresses()) {
        auto* instance = engine_->processor(where);
        const auto* restored = blokkily::song_slot(song, where);
        if (restored == nullptr) continue;
        const auto& slot = *restored;
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
    }, engine_ ? engine_->processor_addresses() : std::vector<blokkily::ProcessorAddress>{});
}
