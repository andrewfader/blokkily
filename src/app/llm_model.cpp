#include "llm_model.hpp"

#include "../llm/system_prompt.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"

#include "blokkily/model/scale.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <variant>

namespace {
// How many turns of the conversation the model is reminded of; beyond that
// the session context carries the state.
constexpr std::size_t history_limit = 8;
// Existing triggers shown to the model, capped so a dense pattern cannot
// flood the context.
constexpr std::size_t shown_trigger_limit = 128;

QString modeName(blokkily::llm::Mode mode) {
    switch (mode) {
    case blokkily::llm::Mode::replace: return QStringLiteral("replace");
    case blokkily::llm::Mode::add: return QStringLiteral("add to");
    case blokkily::llm::Mode::modify: return QStringLiteral("modify");
    }
    return QStringLiteral("replace");
}
} // namespace

LlmModel::LlmModel(SongModel* song, PatternModel* pattern, QObject* parent)
    : QObject(parent), song_(song), pattern_(pattern),
      backend_names_(blokkily::llm::backendNames()) {
    const auto configured = qEnvironmentVariable("BLOKKILY_LLM_BACKEND");
    setBackend(configured.isEmpty() ? backend_names_.value(0) : configured);
}

QString LlmModel::backendName() const {
    return backend_ ? backend_->displayName() : QStringLiteral("none");
}

void LlmModel::setBackend(const QString& name) {
    backend_.reset();
    backend_ = blokkily::llm::makeBackend(name, this);
    backend_name_ = name;
    abandonPending();
    proposed_.reset();
    emit proposalChanged();
    emit backendChanged();
    if (!backend_)
        setStatus(QStringLiteral("Unknown backend '%1'").arg(name));
    else
        setStatus(QStringLiteral("Ready — %1").arg(backend_->displayName()));
}

void LlmModel::setBackendForTesting(std::unique_ptr<blokkily::llm::LlmBackend> backend) {
    backend_.reset();
    backend_ = std::move(backend);
    backend_->setParent(this);
    backend_name_ = QStringLiteral("scripted");
    abandonPending();
    proposed_.reset();
    emit proposalChanged();
    emit backendChanged();
    setStatus(QStringLiteral("Ready — %1").arg(backend_->displayName()));
}

QString LlmModel::contextJson() const {
    const auto& song = song_->song();
    const auto& pattern = song_->editPattern();
    const auto& meter = song.meter.meter_in(song.meter.bar_at(0));
    QJsonObject context{
        {"tuning", song_->tuningName()},
        {"divisions", song.tuning.divisions()},
        {"scale", song_->scaleName()},
        {"root_degree", song_->rootDegree()},
        {"root_name", song_->rootName()},
        {"auto_scale", song_->autoScale()},
        {"ticks_per_beat", static_cast<int>(pattern.ticks_per_beat())},
        {"ticks_per_step", static_cast<int>(PatternModel::ticks_per_step)},
        {"pattern_length_steps", pattern_->stepCount()},
        {"tempo_bpm", song.tempo.bpm_at(0)},
        {"meter", QStringLiteral("%1/%2").arg(meter.numerator).arg(meter.denominator)},
        {"track", song_->tracks().value(song_->selectedTrack())
                      .toMap()
                      .value("name")
                      .toString()},
    };
    auto events = pattern.events();
    if (events.size() > shown_trigger_limit) events = events.first(shown_trigger_limit);
    context["existing_triggers"] =
        blokkily::llm::triggersToJson(events, PatternModel::ticks_per_step);
    return QString::fromUtf8(
        QJsonDocument(context).toJson(QJsonDocument::Compact));
}

void LlmModel::abandonPending() {
    ++generation_;
    if (!busy_) return;
    busy_ = false;
    emit busyChanged();
}

void LlmModel::setStatus(const QString& status) {
    status_ = status;
    emit statusChanged();
}

