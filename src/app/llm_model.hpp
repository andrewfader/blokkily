#pragma once

// The assistant's bridge into the session. It takes the producer's line from
// the prompt bar, shows the model the session it is writing into, checks the
// reply the way any input is checked, and — only when the producer says
// Apply — writes it into the canonical pattern through the same undo-able
// path every editor uses. One generation is one step of history.
//
// The write path honors the architectural invariants (AGENTS.md): the
// assistant writes through Pattern::add into the one canonical pattern, it
// never touches the audio thread (backends answer on the GUI thread), and a
// proposed change is a preview until Apply — nothing reaches the song before.

#include "../llm/llm_backend.hpp"
#include "../llm/trigger_json.hpp"

#include <QObject>
#include <QString>
#include <QStringList>


#include <memory>

class SongModel;
class PatternModel;

class LlmModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool hasProposal READ hasProposal NOTIFY proposalChanged)
    Q_PROPERTY(QString proposalSummary READ proposalSummary NOTIFY proposalChanged)
    Q_PROPERTY(int proposalCount READ proposalCount NOTIFY proposalChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)
    Q_PROPERTY(QString backendKey READ backendKey NOTIFY backendChanged)
    Q_PROPERTY(QStringList backendNames READ backendNames CONSTANT)

public:
    LlmModel(SongModel* song, PatternModel* pattern, QObject* parent = nullptr);

    bool busy() const noexcept { return busy_; }
    QString statusText() const { return status_; }
    bool hasProposal() const noexcept { return proposed_.has_value(); }
    QString proposalSummary() const { return proposal_summary_; }
    int proposalCount() const {
        return proposed_ ? static_cast<int>(proposed_->response.triggers.size()) : 0;
    }
    QString backendName() const;
    QString backendKey() const { return backend_name_; }
    QStringList backendNames() const { return backend_names_; }

    // Sends a line to the model. The answer arrives as a proposal; nothing
    // is written until applyProposal().
    Q_INVOKABLE void ask(const QString& prompt);
    // Writes the proposal into the open pattern as one undo step.
    Q_INVOKABLE bool applyProposal();
    Q_INVOKABLE void discardProposal();
    Q_INVOKABLE void setBackend(const QString& name);
    Q_INVOKABLE void clearHistory();

    // The verify gate and the tests stand a scripted backend in for a live
    // one; the application never calls this.
    void setBackendForTesting(std::unique_ptr<blokkily::llm::LlmBackend> backend);

signals:
    void busyChanged();
    void statusChanged();
    void proposalChanged();
    void backendChanged();
    // A proposal was written into the pattern; `summary` says what, in one
    // line the prompt bar can show.
    void applied(const QString& summary);

private:
    [[nodiscard]] QString contextJson() const;
    void setStatus(const QString& status);
    // Drops the answer still on its way, if any, and hands the bar back:
    // whatever that backend says later no longer applies.
    void abandonPending();
    void finishProposal(blokkily::llm::ParseResult parsed);
    // Bends every pitch of `triggers` onto the session's scale when
    // auto-scale is on; returns how many moved.
    int snapToScale(std::vector<blokkily::Trigger>& triggers) const;

    SongModel* song_;
    PatternModel* pattern_;
    std::unique_ptr<blokkily::llm::LlmBackend> backend_;
    QString backend_name_;
    QStringList backend_names_;
    bool busy_ = false;
    QString status_;
    std::optional<blokkily::llm::ParseResult> proposed_;
    QString proposal_summary_;
    QString last_prompt_;
    std::vector<blokkily::llm::Turn> history_;
    // A reply for a line that was superseded (backend switched, history
    // cleared, another ask) is dropped rather than applied out from under
    // the producer.
    quint64 generation_ = 0;
};
