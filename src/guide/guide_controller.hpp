#pragma once

// The QObject bridge between the guide core (Qt-free: bus, narrator,
// dispatcher, lessons, LLM client) and the QML overlay. Owns the wiring:
// bus events -> narrator -> bubble; questions -> rules, then optional LLM;
// lessons -> overlay tour; guide actions -> dispatcher -> the same command
// functions the QML buttons call.
//
// Settings (guide on/off, narration, LLM endpoint/key) live in the app's own
// QSettings, never in the repo and never in chat.

#include "blokkily/guide/action_dispatcher.hpp"
#include "blokkily/guide/event_bus.hpp"
#include "blokkily/guide/lesson.hpp"
#include "blokkily/guide/llm_client.hpp"
#include "blokkily/guide/narrator.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <map>
#include <string>

class AppController;
class SongModel;
class PatternModel;
class Transport;
class LlmModel;

class GuideController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool guideEnabled READ guideEnabled WRITE setGuideEnabled NOTIFY guideEnabledChanged)
    Q_PROPERTY(bool narrateEnabled READ narrateEnabled WRITE setNarrateEnabled NOTIFY narrateEnabledChanged)
    Q_PROPERTY(QString bubbleTitle READ bubbleTitle NOTIFY bubbleChanged)
    Q_PROPERTY(QString bubbleText READ bubbleText NOTIFY bubbleChanged)
    Q_PROPERTY(QString bubbleKind READ bubbleKind NOTIFY bubbleChanged)
    Q_PROPERTY(bool bubbleVisible READ bubbleVisible NOTIFY bubbleChanged)
    Q_PROPERTY(QString spotlightTarget READ spotlightTarget NOTIFY spotlightChanged)
    Q_PROPERTY(bool lessonActive READ lessonActive NOTIFY lessonChanged)
    Q_PROPERTY(QString lessonTitle READ lessonTitle NOTIFY lessonChanged)
    Q_PROPERTY(QString lessonStepTitle READ lessonStepTitle NOTIFY lessonChanged)
    Q_PROPERTY(QString lessonStepText READ lessonStepText NOTIFY lessonChanged)
    Q_PROPERTY(int lessonStepIndex READ lessonStepIndex NOTIFY lessonChanged)
    Q_PROPERTY(int lessonStepCount READ lessonStepCount NOTIFY lessonChanged)
    Q_PROPERTY(QStringList lessonIds READ lessonIds NOTIFY lessonsLoaded)    Q_PROPERTY(bool asking READ asking NOTIFY askingChanged)
    Q_PROPERTY(bool confirmVisible READ confirmVisible NOTIFY confirmChanged)
    Q_PROPERTY(QString confirmText READ confirmText NOTIFY confirmChanged)
    Q_PROPERTY(bool llmEnabled READ llmEnabled NOTIFY llmChanged)
    // Whether a question the guide's rules cannot answer may go to the
    // prompt bar's model (which may be a cloud service). Off until the
    // producer switches it on; kept in their settings.
    Q_PROPERTY(bool askModel READ askModel WRITE setAskModel NOTIFY llmChanged)
    // The lesson step's progress, "2 of 3", when it asks for more than one.
    Q_PROPERTY(QString lessonProgress READ lessonProgress NOTIFY lessonChanged)