void LlmModel::ask(const QString& prompt) {
    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty() || busy_) return;
    if (!backend_) {
        setStatus(QStringLiteral("No backend configured."));
        return;
    }
    busy_ = true;
    emit busyChanged();
    last_prompt_ = trimmed;
    setStatus(QStringLiteral("Asking %1…").arg(backend_->displayName()));

    blokkily::llm::Request request;
    request.systemPrompt = blokkily::llm::systemPrompt(pattern_->stepCount());
    request.context = contextJson();
    request.history = history_;
    request.prompt = trimmed;
    const auto generation = ++generation_;
    backend_->complete(request, [this, generation](blokkily::llm::Reply reply) {
        if (generation != generation_) return; // superseded; drop it
        busy_ = false;
        emit busyChanged();
        if (!reply.ok) {
            setStatus(reply.error);
            return;
        }
        auto parsed = blokkily::llm::parseResponse(
            reply.text, PatternModel::ticks_per_step, pattern_->stepCount(),
            song_->song().tuning.divisions() * 10);
        finishProposal(std::move(parsed));
    });
}

void LlmModel::finishProposal(blokkily::llm::ParseResult parsed) {
    if (!parsed.ok) {
        setStatus(QStringLiteral("Could not use the reply: %1").arg(parsed.error));
        return;
    }
    proposed_ = std::move(parsed);
    proposal_summary_ = QStringLiteral("%1 %2 trigger%3 to %4 the pattern")
                            .arg(QStringLiteral("Proposed:"))
                            .arg(proposed_->response.triggers.size())
                            .arg(proposed_->response.triggers.size() == 1 ? QString() : QStringLiteral("s"))
                            .arg(modeName(proposed_->response.mode));
    if (!proposed_->corrections.isEmpty())
        proposal_summary_ += QStringLiteral(" — %1")
                                 .arg(proposed_->corrections.join(QStringLiteral("; ")));
    setStatus(QStringLiteral("Proposal ready — Apply writes it, Discard drops it."));
    emit proposalChanged();
}

int LlmModel::snapToScale(std::vector<blokkily::Trigger>& triggers) const {
    const auto& song = song_->song();
    if (!song.auto_scale) return 0;
    int moved = 0;
    for (auto& trigger : triggers) {
        if (std::holds_alternative<blokkily::Note>(trigger.musical_data)) {
            auto& note = std::get<blokkily::Note>(trigger.musical_data);
            const auto snapped = blokkily::snap_to_scale(song.tuning, song.scale,
                                                         song.root_degree, note.key);
            if (snapped != note.key) {
                note.key = static_cast<std::int16_t>(snapped);
                ++moved;
            }
        } else {
            auto& chord = std::get<blokkily::Chord>(trigger.musical_data);
            const auto snapped = blokkily::snap_to_scale(song.tuning, song.scale,
                                                         song.root_degree, chord.root);
            if (snapped != chord.root) {
                chord.root = static_cast<std::int16_t>(snapped);
                ++moved;
            }
        }
    }
    return moved;
}

bool LlmModel::applyProposal() {
    if (!proposed_) return false;
    auto triggers = proposed_->response.triggers;
    const auto mode = proposed_->response.mode;
    const int snapped = snapToScale(triggers);

    // One checkpoint, one write, one announcement: the whole generation is a
    // single step of undo, the same as a recorded take.
    song_->checkpoint();
    auto& pattern = song_->editPattern();
    if (mode != blokkily::llm::Mode::add)
        pattern = blokkily::Pattern(pattern.length(), pattern.ticks_per_beat());
    std::size_t written = 0;
    for (auto& trigger : triggers) {
        trigger.id = 0; // the pattern hands out identifiers
        (void)pattern.add(std::move(trigger));
        ++written;
    }
    song_->notifyStructureChanged();

    QString summary = QStringLiteral("%1 trigger%2 (%3)")
                          .arg(written)
                          .arg(written == 1 ? QString() : QStringLiteral("s"))
                          .arg(modeName(mode));
    if (snapped > 0)
        summary += QStringLiteral(", %1 snapped to %2").arg(snapped).arg(song_->scaleName());
    history_.push_back({last_prompt_, summary});
    if (history_.size() > history_limit) history_.erase(history_.begin());
    proposed_.reset();
    proposal_summary_.clear();
    emit proposalChanged();
    setStatus(QStringLiteral("Wrote %1. Undo takes it back.").arg(summary));
    emit applied(summary);
    return true;
}

void LlmModel::discardProposal() {
    if (!proposed_) return;
    proposed_.reset();
    proposal_summary_.clear();
    emit proposalChanged();
    setStatus(QStringLiteral("Discarded — the pattern is as it was."));
}

void LlmModel::clearHistory() {
    history_.clear();
    abandonPending();
    setStatus(QStringLiteral("Conversation cleared."));
}