public:
    GuideController(AppController* controller, SongModel* song, PatternModel* pattern,
                    Transport* transport, QObject* parent = nullptr, bool persist = true);
    ~GuideController() override;

    bool guideEnabled() const { return guide_enabled_; }
    bool narrateEnabled() const { return narrate_enabled_; }
    QString bubbleTitle() const { return bubble_title_; }
    QString bubbleText() const { return bubble_text_; }
    QString bubbleKind() const { return bubble_kind_; } // "guide" | "llm"
    bool bubbleVisible() const { return bubble_visible_; }
    QString spotlightTarget() const { return spotlight_target_; }
    bool lessonActive() const { return lesson_active_; }
    QString lessonTitle() const;
    QString lessonStepTitle() const;
    QString lessonStepText() const;
    int lessonStepIndex() const;
    int lessonStepCount() const;
    QStringList lessonIds() const { return lesson_ids_; }
    bool asking() const { return asking_; }
    bool confirmVisible() const { return confirm_visible_; }
    QString confirmText() const { return confirm_text_; }
    // True when the guide can ask a model: its own configured client, or the
    // prompt bar's backend once the producer has allowed it (askModel).
    bool llmEnabled() const;
    bool askModel() const { return ask_model_; }
    void setAskModel(bool on);
    QString lessonProgress() const;

    // The guide says something unprompted (tours, greetings).
    Q_INVOKABLE void say(const QString& title, const QString& text,
                         const QString& kind = "guide");
    Q_INVOKABLE void dismiss();
    // Opens the bubble with its question box and lessons; it stays until it
    // is closed or the guide says something else.
    Q_INVOKABLE void open();
    Q_INVOKABLE void spotlight(const QString& target);
    Q_INVOKABLE void clearSpotlight();

    // Free-form question: rules first, LLM when enabled and the rules miss.
    Q_INVOKABLE void askQuestion(const QString& question);

    // Synchronous narration of the last `count` bus events, for MCP and other
    // tools: [{source, kind, label, narration}]. No UI side effects.
    Q_INVOKABLE QVariantList narratedEvents(int count);
    // The rule-based answer to a question, without UI side effects (unlike
    // askQuestion, which shows the bubble and may consult the LLM).
    Q_INVOKABLE QString ruleAnswer(const QString& question);

    // Lessons.
    Q_INVOKABLE void startLesson(const QString& id);
    Q_INVOKABLE QString lessonTitleFor(const QString& id);
    Q_INVOKABLE void lessonNext();
    Q_INVOKABLE void lessonBack();
    Q_INVOKABLE void stopLesson();

    // The guide changing something: through the dispatcher, never directly.
    Q_INVOKABLE void performGuideAction(const QString& id, const QVariantMap& args = {});
    Q_INVOKABLE void confirmPending();
    Q_INVOKABLE void cancelPending();

    Q_INVOKABLE void setGuideEnabled(bool on);
    Q_INVOKABLE void setNarrateEnabled(bool on);
    // The guide's own OpenAI-compatible model. The key travels only over
    // https (or to this machine); false, and nothing kept, otherwise.
    Q_INVOKABLE bool configureLlm(const QString& endpoint, const QString& key);
    Q_INVOKABLE void clearLlm();

    // The prompt bar's LlmModel, whose backend (Ollama local by default) the
    // guide borrows for theory Q&A when its own LLM client is not configured.
    // Set once by the application; null until then.
    void setLlmModel(LlmModel* model);

signals:
    void guideEnabledChanged();
    void narrateEnabledChanged();
    void bubbleChanged();
    void spotlightChanged();
    void lessonChanged();
    void lessonsLoaded();
    void askingChanged();
    void confirmChanged();
    void llmChanged();

private:
    blokkily::guide::SongSnapshot snapshot() const;
    void onBusEvent(const blokkily::guide::AppEvent& event);
    void showBubble(const QString& title, const QString& text, const QString& kind);
    void hideBubbleSoon();
    void enterLessonStep();
    void afterLessonEvent(std::size_t step_before);
    void finishAsking(quint64 generation, const QString& text, const QString& kind);
    void loadLessons();
    std::string sessionContext() const;

    AppController* controller_;
    SongModel* song_;
    PatternModel* pattern_;
    Transport* transport_;
    LlmModel* llm_model_ = nullptr;

    blokkily::guide::Narrator narrator_;
    blokkily::guide::LlmClient llm_;
    blokkily::guide::LessonEngine lessons_;
    blokkily::guide::ActionDispatcher dispatcher_;
    std::map<std::string, std::string> lesson_sources_; // id -> json

    bool guide_enabled_ = false;
    // Whether the guide's choices are kept in the user's settings. A --verify
    // run keeps them in memory only, so no gate reads or changes the user's.
    bool persist_ = true;
    bool narrate_enabled_ = true;
    QString bubble_title_;
    QString bubble_text_;
    QString bubble_kind_ = "guide";
    bool bubble_visible_ = false;
    QString spotlight_target_;
    bool lesson_active_ = false;
    QStringList lesson_ids_;
    bool asking_ = false;
    bool confirm_visible_ = false;
    QString confirm_text_;
    std::string pending_action_;
    blokkily::guide::ActionDispatcher::Args pending_args_;
    QTimer* bubble_timer_ = nullptr;
    // Narration settles before it is shown, so a tempo dragged across forty
    // values is said once, at the value it stopped on.
    QTimer* narration_timer_ = nullptr;
    QString pending_narration_;
    blokkily::guide::EventBus::Token subscription_ = 0;
    // Lesson steps whose action has run this lesson: going back and forward
    // does not run it again.
    std::vector<bool> acted_;
    bool ask_model_ = false;
    // Which question an answer belongs to: one that arrives after another
    // question, a timeout or a change of backend is dropped.
    quint64 ask_generation_ = 0;
    QTimer* ask_timer_ = nullptr;
    // The LLM endpoint/key for the transport closure (mirrors QSettings).
    QString endpoint_;
    QString api_key_;
};
